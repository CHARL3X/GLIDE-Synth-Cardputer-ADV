# 27 — Arp glide snap (field-driven maintenance)

**Status: built 2026-09-15, awaiting hardware test.** Ships on the
`gen-v5-variety` branch (its own commit) rather than its own branch — a
stated deviation from the one-doc-one-branch rule, because the field tester
runs one beta bin per session and this rode the same one.

**UI cost: zero rows, zero gestures** — the fix is automatic.

## The report

"Arp on, bottom row: any sound with more than ~10-15 ms of glide sounds like
ass at higher/faster tempos."

## The mechanism (why it smeared)

The arp alternates exactly two voice ids (`kArpIdA`/`kArpIdB`, `dsp/arp.h`)
*so that* each release tail rings under the next attack. At fast rates a
tail outlives two steps, so every other arp On re-pressed a still-active
voice and landed in `Synth::noteOn`'s re-press branch — which `legatoTo`'d
the tail to the new pitch at the patch's full `glideS`. A chord-interval
slide (3rds, 4ths, octaves) every other sixteenth is smear, not phrasing.
Backing notes were already exempt from the Always-glide slide-in and the
steal-with-glide paths; this was the one glide route left open to arp notes.

## The fix

`Voice::snapTo` (voice.{h,cpp}) — the `legatoTo` hand-off minus the slew —
and one condition in the re-press branch (`synth.cpp`): a **backing** event
reusing an **arp id** whose voice is a mere tail (`!held()`) snaps; the
`retrigger()` that always followed masks the snap with a fresh attack.
Player legato, drones, and loop playback keep their glides bit-for-bit.

## The test (`test_dsp.cpp`, arp section)

Zero-crossing rate over 80 ms after an arp-shaped id-reuse re-press with
`glideS = 0.35`: the snapped note reads ~440 Hz (≥26 crossings) where the
old behavior read a ~130 Hz still-travelling glide (~10). The companion
assertion drives the SAME sequence through non-arp ids and requires the low
count — proving player re-presses still glide, and (since both cases share
timing) that the arp case genuinely exercised the re-press path rather than
passing vacuously via a fresh voice.

## Still open / adjacent

Nothing. The glide-scaled-to-clock idea (glide as a fraction of step time,
rather than a snap) was considered and rejected for now: the snap is
simpler, matches "an arp is articulated steps," and the retrigger masks it.
If field use wants glidey arps as a *feature*, that's a future rate-aware
variant, priced separately.
