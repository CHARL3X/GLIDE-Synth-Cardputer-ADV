# 02 — Osc B blend: the waveform becomes a continuous axis

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. Check `docs/roadmap/00-INDEX.md` for the tag/enum ledger — this doc claims **codec tags 28 and 29** and appends to `ModSource`/`ModDest`.

**Goal:** A second oscillator wave (`waveB`) crossfaded with the main wave by a `oscBlend` amount that is also a **mod-matrix destination** — so an LFO, the mod envelope, or tilt can sweep the *timbre itself* between two waveforms.

**Architecture:** Two new `SynthParams` fields with neutral defaults (blend 0 = bit-for-bit the current voice), one extra table read in `Voice::render` when blend > 0, one new `ModDest::Blend`, and one new `ModSource::Const` (a constant-1 source that turns any matrix slot into a settings-free offset knob — broadly useful beyond this feature).

**Tech stack:** pure C++ under `src/dsp/` + the standard "adding a sound parameter" recipe from CLAUDE.md (codec tag, NVS key, settings row, generator range).

**Effort:** M. **Risk:** low.

**UI-cost budget (the simplicity rule):** zero new gestures, **2 settings rows** (Wave B, Osc blend) in the TONE accordion. That is one over the ≤1-row guideline — flagged deliberately. Trim-to-budget variant: ship only the **Wave B** row and reach blend exclusively through the existing MOD MATRIX rows via the new `Const` source (slot: Const → blend, depth = blend amount). Recommended: ship both rows; the matrix-only route is real but hostile to discovery. **The human decides; both variants are fully specified below.**

## Why this

The mod matrix modulates pitch, filter, amp, FX — everything *around* the oscillator, never the oscillator. Waveform is the last discrete island in an instrument whose identity is continuity ("it glides between every note" — but snaps between timbres). Blend makes waveform a place you can *be between*, and `LFO → blend` is the classic wavetable-synth shimmer trick, which the generative engine can then roll: patches whose *timbre breathes*, a whole new axis of "no two units sound alike."

Bonus: `ModSource::Const` falls out for free and quietly upgrades the whole matrix (constant offsets to any destination = six extra macro knobs without a single new settings row).

## Design

- `SynthParams` gains `uint8_t waveB = 0;` (a `Waveform` value) and `float oscBlend = 0.f;`. Neutral default: blend 0 → only wave A is read → bit-for-bit legacy output.
- `ModSource::Const` appended (evaluates to 1.0). `ModDest::Blend` appended. Effective blend per block = `clamp01(p.oscBlend + matrixBlendSum)`.
- The blend crossfades the **main phase tap only**: `FatSaw`'s detuned taps and `Pulse`'s second saw read stay wave-A. (Blending three taps triples the cost for marginal audible gain — YAGNI.) The B tap reads `tableFor((Waveform)p.waveB, freq)` on the **same phase accumulator**, so A and B stay phase-locked and the crossfade is clean.
- If `waveB == Waveform::Pluck` (if doc 01 has landed): the table path can't render Pluck — treat Pluck as Saw for the B tap (`tableFor` already defaults to saw). Document in code.
- Codec: tag **28** `T_waveB` (T_U8), tag **29** `T_oscBlend` (T_F32) — the 1..39 SynthParams range, appended after `T_filterMode = 27`.
- NVS live keys: `waveb`, `oscblend` (≤15 chars) in `glide_config.cpp` `begin()`/`persistNow()`.
- Generator: `Range` row for oscBlend {0, 1}; `generateSound` rolls a non-zero blend ~35% of the time with a random waveB; `mutateSound` nudges blend continuously and flips waveB categorically.

## Global constraints

- `src/dsp/` pure C++ / C++11-friendly / env:native green: `pio run -e native && .pio/build/native/program`.
- Append-only tags and enums (this doc: tags 28, 29; `ModSource::Const` after `Random`; `ModDest::Blend` after `Reverb`). Never renumber.
- Neutral defaults: a default `SynthParams` must render bit-for-bit as before.
- NVS keys ≤ 15 chars, Preferences namespace "glide".

## Tasks

### Task 1: params + failing identity tests

**Files:** Modify `src/dsp/params.h`, `src/test_dsp.cpp`.

- [ ] Append to `SynthParams` (next to `wave`):
```cpp
uint8_t waveB    = 0;    // Waveform of the B tap (blend target)
float   oscBlend = 0.f;  // 0 = pure wave A — bit-for-bit the legacy voice
```
- [ ] Append `Const` to `ModSource` (after `Random`, before `Count`) with name `"const"`; append `Blend` to `ModDest` (after `Reverb`, before `Count`) with name `"blend"`.
- [ ] Tests in `test_dsp.cpp`:
  - *identity at 0*: render 4096 samples of a default-param voice before/after setting `waveB = (uint8_t)Waveform::Square` with `oscBlend = 0` — buffers must be byte-identical.
  - *identity at 1 with A==B*: `wave = Saw, waveB = Saw, oscBlend = 1` must equal `wave = Saw, oscBlend = 0`.
  - *midpoint is a mix*: `wave = Sine, waveB = Square, oscBlend = 0.5` — output RMS differs from both pure renders.
