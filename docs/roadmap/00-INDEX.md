# GLIDE roadmap — the idea bank

Fourteen designed-and-planned features, written 2026-07-06 (by Claude Fable 5, at the humans' request) so any capable agent can pick one up cold and execute it. Each doc is self-contained: design intent, exact files, task breakdown with test-first steps, risks, and its **UI-cost budget** against the house simplicity rule (*no new feature without pricing its UI: zero new gestures, ≤1 settings row — overruns are flagged in the doc, never hidden*).

**Read `CLAUDE.md` and `README.md` before any doc.** Every plan assumes both, plus the native test gate: `pio run -e native && .pio/build/native/program` green before anything lands.

## The bank

| # | doc | one line | effort | risk | UI cost |
|---|-----|----------|--------|------|---------|
| 01 | [pluck-voice](01-pluck-voice.md) | Karplus-Strong plucked string — glide becomes a *physical* gliss | M | low-med | 0 rows, 0 gestures |
| 02 | [wave-morph](02-wave-morph.md) | Osc B + blend as a mod destination — timbre becomes a continuous axis | M | low | 2 rows ⚠ (1-row variant specified) |
| 03 | [loop-quantize](03-loop-quantize.md) | Loop length snaps to the jam clock — looper + progression stop drifting | S | low | 1 row |
| 04 | [jam-sessions](04-jam-sessions.md) | Save the whole jam (progression + loop + sounds) to SD as `.gjam` | L | med | 2 rows ⚠ (mirrors sound save/load pair) |
| 05 | [wav-bounce](05-wav-bounce.md) | Record the master bus to WAV on SD — takes leave the device | M | med | 1 row |
| 06 | [usb-midi](06-usb-midi.md) | USB MIDI out, MPE-lite: chord slides arrive bent in a DAW | L | high (HW-gated) | 1 row (phase 2) |
| 07 | [microtonal](07-microtonal.md) | Just intonation + Rast/Bayati as ordinary scale entries | S | very low | 0 rows |
| 08 | [breed](08-breed.md) | Crossover two sounds — the third generative verb, lineage for patches | S-M | low | 1 row |
| 09 | [fx-freeze](09-fx-freeze.md) | Reverb freeze as a G0 trigger action — solo dry over your own frozen wash | S | low | 0 rows |
| 10 | [guide-tones](10-guide-tones.md) | Grid marks the current chord's tones while the progression walks | S | very low | 0 rows |
| 11 | [analog-drift](11-analog-drift.md) | Per-voice pitch wander — vintage-analog life, and a great generative gene | S | very low | 1 row |
| 12 | [wasm-glide](12-wasm-glide.md) | dsp/ compiled to WASM: GLIDE playable in a browser + CI + patch workbench | L | low (no FW risk) | n/a (off-device) |
| 13 | [chirp-share](13-chirp-share.md) | Patches travel speaker→mic as an audible chirp (moonshot, probe-gated) | L | high (HW-gated) | 1 row |
| 14 | [echo-trainer](14-echo-trainer.md) | Call-and-response practice that grades the *glide*, not just the note | M-L | low-med | 1 row |

## Suggested order

- **Warm-ups / immediate wins:** 11 (the parameter-recipe exemplar) → 09 → 07 → 03 → 10. Five small features, each shippable in a day or less, all high felt-value.
- **Core instrument depth:** 01 → 02 (02 lists 01's Pluck interaction) → 08.
- **The session layer:** 03 before 04 (quantized jams are worth saving); 05 anytime.
- **Ecosystem:** 12 early if development velocity matters (it makes every later DSP doc audible without hardware, and adds CI); 06 and 13 start with go/no-go hardware probes — run the probes cheap and early, build only on evidence.
- 14 after 07/10 if the "learn" pillar is the season's theme.

## Cross-doc coordination ledger (read before editing shared enums/tags)

These plans are written to land **in any order**. That only stays true if these reservations are honored exactly:

**Patch-codec tags** (append-only, never renumber — `src/storage/patch_codec.cpp`):

| tag | field | claimed by |
|-----|-------|-----------|
| 28 | `T_waveB` | doc 02 |
| 29 | `T_oscBlend` | doc 02 |
| 30 | `T_driftCents` | doc 11 |
| next free: 31 | — | future docs claim here first |

If doc 11 lands before doc 02, it still uses tag 30; 28–29 stay reserved. Update this table when claiming.

**Enum appends** (each enum touched by exactly one doc — no ordering hazard, but append before `Count`, never reorder):

- `dsp::Waveform` + `Pluck` — doc 01
- `dsp::ModSource` + `Const`, `dsp::ModDest` + `Blend` — doc 02
- `TriggerAction` + `Freeze` (glide_config.h) — doc 09
- `dsp::kScales` + 4 rows (Just major, Just minor, Rast, Bayati) — doc 07

**New NVS keys** (namespace "glide", ≤15 chars): `waveb`, `oscblend` (02) · `loopsnap` (03) · `driftcents` (11) · `usbmidi` (06 phase 2).

**New file formats:** `.gjam` (doc 04, magic `'G','J'`, introduces `T_BLOB=6` wire type in its own codec — the `.gpat` codec is untouched by every doc except tag appends).

**Shared seams multiple docs touch:** the G0 trigger dispatch in `keys.cpp` (09), the NoteEvent tap point where `looper::record` sits (06), the audio engine's render source (05 tap, 13 override hook) — each doc specifies a minimal, additive seam; if two land, the seams coexist.

## Execution rules (for whoever picks these up)

1. One doc = one branch = one PR. Don't batch.
2. Every doc's tests go red before its code goes in (the plans are written test-first — follow the order).
3. `dsp/` purity and the audio-path law are non-negotiable (CLAUDE.md rules 1–2); several docs exist *because* of that boundary — don't spend it.
4. Hardware-gated docs (06, 13) have explicit go/no-go probe tasks. The probe result gets written back into the doc. Building past a failed probe is a plan violation.
5. When a doc says "read X first," that's a step, not a suggestion — several plans deliberately leave a decision to what the code actually says.
6. Docs 02 and 04 exceed the 1-settings-row budget and say so; the human decides at review, the docs carry the trim variants.
