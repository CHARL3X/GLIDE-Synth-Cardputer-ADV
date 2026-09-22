# 28 — Field data round 1: LISTEN becomes the czar, Gen V6 learns from 202 ratings

**Status: Part A BUILT 2026-09-22 (local, unreleased) — beta bin awaiting the
owner's hardware verdict. Part B not started. No release from this doc — local
beta bins only, owner feedback gates everything.**

**UI cost: zero new gestures, zero new settings rows.** Every change rides the
existing `fn`+`k` hold, the result card's `space` walk, and the Randomize
button.

**Read first:** `CLAUDE.md` (rule 7 RAM ceiling; the generator freeze),
`docs/roadmap/26-gen-v5-variety.md` (why V5 is frozen and what "tuning
means V6" means), and the memory note `mac-build-and-flash` (the native gate
on this Mac: direct `g++`, no SSE flags, one pre-existing FX-hash failure
that is portability, not a bug — gate on the RED/GREEN failure-list diff).

## The data (what Jordan collected, and what each file is good for)

Lives in `Jordan's Testing/` at the repo root. **Gitignored on purpose**: his
Launcher `config.conf` carries wifi credentials. Never `git add -f` it, never
push it, never copy pieces of it into tracked files. Host tools that read it
take the path on the command line so the checked-in tool never embeds the data.

| file | what it is | what it lets us do |
|---|---|---|
| `glide/lab/lab.csv` | 203 Randomize presses, each rated 1bad / 2meh / 3good / 4great, with seed, archetype, style, preview-ok and played flags | Rank archetypes and styles by how they land with a real ear |
| `glide/lab/*.gpat` (202) | The full patch of every rated roll, provenance inside | Decode on host (`support/gpat_stats`) and ask *which parameter values* separate good from bad, per archetype |
| `glide/keylab/keylab.csv` | 47 listens over 46 songs: true key (his call), the 12-bin chroma the device heard, confidence, rounds, his build's six alternates | Replay the classifier + landing + alternates on the host and score any change against real rooms **before** flashing |
| `tools/` | The three host programs from the 2026-09-22 analysis plus `rolls.csv` (gpat_stats output joined to ratings) | Reproduce every number in this doc |

Two facts that make the data trustworthy:

- **All 202 saved rolls regenerate bit-identical from `generateSoundV5` on
  master** (`tools/regen_check.cpp`). He rated exactly the engine that shipped
  in v3.3 on 2026-09-16. That release is also why V5 is now frozen: genver-5
  devices re-derive their o/p slots through it every boot.
- **`classifyChroma` on the host reproduces all 47 raw verdicts exactly** from
  the chroma column. Everything from the chroma onward is host-testable. The
  front end (Goertzel, floor and harmonic subtraction, octave weights) is not:
  no raw audio was saved. That is the one thing this round cannot tune, and
  Part C asks for it.

### What the numbers say — LISTEN (the shipped v3.3 landing, replayed)

Scored as a player feels it, not as a label match: *nothing sour* = every note
of the applied scale is in the song's pitch set; *home right* = the applied
root is the song's tonic.

| player's scale when they held `fn`+`k` | nothing sour | home right | both |
|---|---|---|---|
| Minor pent (the default) | 38 / 47 | 27 / 47 | 22 / 47 |
| Major (any 7-note canvas) | 29 / 47 | 27 / 47 | 21 / 47 |

- **The `space` walk is ordered wrong for real misses.** Cumulative "first
  alternate that is in-song and home-right", by position 0..5, on min pent:
  `22 22 22 27 31 36`. The mode siblings at positions 1-2 rescue nothing; the
  two runner-up keys at positions 4-5 rescue nine. Promoting the runner-up keys
  to positions 1-2 gives `22 27 34 34 34 36` — **right within two presses goes
  from 22 to 34 of 47**, host-measured, zero RAM.
- **Profile swaps do not help.** Temperley, Albrecht-Shanahan, Aarden-Essen and
  Bellman-Budge all score at or below Krumhansl's 21/47 exact. The misses are
  in the chroma, not the scoring.
- **The remaining misses are the dominant heard as tonic** (C read as G three
  times, D as A twice, E as B, F as C) plus parallel-mode flips (Blackbird G
  major heard G minor; While My Guitar A minor heard A major). Both are
  front-end problems. A few "misses" are label disputes: the two Kinks
  power-chord songs he called Mixolydian are heard with a strong flat third,
  and G minor is a defensible landing.