- [ ] Run native tests: the two identity tests pass trivially (blend unused), midpoint FAILS (no blend path yet). Commit red.

### Task 2: the blend tap in Voice::render

**Files:** Modify `src/dsp/voice.cpp`.

- [ ] Where the main-phase `tableRead` happens, add:
```cpp
float s = tableRead(tbl, ph_[0]);
if (p.oscBlend > 0.001f) {
    const float* tblB = tableFor((Waveform)p.waveB, freq);  // Pluck falls back to saw here
    const float sB = tableRead(tblB, ph_[0]);
    s += (sB - s) * p.oscBlend;
}
```
  Hoist `tblB` out of the sample loop (per-block; the mip choice at block-start freq is fine, matching how `tbl` is chosen today — verify and mirror the existing hoisting).
- [ ] Native tests: all three Task 1 tests green, all pre-existing tests green. Commit.

### Task 3: matrix destination + Const source

**Files:** Modify `src/dsp/synth.cpp`, `src/test_dsp.cpp`.

- [ ] In the per-block mod-matrix evaluation: `ModSource::Const` evaluates to `1.f`; accumulate `ModDest::Blend` contributions into `blendMod`.
- [ ] Apply without mutating persisted state: make the block-local params copy (`SynthParams pEff = p_;` — if render currently passes `p_` directly to voices, introduce the copy for the lead bus only) and set `pEff.oscBlend = clamp01(p_.oscBlend + blendMod)`. One POD copy per block is noise; do NOT write into `p_`.
- [ ] Test: slot `{Const → Blend, depth 1}` on a `Sine`/`Square` pair with `oscBlend = 0` must sound like `oscBlend = 1` (compare RMS within 1e-4). Test an LFO→Blend slot produces time-varying output (RMS of first half ≠ second half on a slow square LFO).
- [ ] Native green. Commit: `dsp: blend is a mod destination; Const source (a free macro knob)`.

### Task 4: persistence (codec + NVS)

**Files:** Modify `src/storage/patch_codec.cpp`, `src/storage/glide_config.cpp`, `src/test_dsp.cpp`.

- [ ] `patch_codec.cpp`: `T_waveB = 28, T_oscBlend = 29` in the Tag enum comment block (append inside the `T_filterMode` line's enum — keep the literal `= 28` to make the number audit-proof), plus two `buildTable` rows:
```cpp
f[n++] = {T_waveB,    T_U8,  &s.waveB};
f[n++] = {T_oscBlend, T_F32, &s.oscBlend};
```
- [ ] Round-trip test: encode a patch with `waveB = Square, oscBlend = 0.42f`, decode into a default, assert both fields. Assert an OLD blob (encoded without these tags) decodes leaving the neutral defaults.
- [ ] `glide_config.cpp`: flat NVS keys `waveb` (uchar) and `oscblend` (float) in `begin()`/`persistNow()`, absent → neutral. Mirror the exact pattern of `filterMode`'s key.
- [ ] Native green. Commit.

### Task 5: settings rows + generator

**Files:** Modify `src/ui/settings_screen.cpp`, `src/dsp/sound_gen.cpp`, `src/test_dsp.cpp`.

- [ ] TONE accordion: row **Wave B** (`format` prints `waveformName((Waveform)waveB)`, `adjust` cycles through `Waveform::Count`) and — unless the human chose the trim variant — row **Osc blend** (percent, step 5%). Copy the `format`/`adjust`/`kItems[]` pattern of the existing Wave row.
- [ ] `sound_gen.cpp`: add `Range` for oscBlend {0.f, 1.f}. In `generateSound`: 35% chance → `oscBlend = rand 0.25..1.0` and random `waveB`; else both neutral. In `mutateSound`: blend nudges as a continuous param; waveB flips as a categorical.
- [ ] Host test: over 300 seeds, some patches have blend > 0, and every rolled blend ∈ [0,1].
- [ ] Native green; `pio run` builds. Commit.

### Task 6: device verification

- [ ] On hardware: set Sine + waveB Square, sweep blend by ear; wire LFO1→Blend and confirm the timbre breathes; save to a slot and to SD, reboot, reload — all fields survive.
- [ ] Confirm a factory patch (GLIDE on `q`) is audibly unchanged.

## Acceptance criteria

- Blend 0 is bit-for-bit legacy (test-enforced). Old `.gpat` files load with neutral defaults.
- `LFO → Blend` audibly sweeps timbre on device; generator rolls blended patches.
- Native suite green; no new gestures; settings cost as chosen (2 rows, or 1 in the trim variant).
