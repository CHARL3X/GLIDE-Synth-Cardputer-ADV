# 04 — Jam sessions: save the whole performance to SD (.gjam)

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination. **Recommended after doc 03 (loop quantize)** so saved jams are clock-locked.

**Goal:** A jam — progression steps + loop take + tempo + the sounds it plays through — becomes a **file**. Save it to SD, power off, load it tomorrow and keep writing the song.

**Architecture:** A new tagged container format (`.gjam`, own magic `'G','J'`, same append-only tag philosophy as the patch codec) with a new `T_BLOB` wire type (u16 length) for the event streams. The looper and progression each gain a serialize/restore API; the backing sound is embedded as a nested `encodePatch` blob so the jam re-voices *identically* even if the player later edits their slots.

**Tech stack:** `src/storage/` new codec (host-testable — pure like patch_codec), `src/io/looper.cpp` + progression state in `src/io/keys.cpp` (device-only, minimal diffs), `src/io/sd_store.cpp`, `src/ui/` (two LIBRARY rows + browser filter).

**Effort:** L (2–3 days). **Risk:** medium (touches performance-state internals; SD pins remain hardware-unverified).

**UI-cost budget (the simplicity rule):** zero new gestures, **2 settings rows** (`Save jam`, `Load jam` in LIBRARY — exact mirrors of the existing Save/Load sound rows). One over the ≤1-row guideline; flagged. There is no honest 1-row version of save+load — mirroring the sound-library pair keeps the *conceptual* cost at zero because the player already knows this pattern.

## Why this

"Loops are performance state. They live until cleared or power-off." That was the right first call — and it caps GLIDE at *instrument*. One file format later it's a *songwriting notebook*: the ten-second progression trick becomes something you can keep, and the "record a bass line, flip to Solo" arrangement becomes a draft you refine across evenings. Every constraint that made the loop cheap (events, not audio — kilobytes) makes it perfect to persist. This is the biggest player-facing capability in this roadmap per byte of risk: no DSP changes at all.

## Design

### The file (`/glide/jams/<name>.gjam`)

Envelope: `'G' 'J' ver=1`, then tagged records. Scalar records reuse the patch codec's `[tag u16][type u8][payload]` shape with the same `T_U8=1`/`T_F32=4` codes, plus:

