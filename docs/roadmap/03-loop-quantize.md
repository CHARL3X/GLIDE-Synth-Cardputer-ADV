# 03 — Loop quantize: the looper locks to the jam clock

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [x]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination.

**Goal:** When the loop closes (second `alt` tap), snap its length to the nearest beat or bar of the jam tempo — so the loop pedal and the auto-progression finally share one clock instead of drifting apart forever.

**Architecture:** One pure, host-tested quantize function in `dsp/`; the looper calls it at loop-close with the live BPM. One settings row (`Loop snap: off / beat / bar`), persisted in NVS.

**Tech stack:** `src/dsp/` (pure helper) + `src/io/looper.cpp` (not host-tested — keep its diff minimal) + settings/NVS glue.

**Effort:** S (half a day). **Risk:** low.

**UI-cost budget (the simplicity rule):** zero new gestures, **1 settings row**. Within budget.

## Why this

The README sells "one tempo drives both the progression and the echo" — but the *looper* is on human-tap time. Record a 4-bar riff over a progression and the two run at slightly different lengths; every cycle they drift a little further apart until the jam collapses. Every hardware looper solved this the same way: quantize the loop length to the clock. This is the single highest musical-value-per-line change available in this codebase: it turns "loop + progression at the same time" from a stunt into the default way to build a song.

Design decision — **default `bar` (on)**: the whole point is that loop and progression lock by default; a player who wants free-time loops flips the row off once. Rationale on tape here: the progression is already on by default, so the out-of-box experience is "everything is in time."

## Design

- New pure helper (host-tested):
```cpp
// dsp/quantize.h — pure C++. Snap a raw loop length to the jam clock.
// mode: 0 = off, 1 = beat, 2 = bar (4 beats). Snaps to the NEAREST unit,
// minimum one unit — a tap 40% into bar 1 was "meant" as a 1-bar loop.
#pragma once
#include <cstdint>
namespace dsp {
inline uint32_t quantizeLoopMs(uint32_t rawMs, float bpm, uint8_t mode) {
    if (mode == 0 || bpm <= 0.f) return rawMs;
    const float unit = (60000.f / bpm) * (mode == 2 ? 4.f : 1.f);
    uint32_t n = (uint32_t)((float)rawMs / unit + 0.5f);
    if (n < 1) n = 1;
    return (uint32_t)((float)n * unit + 0.5f);
}
}  // namespace dsp
```
- `looper::tap()` gains the context it needs. Change the signature to `State tap(uint32_t nowMs, float bpm, uint8_t snapMode);` and update the call site(s) in `src/io/keys.cpp` (grep `looper::tap`). At the record→play transition, pass the raw elapsed length through `dsp::quantizeLoopMs` before storing it.
- Events recorded after the quantized end (the human tapped late): wrap their timestamps modulo the new length (they were played as the downbeat of the next cycle). Events "missing" at the end (tapped early): nothing to do.
- **Chord length is NOT used** — bars are 4 beats flat. The progression's *Chord length* setting is how many beats each chord holds, which is a different axis; don't couple them.
- Config: `uint8_t loopSnap = 2;` in the config struct, NVS key `loopsnap`, settings row under the JAM section: `Loop snap: off / beat / bar`.

## Global constraints

- `src/dsp/` pure C++ / C++11 / env:native green: `pio run -e native && .pio/build/native/program`.
- `io/looper.cpp` is NOT host-tested — every line of logic that *can* live in the pure helper *must* live there; review the looper diff extra carefully.
- NVS key ≤ 15 chars, namespace "glide". Loop content itself never hits flash (unchanged).

## Tasks

### Task 1: the pure quantizer, test-first

**Files:** Create `src/dsp/quantize.h`. Modify `src/test_dsp.cpp`.

- [x] Write failing tests first:
```cpp
// 100 bpm: beat = 600 ms, bar = 2400 ms
assert(dsp::quantizeLoopMs(2500, 100.f, 2) == 2400);   // late tap -> 1 bar
assert(dsp::quantizeLoopMs(1300, 100.f, 2) == 2400);   // >half bar rounds up? no: 1300/2400=0.54 -> 1 bar
assert(dsp::quantizeLoopMs(900,  100.f, 2) == 2400);   // under half a bar -> still min 1 bar
assert(dsp::quantizeLoopMs(4700, 100.f, 2) == 4800);   // 2 bars
assert(dsp::quantizeLoopMs(4700, 100.f, 1) == 4800);   // beats: 4700/600=7.83 -> 8 beats
assert(dsp::quantizeLoopMs(4700, 100.f, 0) == 4700);   // off = untouched
assert(dsp::quantizeLoopMs(4700, 0.f,  2) == 4700);    // no tempo = untouched
```
- [x] Run native — FAIL (header missing). Create the header exactly as in Design. Run native — PASS. Commit.

### Task 2: looper integration

**Files:** Modify `src/io/looper.h`, `src/io/looper.cpp`, call sites in `src/io/keys.cpp`.

- [x] Read `looper.cpp` fully first (320 lines). Find where the record→play tap computes the loop length.
- [x] Change `tap` to `State tap(uint32_t nowMs, float bpm, uint8_t snapMode);` (header comment: "bpm/snapMode only matter on the tap that closes a recording"). Apply `dsp::quantizeLoopMs` to the raw length; wrap any recorded event timestamps `>= length` by `t %= length`.
- [x] Update the `keys.cpp` call site to pass the live jam tempo and the config's `loopSnap`. (The tempo already flows to `SynthParams::tempoBpm` each frame — pull from the same source, not a second copy.)
- [x] Overdub cycles: verify overdub length logic uses the stored (now quantized) length — read the code, confirm, note it in the commit message.
- [x] Build `pio run`. Commit: `looper: loop length snaps to the jam clock (dsp-pure math)`.

### Task 3: setting + persistence

**Files:** Modify `src/storage/glide_config.h/.cpp`, `src/ui/settings_screen.cpp`.

- [x] Config field `uint8_t loopSnap = 2;` (bar), NVS key `loopsnap` in `begin()`/`persistNow()` (absent → 2).
- [x] Settings row in the JAM group: `Loop snap` → `off / beat / bar` (copy an existing 3-state row's `format`/`adjust` pattern).
- [x] `pio run`; native still green. Commit.

### Task 4: device verification

- [ ] Progression at 100 bpm + record a deliberately sloppy 1-bar loop: it must stay locked to the progression for 2+ minutes (before this change it audibly drifts within ~30 s).
- [ ] Tap a ~40%-of-a-bar accident: becomes a clean 1-bar loop.
- [ ] `Loop snap: off` reproduces the old free behavior.
- [ ] Overdubs land where played; peel/undo unaffected; panic/clear unaffected.

## Acceptance criteria

- Native suite green including the quantizer cases above.
- Loop + progression stay phase-locked indefinitely at default settings.
- `off` mode is byte-identical behavior to today.
- One settings row, zero new gestures, loop still never hits flash.

## Risks

- The looper's internal length/wrap representation may differ from the assumption (ms-based). If it stores block indices or event-relative times, port the same math — the pure function stays the single source of the rounding rule.
- Tempo changes after recording: loop keeps its ms length (documented, matches hardware loopers). Do not chase re-stretching — YAGNI.