- **Early locks are the good ones.** Listens that stopped at 5 rounds were
  right 11/22 (mean confidence 0.79); listens that ran to the 15-round cap were
  right 3/12 (mean confidence 0.23). Seven wrong verdicts reported confidence
  ≥ 0.9. So a forced long listen costs every good lock five seconds and cannot
  be shown, from this data, to fix the confident misses. This doc raises the
  minimum heard from 3 s to 5 s and stops there.
- **The previous scale shapes the landing today, by design** (`applyListen`
  maps the verdict into the player's scale *family*: Blues never switches,
  exotics keep a relative-root rule, the same index feeds the confidence gate
  and the stop rule). The owner and Jordan both rejected this on 2026-09-22:
  *"Do not use the scale you're previously in to determine what the next scale
  should be."* That is the headline change.

### What the numbers say — the randomizer (V5, as shipped)

| archetype | rolls | good+great | mean | verdict |
|---|---|---|---|---|
| lead | 19 | 17 | 3.00 | the crowd favourite; 2 of the 3 "great" rolls |
| wobble / keys / gate | 6 / 9 / 9 | 4 / 7 / 7 | 2.8 | healthy |
| bass | 15 | 11 | 2.73 | healthy; style 1 "round" drags it (2.64 vs 3.00 classic) |
| pluck | 20 | 11 | 2.50 | classic 3.00, style 1 "kalimba" 2.33 |
| organ / strings / brass | 12 / 10 / 8 | 7 / 6 / 5 | 2.5-2.6 | fine, small n |
| drone / whistle / bell / pad | 16 / 12 / 17 / 21 | 6 / 4 / 5 / 6 | 2.2 | the sinks by volume: 66 rolls, 45 meh-or-bad |
| acid | 11 | 4 | 2.27 | good acids have deeper filter env (3.24 vs 2.43 oct) |
| chip | 13 | 1 | 1.77 | 4 of the 15 bad rolls |
| wild | 5 | 0 | 1.60 | both bad wilds classify as *pad*: slow attack, long release, high drive |

Per-parameter separators (AUC = P(a good roll has the higher value); far from
0.5 discriminates; n is small so these are tendencies, and each is applied
only where the *within-archetype* data agrees):

- **pluck**: cutoff AUC 0.92 (good median 1440 Hz, bad 616), decay 0.84 (0.50 s vs 0.31)
- **bass**: cutoff 0.86 (729 vs 339 Hz), drive 0.84 (2.14 vs 1.40)
- **chip**: release 1.00 (0.38 vs 0.15 s), cutoff 0.08 (1.9 kHz vs 6.9 kHz), LFO 5-9 Hz on every bad one
- **pad**: drive 0.22 (1.5 vs 1.84), reverbSize 0.26 (0.62 vs 0.85), sub 0.29 (0.10 vs 0.30); style 2 "cinema" 1.90
- **whistle**: lfo2 0.91 (has vibrato vs none), cutoff 0.12 (4.0 kHz vs 6.1 kHz)
- **drone**: both bad sine; bad ones sit at LFO1 0.30 Hz with no vibrato
- **acid**: fenvOct 0.88 (3.24 vs 2.43)
- pool-wide, sine and square rolls average 2.09 against 2.73 for fat saw, and
  bad rolls carry ~2x the reverb mix — both confounded by archetype, so no
  global clamp; the per-archetype rules below carry it.
- The preview complaint from the first field session is gone: preview flagged
  bad once in 203 rolls.

## Part A — LISTEN: the song is the czar

**What changes for the player.** Hold `fn`+`k`; the instrument lands on the
song's own key *and* mode at the song's tonic — Major, Natural minor, Dorian
or Mixolydian as heard — whatever scale you were in. `space` on the card then
walks the fun subsets of that same key and the detector's second guesses.
The pentatonic retreat stays: when the song plays both sixths (or both
sevenths), or the seven-note canvas is audibly sourer than the pentatonic, it
lands the pentatonic at the tonic. Playing fewer notes beats playing a wrong
one, and the data says the seven-note landing sours in 18/47 where the
pentatonic sours in 9/47.