- `T_BLOB = 6` wire type: `[tag_lo][tag_hi][6][len_lo][len_hi][bytes...]` (u16 length — event streams outgrow the patch codec's 1-byte T_STR).

Tags (append-only, this file's own namespace):

| tag | field | type |
|---|---|---|
| 1 | tempoBpm | F32 |
| 2 | chordLenBeats | U8 |
| 3 | soloSlot (0..9, the slot live when saved) | U8 |
| 4 | layout snapshot: root, scaleIdx, octave, rowInterval, jamRows (5×U8 packed) | BLOB |
| 5 | progression steps | BLOB |
| 6 | loop take | BLOB |
| 7 | backing patch (nested `encodePatch` bytes — the frozen backing sound, if the solo/backing split was live) | BLOB |
| 10 | name | BLOB (utf-8) |

Progression-step blob layout, per step: `u8 nPitches (1..3)`, `f32 pitch[n]` (the **resolved** fractional-MIDI chord as built at tap time), `f32 rootPitch` (for the PROG strip label). Serializing resolved pitches — not (string,col) taps — means a jam replays note-for-note even if the player has since changed scale/root/octave; the layout snapshot (tag 4) is restored alongside so the *solo* hand also lands where it did that night.

Loop-take blob, header `u32 lengthMs, u16 eventCount, u8 layerCount`, then per event: `u32 tMs, u8 type, u8 id, u8 lane, u8 flags (bit0 legato), f32 pitchMidi`. 13 bytes × 1024-event ceiling ≈ 13 KB worst case. Overdub-layer boundaries: serialize whatever the looper's internal layer bookkeeping is (investigate in Task 2 — likely per-event layer indices or offsets; extend the per-event record with `u8 layer` if so, and bump nothing — the format is new).

### New APIs

```cpp
// looper.h
int  serializeTake(uint8_t* buf, size_t cap);            // bytes written, -1 = doesn't fit
bool restoreTake(const uint8_t* buf, size_t len, uint32_t nowMs);  // false = malformed (leave state Empty)
```
Progression equivalent lives wherever its state lives today (grep `PROG` / `progression` in `src/io/keys.cpp`) — expose `progSerialize/progRestore` with the same shape. If progression state is tangled into keys.cpp statics, extracting it into `src/io/progression.{h,cpp}` first is in-scope refactoring (it's the boundary this feature needs), but keep it a pure mechanical move, separately committed.

### Restore semantics

Loading a jam: panic-equivalent clear first (silence, keep nothing), then restore tempo/chordLen/layout, progression (playing from step 0 at the next bar), loop take in `Stopped` state (the player taps `alt` to bring it in — deliberate: loading a file should not start sound unasked), backing patch into the split if tag 7 present, and the solo slot made live. HUD flash `JAM <name>`.

## Global constraints

- The new codec must be **pure** (no Arduino/SD includes — it reads/writes memory buffers) and covered in `env:native` exactly like patch_codec: `pio run -e native && .pio/build/native/program` green.
- Failure-visible: no card / full card / malformed file → red HUD flash with a reason, never a silent no-op. The instrument stays fully playable with no card.
- SD pins in `config.h` remain hardware-unverified (CLAUDE.md) — nothing about *playing* may depend on this feature.
- Append-only tags in the new format from day one. NVS untouched (jams are SD-only, by design — flash wear + "performance state never hits flash" both hold).

## Tasks

### Task 1: the codec, pure and test-first

**Files:** Create `src/storage/jam_codec.h`, `src/storage/jam_codec.cpp`. Modify `src/test_dsp.cpp`, `platformio.ini` (add `+<storage/jam_codec.cpp>` to `env:native`'s `build_src_filter`).

- [ ] Define plain structs the codec encodes/decodes (`JamData` holding tempo, chordLen, soloSlot, layout snapshot, a bounded `ProgStep steps[16]` + count, a byte-blob view for the loop take, a byte-blob view for the backing patch, `char name[24]`).
- [ ] Failing tests: round-trip a fully-populated `JamData`; decode-with-unknown-tag (hand-craft a record with tag 99) skips cleanly; truncated buffer decodes what it can and returns false only on a broken envelope; empty progression + empty loop is legal.
- [ ] Implement `encodeJam`/`decodeJam` mirroring patch_codec's table-driven style (T_BLOB dispatch alongside the scalar path).
- [ ] Native green. Commit.

### Task 2: looper serialize/restore

**Files:** Modify `src/io/looper.h`, `src/io/looper.cpp`.

- [ ] Read `looper.cpp` fully. Document (in the commit message) the internal event record and layer bookkeeping you found.
- [ ] Implement `serializeTake`/`restoreTake` per the blob layout above (extend the per-event record if layers need it). `restoreTake` validates counts/length and leaves state `Empty` on any inconsistency.
- [ ] Build `pio run`. Manual smoke on device if available: record → serialize → clear → restore → `alt` plays the identical take. Commit.

### Task 3: progression serialize/restore

**Files:** Modify `src/io/keys.cpp` (or create `src/io/progression.{h,cpp}` if extracting — see Design).

- [ ] Locate progression state (steps as resolved chords? as taps? — the PROG strip and the per-bar walker both read it; follow those reads).
- [ ] Implement `progSerialize/progRestore` producing/consuming the tag-5 blob layout. On restore, the walker starts at step 0 aligned to the next bar tick.
- [ ] Build; smoke; commit.

### Task 4: SD plumbing + UI

**Files:** Modify `src/io/sd_store.h/.cpp`, `src/ui/settings_screen.cpp`, `src/ui/sd_browser.h/.cpp`.

- [ ] `sd_store`: `saveJam(name, bytes, len)` / `loadJam(name, buf, cap)` targeting `/glide/jams/` (mirror the `.gpat` functions; create the dir on first save). Reuse the existing failure-reporting path.
- [ ] Settings LIBRARY rows: **Save jam** (default name: `jam-` + the progression's first root note + a collision counter, e.g. `jam-A-2`, editable via the existing `text_entry`) and **Load jam** (opens `sd_browser` filtered to `.gjam`; browser gains a mode flag, not a fork of the file).
- [ ] Save is a no-op with a red flash if there's neither a progression nor a take ("NOTHING TO SAVE"). Load follows Restore semantics above.
- [ ] `pio run`; commit.

### Task 5: device verification (the real gate)

- [ ] Build a jam: 4-chord progression on Ethereal, loop a bass riff, flip to Solo (split engaged). Save. Power-cycle. Load. Verify: tempo, chords, take (after `alt`), the *backing still sounds like Ethereal* even after trashing the Ethereal slot via Randomize (the embedded blob is doing its job), and the solo slot is live.
- [ ] No card: both rows flash red, instrument unaffected.
- [ ] Malformed file (truncate one with a hex editor): red flash, state stays clean.

## Acceptance criteria

- Native suite green including jam-codec round-trip/skip/truncation tests.
- The Task 5 scenario passes end-to-end on hardware.
- Zero new gestures; 2 LIBRARY rows; performance state still never touches NVS.

## Risks

- Looper/progression internals may not match assumptions — Tasks 2–3 start with reading, and the codec (Task 1) is deliberately independent of them.
- ~14 KB encode buffer: allocate it on demand (stack is too small; a static or one-shot heap buffer in sd_store, freed after) — note the choice.
- Scope creep magnet: resist auto-load-on-boot, jam playlists, per-step editing. YAGNI — this doc is save/load only.
