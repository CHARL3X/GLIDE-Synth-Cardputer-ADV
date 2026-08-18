# GLIDE — design & internals

Where the instrument came from, the ideas it encodes, and how the firmware is built. For playing it, see the [manual](manual.md); for install, the [README](../README.md).

> Lineage, for the curious: this is the second build of the instrument from the brainstorm, the "pitch touch bar" / "digital slide whistle," STRATA-1's little sibling. The touchscreen prototype proved sliding chords on a continuous-pitch surface is *absolutely sick*. This one answers a harder question: can that soul survive on 56 mechanical keys and a one-watt speaker? It can.

## The translation

The Cardputer's keyboard is a 4×14 matrix with staggered rows. Physically it's already a tiny fretboard, so GLIDE treats it like one:

- **Rows are strings.** Four of them, tuned a fourth apart (configurable), bottom row lowest. Columns step up the scale. It's an isomorphic grid, the same layout idea as the LinnStrument, the closest living relative of the instrument we sketched.
- **Continuous pitch lives in time, not space.** On glass you slid your finger. Here the *notes slide themselves*. Every legato transition portamentos: hold a chord shape, re-finger it three columns over, and every voice glides to its new target. That's the chord slide, the spark from the original conversation, intact.
- **Bend keys** (`[` and `]`) push the pitch up or down while held, like bending a string. Between glide and bend you can land anything between the twelve western notes. Microtonal, fretless, the whole point.

## The philosophy, encoded

- **One device, your sound.** Everyone who buys a Cardputer holds the same 56 keys and the same one-watt speaker, so the engine was built deep enough that no two players' instruments need sound alike: a modulation matrix (two LFOs, a second envelope, tilt and per-note random as sources, routable to pitch, filter, amp, drive, and the FX), a multimode filter, and a full send-FX rack. It ships sounding good, a curated bank of hand-tuned voices ready to play, but it's built to *become yours*. That's the real product. Roll a new patch, mutate toward a vibe, undo back to the one you liked, keep it on a slot or in an unlimited SD library named in your own words. Two of your ten slots are generative from first boot, rolled from a seed only your device has, so no two units start alike, and you can roll fresh ones any time. Owning one was never meant to mean sounding like everyone else who owns one. Thanks to the tagged patch format, none of what you make is ever wiped by a future update.
- **The skill gap is the product.** Basic play takes minutes (scale lock plus degree mapping means the first session sounds good). Mastery takes honest practice: clean legato overlaps, accurate shape re-fingering, controlled bends into chromatic passing tones, two-row voice management under the 4-lane limit. The gap between what you hear in your head and what your fingers can do closes slowly, the way it's supposed to.
- **Nothing hardcoded.** 20+ parameters, all editable on-device (a quick-edit layer for the performance-critical ten, the settings screen for the rest), all persisted to NVS with debounced writes.
- **Failures are visible.** If the audio path can't start you get a red AUDIO INIT FAILED screen with the reason, never a silently dead instrument. Rejected changes (octave ceiling and the like) flash red in the HUD. A pocket instrument that dies mid-jam without warning is the same sin, so below 20% battery the perform screen says so (blinking red at 10%), and settings always shows the exact percentage.
- **Effects in service of the sound, not a rack to get lost in.** A per-voice lowpass with resonance, soft saturation, and a speaker-protecting highpass, plus one shared send block: chorus, a tempo-synced delay, and a small reverb. Every send is editable and saved per slot, but the tunings are curated and the defaults do the work. The identity is the sounds and how it plays, not knob-twiddling. (The Omnichord rule, with a delay that finally locks to the beat.)
- **Tilt is never pitch bend.** Nobody wants to lean the instrument over again.

## Architecture (why it's split this way)

```
src/
├── dsp/        PURE C++ - no Arduino, no M5, no ESP-IDF. The instrument:
│               voices, glide engine, wavetables, filter, pitch math,
│               degree mapping. Compiles and tests on a PC (env:native).
├── io/         The hardware boundary: M5Unified playRaw streaming
│               (render task on core 0), positional keyboard reader,
│               IMU tilt. The ONLY code that knows it's on an ESP32.
├── ui/         Perform screen (scope/readout/grid-map/HUD), settings,
│               splash. Core 1, ~30 fps canvas.
└── storage/    NVS persistence, debounced.
```

The `dsp/` purity rule is the point. When this instrument grows into real hardware (a Daisy Seed brain, force-sensing strips, the deformable surface), the entire musical core moves over unchanged. The Cardputer is incarnation two. It won't be the last.

Audio path facts (verified against M5Unified source, not vibes): `playRaw` keeps a *pointer* (no copy) and queues 2 per channel, so GLIDE rotates 3 buffers and paces on `isPlaying()`. 32 kHz / 128-sample blocks gives a 4 ms cadence, ~12 ms output latency, under 25 ms key-to-ear. M5Unified owns the ES8311 codec's undocumented power-up sequence, which is why this firmware never touches raw I2S and why the library versions are pinned.

The generative sound engine (`dsp/sound_gen`) gets its own design doc: [random-sound-generation.md](random-sound-generation.md).

## Building

```
pio run                    # instrument -> dist/GLIDE.bin
pio run -e phase0-probe    # hardware probe
pio run -e native          # pure-DSP host tests (no hardware needed)
.pio/build/native/program  # run them
```

PlatformIO, `espressif32@6.12.0`, `m5stack/M5Cardputer@^1.1.1`. Serial monitor at 115200 (`pio device monitor`).

Direct USB upload (overwrites Launcher): `pio run -t upload`. Entry procedure: power OFF, hold G0, plug USB-C, release G0.

## Before you trust it: the Phase 0 probe

Two hardware assumptions need validating on *your* unit before the instrument's behavior can be trusted. The probe firmware tests both. Flash it the same way as the instrument (copy `dist/GLIDE-probe.bin` to `/apps/` on the SD for Launcher), or direct:

```
pio run -e phase0-probe -t upload
```

1. **Gapless audio.** Streams a 440 Hz sine through the same 3-buffer playRaw loop the instrument uses. The `STARVED` counter must stay 0 (green) for minutes. Press `space` to inject a deliberate 6 ms stall and prove the DMA headroom is real.
2. **Key rollover.** Mash chords; every key the keyboard controller reports lights green on the 4×14 grid, and `max seen` records your ceiling. The ADV's TCA8418 should do far better than the old matrix's ~3 keys. Whatever your ceiling is, set `voices` (fn+8) at or below it. Findings worth recording:

   | unit | starved (5 min) | max rollover | date |
   |------|-----------------|--------------|------|
   | _your Cardputer ADV_ | _?_ | _?_ | _?_ |