**Why this order for `space`.** Two jobs compete for the walk: fix a miss, or
pick a flavour. Jordan asked for the flavour ("it threw me E major, I want the
pentatonic"); the data says the runner-up keys are what rescue misses. The
plan measures both orders in the harness and applies a rule (Task A2).

### Task A1 — the host scoreboard (`support/keylab_eval/`)

**Files:** create `support/keylab_eval/keylab_eval.cpp`, `support/keylab_eval/README.md`.
Start from `Jordan's Testing/tools/keylab_eval.cpp` (already works); add the
SPDX header, a `--order` flag is NOT needed — the tool always prints the
cumulative curve for the alternates the library returns.

- [ ] Copy the tool in, SPDX header, README with the exact build line:
  `g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I ../../src keylab_eval.cpp ../../src/dsp/key_detect.cpp -o keylab_eval`
  and run line `./keylab_eval "<path>/keylab.csv"`. Document the CSV columns it
  reads (col 4 true key, 12 chroma columns from col 16, title last).
- [ ] Run it on master before touching anything. Record the baseline block in
  this doc under "Scoreboard": min pent `both=22`, alternates `22 22 22 27 31 36`.
- [ ] Commit: `keylab_eval: replay a keylab.csv through the shipped landing`.

### Task A2 — `dsp/key_detect`: the scale-free landing

**Files:** modify `src/dsp/key_detect.h`, `src/dsp/key_detect.cpp`,
`src/test_dsp.cpp` (the LISTEN block, ~lines 2302-2560, 75 CHECKs).

**Interfaces (new; the old ones go away):**

```cpp
// The song's own landing. No scale argument: what the player was in is not
// evidence about the song. rootPc == tonicPc always.
ListenApply landListen(const KeyGuess& g);

// Alternates for the space walk, primary first. See order below.
int listenAlternates(const KeyGuess& g, ListenApply* out, int cap);

// classifyChroma, but confidence = margin over the best rival whose PITCH
// SET differs from the winner's (the relative twin is excluded: it shares
// every note and the pentatonic retreat makes it harmless).
KeyGuess classifyChromaSong(const float chroma[12]);
```

Delete `applyListen(int, const KeyGuess&)`, `classifyChromaForScale`,
`applyScaleForKey`, `applyScaleForKeyChroma`, `applyRootForScale`,
`scaleIsMinorish` once `grep -rn` shows no caller outside `listen_screen.cpp`
and `test_dsp.cpp`. `ListenApply` keeps its fields.

**Landing rule** (`landListen`), in order:
1. `modeFromChroma` at the raw K-S root (unchanged gates: presence 0.20,
   b7 presence 0.30, ratio 1.8).
2. Conflict on the deciding degree → `SC_MIN_PENT` / `SC_MAJ_PENT` at the raw
   root, `safe=true`, return.
3. Mixolydian tonic tiebreak, unchanged (`kTiebreakEps` 0.12; Dorian twin at
   +7, Ionian parent at +5).
4. Scale = the mode's canvas (`SC_MAJOR` / `SC_MINOR` / `SC_DORIAN` /
   `SC_MIXO`) at `tonicPc`; then `guardCanvasSourness` (unchanged floors
   0.15 / 0.10) may retreat it to the side's pentatonic.

**Alternates order** (`listenAlternates`), deduped on (scale, root):
- `[0]` primary.
- If the primary is a pentatonic (safe retreat) → the mode's canvas at the
  tonic; else the side's pentatonic at the tonic.
- Blues at the *minor* home: tonic on the minor side, `tonic+9` on the major
  side (the boxes trick, as today).
- Runner-up key 1, runner-up key 2: rescore the 24 profiles (existing loop),
  landing = plain `SC_MAJOR` / `SC_MINOR` at that root.
- The relative twin's canvas.
- **Order rule, decided by A1's numbers:** run the harness with blues at
  position 2 (above) and with blues at position 4 (runner-ups first). Ship the
  order whose "right within two presses" is higher, unless they differ by
  fewer than 3 songs — then blues stays at 2 (the owner's favourite scale
  earns the tie).

**Tests to rewrite** (same synthetic chromas the block already builds —
`gC`, `gAm`, `gEmWrong`, the Am7-D9 vamp, the conflicted-6th case — drop the
scale argument and assert the new contract):

