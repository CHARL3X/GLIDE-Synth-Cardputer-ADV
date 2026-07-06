# 18 — Groove: a synthesized pulse under the jam, and swing

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination.

**Goal:** The backing trio (drones, progression, loop) has harmony but no pulse. Add a fourth, *rhythmic* backing layer — a tick/hat/thump groove synthesized from the engine's own primitives (filtered noise bursts + a pitch-dropping sine), riding the jam clock, with a swing control that also humanizes the arp/pulse jam motions.

**Architecture:** A pure percussion synth (`dsp/perc.{h,cpp}`: three one-shot generators — no samples, no wavetable changes) rendered into the existing **backing bus** in `Synth::render`, triggered by the same jam clock that walks the progression (io-side scheduling, mirroring how progression events are block-scheduled). Swing is one parameter applied at the scheduling layer: every off-beat eighth delays by `swing × (eighth/2)`.

**Tech stack:** `src/dsp/perc.{h,cpp}` (pure, host-tested), `src/dsp/synth.{h,cpp}` (a render hook on the backing stage), the jam clock in `src/io/keys.cpp`, two settings rows, NVS.

**Effort:** M (1–2 days). **Risk:** low-medium (taste is the risk — the DSP is trivial).

**UI-cost budget:** zero new gestures, **2 settings rows** (JAM: `Groove: off / tick / soft / full`, `Swing: 0–60%`). Over the 1-row guideline — accepted per the humans' "intuitive UX can flex the rules" call (2026-07-06): both rows live in the JAM group beside tempo, exactly where a player's hand already is when shaping the backing.

## Why this

Tap four chords, solo over the wash — the ten-second song trick works, and then every jam floats in rubato space because *nothing states the beat*. The jam blink shows it visually; nothing plays it. A drum *machine* would be off-identity (samples, patterns, a second instrument to program). But a **pulse** — the hurdy-gurdy's trompette buzz, the train-beat of a strummed drone — is squarely in the drone-instrument lineage this project already claims. Three tiny generators, zero UI beyond two rows, and suddenly tempo-synced delays, the progression walk, and the looper all sit *on* something. Swing then converts the mechanical grid into a groove — and applying it to the existing arp/pulse jam motions upgrades those for free.

## Design

### The generators (all < 150 ms one-shots, rendered additively into the backing bus)

```cpp
// dsp/perc.h — pure C++, host-tested. Three synthesized one-shots.
namespace dsp {
enum class PercHit : uint8_t { Tick, Hat, Thump };
class Perc {
public:
    void init(float sr);
    void trigger(PercHit h, float level);   // start/restart a one-shot
    void render(float* addTo, int n);       // adds into the backing bus
    bool quiet() const;                     // all envelopes idle
private:
    // tick: 2 ms noise click through a 4 kHz bandpass (the "rimshot-ish" beat marker)
    // hat:  30 ms white noise, one-pole highpass ~6 kHz, exp decay
    // thump: sine with pitch envelope 160->55 Hz over 60 ms, amp exp 120 ms,
    //        plus a 1 ms click transient; runs through the SAME virtual-sub
    //        floor logic as doc 16 if that landed (a thump the speaker can say)
    ...
};
}
```

### Levels (the `Groove` row)

- **tick** — beat 1 of each bar only, quiet: a metronome you can feel, not hear as music.
- **soft** — thump on 1, hat on the off-beat eighths, everything low in the mix (fixed −12 dB-ish under the backing).
- **full** — thump on 1 and 3, hat eighths, tick on 2 and 4. Still texture, not drums.

Patterns are **fixed per level** (curated, like the FX tunings — "the Omnichord rule"). No pattern editor, ever; that's the identity line this feature must not cross.

### Scheduling + swing

