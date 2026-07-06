# 14 — Echo trainer: the instrument teaches the space between the notes

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination.

**Goal:** A call-and-response practice mode: GLIDE plays a short phrase through the live sound, the player echoes it, and the device grades the echo — including, uniquely, **grading the glides**: not just "right note" but "right curve into the note."

**Architecture:** Phrase generation and scoring are pure, host-tested `dsp/` code (`dsp/trainer.{h,cpp}`): phrases are built from the current scale/layout; scoring compares two sampled pitch-vs-time traces with a cents-tolerant, glide-aware metric. The mode itself reuses the demo/audition machinery (`io/demo.cpp` proves "the instrument plays itself" already works) and the perform screen (the player just… plays). One settings row enters the mode; `` ` `` exits, like everywhere else.

**Tech stack:** `src/dsp/trainer.{h,cpp}` (pure), `src/io/` scheduling reuse, `src/ui/trainer_card.{h,cpp}` or an overlay state in perform_screen (choose during Task 3 by reading how demo mode overlays — mirror it).

**Effort:** M–L (2 days). **Risk:** low-medium (all judgment logic host-tested; the rest is orchestration).

**UI-cost budget (the simplicity rule):** zero new gestures, **1 settings row** (`Echo trainer` under a LEARN/PLAY group beside Demo — wherever demo mode's entry lives, sit next to it). Within budget.

## Why this

"The skill gap is the product. Mastery takes honest practice: clean legato overlaps, accurate shape re-fingering, controlled bends." The README names the curriculum — and then leaves the player to self-assess by ear. Every practice tool on earth grades *which* note you hit; none grade *how you traveled there*, because on a piano there is no travel. GLIDE's travel is its entire identity, and it already measures itself: `leadPitchMidi()` tracks the sounding pitch through glides and bends, per frame, today (the pitch trail literally draws it). A trainer that says "your slide arrived 40 ms late and overshot by 20 cents" is a feature *no other instrument can copy*, built almost entirely from parts on the shelf: demo mode plays phrases, the audition system plays licks, the scope tracks pitch.

It also closes the beginner loop: scale lock makes day one sound good; the trainer makes day thirty *be* good.

## Design

### Levels (a curriculum, not a game economy)

1. **Echo** — 2–4 quarter notes on one string, no glides. Pass: right degrees, roughly right time.
2. **Hammer/pull** — phrases with legato pairs; re-attacked notes that should have been legato are scored down (the trace shows an amp dip + pitch snap where a glide should be).
3. **Slides** — phrases where the *glide is the content*: long portamento between two degrees. Scored on arrival pitch, arrival time, and curve overshoot.
4. **Bends** — target a note between the frets (the trainer announces e.g. `E +50c`); scored on held accuracy.

Level advances after 3 consecutive passing rounds; a session is just rounds until `` ` ``. Score displayed as the classic S/A/B/C with one number (mean cents error) so improvement is measurable across days.

### The pure core

```cpp
// dsp/trainer.h — pure C++, host-tested. Phrases and grading.
namespace dsp {
struct TrainerNote { float pitchMidi; uint16_t startMs, durMs; bool legato; };
struct Phrase { TrainerNote notes[8]; int count; uint16_t lengthMs; };

// Deterministic phrase for (level, layout, seed): all pitches on-scale via
// gridToMidi, singable range, level dictates the gesture vocabulary.
Phrase makePhrase(int level, const Layout& l, uint32_t seed);

// A trace sample: the sounding lead pitch (or <0 = silent) at 20 ms steps.
// Grade the player's trace against the reference trace rendered from the
// phrase. Returns 0..100; fills a per-note breakdown for the UI.
struct NoteGrade { float centsErr; float timeErrMs; bool legatoOk; };
int gradeTrace(const Phrase& ref, const float* trace, int traceLen,
               NoteGrade* out, int outCap);
}
```
- `gradeTrace` alignment: per note, search ±150 ms around the nominal start for the trace's arrival at (±35 cents of) the target; cents error = median error over the note's sustained region; legatoOk = no silent gap at a legato boundary. Slides (level 3): the "sustained region" is the last 30% of the note; overshoot = max excursion beyond target after first crossing. Simple, explainable, testable — no DTW; YAGNI until real traces demand it.
- The device records the trace by sampling `synth.leadPitchMidi()` (or the same source the pitch trail uses — find it and share it) every 20 ms into a bounded buffer (8 s × 50 Hz = 400 floats).

