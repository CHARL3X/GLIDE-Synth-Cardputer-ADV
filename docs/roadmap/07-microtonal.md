# 07 — Microtonal scales: just intonation and half-flat maqamat

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination.

**Goal:** Scales whose degrees sit *between* the twelve notes — 5-limit just intonation (major and minor) and two half-flat maqam scales (Rast, Bayati) — as ordinary entries in the existing Scale list.

**Architecture:** `dsp::Scale` gains an optional per-degree **cent-offset table** (`const int8_t* cents`, nullable — every existing scale row is untouched thanks to C++ aggregate value-init of the new trailing member). `gridToMidi` and `chordPitches` add the offset ×0.01 to the fractional MIDI they already return. Everything downstream — glide, drones, progression triads, the cents readout, the pitch trail — already speaks fractional MIDI and needs **zero changes**.

**Tech stack:** `src/dsp/scales.h`, `src/dsp/pitch.h`, tests. Nothing else.

**Effort:** S (half a day). **Risk:** very low.

**UI-cost budget (the simplicity rule):** zero new gestures, **zero new settings rows** — the existing Scale row simply gains four entries. The cheapest feature in this roadmap.

## Why this

The README's own words: "Microtonal, fretless, the whole point." Today that's true of the *performance* layer (glide + bend land anywhere) but the *grid itself* only ever offers 12-TET targets. Meanwhile every piece of plumbing is already fractional: pitches are `float` MIDI end-to-end, the readout displays cents, the trail draws them. The instrument is one lookup table away from being one of the only playable just-intonation instruments under $200:

- **JI + drones is the killer pairing.** A latched drone root with pure 5-limit thirds and fifths above it produces the zero-beating lock that equal temperament physically cannot — on a monophonic-speaker instrument built around sustained backing, this is *audible to a beginner*.
- **Rast/Bayati** open the maqam world, where the half-flat third is the identity of the music — and GLIDE's degree-mapped rows handle 7-tone scales natively.
- The auto-progression builds triads through the same `chordPitches` path, so the backing comes out in JI too — an "impossibly in-tune" pad.

## Design

- `Scale` gains a trailing member: `const int8_t* cents; // per-degree offset from 12-TET, in cents; nullptr = pure ET`. Existing rows need **no edits** (fewer aggregate initializers → value-init → `nullptr`). Verify `constexpr` still compiles under gnu++14 with pointers-to-file-scope arrays (they're `constexpr` arrays defined above `kScales`).
- New scales appended (append-only: NVS stores the scale index):

| name | short | steps (ET semis) | cents | notes |
|---|---|---|---|---|
| Just major | jmaj | 0 2 4 5 7 9 11 | 0 +4 −14 −2 +2 −16 −12 | 5-limit: 9/8, 5/4, 4/3, 3/2, 5/3, 15/8 |
| Just minor | jmin | 0 2 3 5 7 8 10 | 0 +4 +16 −2 +2 +14 +18 | 5-limit: 9/8, 6/5, 4/3, 3/2, 8/5, 9/5 |
| Rast | rast | 0 2 4 5 7 9 11 | 0 0 −50 0 0 0 −50 | E½♭, B½♭ — the maqam Rast identity |
| Bayati | bayat | 0 2 3 5 7 8 10 | 0 −50 0 0 0 0 0 | E½♭ over a minor frame |

  (Cent values rounded to integers; e.g. 5/4 = 386.3¢ → −14 from 400.)
- Harmony parents: `Just major` → itself; `Just minor` → itself; `Rast` → itself; `Bayati` → itself. All are 7-note, so the progression stacks thirds on their own degrees — *with* the cent offsets, giving JI triads. (Add the four names to the `SC_*` index enum.)
- `gridToMidi`: in the scale-locked branch, `return base + 12.f*oct + sc.steps[idx] + centsOf(sc, idx);` with `inline float centsOf(const Scale& sc, int i) { return sc.cents ? sc.cents[i] * 0.01f : 0.f; }`.
- `chordPitches`: the harmony-scale reads add `centsOf(hsc, deg % hsc.len)` the same way. The pitch-class **snap** logic keeps comparing integer `steps` (cent offsets never move a degree far enough to change nearest-degree answers at ±50¢ — assert this in a test rather than reasoning about it).
- `chromaticInScale` (UI highlight when lock is off): keeps integer comparison — a half-flat degree highlights its nearest ET key. Fine and intentional.
- `midiToNoteCents` already renders e.g. `E4 −50` — Rast's third reads honestly on the HUD with no display work.

## Global constraints

- `src/dsp/` pure C++ / C++11 / env:native green: `pio run -e native && .pio/build/native/program`.
- Append-only: new scales go after `Hirajoshi`; never reorder existing rows (stored indices).
- Neutral behavior: every existing scale must produce bit-identical pitches (nullptr cents path).

## Tasks

### Task 1: failing tests

**Files:** Modify `src/test_dsp.cpp`.

- [ ] Tests (use a `Layout` with `rootSemis=0, octave=3, scaleLock=true`):
  - Just-major degree 2 (E): `gridToMidi` returns ET value − 0.14 (±0.005).
  - Rast degree 2: ET − 0.50.
  - Existing scales: pick 3 (min pent, major, blues) and assert `gridToMidi` output is *unchanged* against hardcoded expected values computed from today's code (write these expectations by running the current code first, or derive by hand from the tables).
  - `chordPitches` on Just major degree 0 returns root/3rd/5th at 0/+3.86/+7.02 semitones over the (bass-octave) tonic (±0.01).
  - Snap-stability: for every degree of all four new scales, the chord-root snap picks the same harmony degree as the ET version of the scale would (guards the ±50¢ reasoning).
- [ ] Run native — FAIL (no `cents` member yet). Commit red.

### Task 2: the tables + the math

**Files:** Modify `src/dsp/scales.h`, `src/dsp/pitch.h`.

- [ ] Add `centsOf` and the `cents` member (trailing, after `harm`), the four `constexpr int8_t` arrays, the four appended `kScales` rows exactly per the Design table, and the four `SC_*` enum names.
- [ ] Wire `centsOf` into `gridToMidi` and both harmony-scale reads in `chordPitches`.
- [ ] Native green (all Task 1 tests + entire suite). Commit: `dsp: microtonal scales — JI major/minor, Rast, Bayati (per-degree cents)`.

### Task 3: device verification

- [ ] `pio run`, flash. Scale row shows the four new entries; pick **Just minor**, latch a root drone, play the third and fifth above: listen for the beating to *stop* relative to Natural minor (the whole payoff — A/B it).
- [ ] Rast: the HUD note readout shows `−50` on the third; the pitch trail draws the half-flat gridline gap.
- [ ] Progression in Just major: triads audibly beat-free; step labels still name sensible roots.
- [ ] Settings → scale persists over reboot (index-based NVS — nothing to do, just verify).

## Acceptance criteria

- Native suite green, including unchanged-ET regression expectations.
- Drone + JI third audibly beat-free on hardware vs the ET scale (subjective gate, note it in the commit).
- Zero new rows/gestures; stored scale indices from older firmware still select the same scales.

## Risks

- `int8_t` caps offsets at ±127¢ — ample for 5-limit and quarter-tones; septimal scales (7/4 = −31¢ fits fine too). Not a real constraint; documented.
- Players may find half-flats "out of tune" if they stumble in blind — mitigation: the names (`rast`, `bayat`, `jmaj`) signal intent, and the scale row was already a curated list.