```cpp
// a C-major chroma lands C major at C — and there is no "previous scale" to ask
ListenApply ap = landListen(gC);
CHECK(ap.scaleIdx == SC_MAJOR && ap.rootPc == 0 && ap.tonicPc == 0, "czar: C major lands C maj at C");
// an A-minor chroma lands A natural minor at A
ap = landListen(gAm);
CHECK(ap.scaleIdx == SC_MINOR && ap.rootPc == 9, "czar: A minor lands A min at A");
// the Am7-D9 vamp still re-seats to D dorian... (keep the existing tiebreak CHECKs, minus the scale arg)
// conflicted sixth → minor pent at the tonic, safe
CHECK(apConflict.scaleIdx == SC_MIN_PENT && apConflict.safe, "czar: conflicted 6th retreats to pent");
// alternates: order + dedupe + runner-ups are different roots
ListenApply alts[6]; const int n = listenAlternates(gC, alts, 6);
CHECK(n == 6, "alternates fill the card");
CHECK(alts[1].scaleIdx == SC_MAJ_PENT && alts[1].rootPc == 0, "alt 1 is the pent at the tonic");
CHECK(alts[2].scaleIdx == SC_BLUES && alts[2].rootPc == 9, "alt 2 is blues at the relative minor");  // or [4], per the order rule
CHECK(alts[3].rootPc != 0 && alts[4].rootPc != 0 && alts[3].rootPc != alts[4].rootPc, "runner-up keys are other tonics");
for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j)
    CHECK(!(alts[i].scaleIdx == alts[j].scaleIdx && alts[i].rootPc == alts[j].rootPc), "no duplicate landings");
// confidence: the relative twin does not deflate it, a fifth-off rival does
CHECK(classifyChromaSong(gC.chroma).confidence >= classifyChroma(gC.chroma).confidence, "twin excluded from the margin");
```

- [ ] Write the new CHECKs first; native gate → they fail to compile (no `landListen`).
- [ ] Implement `landListen` / `listenAlternates` / `classifyChromaSong`; delete the scale-aware helpers.
- [ ] Native gate: RED/GREEN diff against a clean tree shows only the known FX-hash failure.
- [ ] Rebuild A1's harness; paste the new scoreboard into this doc. Expect
  `both` unchanged at ~22 (the primary landing is the same music) and the
  two-press number ≥ 31.
- [ ] Commit: `LISTEN lands the song, not the scale you were in`.

### Task A3 — `ui/listen_screen.cpp`: the gate and the card follow

**Files:** modify `src/ui/listen_screen.cpp` (segment callback ~396-420,
constants ~327-328, apply/cycle ~405-415 and ~517-531, the alternates comment
~523).

- [ ] Segment callback: drop `store::get().layout.scaleIdx`; `ctx.guess =
  classifyChromaSong(ctx.chroma)`; `applied = landListen(ctx.guess)` packed as
  before; `kMinHeardForStop` 3 s → 5 s. `kEnoughConfidence` stays 0.5.
- [ ] Apply path: `listenAlternates(ctx.guess, alts, 6)`; `prevScale` survives
  only for the "scale changed" toast text.
- [ ] Card copy: the second-guess hint lists "pent · blues · other keys · twin"
  in the shipped order. The `(safe)` serial line and the `[listen]` chroma
  dump stay (support/listen_log.py reads them).
- [ ] `pio run`; compare the RAM line with the v3.3 build. Expect ≤ 0 B delta
  (fewer functions).
- [ ] Commit: `The listen gate and card stop asking which scale you were in`.

### Task A4 — docs and the beta bin

