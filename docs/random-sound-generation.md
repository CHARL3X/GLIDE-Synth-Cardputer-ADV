# Random sound generation + the SD patch library

> *"Everyone who uses this has a different experience, versus a standardized
> sound and setup. The randomize really makes that vibe come to life."*

This is the design note for the feature that turns the randomizer from a buried
settings action into the **center of gravity** of the instrument: every device
is unique out of the box, every player can roll/sculpt/keep their own sounds,
and an SD card holds an unlimited personal library. It builds on the seams the
codebase already laid for "the (future) SD-library load path" — it does not
fight the architecture.

## The ethos, made literal

Owning a Cardputer shouldn't mean sounding like everyone else who owns one. The
engine was always deep enough for that (mod matrix, multimode filter, full FX),
but the *default* was a standardized bank of ten named instruments. This feature
flips the default:

- **Your instrument is unique from first boot.** Each unit derives a stable
  random `seed` and rolls its nine non-anchor slots (`w..p`) from it. No two
  devices boot the same nine sounds. Slot `q` stays **GLIDE** — the home/boot
  anchor — so the signature tone and the instant-play experience never change.
- **Discovering a sound is play, not menu-diving.** Roll → audition → Mutate →
  audition → Keep. A tight loop with sound on the whole time.
- **You can explore fearlessly.** A non-destructive Undo/Redo history means a
  roll never costs you the sound you just liked. *Test without trashing.*
- **Your collection is yours and portable.** Save the sounds you love to SD as
  named files; they travel card-to-card and survive firmware updates.

## Architecture

```
dsp/sound_gen.{h,cpp}      PURE. generateSound(seed) / mutateSound(base,amt,seed)
  (host-tested)            -> GenPatch (synth + tilt). patchHash + nameForSeed.
        │
storage/glide_config.cpp   per-device seed (NVS), first-boot unique bank,
                           reRollBank(), RAM-only undo/redo history,
                           applyGenerated() / applyStoredPatch()
        │
storage/patch_codec.{h,cpp}  the tagged patch stream — ONE format for both
                           NVS slots AND SD files (byte-compatible)
        │
io/sd_store.{h,cpp}        .gpat files on microSD: save/list/load/delete
ui/sd_browser.{h,cpp}      the library browser (load-to-live, delete)
ui/settings_screen.cpp     the generative controls, in the existing SOUND menu
```

### Why the generator lives in `dsp/`

The randomizer used to be ~40 lines of `millis()`-seeded LCG inside the settings
UI. Moving it under the pure `dsp/` boundary buys three things at once:

1. **Determinism.** Seeded, so a per-device seed yields a reproducible bank — the
   whole "unique but stable" property depends on this.
2. **Testability.** `env:native` covers it: 200 rolls validated in-range and
   rendering finite/bounded, determinism, mutate range-safety, amount-0 identity.
3. **Portability.** When GLIDE grows into dedicated hardware, the sound
   *generator* ports over with the synth voice — the soul moves intact.

All field bounds live in one `Range` table, shared by `generate` (paint within)
and `mutate` (nudge a fraction of the span, then clamp back) — so a mutation can
never drift a value outside the playable window a roll respects.

### The slot model (what changed, what didn't)

The 10 NVS slots and the `fn+q..p` / `fn+shift+q..p` gestures are **unchanged**.
The named factory bank still exists in firmware (the "roots") and is what
*Reset all sounds* and per-slot *Sound reset* restore to. What changed:

| | before | after |
|---|---|---|
| `w..p` at first boot | named factory instruments | **rolled unique per device** |
| `q` (home/boot) | GLIDE | GLIDE (unchanged anchor) |
| `*` next to a name | "you saved over this slot" | "your own sound (made or saved), not stock" |
| randomize | buried, ephemeral | foregrounded loop: Roll/Mutate/Undo/Keep |
| library size | 10 slots | 10 fast slots + unlimited SD |

**Migration safety (Hard Rule #3 — never wipe a save).** The first-boot bank
only fills slots on a *fresh* device (`prevBoot == 0`) and never overwrites a
slot that already carries an override; it runs once (`bankgen` sentinel). An
existing device updating to this firmware keeps its factory bank and every saved
sound untouched, and opts into the generative world deliberately via
**Re-roll bank**.

### The SD library

One `.gpat` file per patch under `/glide` on the card. The file *is* the tagged
`patch_codec` stream the NVS slots already use, so a card patch and a slot patch
are byte-compatible and both forward/backward-compatible across firmware
versions. Saved sounds get an evocative, deterministic name derived from the
sound's own hash (`warm-haze-3f`) — re-saving the same sound is idempotent.

The instrument is **fully playable with no card**: `sdstore::available()` gates
every call, failures are visible (the browser shows the error), and the ten NVS
slots are the always-works core. SD only *extends* the library.

## ⚠️ Hardware-unverified: the SD/SPI pins

This environment has no device build and no way to exercise SPI/SD. The SD pins
in `config.h` (`kSdSckPin/MisoPin/MosiPin/CsPin`) are the **standard
M5Cardputer** values; the **Cardputer ADV pinout may differ**. In the spirit of
the Phase 0 probe: confirm on a real unit before trusting SD. If saves don't
appear, the CS/clock pins are the first suspects — fix them in `config.h`.
Everything else (generation, mutation, history, the unique bank, NVS slots) is
device-independent and host-tested.

## Player-facing surface (settings → SOUND)

- **Randomize** — roll a whole new patch; auditions instantly.
- **Mutate** + **Mutate amt** — evolve the current sound a little or a lot.
- **Undo / Redo** — step back/forward through sounds you've had this session.
- **Init sound** — a blank slate to build from by hand.
- **Save to SD** / **Load from SD** — the personal library.
- **Re-roll bank** — nine brand-new slots (`w..p`); `q` stays GLIDE.
- **Sound reset / Reset all sounds** — back to the named factory roots.

Keep a sound the fast way with `fn+shift+q..p` (onto a slot); keep it the
unlimited way with Save to SD.

## Possible next steps (not in this branch)

- A text-entry rename for SD patches (the Cardputer has a full keyboard; today
  names are auto-generated). Mind Hard Rule #4 if reading the keyboard.
- "Breed" two saved patches (genetic crossover) as a third generation verb.
- Persisting *Mutate amt* to NVS (currently a session preference).
- A boot-time `sdstore::begin()` so SD status is known before the first save.
