# 17 — MIDI export: the loop take becomes a .mid on the card

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination. Pairs naturally with doc 03 (quantized takes export on-grid) and doc 04 (same take-access API); depends on neither.

**Goal:** One LIBRARY row writes the looper's take (and the progression, if one is playing) to `/glide/midi/take-NNN.mid` — your riff opens in any DAW as editable notes, no USB, no doc-06 hardware gamble.

**Architecture:** A pure, host-tested Standard MIDI File writer (`storage/smf_writer.{h,cpp}` — format 1, 480 ticks/quarter, tempo meta from the jam BPM). Track 0 = tempo/meta, tracks 1–4 = string lanes 0–3 from the loop take, track 5 = the progression's chords. Event source: the looper's recorded NoteEvents (if doc 04 landed, its `serializeTake` path; otherwise a minimal read-only iterator added here — specified below so this doc stands alone).

**Tech stack:** `src/storage/smf_writer.{h,cpp}` (pure — memory buffer in/out, host-tested byte-exactly), `src/io/looper.{h,cpp}` (tiny read API), `src/io/sd_store.cpp` (one save fn), one settings row.

**Effort:** M (1 day). **Risk:** low.

**UI-cost budget:** zero new gestures, **1 settings row** (LIBRARY: `Export MIDI`). Within budget.

## Why this

The looper records a *performance as events* — which is to say, it already records MIDI in all but encoding. Doc 05 exports the *sound*; doc 06 exports the *live stream* if the USB gamble pays off; this exports the *composition*, works on every unit with an SD card, and takes an afternoon. For a player, it's the moment GLIDE graduates from toy: the bass line you looped at lunch is arranged in Ableton by dinner. Honest scope note, stated up front: the take stores note *targets*, not glide trajectories (glides are re-synthesized live), so the export carries notes — quantized to semitones — not pitch curves. That's exactly what you want for DAW editing anyway.

## Design

### The writer (pure, byte-exact)

```cpp
// storage/smf_writer.h — Standard MIDI File format 1 encoder. Pure C++,
// host-tested against hand-computed bytes. Caller supplies events per
// track in time order; the writer owns VLQ delta-times and running status.
namespace store {
class SmfWriter {
public:
    // cap-bounded output buffer; 480 ticks per quarter note.
    void begin(uint8_t* buf, int cap, float tempoBpm, int nTracks);
    void noteOn (int track, uint32_t tick, uint8_t ch, uint8_t note, uint8_t vel);
    void noteOff(int track, uint32_t tick, uint8_t ch, uint8_t note);
    int  finish();   // writes header + track chunks; returns bytes or -1 (overflow)
};
}
```
Implementation detail that keeps it simple: buffer events per track in a small array (the take is ≤ 1024 events; progression ≤ 16 chords × 3), sort per track by tick at `finish()`, then emit. Milliseconds→ticks: `tick = ms * bpm * 480 / 60000` (float, rounded).

### Event sources

- Loop take: `looper::forEachEvent(void (*cb)(void* u, uint32_t tMs, const dsp::NoteEvent& ev), void* u)` — a read-only iterator over the *audible* layers (respect the peel state; read looper.cpp's layer bookkeeping first). On events map: `On(legato=false)` → NoteOn(round(pitchMidi)); `On(legato=true)` → NoteOff(previous note on that lane) + NoteOn (a legato hand-off is a new target — in a DAW that's two notes butted together, which is the honest rendering); `Off` → NoteOff; `Retarget` → NoteOff+NoteOn likewise. Lane n → track 1+n, channel 1+n. Velocity: fixed 100 (the keyboard has none — don't fake it).
- Progression: chords via the same accessor family as doc 10 (`progChordPcs`-adjacent — here the resolved chord *pitches* + per-bar timing from tempo/chordLen), one bar per step, looping ONCE through the sequence (a DAW loops it from there). Track 5, channel 5.
- Take empty AND no progression → red flash `NOTHING TO EXPORT`.

### The file

`/glide/midi/take-NNN.mid` (NNN counting like doc 05's WAV naming). ~14 KB worst-case buffer, same allocation note as doc 04 (one-shot, freed after).

## Global constraints

- The writer is pure (no Arduino/SD includes; buffers only) and joins `env:native` via `build_src_filter` (`+<storage/smf_writer.cpp>`), like patch_codec. Native gate: `pio run -e native && .pio/build/native/program`.
- Failure-visible (no card, nothing to export, overflow). Instrument playable without SD; looper behavior untouched (the iterator is read-only).

## Tasks

### Task 1: the writer, byte-exact and test-first

**Files:** Create `src/storage/smf_writer.{h,cpp}`. Modify `src/test_dsp.cpp`, `platformio.ini` (native filter).

- [ ] Failing tests first — hand-compute the bytes (this is the point: SMF has sharp edges and VLQ off-by-ones are silent corruption):
  - header chunk: `4D 54 68 64 00 00 00 06 00 01 <nTracks> 01 E0`.
  - tempo meta at 120 BPM: `FF 51 03 07 A1 20`.
  - VLQ: delta 0 → `00`; 127 → `7F`; 128 → `81 00`; 100000 → `86 8D 20`.
  - one C4 quarter note at 120 BPM: full two-track file byte-for-byte.
  - running status: two NoteOns same channel back-to-back omit the second status byte.
  - overflow: cap too small → `finish()` returns −1, no partial garbage.
- [ ] Implement; native green; commit.

### Task 2: the looper iterator

**Files:** Modify `src/io/looper.h/.cpp`.

- [ ] `forEachEvent` per Design (read the internal buffer + layer/peel bookkeeping first; iterate audible layers only, in time order). Read-only — assert no state mutation by code inspection, note it in the commit.
- [ ] `pio run`; commit.

### Task 3: assembly + row

**Files:** Create the export glue in `src/io/sd_store.cpp` (`exportMidi(...)`), modify `src/ui/settings_screen.cpp`.

- [ ] Wire sources → writer → file per Design; the LIBRARY row `Export MIDI` with all three failure flashes.
- [ ] `pio run`; commit.

### Task 4: verification

- [ ] Record a take with hammer-ons and an octave sweep over a 4-chord progression; export; open in a DAW (or `mido`/`python -m mido.ports` locally): lanes on separate tracks, legato pairs render butted, progression bars land on the grid, tempo correct.
- [ ] With doc 03 landed: a bar-quantized take exports with note starts sitting *on* DAW gridlines — the compounding payoff; note it in the commit.
- [ ] No card / empty state flashes correct.

## Acceptance criteria

- Native green with byte-exact SMF tests (incl. VLQ table and running status).
- A real take opens correctly in a real DAW; looper provably untouched.
- 1 row, zero gestures, pure writer host-gated.

## Risks

- SMF edge cases (VLQ, end-of-track meta, track length backpatching) — mitigated by byte-exact tests written *first*.
- The looper's layer bookkeeping may complicate "audible events only" — the iterator's contract (audible layers, time order) is what matters; implementation follows what the code says.
