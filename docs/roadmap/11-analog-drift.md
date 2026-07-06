# 11 — Drift: per-voice analog instability as a sound parameter

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for the tag ledger — this doc claims **codec tag 30**.

**Goal:** A `driftCents` parameter (0–12¢): each voice's pitch wanders independently on a slow, bounded random walk — the "two VCOs that never quite agree" instability that makes vintage analog polys sound alive.

**Architecture:** Per-voice random-walk state (a float + an RNG already exists per voice), advanced once per render block, added into the `centsOffset` the voice already applies. Neutral default 0 = bit-for-bit today. Full "adding a sound parameter" recipe: codec tag 30, NVS key, one TONE row, generator Range.

**Tech stack:** `src/dsp/params.h`, `src/dsp/voice.{h,cpp}`, codec/NVS/settings/sound_gen touchpoints per CLAUDE.md's five-step recipe.

**Effort:** S (half a day). **Risk:** very low.

**UI-cost budget (the simplicity rule):** zero new gestures, **1 settings row** (TONE: `Drift`). Within budget.

## Why this

The engine's character-makers (sub, noise, drive, vibrato) are all *deliberate* motions. What digital synths lack — and what ears read instantly as "expensive analog" — is *unintentional* motion: voices that drift apart by a few cents, chords that shimmer because no two oscillators agree. GLIDE is a chord-slide instrument; slow independent drift on held chords is disproportionately flattering here. It's also the perfect generative gene: a few cents of rolled drift gives generated pads a liveliness that no static parameter can, at ~15 lines of DSP.

Distinct from what exists: `detuneCents` spreads the FatSaw's *taps statically*; `ModSource::Random` is per-*note* (held constant); vibrato is periodic. Drift is *continuous, aperiodic, per-voice* — a different (and missing) kind of motion.

## Design

- `SynthParams` gains `float driftCents = 0.f;  // 0..12 — per-voice slow random walk depth` (a persisted **sound** field, next to `autoVibCents`).
- Per-voice state in `Voice`: `float drift_ = 0.f;` (the walk, normalized −1..1). Reuse the existing `rng_` member — but step it with its own local sequence so the noise oscillator's stream isn't perturbed when noiseLevel > 0… **no**: `rng_` feeds env-gated noise; sharing would change noise output when drift is on. Add a separate `uint32_t driftRng_ = 0xB5297A4Du;` (per-voice member, seeded differently per voice by adding the voice's `seq_` on first use — or simply left identical: the walks diverge anyway once note-on times differ; simpler is fine, note the choice).
- Advance once per `render()` call (block rate), before the sample loop:
```cpp
if (p.driftCents > 0.01f) {
    driftRng_ = driftRng_ * 1664525u + 1013904223u;
    const float r = (float)((driftRng_ >> 8) & 0xFFFF) * (1.f / 32768.f) - 1.f; // -1..1
    const float dt = (float)n / sr_;
    drift_ += r * 1.2f * dt;      // random walk, ~1.2 units/sec of injected motion
    drift_ -= drift_ * 0.25f * dt; // leak toward 0: bounded, mean-zero (OU process)
    if (drift_ > 1.f) drift_ = 1.f; else if (drift_ < -1.f) drift_ = -1.f;
    centsOffset += drift_ * p.driftCents;
}
```
  (An Ornstein-Uhlenbeck walk: the leak bounds excursion so drift never becomes detune; time constant ~4 s reads as "warming up gear," not wobble.)
- Codec: tag **30** `T_driftCents` (T_F32), appended in the 1..39 SynthParams range after tag 29 (doc 02) — **if doc 02 has not landed, still use 30 and leave 28–29 reserved** (the ledger owns the numbers, not landing order).
- NVS live key: `driftcents` (10 chars) in `glide_config.cpp` `begin()`/`persistNow()`, absent → 0.
- Settings TONE row `Drift`, 0–12¢ in 1¢ steps (display `off` at 0, `4c` style otherwise — match how `autoVibCents` formats, read it first).
- Generator: `Range {0.f, 12.f}`; `generateSound` rolls drift > 0 ~30% of the time, uniform 1..7¢; `mutateSound` treats it as a normal continuous field.
- Backing bus: drones/loop render with `pBack_` — a saved sound's drift applies to its backing incarnation too, which is correct (it's part of the sound).

## Global constraints

- `src/dsp/` pure C++ / C++11 / env:native green: `pio run -e native && .pio/build/native/program`.
- Neutral default: `driftCents = 0` must be bit-for-bit (the guard `> 0.01f` ensures the walk never advances — the identity test enforces it).
- Tag 30 append-only; NVS key ≤ 15 chars; all bounds in the Range table.

## Tasks

### Task 1: failing tests

**Files:** Modify `src/test_dsp.cpp`.

- [ ] *identity*: default params, render 8192 samples, checksum — must equal the pre-change golden (capture the golden FIRST, against unmodified code).
- [ ] *bounded wander*: `driftCents = 8`, render a held note 10 s in blocks; track the voice's effective pitch via zero-crossing period per 0.5 s window; assert every window's deviation from nominal ≤ 8¢ + 2¢ margin AND at least two windows differ from each other by ≥ 1¢ (it *moves*, but stays *bounded*).
- [ ] *independence*: two voices, same params, same note: their per-window pitch tracks are not identical (if the "identical seed" simplification makes them identical because note-ons align in the test, seed `driftRng_` with `seq_` — the test decides the Design's open choice).
- [ ] Run native — identity passes, others FAIL. Commit red.

### Task 2: implement

**Files:** Modify `src/dsp/params.h`, `src/dsp/voice.h`, `src/dsp/voice.cpp`.

- [ ] Field, members, block-rate walk per Design. Native green. Commit: `dsp: driftCents — per-voice analog wander (OU walk)`.

### Task 3: persistence + row + generator

**Files:** Modify `src/storage/patch_codec.cpp`, `src/storage/glide_config.cpp`, `src/ui/settings_screen.cpp`, `src/dsp/sound_gen.cpp`, `src/test_dsp.cpp`.

- [ ] Tag 30 + buildTable row; codec round-trip test (0.0 default on old blobs).
- [ ] NVS key `driftcents`; TONE row per Design; Range + generate/mutate handling + a rolled-bounds test.
- [ ] Native green; `pio run`. Commit.

### Task 4: device verification

- [ ] A/B a held 3-voice chord on GLIDE (drift 0) vs drift 5¢: the drifted chord should shimmer/breathe without sounding out of tune. 12¢ should be audibly "old gear," still musical.
- [ ] Confirm the note readout: the cents display will wander a few cents on a held note — expected and honest; verify it doesn't read as a bug (it tracks the lead voice's real pitch). If it's distracting, that's a finding to note, not something to hide.

## Acceptance criteria

- Native green including the golden identity and bounded-wander tests.
- Factory patches (drift 0) bit-identical; drifted patches audibly alive on device.
- One TONE row, zero gestures, tag 30, key `driftcents`.

## Risks

- Interaction with the pitch trail: drift draws as slight line thickness/waver — arguably a feature (you can *see* the analog). Verify it doesn't alias into ugly steps at the trail's resolution.
- None structural; this is the recipe feature par excellence — it exists partly to be the easy warm-up before docs 01/02.
