# 10 — Guide tones: the grid shows the chord while the progression plays

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination.

**Goal:** While the auto-progression plays, the mini grid-map quietly marks every key whose note belongs to the **current chord** — so a learning player's eye is drawn to the notes that will sound *inevitable*, right as the harmony changes under them.

**Architecture:** Pure visualization. The progression already knows its current chord (the PROG strip boxes it; the root is already outlined on the grid-map). Export the current chord's pitch classes from the progression state; in the grid-map draw loop, test each cell's pitch class against them and add a subtle marker. No DSP, no storage, no new state machines.

**Tech stack:** `src/io/keys.cpp` (or `progression.{h,cpp}` if doc 04's extraction landed) — one small accessor; `src/ui/perform_screen.cpp` — the draw change; `src/ui/theme.h` if a color constant is needed.

**Effort:** S (2–4 hours). **Risk:** very low.

**UI-cost budget (the simplicity rule):** zero new gestures, **zero new settings rows** — it's always-on while a progression plays, off otherwise (it marks *the progression's* chord; no progression, nothing to mark). If it reads as clutter on device, the fallback is one row (`Guide keys: on/off`, default on) — decide at Task 3, on hardware, not in review.

## Why this

Scale lock already guarantees "no wrong notes." The next rung of the ladder — the one that separates noodling from *playing changes* — is knowing which of the right notes are the **strong** ones over the chord of the bar. Guitarists get this from years of arpeggio drills; GLIDE can just *show* it, live, on the map it already draws. This is the "you can't hit a wrong note" philosophy extended to "you can *see* the sweet notes," and it costs nothing: no sound path changes, no state, no persistence. It also makes the auto-progression feature teach harmony as a side effect — watch the marks walk when the chord changes and you're watching voice leading.

## Design

- **Accessor:** wherever the progression's current step lives (the PROG strip reads it — follow that read), add:
```cpp
// Current progression chord as pitch classes (0..11). Returns count (0..3);
// 0 when no progression is playing. UI-thread only.
int progChordPcs(uint8_t* pcs, int cap);
```
  Implemented from the current step's resolved chord pitches: `pc = ((int)(pitch + 0.5f)) % 12`.
- **Draw:** in the grid-map cell loop (perform_screen.cpp — find where held-lead green / drone amber / root outline are drawn), compute each cell's pitch class via `dsp::gridToMidi(layout, string, col, false)` — **reuse the exact call the grid uses elsewhere** so scale-lock/degree mapping is honored — and if its pc is in the chord set, draw the marker.
- **The marker must whisper, not shout.** The grid-map is dense with meaning already (green held, amber drones, white jam blink, root outline). Chord tones get a **1-px corner tick** (or a dim center dot ≤ 2 px) in a desaturated version of the accent color at ~40% brightness. Root-of-chord keeps its existing stronger outline. Held-key green always draws *over* the guide mark.
- **When:** only while the progression is actively walking (same condition that animates the PROG strip). Not for drones-only, not for the loop — those layers don't define a "current chord."
- Cost: ≤ 40 cells × a 3-entry set test per frame — nothing.

## Global constraints

- Zero behavior change to any sound path. UI-thread only.
- The 30 fps full-frame canvas budget must hold — this adds trivial work, but verify no per-cell allocations or float-heavy work sneak in (`gridToMidi` per cell per frame is floats; if the frame profiler complains, cache the 40 pcs and recompute only when layout/chord changes — likely unnecessary, measure first).
- Native suite untouched but must stay green: `pio run -e native && .pio/build/native/program`.

## Tasks

### Task 1: the accessor

**Files:** Modify `src/io/keys.cpp` (or `src/io/progression.{h,cpp}`).

- [ ] Find the current-step storage (grep `PROG` in keys.cpp / perform_screen.cpp and follow the strip's data source). Implement `progChordPcs` as in Design, returning 0 unless the progression is playing.
- [ ] `pio run`; commit: `io: expose the progression's current chord pitch classes`.

### Task 2: the marks

**Files:** Modify `src/ui/perform_screen.cpp` (+ `src/ui/theme.h` for the color if the theme has no dim-accent yet).

- [ ] In the grid-map loop: fetch the pcs once per frame (before the cell loop); per cell, pc-test and draw the corner tick per Design. Draw order: guide tick → drone amber → held green → root outline (existing order wins conflicts; guide is the lowest layer).
- [ ] Chord *root* cells: skip the tick where the existing root outline already marks them (no double-marking).
- [ ] `pio run`; commit: `ui: guide tones — the grid shows the chord while the progression walks`.

### Task 3: device verification + the clutter call

- [ ] Run a 4-chord progression; watch the marks walk on the change, on the beat. Verify they're invisible-ish in peripheral vision but findable when sought (the whisper test).
- [ ] With jam blink + drones + held keys all active at once: the grid must still read. If it doesn't, either dim the tick further or add the fallback settings row (`Guide keys`, default on) — decide **here**, and note the decision in the commit message.
- [ ] Scale-lock off (chromatic grid): marks still land on chord tones (gridToMidi handles it — verify visually).

## Acceptance criteria

- Marks track the current chord in real time, honor degree mapping, and never obscure held/drone/root states.
- Zero sound-path diffs (`git diff` shows only ui/ + the accessor), native green, frame rate unchanged.
- Zero gestures; zero-or-one rows per the Task 3 decision.

## Risks

- Visual clutter is the only real risk — hence the whisper-spec and the on-device decision gate.
- `gridToMidi` per-cell cost: measure before optimizing (see Global constraints).
