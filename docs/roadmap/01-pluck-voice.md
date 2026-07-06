# 01 — PLUCK: a Karplus-Strong plucked-string waveform

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first; this doc assumes both. Check `docs/roadmap/00-INDEX.md` for the cross-doc tag/enum ledger before touching enums.

**Goal:** Add `Waveform::Pluck` — a physically-modeled plucked string (Karplus-Strong) as a seventh waveform, so the instrument that *plays* like a fretless string can *sound* like one.

**Architecture:** KS lives entirely inside `dsp::Voice` (a per-voice delay line + damping filter). No synth-level changes: the ADSR gates the string's output, legato hand-offs glide the delay length instead of re-plucking, and the existing per-sample pitch slew becomes a *true* string gliss — the delay line physically shortens under your finger.

**Tech stack:** pure C++ under `src/dsp/` (env:native gate applies). No new libraries.

**Effort:** M (1–2 days). **Risk:** low-medium (RAM + CPU headroom to verify on device).

**UI-cost budget (the simplicity rule):** zero new gestures, **zero** new settings rows. `fn+6` (wave) simply gains one entry; the generator can roll it.

## Why this

GLIDE's whole metaphor is a fretless string instrument — rows are strings, hammer-ons, pull-offs, chord slides. But every current waveform is an oscillator. Karplus-Strong is the cheapest physical model in existence (one buffer read, one average, one write per sample), and it pays the metaphor back with interest:

- **Glide on a pluck is a real gliss.** Changing the delay-line length while the string rings is exactly what sliding a finger on a string does. No other synth parameter change will ever sound this "right" on this instrument.
- Legato hand-offs (no re-attack) map perfectly: hammer-ons don't re-pluck, they re-fret.
- It's a huge new timbre family (harp/koto/guitar/bass) for ~zero UI cost and a bounded DSP cost.

## Design