- The jam clock that fires progression bars/arp steps (find it in `keys.cpp`; doc 04 may have moved it to `progression.cpp`) gains eighth-note callbacks when groove ≠ off, scheduled block-accurately the way progression/loop events already are (`audio::pushEventAt`-style — but perc isn't a NoteEvent; add a tiny parallel `audio::pushPercAt(tMs, hit, level)` that lands in the same scheduled-event machinery; read how the loop schedules first and mirror the mechanism, not the type).
- **Swing** (0–60%): off-beat eighths delay by `swing * eighthMs / 2` (50% ≈ classic triplet feel at the top of the musical range; 60% is drunk on purpose). Applied in the scheduler to groove hits AND to the existing `arp`/`pulse` jam-motion steps (one multiplier at one place — verify arp/pulse go through the same clock; if not, that's the refactor moment, mechanical and separate).
- Groove mutes while the looper is *recording* the first take? No — the tick is exactly what you want to record against. It plays always when enabled and the jam clock runs (progression or drones+motion active). Panic silences pending hits (they're scheduled events; clear with the others).
- Perc renders on the **backing bus** (steady, no tilt/bend — correct) with a fixed small reverb send via the shared room (implementation: add perc post-backing-filter, pre-FX — read where `backBuf_` joins the chain and insert there).

### Persistence

Rig-level (about the jam, not the patch — like jam tempo): NVS keys `groove` (u8, default off) and `swing` (u8 percent, default 0). No codec tags.

## Global constraints

- `src/dsp/` pure C++ / C++11 / env:native green: `pio run -e native && .pio/build/native/program`.
- Neutral default: groove off = bit-for-bit today. No patch-format impact.
- Backing-layer laws apply: cap-exempt (perc uses no voices at all), steal-proof (nothing to steal), silenced by panic.
- Curated patterns only — no editor, no per-hit settings. That's a design law of this doc, not a v1 deferral.

## Tasks

### Task 1: the generators, test-first

**Files:** Create `src/dsp/perc.{h,cpp}`. Modify `src/test_dsp.cpp`.

- [ ] Failing tests: each hit renders nonzero then decays to `quiet()` within its budget (tick < 10 ms, hat < 80 ms, thump < 250 ms); hat's zero-crossing rate ≫ thump's (spectral sanity without FFT); `render` on idle adds exactly nothing (bit-identity of the buffer); re-trigger mid-decay restarts cleanly (no click: first sample after trigger within ±0.1 of last sample before — soft-start the envelopes).
- [ ] Implement per the header sketch. Native green. Commit.

### Task 2: the backing-bus hook

**Files:** Modify `src/dsp/synth.h/.cpp`, `src/test_dsp.cpp`.

- [ ] `Synth` owns a `Perc perc_;` + `void percTrigger(PercHit, float)` (called from the audio thread's event path); render adds into `backBuf_` at the point found per Design.
- [ ] Test: trigger via synth, render, output contains the hit; with groove idle, master render bit-identical to before (golden).
- [ ] Native green. Commit.

### Task 3: clock, swing, scheduling

**Files:** Modify `src/io/keys.cpp` (jam clock + swing math), `src/io/audio_engine.{h,cpp}` (the pushPercAt seam per Design — smallest possible).

- [ ] Eighth callbacks per level's pattern table (a `constexpr` array of {eighthIndex, hit, level} per level — data, not code); swing offset applied to odd eighths, and to arp/pulse steps.
- [ ] Panic clears pending perc events with the other scheduled events.
- [ ] `pio run`; commit.

### Task 4: rows + persistence

**Files:** Modify `src/ui/settings_screen.cpp`, `src/storage/glide_config.cpp`.

- [ ] `Groove` + `Swing` rows in JAM (beside tempo); NVS `groove`/`swing`. `pio run`; commit.

### Task 5: device verification (taste gate)

- [ ] `soft` under an Ethereal progression at 90 BPM: the pulse must read as *part of the backing*, not a drum machine. If it pokes out, drop the fixed mix level — that constant is the one taste knob; tune it on the speaker, note the value.
- [ ] Swing 55% + arp jam motion: the arp swings with the hats (one clock, one feel).
- [ ] Loop recorded against the tick lands tight; delay sync + groove + progression all breathe together at one tempo.
- [ ] Thump on the built-in speaker: audible (with doc 16: *feels* low). On line-out (doc 15, if present): not boomy.

## Acceptance criteria

- Native green incl. bit-identity with groove off.
- One clock: progression, arp, delay, groove, swing all share `tempoBpm`.
- Curated levels only; 2 JAM rows; panic-clean; no voices consumed.

## Risks

- **Taste.** The DSP cannot fail; the mix level and pattern choices can. Task 5 is the real gate, and the tunables are isolated constants.
- Scheduler seam creep — `pushPercAt` must stay a trivial parallel of the existing mechanism; if it wants to become a framework, stop and copy the loop's pattern more literally.
