# GLIDE — agent notes

Synth firmware for the M5Stack Cardputer ADV. README.md is the end-user
landing page (install + first five minutes); the design intent lives in
docs/manual.md (keymap, every feature) and docs/design.md (philosophy,
architecture, audio-path facts). Read those two first — anywhere an older
doc says "read the README", it means them.

## Build / test

```
pio run                    # instrument -> dist/GLIDE.bin (Launcher SD: /apps/)
pio run -e phase0-probe    # hardware risk probe (audio gapless + key rollover)
pio run -e native          # host build of dsp/ + tests
.pio/build/native/program  # run the tests — must pass before any dsp/ change lands
```

On this machine `pio` lives at `~\.platformio\penv\Scripts\pio.exe` and the
native env needs `~\.platformio\packages\toolchain-gccmingw32\bin` on PATH.

**Never design a screen blind — render it.** `support/viz_render/` compiles the
REAL `ui/screensaver.cpp` over a stub canvas plus the real `ui/theme.cpp` and
writes a contact sheet, in any of the ten palettes. Use it before asking a human
to flash and stare at a panel for 90 s. It has already caught things invisible in
source: a figure using a third of the panel, a depth cue that read flat, a glow
wash quantising into bands and then stripes, and a `uint8_t` blend factor that
wrapped and would have inverted a highlight to black. See its README.

## Hard rules

1. **`src/dsp/` is pure C++.** No Arduino.h, no M5*, no ESP-IDF, no
   `millis()`. It must keep compiling in `env:native` (gnu++14, and keep it
   C++11-friendly: no inline variables, no aggregate-init of structs with
   default member initializers — use `NoteEvent::make()`). This is the
   porting boundary for future dedicated hardware.
2. **Audio = M5Unified playRaw only, never raw I2S.** M5Unified owns the
   ES8311's undocumented power-up sequence. `playRaw` stores the *pointer*
   (no copy) and queues 2/channel → the 3-buffer rotation in
   `io/audio_engine.cpp` is load-bearing; don't reduce it.
3. **Failures must be visible.** Audio init failure → full-screen red error
   (`fatalAudio` in main.cpp). Never let the instrument be silently dead.
4. **Keyboard is read positionally** (`keyList()`, codes `y*14+x`), never via
   the char `word` — chars mutate under shift, which would break the
   momentary-chromatic gesture.
5. **Tilt is never pitch bend.** Rejected by the humans, on tape.
6. Library versions are pinned (`m5stack/M5Cardputer@^1.1.1`,
   `espressif32@6.12.0`) to match the verified Speaker_Class source. Don't
   bump without re-running the phase0 probe on hardware.

7. **RAM is at the ceiling — no new statics, no resident heap.** The 65 KB
   frame-buffer sprite boots within ~1 KB of the RAM limit: +1.2 KB of .bss
   is a MEASURED boot failure ("UI ALLOC FAILED"), and a 4 KB static broke
   first hardware contact of the LISTEN tempo feature. Resident heap is just
   as banned: LISTEN sizes its record rounds from the largest free block, so
   a lingering allocation silently shrinks fn+k forever (a 19.5 KB field did,
   measured). New frame-to-frame UI state goes INSIDE the perform screen's
   VizState union; anything bigger is malloc'd for a modal's lifetime and
   freed on every exit path. Check `pio run`'s RAM line against the previous
   build before calling any change done.
8. **The mic is config-only at boot.** `internal_mic = true` in main.cpp sets
   pins + the ES8311 record callback but starts nothing (verified in the
   vendored M5Unified source). The codec is half-duplex; the entire
   `Speaker.end() → Mic.begin() → Mic.end() → Speaker.begin()` handoff lives
   solely in `io/listen.cpp` (LISTEN), which parks the render task first —
   `playRaw` lazily restarts the speaker, so an unparked render task would
   clobber the mic mid-record.

## Conventions (inherited from the sibling firmwares in ../CardPuter Custom)