- `Waveform::Pluck` appended before `Count` in `dsp::Waveform` (append-only: the value is persisted via tag `T_wave`).
- Per-voice state: a `kKsMax = 1024`-sample float delay line (32 kHz / 1024 ≈ 31 Hz floor — clamp the model's fundamental to ≥ 32 Hz), a fractional read length, and a one-pole damping state. 8 voices × 4 KB = **32 KB** of extra BSS — verify RAM headroom in Task 1.
- **Excitation** happens on fresh attacks only (`noteOn`, `retrigger`) — never on `legatoTo` or `retarget`. The burst is white noise shaped by a one-pole lowpass whose coefficient maps from `p.cutoffHz` (so the existing cutoff control = pick brightness/position).
- **Ring time** maps from `p.sustain` (0 → staccato ~0.99 loss, 1 → long ring ~0.9999 loss). The amp ADSR still multiplies the output, so attack/release behave as on every other wave; decay is naturally the string's own.
- Filter, sub-osc, noise, drive, FX, mod matrix: unchanged — the string feeds the same per-voice bus a table read does.
- `tableFor()` is never called for Pluck (voice branches before the table path).
- `morphParams` (dsp/morph.cpp) switches enums at the midpoint. When a morph crosses INTO Pluck mid-note the buffer is silent until the next fresh attack. Accept and document (a "string that hasn't been plucked yet" is honest); do NOT auto-excite on waveform change — that would fire plucks during G0 morphs.

## Global constraints

- `src/dsp/` stays pure C++ (no Arduino/M5/IDF/`millis()`), C++11-friendly, compiles in `env:native`.
- Native tests must pass: `pio run -e native && .pio/build/native/program` (host fallback: `g++ -std=gnu++14 -DGLIDE_HOST_BUILD -I src src/test_dsp.cpp src/dsp/*.cpp src/storage/patch_codec.cpp && ./a.out`).
- Append-only enums; never renumber. No change to the patch codec is needed (the wave field already persists).
- Audio budget: render must stay comfortably inside the 4 ms block cadence at 8 voices.

## Tasks

### Task 1: RAM/CPU headroom check (gate)

**Files:** none (measurement only).

- [ ] Build the current firmware: `pio run`. Record `RAM:` and `Flash:` from the size report.
- [ ] Confirm ≥ 40 KB static RAM headroom for the 32 KB of KS buffers. If not, change the design to a single shared `static float gKsPool[8][1024]` in voice.cpp and note it — do not silently shrink `kKsMax`.

### Task 2: the enum + name

**Files:** Modify `src/dsp/params.h` (Waveform enum + `waveformName`).

- [ ] Append `Pluck` before `Count`:
```cpp
enum class Waveform : uint8_t { Sine, Triangle, Saw, Square, FatSaw, Pulse, Pluck, Count };
```
- [ ] Add `case Waveform::Pluck: return "pluck";` to `waveformName`.
- [ ] Run the native tests — must still pass (nothing consumes Pluck yet).
- [ ] Commit: `dsp: Waveform::Pluck enum (append-only)`.

### Task 3: failing host test for the string

**Files:** Modify `src/test_dsp.cpp`.

- [ ] Add a test: init a `Voice` at 32 kHz, `SynthParams p; p.wave = Waveform::Pluck; p.sustain = 0.9f;`, `noteOn` at MIDI 57 (A3, 220 Hz), render 16000 samples in 128-blocks into a buffer, then assert:
  - RMS of samples [0..4000) > 0.01 (it sounds),
  - RMS of samples [12000..16000) < RMS of [0..4000) (it decays),
  - estimated period from zero-crossings of samples [2000..6000) is within 3% of 32000/220 ≈ 145.5 samples.
```cpp
// zero-crossing period estimate: mean distance between rising crossings
static float estPeriod(const float* b, int n) {
    int first = -1, last = -1, count = 0;
    for (int i = 1; i < n; ++i)
        if (b[i - 1] <= 0.f && b[i] > 0.f) { if (first < 0) first = i; last = i; ++count; }
    return count > 1 ? (float)(last - first) / (float)(count - 1) : 0.f;
}
```
- [ ] Run: `pio run -e native && .pio/build/native/program` — expect the new test to FAIL (Pluck currently falls through to the saw table and never decays like a string; the period check may accidentally pass — the decay check is the real gate).
- [ ] Commit: `test: pluck voice — sounds, decays, holds pitch (red)`.

### Task 4: the Karplus-Strong core

**Files:** Modify `src/dsp/voice.h`, `src/dsp/voice.cpp`.

- [ ] Add state to `Voice` (private):
```cpp
// Karplus-Strong string (Waveform::Pluck). The delay line IS the string:
// its length is the pitch, so the per-sample glide slew becomes a physical
// gliss. ~4 KB per voice — measured against RAM headroom before landing.
static constexpr int kKsMax = 1024;   // 32 kHz / 1024 ≈ 31 Hz floor
float ksBuf_[kKsMax] = {0.f};
int   ksW_ = 0;
float ksLp_ = 0.f;    // damping-filter state
bool  ksLive_ = false;
```
- [ ] Excite on fresh attacks. In `noteOn` and `retrigger` (NOT `legatoTo`/`retarget`), when the current params' wave is Pluck — the voice doesn't hold params, so instead excite lazily in `render()`: keep a `bool ksNeedsExcite_` set by `noteOn`/`retrigger`, consumed by the render branch when `p.wave == Waveform::Pluck`. Excitation:
```cpp
// pick brightness from cutoff: 80 Hz..12 kHz -> lp coeff 0.9..0.05
const float bright = 1.f - clamp01((p.cutoffHz - 80.f) / 11920.f);
const float k = 0.05f + 0.85f * bright;
float lp = 0.f;
for (int i = 0; i < kKsMax; ++i) {
    rng_ = rng_ * 1664525u + 1013904223u;
    const float w = ((rng_ >> 8) & 0xFFFF) * (1.f / 32768.f) - 1.f;
    lp += (w - lp) * (1.f - k);
    ksBuf_[i] = lp;
}
ksLp_ = 0.f; ksLive_ = true;
```
- [ ] In `render()`, add the Pluck branch in the per-sample loop (the existing loop already computes the slewed per-sample frequency for the phase increment — reuse that `freq`):
```cpp
// string read: fractional delay = sr/freq, -0.5 compensates the 2-tap average
float d = sr_ / (freq < 32.f ? 32.f : freq) - 0.5f;
if (d > (float)(kKsMax - 3)) d = (float)(kKsMax - 3);
float rp = (float)ksW_ - d;
if (rp < 0.f) rp += (float)kKsMax;
const int i0 = (int)rp, i1 = (i0 + 1) % kKsMax;
const float frac = rp - (float)i0;
const float a = ksBuf_[i0] + (ksBuf_[i1 % kKsMax] - ksBuf_[i0]) * frac;
const float b = ksBuf_[i1] + (ksBuf_[(i1 + 1) % kKsMax] - ksBuf_[i1]) * frac;
// loss: sustain 0..1 -> ring 0.990..0.9999 (perceptually musical range)
const float loss = 0.990f + 0.0099f * p.sustain;
ksLp_ = 0.5f * (a + b) * loss;
ksBuf_[ksW_] = ksLp_;
ksW_ = (ksW_ + 1) % kKsMax;
const float s = ksLp_;               // replaces the tableRead sample
```
  The ADSR level multiplies `s` exactly as it does the table sample; sub-osc and noise still add on top.
- [ ] Run native tests — the Task 3 test must PASS, and every pre-existing test must stay green (no other waveform's path may change — the Pluck branch must be `if (p.wave == Waveform::Pluck) {...} else {existing}`).
- [ ] Commit: `dsp: Karplus-Strong pluck voice — the string finally sounds like one`.

### Task 5: the generator can roll it

**Files:** Modify `src/dsp/sound_gen.cpp`.

- [ ] Find the waveform roll in `generateSound` and give Pluck a seat (~1/7 weight, matching the others). In `mutateSound`, ensure the categorical wave flip includes it (it will if the flip draws from `Waveform::Count`) — verify, don't assume.
- [ ] Add a host test: roll 200 seeds, assert at least one patch lands `Waveform::Pluck` and that all Pluck patches still pass the existing bounds checks.
- [ ] Run native tests. Commit: `sound_gen: generative bank can roll plucked strings`.

### Task 6: device verification

- [ ] `pio run` — compare RAM/Flash against Task 1 numbers; note the delta in the commit message.
- [ ] On hardware: select pluck via `fn+6`. Verify: (a) chord slides gliss audibly, (b) hammer-on does NOT re-pluck, (c) 8-voice chord + FX has no `STARVED`-style dropouts (listen for gaps; if in doubt run the phase0 probe mindset: sustained playing for 5 minutes), (d) G0 morph into/out of Pluck doesn't click.
- [ ] Commit any tuning (loss range, brightness map) with listening notes.

## Acceptance criteria

- Native tests green, including the new pluck decay/pitch test.
- Pluck selectable on `fn+6`, save/load round-trips through NVS slots and `.gpat` (no codec change needed — assert by saving a Pluck patch to SD and reloading).
- Chord-slide gliss on Pluck is audibly continuous; no added dropouts at 8 voices.
- Default patches unchanged (bit-for-bit default `SynthParams` behavior).

## Risks

- **RAM:** 32 KB BSS. Gate at Task 1.
- **CPU:** KS is ~5 ops/sample/voice — cheaper than the 3-phase FatSaw path. Still: verify on device.
- **Tuning:** the -0.5 sample compensation is approximate; if plucks read flat/sharp at high pitch, adjust with an allpass-interpolated read (documented in any KS reference) — acceptable follow-up, not a blocker below ±10 cents.
