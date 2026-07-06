# 20 — User wave: import a single-cycle wavetable into a patch

> **For agentic workers:** Execute task-by-task. Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for the ledger — this doc claims **codec tags 119–127** and `Waveform::User = 7` (explicit value; see the enum-ordering note in the ledger).

**Goal:** `Waveform::User` — an eighth waveform whose single cycle the player *imports from a `.wav` on the SD card*. Any sound on the internet's endless single-cycle libraries (AKWF alone has 4000+) becomes a GLIDE oscillator — and the patch **carries the wave inside itself**, so slots, `.gpat` files, undo history, morphing and (doc 13) chirps all keep working on plain params.

**Architecture:** The patch grows a 1024-sample int16 table (2 KB), stored in the *existing* codec as **eight 256-byte `T_STR` chunk records (tags 120–127)** — the trick that keeps old firmware perfectly safe, because unknown `T_STR` tags are already skipped by length. At runtime the decoded table lives in a global user-table slot in `wavetables` (`gUserTable`), and `Voice::render` reads it through the same `tableRead` path as every other wave. Import = SD browser picks a `.wav` → resample to 1024 → normalize → into the live patch.

**Tech stack:** `src/dsp/wavetables.{h,cpp}`, `src/dsp/params.h`, `src/storage/patch_codec.cpp`, a small pure `.wav`-cycle reader (`src/storage/wav_cycle.{h,cpp}`, host-tested), `src/ui/sd_browser` reuse, one LIBRARY row.

**Effort:** M–L (1.5–2 days). **Risk:** medium (the codec-compat design is the crux, and it's solved below — implement it exactly).

**UI-cost budget:** zero new gestures, **1 settings row** (LIBRARY: `Import wave`). The wave then behaves as a normal `fn+6` waveform entry.

## Why this

The oscillator is the last closed door in "your instrument is yours": you can roll, mutate, breed and morph every parameter *around* the wave, but the waves themselves are the six we shipped (+ Pluck, doc 01). One import row opens the door to the entire single-cycle ecosystem — voices, organs, broken VHS tones — and to a player's *own* recorded cycles (cut one in Audacity from your voice; that's the "no two units alike" story at its logical extreme). The self-contained-patch rule is what makes it GLIDE-grade instead of a sample-player hack: a `.gpat` with a user wave is still one file, still byte-compatible across cards, slots and firmwares, still 2.3 KB.

## Design

### Codec compatibility (the crux — do not deviate)