- [ ] `docs/manual.md` line 45: replace the "lands in YOUR scale family" run
  (from "The verdict then lands in YOUR scale family" through "take the
  relative root as always") with: *The verdict lands the song's own key and
  mode at its tonic, whatever scale you were in — Major, Natural minor, Dorian
  or Mixolydian as heard — and `space` walks that key's pentatonic, its blues,
  the detector's two runner-up keys, and the relative twin.* Update the
  second-guess sentence near the end of the paragraph to the same list, and
  "in ~3-second rounds ... up to ~9 s" stays true (only the minimum moved).
- [ ] Rebuild `dist/GLIDE.bin` locally; copy to the card under the name the
  owner gives (his convention: `GLIDE-Beta<X>.bin`), remove the `._` sidecar.
  **Never list the card.**
- [ ] Owner plays it against real songs. Feedback gates Part B's bin, not its
  code.

## Part B — Gen V6: the windows learn from 202 ratings

**Why a new version and not edits.** V5 shipped in v3.3; genver-5 devices
re-derive their o/p slots through `generateSoundV5` every boot, so an edit
would change sounds players already have. V6 = V5 called, never copied, then
`applyStyleV6` + `rollPolishV6` + `sanitizePatch`, over `archetypeForSeedV6`.
The Randomize button and fresh seeds move to 6; genver 5 seeds stay bit-exact.

**Design rule for every tuning below:** one measured signal → one clamp or
one style edit → one CHECK over a 700-seed sweep. No rule without a data row
in the table above. Families the data calls healthy (lead, wobble, keys,
gate, strings, organ, brass, bass classic) are **not touched** and the suite
asserts V6 classic == V5 bit-exact for them.

### Task B1 — pin V5 (it is frozen now)

**Files:** modify `src/test_dsp.cpp` (next to the V2/legacy goldens).

- [ ] Add golden `patchHashFull` values for `generateSoundV5` at eight fixed
  seeds spanning the pool (compute them on master first; the values go in the
  test verbatim). This is the freeze tripwire V2 and legacy already have and
  V5 lacked.
- [ ] Commit: `V5 goldens: the pool v3.3 shipped can no longer drift`.

### Task B2 — scaffold V6

**Files:** modify `src/dsp/sound_gen.h`, `src/dsp/sound_gen.cpp`,
`src/storage/glide_config.cpp` (ladders ~331-346, `kGenVerNewest` uses ~996,
~2037), `src/ui/settings_screen.cpp` (~798-809), `support/gpat_stats/gpat_stats.cpp`
(style column for `rollVer >= 6`), `src/test_dsp.cpp`.

**Interfaces:**

```cpp
constexpr uint8_t kGenVerNewest = 6;
Archetype archetypeForSeedV6(uint32_t seed);   // 40-entry table, reweighted (B3)
int       styleForSeedV6(uint32_t seed);       // same draw as V5 today; its own function so V6 can move it
GenPatch  generateSoundV6(uint32_t seed);
GenPatch  generateSoundV6(uint32_t seed, Archetype a);  // = generateSoundV5(seed,a) + applyStyleV6 + rollPolishV6 + sanitizePatch
```

`applyStyleV6(GenPatch&, Archetype, int style)` and
`rollPolishV6(GenPatch&, Archetype)` are file-local like their V5 twins.
`applyStyleV6` starts as *corrections on top of* the V5 recolor (it runs after
V5 already applied its style), so a family without a V6 rule is untouched.

- [ ] Tests first: determinism; `generateSoundV6(seed) == generateSoundV6(seed, archetypeForSeedV6(seed))`;
  for every untouched family `patchHashFull(V6(seed,a)) == patchHashFull(V5(seed,a))`
  over 200 seeds; the V3 and V5 pools never roll anything new (unchanged
  tripwires).
- [ ] Wire storage: `gGenVer < 6 ? V5 : V6` in both ladders (patch and
  archetype); `NameVer::V2` for ≥5 stays; provenance `rollVer = gGenVer`.
  Randomize: `archetypeForSeedV6` / `generateSoundV6` / `styleForSeedV6` in
  the three adjacent lines. gpat_stats prints `styleForSeedV6` for ver ≥ 6.
- [ ] Native gate RED/GREEN. `pio run` RAM line: budget ≤ +64 B (one table).
- [ ] Commit: `Gen V6 scaffold: V5 called, never copied`.

### Task B3 — the rules, one per data row

**Files:** `src/dsp/sound_gen.cpp` (`archetypeForSeedV6`, `applyStyleV6`,
`rollPolishV6`), `src/test_dsp.cpp` (a 700-seed V6 sweep block; each rule is a CHECK).

Pool table V6 (40 entries; V5 counts in brackets): pluck 3, bell 3, pad 4,
bass 3, acid 3, **lead 4 [3]**, brass 2, **chip 1 [2]**, **wild 1 [2]**,
whistle 2, organ 2, **keys 4 [3]**, wobble 2, strings 2, drone 2, gate 2.
Why: lead is 17/19 good and holds two of the three "great" rolls; keys 7/9;
chip and wild are the sinks. Both stay in the pool — variety is the point —
at half weight. Scramble constant: the next word of pi's fraction after
`0x03707344` (look it up; do not reuse a V3/V5 constant).

`rollPolishV6` clamps (after `rollPolishV5`), each with its data row:

| family | rule | data |
|---|---|---|
| chip | `cutoffHz ≤ 5000`; `releaseS ≥ 0.22`; `lfo1RateHz ≤ 6` | good chip 1.9 kHz / 0.38 s; bad 6.9 kHz / 0.15 s, LFO 5-9 Hz |
| wild | `attackS ≤ 0.15`; `releaseS ≤ 0.8`; `drive ≤ 3.5` | both bad wilds: attack 0.47 / 0.77 s, release 1.85 / 1.56 s, drive 4.9 / 5.2, classified pad |
| pad | `drive ≤ 1.6`; `subLevel ≤ 0.25` | good 1.5 / 0.10 vs bad 1.84 / 0.30 |
| pluck | `cutoffHz ≥ 800`; `decayS ≥ 0.35` | good 1440 Hz / 0.50 s vs bad 616 / 0.31 |
| bass | `cutoffHz ≥ 450` | bad 339 vs good 729 |
| whistle | `cutoffHz ≤ 5000`; `autoVibCents ≥ 5` | bad 6.05 kHz, no vibrato; good 4.05 kHz with vibrato |
| drone | `lfo1RateHz ≥ 0.6`; `autoVibCents ≥ 2`; `wave == Sine → Triangle` | bad drones at 0.30 Hz, no vibrato, both sine |
| acid | `fenvOct ≥ 2.6` | good 3.24 vs bad 2.43 |

`applyStyleV6` corrections (run after V5's recolor):

| family, style | correction | data |
|---|---|---|
| pad 2 "cinema" | cutoff back up ×1.4 (net ×0.7); sub −0.15 (net +0.15); `reverbSize ≤ 0.70`; `reverbMix` −0.07 (net +0.08) | style mean 1.90; reverbSize 0.85 on the bad ones |
| pluck 1 "kalimba" | cutoff ×1.45 (net ×0.8), floor 900; decay ×1.3 (net ×0.85) | 2.33 vs classic 3.00; the cutoff/decay separators |
| bass 1 "round" | cutoff ×1.33 (net ×0.8); drive ×1.5 (net ×0.75) | 2.64 vs 3.00; the cutoff/drive separators |
| chip 2 "arcade" | `cutoffHz ≤ 4000` (V5 forced ≥ 7000); `lfo1RateHz ≤ 5` | every bad chip sat at 7 kHz |
| whistle 1 "airy" | cutoff ×0.85 (net ×1.1) | bad whistles brighter than good |
| bell 1 "music box" | `decayS ≥ 0.7` (was 0.5) | n = 4, mean 1.75 — light touch only |

- [ ] Sweep test first (700 seeds × 16 archetypes × 3 styles): one CHECK per
  row above, plus the existing guardrails (sanitize couplings, audition-lick
  pitch landing, plays-audibly ⇒ previews-audibly, never near-silent).
- [ ] Implement the table and the two functions.
- [ ] Native gate RED/GREEN.
- [ ] Commit: `Gen V6 rules: what 202 rated rolls said about the windows`.

### Task B4 — prove it against the rated rolls (`support/gpat_stats/roll_diff.cpp`)

The one thing the ratings can verify *before* a second field round: every
roll Jordan called bad must actually change on a flagged field, and every
roll he called great must not change at all.

**Files:** create `support/gpat_stats/roll_diff.cpp` (start from
`Jordan's Testing/tools/regen_check.cpp`), README section.

- [ ] Tool: reads a `lab.csv` (seed, arch, rating), rolls each seed through
  V5 and V6 at the saved archetype, prints per-roll the fields that moved.
  Exit non-zero if any `1bad` roll is unchanged or any `4great` roll changed.
- [ ] Run it; paste the summary line into this doc ("15/15 bad rolls moved,
  3/3 great unchanged, N meh rolls moved").
- [ ] Commit: `roll_diff: the rated rolls audit V6`.

### Task B5 — the beta bin and round two

- [ ] `pio run`; RAM line vs the Part A bin; copy to the card under the
  owner's name for it.
- [ ] Jordan's rating lab is his own build. He rebases it on this V6 and
  rates a second 200 rolls; the new `lab.csv` + `.gpat` go in
  `Jordan's Testing/round2/`, and `tools/` re-run gives the before/after
  table per archetype. The target is not a number — it is that chip, wild,
  pad and whistle stop sitting at the bottom while lead stays where it is.

## Part C — the next lab build (not code in this repo; a request to Jordan)

The ten unrescued key misses live in the chroma, upstream of everything Part A
touches. To tune the front end on the host we need what the mic heard:

- The key lab should write each **audible round's PCM to SD as a 16 kHz mono
  WAV** next to the chroma row (`keylab/NNN-rN.wav`; ~96 KB per 3 s round,
  written after the round from the same buffer, before the next `record`).
  No RAM cost: the buffer already exists; the SD is mounted at boot.
- Keep the true-key column, and add a free-text `note` column for "power
  chords, no third" style observations — the label disputes above were
  guessable only from the titles.

With 47 labelled WAVs the whole chain — Goertzel bands, floor subtraction,
harmonic terms `kH2/kH3/kH7`, octave weights — becomes a host regression, and
"dominant heard as tonic" becomes a number to move instead of a story.

## Scoreboard (filled in as tasks land)

Primary landing is the song's canvas in every row below A2 (there is no
player scale any more), so "nothing sour" is the canvas number, not the
pentatonic one. The walk columns count songs whose first in-song-and-home
landing sits at or before that press.

| step | nothing sour | home right | both | 1 press | 2 presses | 3 presses |
|---|---|---|---|---|---|---|
| v3.3 shipped, player on min pent (baseline) | 38 / 47 | 27 / 47 | 22 / 47 | 22 | 22 | 22 |
| v3.3 shipped, player on Major | 29 / 47 | 27 / 47 | 21 / 47 | 22 | 22 | 27 |
| A2 czar, walk = pent · blues · runner-ups · twin | 29 / 47 | 27 / 47 | 21 / 47 | 22 | 22 | 27 |
| A2 czar, walk = runner-ups · twin · pent · blues | 29 / 47 | 27 / 47 | 21 / 47 | 26 | 31 | 36 |
| **A2 czar, SHIPPED: twin · runner-ups · pent · blues** | 29 / 47 | 27 / 47 | 21 / 47 | **26** | **31** | **36** |

Every order with the three rescuers in slots 1-3 scores the same; the twin
leads because it can never play sour. The two flavour slots (pentatonic,
blues) fix one song between them, at press 4. The 5 s minimum-heard change
is not in these numbers (it needs live rounds, not stored chromas).

## Verification protocol (both parts)

1. Native gate on this Mac: the direct `g++` line from `CLAUDE.md` minus the
   SSE flags, `src/ui/theme.cpp` appended; compare the failure list against a
   clean tree — only the known FX-hash line may differ.
2. `pio run`, read the RAM line, compare with the previous build. Rule 7.
3. Beta bin to the owner's card under the name he gives. Two bins, one per
   part, so feedback attributes cleanly: Part A first (it is what he feels on
   the first `fn`+`k`), Part B to Jordan for round two.
4. **No tag, no release, no push without asking.** Commit locally, report SHAs.

## Risks

- **The czar can feel worse to a Blues player** for one press: the primary is
  now the seven-note mode, and Blues sits one or three `space` presses away.
  The order rule in A2 decides that from the numbers; if the owner hates it on
  hardware, blues moves to position 1 (one constant) — it does not come back
  as a landing rule.
- **Mode flips at the right tonic** (Blackbird, Sunny Afternoon, the Kinks)
  now play sour where the old pentatonic-first landing did not. The pentatonic
  retreat catches the *conflicted* ones; the clean parallel flips it cannot,
  and only Part C's WAVs can fix those. Accepted for the beta; measured in the
  scoreboard.
- **Small n on the randomizer.** Every rule is a clamp toward the good median,
  never a new window, so the worst case is a family sounding a little more
  like its good rolls. Round two is the check.
- **RAM.** Part A removes code; Part B adds one 40-byte table. Both bins get
  the RAM line compared before they reach the card.
- **V5 has no goldens today** — B1 fixes that before B2 can accidentally
  change it.
