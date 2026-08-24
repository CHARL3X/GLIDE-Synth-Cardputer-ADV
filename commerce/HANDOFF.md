# Where the work is — a cold-start note

Written 2026-08-23 for whoever picks this up next (human or agent) on a fresh
machine. Read `CLAUDE.md` and `README.md` first; this file only says *what is
in flight*, not how the project works.

## Context

A small run of **pre-flashed Cardputer ADV units** is being prepared — hardware
bought, flashed, tested and boxed by the author, sold direct. The demand is
real and already counted (a demo video did numbers; the inbox did the rest).

The commerce side of that — listing copy, pricing, fulfilment — is deliberately
**not in this repository**, per the licence change in `LICENSING.md` ("the code
stays public; the commerce stays home"). `commerce/` is gitignored so a local
working copy can live there without ever being committed. Nothing below depends
on that material; these are all firmware/repo tasks that happen to be gated by
the run.

Three items, in the order they block things.

---

## 1. Ship a post-v2.5 build (blocking)

`src/config.h` still reads `kVersion = "v2.5"`, and `LICENSING.md` states the
PolyForm noncommercial grant applies to **everything after v2.5** — v2.5 and
earlier shipped GPL-3.0, and that grant is irrevocable.

So the binary currently in `dist/` is a GPL binary. Distributing it on hardware
is permitted but carries GPL obligations, and it does not sit under the
"commercial use is the author's alone" terms the rest of the repo now asserts.
Any unit flashed for the run should carry a **v2.6+** build.

Steps:

1. Bump `cfg::kVersion` to `"v2.6"` (it is the only place the version lives —
   the splash reads it).
2. `pio run -e native && .pio/build/native/program` — green before anything.
3. `pio run` — **check the RAM line against the previous build** (198936 B at
   the last measured commit; the frame-buffer sprite boots within ~1 KB of the
   ceiling, see `CLAUDE.md` rule 7).
4. Rebuild `dist/GLIDE.bin` via `support/copy_dist.py`.
5. Tag the release. Release notes should say plainly that this build is
   PolyForm-licensed and that v2.5 and earlier remain GPL — people will ask.

This is a version bump and a tag. It is not a feature; don't let it grow into
one.

## 2. Verify the SD pins on real ADV hardware (blocking a promise)

`src/config.h` marks `kSdSckPin` / `kSdMisoPin` / `kSdMosiPin` / `kSdCsPin` as
`VERIFY on ADV`. They have never been confirmed on the actual board.

This does **not** affect booting GLIDE — the Launcher reads the card with its
own driver, so `/apps/GLIDE.bin` loads regardless. What is unproven is GLIDE's
own SD access: the `.gpat` library under `/glide`, the SD browser, save-to-card.

Test, on one ADV, in this order:

1. Put two or three `.gpat` files in `/glide` on the card.
2. Open the SD browser. Does it list them? Does loading one land the sound?
3. Save a sound to the card. Does the file appear, with the right name?
4. Pull the card, read it on a PC, put it back. Still listed?

Then update the comments in `config.h` with what was found — verified pin
numbers if they work, corrected ones if they don't. Either way the `VERIFY on
ADV` markers should stop being true after this. Record it the way the Phase 0
probe results are recorded in `docs/design.md`; that is the house habit and it
exists because guessing at hardware is how this project has been burned.

If the pins are wrong, the SD library is off the table until they're fixed, and
anything promising a preloaded card has to change.

## 3. The author's own `.gpat` library — get it into the repo

The author has a set of `.gpat` patches saved off their own device and wants
them handled. They are not in the repo yet; they'll arrive by copy from the PC.

When they land:

- **Check the names first.** `storage/patch_name.{h,cpp}` holds the rules
  (case-preserving, spaces allowed, trimmed, ≤20 chars). More importantly, FAT
  lookup is **case-insensitive**: two files whose names differ only in case
  collide on a card and one spelling silently wins. Compare with
  `store::patchNameEqualsFold`, never `strcmp` — flag any collision before the
  set is treated as final.
- **Then decide what they are**, and say which:
  - *A card library* — a `/glide` folder cloned onto cards as-shipped sounds.
    Costs nothing in firmware, gated entirely on item 2 above.
  - *Factory presets* — baked into `dsp/patches.cpp`. There is direct precedent:
    slots `e`..`i` are six sounds the author designed and saved to SD, decoded
    from their `.gpat` files and compiled in verbatim (see the bank comment at
    the top of that file). This is the heavier option: it changes the in-box
    bank for everyone, and the factory bank is what a reset returns to.
  - Or both, for different patches.

Note what does **not** change either way: slots `o` and `p` stay generative,
regenerated from the unit's own seed. That is the "no two units sound alike"
property and nothing in this task should touch it.

---

## Two facts that keep surprising people

Both are load-bearing for the run, and both are the *intended* behaviour — do
not "fix" either.

- **A factory reset does not wipe a unit's identity.** `eraseAllStorage()`
  deliberately rewrites the seed, the genver, and the odometer after the erase,
  so the generative slots come back as the sounds the player knows and the
  lifetime counters ride across. The only thing that rolls a fresh seed is the
  player's own *Re-roll bank* (`reRollBank()`). Consequence for a hardware run:
  any minutes and notes put on a unit while testing it **ship with that unit**
  and cannot be cleared. Keep test sessions short; be honest with buyers about
  it rather than trying to hide it.
- **A unit's seed is created on its first ever boot**, from `esp_random()`, and
  persisted (`storage/glide_config.cpp`). A new seed takes `genver = 3`, so any
  unit first powered on with a current build gets the full fourteen-archetype
  pool. Whoever boots it first creates it — that is fine, it is still unique to
  that device, and it is stable from then on.

## The gate, as always

`pio run -e native && .pio/build/native/program` green, and the `pio run` RAM
line checked against the previous build, before anything lands.