- A new **wire type is forbidden**: `decodePatch` treats an unknown type byte as a scalar of that width, so a `T_BLOB=6` record would desync every older decoder. But **unknown `T_STR` tags are skipped cleanly by their length byte** (patch_codec.cpp's decoder handles this today, explicitly for forward compat).
- Therefore: tag **119** `T_userWaveMeta` (T_U8: format version = 1) + tags **120–127** `T_userWaveChunk0..7`, each a `T_STR` of exactly 128 bytes? No — T_STR length caps at 255, so **eight chunks of 256 bytes** carry 2048 bytes = 1024 int16 samples (little-endian). Emit chunks (like the name record) **after** all scalars; order among chunks by tag. A patch without chunks has no user wave (and if `wave == User`, the voice falls back to Saw — never silence).
- Encode: only when the live patch actually has a user table (presence flag below). Decode: chunks assemble into the destination's table buffer; a missing/short chunk set → table invalid → fallback flag.
- `PatchData` (storage side) grows `int16_t userWave[1024]; bool userWaveValid;` — **check what this does to NVS slot blob sizes and the undo history's RAM** (10 slots × 2 KB = 20 KB NVS worst case only for patches that carry a wave; history checkpoints grow by 2 KB each — audit the history depth and total, report in Task 1).

### Runtime

- `wavetables` gains `float gUserTable[kTableSize + 1];` + `void setUserTable(const int16_t* w1024);` (converts to float, writes the +1 guard sample, and builds nothing else — **no mip pair**: a user table aliases at high pitch, accepted as character per the house band-limiting stance, documented at the setter).
- `Waveform::User = 7` (explicit value; Pluck = 6 from doc 01 — if 01 hasn't landed, still use `User = 7` with a placeholder comment; the ledger owns the numbers). `tableFor` returns `gUserTable` for User; name `"user"`.
- **One global user-table slot** (the *live lead* patch's). Known v1 limitation, stated honestly: if the frozen backing (solo/backing split) and the lead both use `Waveform::User`, the backing borrows the lead's table. Acceptable (rare, non-crashing, audibly "wrong wave" at worst); a `gUserTableBack` second slot is the documented v2 if it ever matters.
- Morphing (`morphParams`): enums switch at midpoint as always; the table itself doesn't morph (it's not a param) — fine.
- Generator: `generateSound`/`mutateSound` must **never roll `User`** (a generated patch may not depend on an asset the roll didn't create — same principle as `randTiltRoute` never rolling Morph; clamp the wave roll below User and add a test).

### Import path

- Pure reader `src/storage/wav_cycle.{h,cpp}` (host-tested): parse a mono PCM16/PCM8/float32 `.wav` header, take the **first ≤ 4096 samples as the cycle** (single-cycle files are exactly one cycle long by convention), linear-resample to 1024, remove DC, peak-normalize to ±0.891 (−1 dB, matching the stock tables' headroom — measure theirs first and match exactly).
- UI: LIBRARY row `Import wave` → `sd_browser` filtered to `/glide/waves/*.wav` → on pick: reader → `setUserTable` → live patch's `wave = User`, `userWaveValid = true` → audition lick. Failure (not a wav, too long, stereo) → red flash with reason. Save behaves as ever (`fn+shift+letter`, Save to SD) — the wave rides inside.

## Global constraints

- `src/dsp/` purity; the reader is storage-side but pure (memory in/out) and joins env:native. Gate: `pio run -e native && .pio/build/native/program`.
- Codec: tags 119–127 append-only; **no new wire types**; old-firmware safety is a tested requirement, not an aspiration (Task 2's compat test).
- Failure-visible everywhere; no card → row flashes, instrument unaffected.

## Tasks

### Task 1: RAM/NVS audit (gate)

- [ ] Measure: current NVS free space in the "glide" namespace with a full bank; undo-history depth × checkpoint size; free heap. Confirm +2 KB per patch-with-wave fits all three with margin. Report numbers in this doc's margin. If history RAM is the pinch, checkpoint the wave by reference (single live copy) — decide from data.

### Task 2: codec chunks, test-first

**Files:** Modify `src/storage/patch_codec.{h,cpp}`, `src/test_dsp.cpp`.

- [ ] Failing tests: round-trip a patch with a known 1024-sample ramp table (bit-exact recovery); a patch *without* a wave emits no 119–127 records (byte-size assertion); **old-decoder simulation** — decode a with-wave blob using a table build that omits the new tags (compile-time trick: hand-roll a mini-decoder in the test replicating today's skip loop, or better, keep a captured hex fixture of a pre-change decoder's behavior): every scalar decodes correctly and the chunks are skipped; truncated chunk set → `userWaveValid == false`, scalars intact.
- [ ] Implement encode/decode + `PatchData` fields. Native green. Commit.

### Task 3: runtime wave

**Files:** Modify `src/dsp/params.h`, `src/dsp/wavetables.{h,cpp}`, `src/dsp/voice.cpp` (only if `tableFor` dispatch needs it), `src/dsp/sound_gen.cpp`, `src/test_dsp.cpp`.

- [ ] `User = 7` explicit, name, `gUserTable` + setter, `tableFor` case, Saw fallback when invalid; generator never rolls User (test over 500 seeds).
- [ ] Test: set a sine-cycle user table, render at 440 Hz, Goertzel confirms the fundamental; invalid table renders identically to Saw.
- [ ] Native green. Commit.

### Task 4: the reader + import row

**Files:** Create `src/storage/wav_cycle.{h,cpp}`. Modify `src/test_dsp.cpp`, `platformio.ini` (native filter), `src/ui/settings_screen.cpp`, `src/ui/sd_browser.*` (filter mode), the live-patch plumbing in `src/storage/glide_config.cpp`.

- [ ] Reader tests first: hand-built 44-byte-header PCM16 fixtures (in-test byte arrays) — 600-sample cycle resamples to 1024 with correct endpoints; DC removed; normalization matches the stock-table peak; stereo/compressed → error codes.
- [ ] Implement reader; wire the row → browser → apply → audition per Design. Native green; `pio run`. Commit.

### Task 5: device verification

- [ ] Import an AKWF cycle from SD: plays on `fn+6` `user`; save to slot + SD; reboot; both reload with the wave intact. Load that `.gpat` on the **previous firmware build** (keep one around): patch loads, wave ignored, no corruption — the compat crown jewel, verify it for real.
- [ ] Randomize/Mutate/Breed on a user-wave patch: wave field may flip away from User (fine) but never *to* it; undo restores the wave.
- [ ] High-pitch aliasing check by ear: acceptable as character? Note the verdict.

## Acceptance criteria

- Bit-exact wave round-trip; old-firmware load proven harmless on a real previous binary.
- Import → play → save → reload works end-to-end from a stock AKWF file.
- Generator provably never rolls User; Saw fallback never silent; 1 row, zero gestures.

## Risks

- NVS/history growth — gated by Task 1's measurements.
- The one-global-table limitation (backing borrows lead's wave) — documented, rare, v2 path named.
- Codec compat is the catastrophic-if-wrong part; it's why Task 2 simulates the *old* decoder against new bytes instead of trusting the design prose.
