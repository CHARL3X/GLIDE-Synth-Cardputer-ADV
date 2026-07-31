# LISTEN auto-scale (mode-matched scale swap)

**Date:** 2026-07-31
**Status:** approved design, pre-implementation

## Problem

LISTEN detects a key (root pitch class + major/minor) but applies only the
root. When the detected mode disagrees with the player's scale,
`applyRootForScale` shifts to the relative key so the *notes* fit — but the
keyboard's home key is then not the song's tonic. Song in A minor + player on
Major pent → root becomes C: every phrase the player resolves "home" lands on
C, while the song resolves to A. Right notes, wrong feel.

The detector already computes the mode bit every round; it is currently
discarded at apply time.

## Decision

When LISTEN locks a key whose mode disagrees with the current scale, and the
scale has an obvious opposite-mode sibling, **swap the scale to the sibling
and put the root on the detected tonic**. Only the four "vanilla" scales
participate:

| current scale | detected mode | becomes |
|---|---|---|
| Major (`SC_MAJOR`) | minor | Natural minor (`SC_MINOR`) |
| Natural minor | major | Major |
| Major pent (`SC_MAJ_PENT`) | minor | Minor pent (`SC_MIN_PENT`) |
| Minor pent | major | Major pent |
| everything else | either | unchanged |

Exotic scales (Blues, Dorian, Mixolydian, Harmonic minor, Phrygian dom,
Lydian, Whole tone, Hirajoshi, Chromatic) are a deliberate flavor choice —
they keep today's relative-root behavior exactly.

Rejected alternative: full mode detection (dorian/mixolydian/… from the
chroma). Church modes are rotations of the same pitch-class set; a chromagram
is identical for C major / A minor / D dorian, so discrimination rests
entirely on tonic-emphasis — the weakest signal in the system. Expanding the
candidate set would collapse confidence margins and worsen lock time. Off the
table.

## How it composes (the nice part)

`applyRootForScale(detectedPc, detectedMinor, scaleIdx)` already returns
`detectedPc` unchanged when the scale's mode matches the detected mode, and
the relative shift otherwise. So the implementation is:

1. `newScale = applyScaleForKey(scaleIdx, detectedMinor)` — the new pure
   function (sibling table above).
2. `root = applyRootForScale(detectedPc, detectedMinor, newScale)` — the
   existing function, now fed the post-swap scale. For swapped or
   already-matching scales it returns the true tonic; for exotic scales it
   does today's relative shift. No new root logic.

## Components

### `src/dsp/key_detect.{h,cpp}` (pure, host-tested)

New function:

```cpp
// The scale to actually play under a detected key: the four vanilla scales
// swap to their opposite-mode sibling when the detected mode disagrees
// (Major<->Natural minor, Maj pent<->Min pent); every other scale — an
// exotic, deliberate flavor choice — is returned unchanged.
int applyScaleForKey(int scaleIdx, bool detectedMinor);
```

Out-of-range `scaleIdx` returns it unchanged (mirrors `scaleIsMinorish`'s
guard).

### `src/ui/listen_screen.cpp`

In `run()`, where the result is applied (`listen_screen.cpp:221-226`):
compute `newScale` first, feed it to `applyRootForScale`, write **both**
`g.layout.scaleIdx` and `g.layout.rootSemis`, `markDirty()` as today.

The swap applies whenever the root applies — including weak-signal results.
Symmetry with today: if the root is trustworthy enough to retune, the mode
bit that came with it is trustworthy enough to pick the sibling. Worst case
(wrong relative twin) is note-for-note identical to today's *every-time*
behavior.

The in-progress stability check (`prevApplied`) keys on the applied root;
with the mode fixed per-classification, root+scale move together, so the
existing root-only check still captures stability. Unchanged.

### Result card + HUD (`drawResult`, final `hud::show`)

- Sub-line gains the scale when it changed:
  `-> root A · min pent` (short name from `kScales[...].shortName`).
- The existing `RETUNED` badge also covers a scale swap: show it when
  `applied != prevRoot || newScale != prevScale`.
- Final HUD: `hud::show("KEY", ...)` value becomes `A min pent` when the
  scale swapped, plain root name otherwise (fits the HUD width; short names
  are ≤6 chars).
- Serial log line gains the scale swap for debugging.

### Confidence / stop rule: deliberately untouched

`classifyChromaForScale` still excludes relative-twin rivals from the
confidence margin, computed against the **current** (pre-swap) scale. Under
auto-scale the twin choice now changes the outcome (different tonic), so
this is a real trade-off, made consciously: including the twin would deflate
confidence and slow every lock — reintroducing the "won't lock" complaint —
to defend against an error whose cost is landing exactly where today's
behavior lands *always*. Fast lock wins.

## Error handling

No new failure modes: no allocation, no I/O, no new gates. Invalid guess /
cancelled / mic-failure paths are unchanged. `applyScaleForKey` is total
(returns input on any unexpected index).

## Persistence

`layout.scaleIdx` is already persisted by the existing config path;
`markDirty()` already fires on apply. Nothing new to store.

## Testing (native, `test_dsp.cpp`)

1. Sibling swaps: all four vanilla scales, both directions
   (Major+minor→Minor, Minor+major→Major, Mpent+minor→mpent,
   mpent+major→Mpent).
2. Mode already matches → unchanged (Major+major, mpent+minor).
3. Exotic scales unchanged for both modes (at least Blues, Dorian,
   Chromatic, Hirajoshi).
4. Composition: `applyRootForScale(pc, minor, applyScaleForKey(s, minor))`
   returns `pc` exactly when the post-swap scale's mode matches — i.e. for
   the four vanilla scales the root is always the true tonic.
5. Out-of-range scale index → returned unchanged.

Native gate: `g++ -std=gnu++14 -DGLIDE_HOST_BUILD -I src src/test_dsp.cpp
src/dsp/*.cpp src/storage/patch_codec.cpp` must pass.

## UI-cost budget (per project rule)

Zero new gestures, zero new settings rows. LISTEN's existing gesture gains
the behavior; the result card and HUD reuse existing text slots.
