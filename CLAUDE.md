# GLIDE — agent notes

Synth firmware for the M5Stack Cardputer ADV. Read README.md first; it
carries the design intent (the keymap, the philosophy, the audio-path facts).

## Build / test

```
pio run                    # instrument -> dist/GLIDE.bin (Launcher SD: /apps/)
pio run -e phase0-probe    # hardware risk probe (audio gapless + key rollover)
pio run -e native          # host build of dsp/ + tests
.pio/build/native/program  # run the tests — must pass before any dsp/ change lands
```

On this machine `pio` lives at `~\.platformio\penv\Scripts\pio.exe` and the
native env needs `~\.platformio\packages\toolchain-gccmingw32\bin` on PATH.

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

7. **The mic is config-only at boot.** `internal_mic = true` in main.cpp sets
   pins + the ES8311 record callback but starts nothing (verified in the
   vendored M5Unified source). The codec is half-duplex; the entire
   `Speaker.end() → Mic.begin() → Mic.end() → Speaker.begin()` handoff lives
   solely in `io/listen.cpp` (LISTEN), which parks the render task first —
   `playRaw` lazily restarts the speaker, so an unparked render task would
   clobber the mic mid-record.

## Conventions (inherited from the sibling firmwares in ../CardPuter Custom)

- `` ` `` = exit, full-frame M5Canvas pushed once per frame (~30 fps), NVS
  keys ≤15 chars, Preferences namespace "glide", dist binary via
  support/copy_dist.py.

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
2 = with the archetype engine. `genver` moves to 2 ONLY when the seed itself is
new (first boot, wiped NVS, or the player's own Re-roll bank) — never as a side
effect of a firmware update. The Randomize button always uses the new engine
(fresh random seed each press; no continuity to preserve). The bank is
**curated**, not random: slots q..i are fixed factory sounds (q=GLIDE, w=ACID,
e..i = presets baked from the player's SD `.gpat` files — see `dsp/patches.cpp`),
and only the last two slots (o,p, i.e. `slot >= dsp::kFirstGenSlot`) are
GENERATIVE — regenerated from the seed on demand in `loadPatchData` (nothing
stored). `reRollBank()` resets the bank to those presets and rolls fresh randoms
for o,p. A RAM-only undo/redo history (`historyCheckpoint/Undo/Redo`) means a
Randomize/Mutate never trashes a sound the player liked. (Earlier builds filled
ALL of w..p generatively at first boot; the `regen1` self-heal reclaims any blobs
those left behind.)

SD library (`io/sd_store.{h,cpp}` + `ui/sd_browser.{h,cpp}`): one `.gpat` file
per patch, **the same tagged codec as NVS slots**, so cards and slots are
byte-compatible. Optional + failure-visible: the instrument is fully playable
with no card. **The SD/SPI pins in `config.h` are HARDWARE-UNVERIFIED** — confirm
on a real ADV before trusting SD (Phase 0 spirit); nothing about playing the
instrument depends on the card.

`env:native` compiles `dsp/` only, so `sound_gen` IS host-tested but
`glide_config` / `sd_store` / `sd_browser` are NOT. After touching those, keep
the native tests green and review carefully — there's no on-device build here.
(No `pio` in this environment; reproduce the native gate with
`g++ -std=gnu++14 -DGLIDE_HOST_BUILD -I src src/test_dsp.cpp src/dsp/*.cpp
src/storage/patch_codec.cpp`.)

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
