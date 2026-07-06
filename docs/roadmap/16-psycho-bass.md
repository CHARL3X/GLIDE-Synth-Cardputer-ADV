# 16 — Deep bass: the missing-fundamental trick for a one-watt speaker

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination.

**Goal:** Make the sub-oscillator *audible* on the built-in speaker. A micro speaker physically cannot reproduce an 80 Hz sub — but the ear reconstructs a missing fundamental from its harmonic series. Play harmonics 2–6 of the sub's pitch (all above the speaker's rolloff) and the brain hears the bass that isn't there. Every phone speaker ships this trick; no pocket synth does.

**Architecture:** One new wavetable (`TblVirtSub`: a single cycle containing harmonics 2–6 with rolloff and **zero fundamental**) plus a branch in the sub-osc read: when *Deep bass* is on and the sub's frequency is below the speaker's usable floor, the sub phase accumulator reads `TblVirtSub` instead of the square. Same phase, same level, same envelope — only the spectrum moves up. It's a **rig setting** (about *this speaker*, not the sound), so no codec tag: patches are untouched, and line-out users (doc 15) turn it off.

**Tech stack:** `src/dsp/wavetables.{h,cpp}`, `src/dsp/voice.cpp`, `src/dsp/params.h` (one live-config field), settings row + NVS.

**Effort:** S–M (1 day). **Risk:** low.

**UI-cost budget:** zero new gestures, **1 settings row** (AUDIO: `Deep bass: off/on`, default **on** — it only alters subs that were inaudible anyway).

## Why this

`subLevel` is one of the five engine character-makers, the Bass factory patch is built on "square sub for weight" — and below roughly MIDI 45 the built-in speaker reproduces almost none of it. Players are editing a parameter they literally cannot hear on the stock hardware. Psychoacoustic bass fixes the *product* (the pocket instrument as actually held) with ~30 lines of pure DSP and zero patch-format impact. It also compounds with doc 15: on line-out you get the true sub; on the speaker you get the phantom of it; the patch is identical.

## Design

- **The table.** In `initWavetables`, build `TblVirtSub`: `sum(k=2..6) a_k * sin(k·2πx)` with `a_k = 1/k` (rolloff keeps it warm, not buzzy), normalized to the same peak as the square tables. **Fundamental bin exactly zero** — that's the entire trick. Played at the sub's frequency f/2, its components land at f, 1.5f, 2f, 2.5f, 3f; the non-octave 1.5f/2.5f terms are what force the ear to infer f/2 periodicity.
- **The switch.** `SynthParams` live-config block (never persisted with patches, set by config like `tempoBpm`): `uint8_t deepBass = 1; // rig: 1 = virtual-sub mode on the tiny speaker`. In `Voice::render`'s sub-osc read: if `p.deepBass` and the sub frequency `< kSpeakerFloorHz` (≈ 350 Hz — tune on hardware, named constant in wavetables.h), read `TblVirtSub` with the *same* `ph_[3]` accumulator; else the square as today. Crossover: to avoid a timbre step mid-glide at the threshold, crossfade over an octave below the floor (`mix = clamp01((floor - f) / (floor*0.5))` — virtual table fades in as the sub descends).
- Backing bus inherits it (same speaker). Line-out (doc 15) documentation says: turn Deep bass off for full-range rigs.
- NVS key `deepbass` (default on), settings row in AUDIO next to volume-ish things.

## Global constraints

- `src/dsp/` pure C++ / C++11 / env:native green: `pio run -e native && .pio/build/native/program`.
- No codec tag — this must never enter `.gpat`/slot data (it's rig, not sound; the precedent is tilt-morph-as-rig-setting, commit `08acd28`).
- Neutrality: with `deepBass = 0` output is bit-for-bit today's. With it on, patches with `subLevel = 0` are also bit-for-bit unchanged.

## Tasks

### Task 1: failing host tests

**Files:** Modify `src/test_dsp.cpp`.

- [ ] Write a 3-line Goertzel helper in the test file (power at frequency f over an N-sample buffer).
- [ ] Tests: render a voice at MIDI 40 (≈82 Hz, sub ≈41 Hz) with `subLevel = 1`:
  - `deepBass = 0`: Goertzel power at 41 Hz strong (the square's fundamental) — baseline.
  - `deepBass = 1`: power at 41 Hz ≈ 0 (< −40 dB of baseline), power at 82 Hz and ≈123 Hz strong (harmonics 2 and 3 of the sub).
  - `deepBass = 1` at MIDI 70 (sub ≈233 Hz > floor): byte-identical to `deepBass = 0` (above-floor path untouched).
  - `subLevel = 0`: byte-identical regardless of the flag.
- [ ] Run native — FAIL (no field/table). Commit red.

### Task 2: table + branch

**Files:** Modify `src/dsp/wavetables.h/.cpp`, `src/dsp/params.h`, `src/dsp/voice.cpp`.

- [ ] `TblVirtSub` in the TableId enum + `initWavetables` synthesis per Design; `kSpeakerFloorHz` constant; the `deepBass` field; the crossfaded read in the sub path.
- [ ] Native green. Commit: `dsp: virtual sub — the speaker plays the bass it can't`.

### Task 3: setting + plumb

**Files:** Modify `src/storage/glide_config.cpp` (NVS `deepbass`), `src/ui/settings_screen.cpp` (AUDIO row), wherever config publishes rig fields into the live `SynthParams` each frame (find how `tempoBpm` flows; mirror it).

- [ ] Row + persistence + per-frame publish. `pio run`; native green. Commit.

### Task 4: device verification (ears required)

- [ ] Bass patch (`e`), octave down, A/B the row: with Deep bass on, low notes must gain *perceived* weight on the built-in speaker (not louder — *lower*). If it reads as "buzzy" instead of "bassy," lower `a_k` rolloff to `1/k²` and retest.
- [ ] Sweep a glide from above the floor to the bottom: the crossfade must be inaudible as a step.
- [ ] Tune `kSpeakerFloorHz` by ear against the actual ADV speaker; record the chosen value + rationale in the code comment.

## Acceptance criteria

- Native green including the Goertzel spectrum assertions and both bit-identity cases.
- On hardware: audibly deeper bass on the speaker, no change on patches without sub, no step artifacts through the crossover.
- 1 row, no codec impact, rig-setting precedent followed.

## Risks

- Perception is the spec: the A/B in Task 4 is the real gate, and the two tunables (`a_k` rolloff, floor frequency) are named so tuning is a knob-turn, not a redesign.
- Intermodulation with drive at high `drive` settings — listen during Task 4; if the virtual sub gets gnarly under heavy drive, that's usually *good* on this instrument, but note it.
