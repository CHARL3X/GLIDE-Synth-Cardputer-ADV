# Sound visualization — design

Date: 2026-07-01. Approved direction: "all three, phased".

## Problem

GLIDE's core identity is "your sounds are yours" — rolled, mutated, named —
but a patch has no visual identity. You can hear a sound; you can't see one.
Meanwhile the perform screen already visualizes pitch, loudness and timbre.

## Approach

One shared visual language, three surfaces. New module `ui/sound_viz.{h,cpp}`
holds small drawing primitives, each taking an `M5Canvas&`, a rect, and values
from `dsp::SynthParams`:

- `drawWave` — one cycle of the waveform, sampled from the REAL wavetable
  (`dsp::gTables`), so the icon is the wave. FatSaw = two detuned saw traces;
  Pulse = asymmetric-duty square (stylized).
- `drawEnv` — ADSR polyline, segment widths time-proportional with a floor so
  a 5 ms attack still reads.
- `drawFilter` — stylized LP/HP/BP/notch response; cutoff on a log 80 Hz–12 kHz
  axis, resonance bump at the corner.
- `drawGauge` / `drawBipolar` — fill bar; center-zero variant for mod amounts.
- `drawLfoIcon` — two cycles of sine/tri/saw/square, fixed step pattern for S&H.

UI-only: no `dsp/`, `io/` (beyond one call site in keys.cpp), or `storage/`
changes. Hard rules untouched. All colors from `theme.h`.

## Phase 1 — sound identity card (`ui/sound_card.{h,cpp}`)

A transient panel (kPanel fill, amber rounded border, HUD-style timed fade,
never blocking play) showing the live sound's face: name + unsaved `*`, wave
icon, ADSR curve, filter curve + mode/cutoff tag, glide-time gauge, and
cho/dly/rev send gauges.

Shown on:
- sound switch (fn+q..p) — replaces the plain "SOUND name" HUD flash
  (`keys.cpp:895`); the save confirmation keeps the HUD.
- Randomize / Mutate / Undo / Redo / Init in settings — drawn as an overlay
  while the audition phrase plays.
- SD-browser preview (space) — same overlay.

API mirrors hud: `soundcard::show()` arms a timer; `soundcard::draw(c, now)`
renders from `store::get()` (always-current, no snapshot). Perform loop draws
it after `hud::draw`.

## Phase 2 — quick-edit context viz (perform_screen.cpp)

While fn is held, the right column (sound list) becomes a context
visualization for the selected parameter: params 1–4 (ADSR) → live envelope
curve; 5 (wave) → big wave icon; 6 (cutoff) → filter curve. Any other param →
the sound list returns. fn+q..p still switches sounds regardless (keys don't
need the list). Zero new layout.

## Phase 3 — settings visual pass (settings_screen.cpp)

- `Item` gains an optional `float (*fill)()` (nullptr = no gauge). Continuous
  sound rows (resonance, detune, FX sends/feedback/sizes, tilt/trigger depths,
  mutate amt, LFO rates, mod-env times, bend time) draw a dim fill gauge
  between name and value.
- Mod-matrix amount rows use the bipolar center-zero gauge.
- LFO shape and Filter mode rows draw a small icon beside the value text.

## Verification

`env:native` doesn't compile `ui/`, so the gates are: native tests stay green
(dsp untouched), `pio run` device build compiles clean, then hardware review —
`dist/GLIDE.bin` copied to the SD `/apps/` for a flash-and-veto per phase.
Each phase is its own commit.
