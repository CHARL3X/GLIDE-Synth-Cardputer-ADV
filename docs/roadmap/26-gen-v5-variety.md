# 26 — Gen V5: the third wave, style substreams, roll provenance

**Status: built 2026-09-15 on branch `gen-v5-variety`, awaiting hardware test.**
This doc is the record of what landed and why, in the bank's plan format so
the Phase-3 tuning work can pick it up cold.

**UI cost: zero new gestures, zero new settings rows.** Everything rides the
existing Randomize button, the sound card, and the `.gpat` format.

## Why

The randomizer clustered: fourteen archetypes, one personality window each,
and consecutive Randomize presses could serve the same family twice. Variety
is the "your instrument is yours" core, so the upgrade had to be audible on
the first ten presses of a flashed unit. Separately, the range-tuning session
(the team saving ~100 deliberately BAD rolls) needed its data capture shipped
BEFORE it happens — a saved bad roll had no record of which paint window
produced it.

## What shipped

1. **Roll provenance** — `{rollSeed, rollArch, rollVer}` on `store::PatchData`;
   codec tag **111 `T_rollProv`** (T_STR, 6 bytes, emitted AFTER `T_name` —
   pre-T_STR decoders stop at the first T_STR record and must keep the name);
   ONE packed NVS key **`rollid`** (u64: ver<<40 | arch<<32 | seed — one entry,
   not three, because of debt D1). Provenance survives edits and mutates by
   design (it answers "where did this come from"), rides the undo history,
   slot saves, and SD saves, and clears when a non-roll patch loads.
2. **`support/gpat_stats/`** — host CSV dump of `.gpat` folders (see its
   README): provenance + both classifier verdicts + every continuous field.
   The bad-roll session's output becomes a spreadsheet.
3. **Anti-repeat Randomize** (`ui/settings_screen.cpp aRandomize`) — redraw
   the fresh hardware seed (≤4 tries) until the archetype differs from the
   last press's. Session-only static, one byte.
4. **`generateSoundV5`** (`dsp/sound_gen.{h,cpp}`) — genver 5:
   - `archetypeForSeedV5`: 40-entry table; third wave ~1 roll in 10.
   - **Drone** and **Gate** archetypes (enum values 14, 15 — see ledger):
     new switch cases in the paint engine, reachable only from the V5 pool.
   - **Style substreams**: `styleForSeedV5(seed)` (own Rng stream, next pi
     word `0x03707344`) picks 0..2; style 0 = classic (bit-identical to V4,
     asserted); 1..2 are pure RNG-free recolors per family (`applyStyleV5`).
     Wild, Drone, Gate keep no styles (Wild's identity is chaos; the new
     windows await Phase-3 tuning).
   - `rollPolishV5`: superset of the FROZEN `rollPolish` (never edit that
     one — V3/V4 share it); adds Drone hold floors + Gate chop guarantees.
   - `classifySoundV2` + `soundNameForPatchV2`: the versioned namer reaches
     the reserved second-wave noun rows and the new drone/gate rows. Used
     ONLY for fresh mints (rolls/mutates) and genver≥5 regen — genver 1..4
     regeneration keeps its frozen word engines (`NameVer` in glide_config).
5. **Storage** — `loadPatchData` ladder gains `gGenVer < 5 ? V4 : V5`;
   regenerated o/p slots bake their own provenance; `dsp::kGenVerNewest = 5`
   now stamps first-boot and re-roll genver (the only places genver moves).

## Tuning round 1 (2026-09-15, same branch) — from the first field session

The first on-device session reported: whistle previews poorly / plays quiet /
over-glides; bell likewise (milder); styles too subtle. Measured with a
speaker-weighted probe (4-pole highpass at 900 Hz over the audition lick —
raw float peaks showed NOTHING wrong, because a lone sine partial at playing
pitch is simply below the 1 W driver's band; the weighting is what matches
the ear): bell's weighted presence was 8-10x under the saw families.

Landed, all in the still-unfrozen V5 layer:
- `rollPolishV5` whistle rules: drive floor 3.2 (the measured floor at
  which a pure tone carries the speaker — first found by the v3.2-era
  vocal-tract experiment), breath-noise cap 0.05, glide
  caps 0.09 s (Always) / 0.13 s.
- `rollPolishV5` bell rules: drive floor 2.8, glide cap 0.08 s, strike kept
  bright + ringing (cutoff ≥ 3 kHz, fenvOct ≥ 2, fenvDec ≥ 0.12 s), mallet
  clack (noise ≥ 0.035 — under the 0.06 gritty-naming gate).
- Style recolors ~1.5-2x bolder across every family; whistle style 2
  re-aimed from "dark flute" (wrong direction for a too-quiet family) to
  "flutter flute" (tremolo + wider vibrato); classic's share cut from 1/3
  to 1/5 in `styleForSeedV5`.
- Consequence, accepted: the classic==V4 bit-exact test now EXEMPTS whistle
  and bell — V5 deliberately re-tunes them. The suite instead pins the
  audibility levers directly (drive/noise/glide/ping floors and caps), plus
  classifier stability for both families.

Weighted-presence movement (median / p10 / min, 60 rolls each):
bell 0.0079/0.0024/0.0021 → 0.0123/0.0071/0.0062; whistle
0.0164/0.0112/0.0063 → 0.0229/0.0155/0.0123. The worst-case rolls — the
ones a player actually complains about — moved 2-3x. Keys shares the
pure-wave physics (0.0086 median, un-flagged so untouched) — a watch item
for the bad-roll harvest.

**Test-phase addition (2026-09-15):** the roll card's character tag now
prints the STYLE with the family — "gong bell", "glass pad"; bare family =
classic — so the tester's ear gets a label to hang differences on and a
bad-roll report can name the exact window+style. `showRolled` gained the
style argument; display words live in `ui/sound_card.cpp` `kStyleWord`
(Count-indexed like `kArchColor`: a future archetype append adds a row here
too). Zero gestures, zero rows.

## Phase 3 — the tuning loop (OPEN, gated on field data)

V5 freezes the moment a release ships (genver-5 devices re-derive o/p through
it every boot). Until then `applyStyleV5` + `rollPolishV5` + the Drone/Gate
paint windows are legal to tune. The loop: flash the beta → team rolls and
saves bad sounds → `gpat_stats` on the card → adjust windows/styles/polish →
native suite green → repeat. **After the release ships, tuning means V6.**

## Tests (all in `src/test_dsp.cpp`, native gate)

Determinism + picker contract; classic-style V5 == V4 bit-exact; styled rolls
differ ≥90%; 700-seed sweep (all 16 archetypes, every guardrail, bounded
render); Drone/Gate identity + versioned-classifier verdicts + never
near-silent; second-wave classifySoundV2 verdicts; V2 namer reaches the new
noun banks; audition-lick pitch landing + plays-audibly ⇒ previews-audibly
over V5; codec tag-111 round-trip, after-name ordering, absent-tag defaults;
V3 pool never rolls the third wave (the freeze tripwire, alongside the
existing legacy/v2 goldens).

## Risks / notes

- +264 B static RAM measured (noun rows, V5 table, provenance globals) —
  within rule 7's margin, but hardware boot must confirm ("UI ALLOC FAILED"
  is the failure face).
- The `arch` drawn in `aRandomize` and the provenance stamped must both come
  from `archetypeForSeedV5` — a future engine bump must update both together
  (they're adjacent lines).
- `soundcard` kArchColor is Count-indexed: any future archetype append needs
  a colour row in the same commit or the tag renders black.