### The loop on device

Enter via the row → overlay shows level + round. **Call:** phrase plays through the live sound (schedule NoteEvents exactly the way demo mode does — block-accurate). **Response window:** a count-in tick (two soft audition-style blips), trace records for `phrase.lengthMs + 500 ms`. **Grade:** overlay shows S/A/B/C + per-note ticks/crosses on a strip; pitch trail keeps drawing throughout — the player *sees* their curve vs the ideal (draw the reference trace as a ghost line over the trail: the single best teaching visual this can have). Any key press during the call takes over (demo-mode convention: the player is never locked out); `` ` `` exits.

## Global constraints

- `src/dsp/` pure C++ / C++11 / env:native green: `pio run -e native && .pio/build/native/program`.
- No new gestures; the mode is entered from settings and exited with `` ` `` (the universal exit).
- Scoring must run from the same pitch source the player *hears* (lead voice incl. bend) — never a parallel model.
- Trainer state is session-only: nothing persists except (optionally, later — not this doc) a best-score NVS int. YAGNI now.

## Tasks

### Task 1: phrase generator, test-first

**Files:** Create `src/dsp/trainer.h`, `src/dsp/trainer.cpp`. Modify `src/test_dsp.cpp`.

- [ ] Failing tests: determinism (same args → same phrase); every note's pitch equals some `gridToMidi(l, s, c, false)` value (on-scale by construction); level 1 has no legato flags, level 3 has ≥ 1 glide pair with the pair on one string; lengths within 2–8 s.
- [ ] Implement (LCG-seeded like sound_gen — mirror its RNG discipline). Native green. Commit.

### Task 2: grader, test-first

**Files:** same.

- [ ] Build reference traces in the test from phrases (synthesize the ideal trace: exponential-ish glide segments — a helper in the test, not in dsp/), then failing tests:
  - perfect trace → ≥ 95.
  - +30 cents flat everywhere → cents errors ≈ 30, score band B.
  - 200 ms late arrivals → timeErr caught, score drops.
  - re-attack (a 60 ms silent gap) at a legato boundary → `legatoOk == false` for that note.
  - slide overshoot of 80 cents → penalized; clean slide → not.
  - silence → score 0, no crashes on empty/short traces.
- [ ] Implement `gradeTrace` per Design. Native green. Commit.

### Task 3: the mode on device

**Files:** Create `src/ui/trainer_card.{h,cpp}` (or extend perform overlay — decide by reading demo mode's structure first and mirroring it). Modify `src/ui/settings_screen.cpp` (the row), `src/io/demo.cpp`-adjacent scheduling call sites as needed.

- [ ] Entry row beside Demo; the call/response/grade loop per Design; trace capture from the pitch-trail's source; ghost-line rendering of the reference over the trail during response + grade.
- [ ] Any-key-takeover during call; `` ` `` exit; panic works.
- [ ] `pio run`; commit.

### Task 4: device verification (play it honestly)

- [ ] Play level 1 badly and well: grades must match self-perception (the credibility test — if a clean take gets a C, the aligner is wrong; fix before shipping).
- [ ] Level 3: deliberately overshoot a slide → see it named. Nail one → S. The ghost-line comparison must make *why* legible at a glance.
- [ ] 15-minute session: no heap creep, exit always clean, sounds/settings untouched afterward.

## Acceptance criteria

- Native suite green: phrase determinism + the full grading matrix.
- On device: call → echo → explainable grade, with the ghost trace teaching visually.
- One settings row, zero gestures, zero persistence, demo-mode conventions respected.

## Risks

- **Grader credibility** is the product risk: an unfair grade kills the feature. That's why the entire metric is host-tested against synthetic traces first and hand-validated on device (Task 4) — tune tolerances (±35 cents, ±150 ms) from real playing, and keep them named constants.
- Alignment without DTW may struggle on rubato — acceptable: the trainer teaches *timing* too. Revisit only with evidence.