- `` ` `` = exit, full-frame M5Canvas pushed once per frame (~30 fps), NVS
  keys ≤15 chars, Preferences namespace "glide", dist binary via
  support/copy_dist.py, which also regenerates `dist/NOTICES.txt`
  (support/gen_notices.py). That file is the MIT/BSD/Apache/LGPL attribution
  that must ship with any copy of the binary — a download, an SD card, or a
  device sold with GLIDE on it. Adding a `lib_deps` entry means adding its
  licence to support/licenses/ and a section to gen_notices.py; the generator
  warns about anything in `.pio/libdeps/` it has no section for. `cfg::kVersion`
  is the only version string and gen_notices.py reads it, so the notices can't
  drift from the build.
- Every file under `src/` and `support/` opens with the two-line SPDX +
  copyright header (`PolyForm-Noncommercial-1.0.0`, Charles Tobin / CHARL3X) —
  new files included, and `support/gen_logo.py` emits it into the header it
  generates. The one exception is `support/viz_render/shim/glcdfont.h`, which
  keeps its Adafruit BSD notice. GLIDE is dual-licensed (PolyForm NC + a
  commercial licence; releases ≤ v2.5 shipped GPL-3.0 irrevocably), so
  contributed code needs the relicensing grant in CONTRIBUTING.md; see
  LICENSING.md before changing anything about licensing or branding.

## The generative sound system (the "your instrument is yours" core)

The randomizer is a first-class engine feature, not a UI gimmick. It lives in
`dsp/sound_gen.{h,cpp}` (pure, seeded, deterministic, host-tested):
- `generateSound(seed)` rolls a complete `GenPatch` (synth + tilt). Deterministic
  so a per-device seed gives every unit a unique-but-reproducible bank. It is
  ARCHETYPE-driven: the seed first picks a character (pluck/bell/pad/bass/acid/
  lead/brass/chip/wild, weighted), then paints correlated values inside that
  character's window — this is what keeps rolls from regressing to one mid-
  everything mush. Every roll ends in `sanitizePatch` (pure coupling rules: HP
  keeps a body, reso×drive can't shriek, echo+hall can't jointly wash out,
  pitch-mod depth ≤ ~1 semitone, always-glide ≤ 0.16 s so notes LAND…). The
  test suite asserts the variety, the guardrails, and that every roll lands
  the audition lick's final pitch.
- `generateSoundV3(seed[, a])` is the EXPANDED pool (genver 3): the same paint
  engine over `archetypeForSeedV3`, which adds the second wave — whistle/organ/
  keys/wobble/strings — at ~a quarter of the rolls. The V3 layer also applies
  `rollPolish` (pure rules, no RNG) to EVERY archetype — a pure sine/triangle
  can never be stranded behind an HP/BP passband above its only partial, the
  frozen pool's one dead-roll quirk (it made rolls audition silent yet play
  subtle). New-roll fixes belong in that layer, never in the frozen paint. `generateSound(seed)` (the v2
  nine-archetype pool) is now FROZEN exactly like the legacy engine, pinned by
  golden hashes: genver-2 devices re-derive their o/p slots through it every
  boot, so its output — `archetypeForSeed`, the nine v2 paint windows, and
  `sanitizePatch` — must never drift. New archetypes go in new switch cases +
  the V3 table only. The five second-wave paint windows are deliberately shaped
  so the FROZEN `classifySound` names them from existing families (whistle→lead
  words, wobble→bass, organ→wild/bass, keys→wild, strings→pad/lead) — the
  reserved `kFamNouns` rows for them are unreachable until a future *versioned*
  classifier exists; extending `classifySound` itself would relabel players'
  re-derived slot names.
- The audition phrase's NOTES/ids are fixed (hard-won honesty — see
  docs/random-sound-generation.md) but its CLOCK is per-patch:
  `dsp/audition_plan.h` stretches the phrase and holds the final note for
  slow-developing characters (swells, blooms, flutter, synced wobbles). Any
  change to the phrase must update the mirrored walk in `test_dsp.cpp`
  (`walkAuditionLick`), which enforces pitch-landing AND the plays-audibly ⇒
  previews-audibly invariant.
- `classifySound` + `soundNameForPatch` name a sound from its own character
  (family noun + timbre adjective; word choice from `patchHash` bits). The old
  `soundName(seed)` word tables are FROZEN: genver-1 devices re-derive their
  o/p slot labels through them every boot, so an update must never relabel.
  Bank arrays are append-only — never reorder or replace existing words.
- `generateSoundLegacy(seed)` is the pre-archetype generator, FROZEN and pinned
  by golden hashes in `test_dsp.cpp` — existing devices regenerate their o/p
  slots with it (see `genver` below) so an update never changes a sound a
  player already has. Never edit it.
- `mutateSound(base, amount, seed)` evolves a patch within its neighbourhood.
- `patchHash` + `nameForSeed` give a sound an evocative, content-derived name.
- All bounds live in one place (the `Range` table) so generate and mutate can't
  drift a value out of the playable window. If you add a `SynthParams` field,
  add it to `generateSound`/`mutateSound` too (and a `Range` if continuous).

Storage (`storage/glide_config.cpp`): a per-unit `seed` (NVS) plus a `genver`
flag: 1 (or absent) = the o/p slots regenerate with `generateSoundLegacy`,
2 = with the frozen v2 archetype engine (`generateSound`), 3 = with the
expanded pool (`generateSoundV3`). `genver` moves forward ONLY when the seed
itself is new (first boot, wiped NVS, or the player's own Re-roll bank) — never
as a side effect of a firmware update. The Randomize button always uses the
newest engine (fresh random seed each press; no continuity to preserve). The bank is
**curated**, not random: slots q..i are fixed factory sounds (q=GLIDE, w=ACID,
e..i = presets baked from the player's SD `.gpat` files — see `dsp/patches.cpp`),
and only the last two slots (o,p, i.e. `slot >= dsp::kFirstGenSlot`) are
GENERATIVE — regenerated from the seed on demand in `loadPatchData` (nothing
stored). `reRollBank()` resets the bank to those presets and rolls fresh randoms
for o,p. A RAM-only undo/redo history (`historyCheckpoint/Undo/Redo`) means a
Randomize/Mutate never trashes a sound the player liked. (Earlier builds filled
ALL of w..p generatively at first boot; the `regen1` self-heal reclaims any blobs
those left behind.)

**The live sound's identity is STORED, never re-derived.** The live sound rides
flat, quantized NVS keys (attack in ms, detune in *whole* cents) while a slot
rides the exact-float blob, so the two do NOT round-trip to the same
`patchHashFull` bucket — measured, 244/500 generated sounds and 5/10 factory
patches differ, across 19 of the 24 continuous fields. Boot therefore restores
the name (`lvnm`) and the saved-or-not state (`lvclean`) from NVS rather than
inferring them from a live-vs-slot hash compare; inferring renamed a player's
sound to a fresh content name and flagged saved sounds as unsaved. Absent keys
fall back to the old derivation for devices that predate them. Never reintroduce
a hash compare between the live sound and stored bytes — it is a coin flip.
(Consequence, accepted: detune/auto-vibrato settle by <1 cent on the first
reboot. Widening those keys needs a unit migration; it is sub-audible.)

SD library (`io/sd_store.{h,cpp}` + `ui/sd_browser.{h,cpp}`): one `.gpat` file
per patch, **the same tagged codec as NVS slots**, so cards and slots are
byte-compatible. Optional + failure-visible: the instrument is fully playable
with no card. The SD/SPI pins in `config.h` are verified on real ADV hardware
(the library saves and loads in regular use); nothing about playing the
instrument depends on the card.

`env:native` compiles `dsp/` only, so `sound_gen` IS host-tested but
`glide_config` / `sd_store` / `sd_browser` are NOT. After touching those, keep
the native tests green and review carefully — there's no on-device build here.
(Reproduce the native gate without `pio` via
`g++ -std=gnu++14 -O2 -msse2 -mfpmath=sse -DGLIDE_HOST_BUILD -I src
src/test_dsp.cpp src/dsp/*.cpp src/storage/patch_codec.cpp
src/storage/patch_name.cpp`. The SSE flags are load-bearing: the pio mingw
toolchain is 32-bit, and its default x87 excess precision truncates one
borderline hash-quantise product differently than the device FPU, failing a
frozen-generator golden.)

`storage/patch_name.{h,cpp}` is the other host-safe file: the SD patch-name
rules (case-preserving, spaces allowed, trimmed, ≤20 chars). `sdstore::sanitize`
is a one-line forwarder to it so `env:native` can test rules that `io/` can't
host-build. NOTE: FAT lookup is case-INSENSITIVE — writing "Big" over an
existing "big" keeps the OLD spelling on the card, so anything that deletes an
old file after writing a new one must compare with `store::patchNameEqualsFold`,
never `strcmp` (see the rename path in `ui/sd_browser.cpp`).

## Adding a sound parameter (the expansion-safe way)

Patches are a **tagged format** (`storage/patch_codec.{h,cpp}`), so adding a
`SynthParams` field never wipes saved patches. To add one:
1. Add the field to `dsp::SynthParams` with a **neutral default** (so the stock
   tone is unchanged and `test_dsp.cpp` keeps passing).
2. Give it a **new, never-reused tag** in `patch_codec.cpp`'s `Tag` enum + a row
   in `buildTable` (append-only — never renumber a tag).
3. Persist the live value as a flat NVS key in `glide_config.cpp`
   `begin()`/`persistNow()` (absent key → the neutral default).
4. Add a `format`/`adjust` pair + a `kItems[]` row in `settings_screen.cpp`.
5. Consume it in `synth.cpp` (per-block is ~free; avoid per-sample).
The modulation matrix (LFOs / mod-env / 6 routing slots) and the multimode
filter were both added this way — copy them as the pattern.
