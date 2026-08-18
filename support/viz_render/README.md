# viz_render — see a screen before you flash it

Renders GLIDE's resting screen on the host and writes a contact sheet, so a
visual change can be reviewed in seconds instead of a build-copy-boot-wait-90s
cycle on hardware.

It is **not** a mock-up. It textually `#include`s the real `src/ui/screensaver.cpp`
over a stub `M5Canvas`, and links the real `src/ui/theme.cpp`, so the geometry,
the dither, the blend math and the palettes are all the shipping ones. If the
render is wrong, the firmware is wrong.

## Why it exists

The screensaver was first written blind and looked fine in source. Every real
problem was invisible until rendered:

- the figure used a third of the panel and read as an empty screen
- the depth cue did nothing — a "helix" that looked like two flat mirrored sines
- the glow wash quantised into hard vertical **bands**; dithering per column
  turned those into visible **stripes**; only the full 4x4 Bayer scatters it
- summed glow above 1.0 (where the two lights overlap) wrapped a `uint8_t` blend
  factor, which would have inverted the flare to **black** at the exact moment
  the screen is most interesting

That last one is a real bug that no test would have caught and that reading the
code did not catch.

## Build and run

Needs the same MinGW toolchain as `env:native` on PATH:

```
export PATH="$HOME/.platformio/packages/toolchain-gccmingw32/bin:$PATH"

g++ -std=gnu++14 -O2 -I support/viz_render/shim -I src \
    support/viz_render/render.cpp src/ui/theme.cpp -o viz_render.exe

./viz_render.exe                 # resting, phosphor
./viz_render.exe active          # with a loop/jam still sounding
./viz_render.exe active 9        # theme index 9 (paper — a LIGHT ground)
```

Writes `viz_<theme>_<state>.bmp`. To view as PNG on Windows:

```powershell
Add-Type -AssemblyName System.Drawing
$i = [System.Drawing.Image]::FromFile("$PWD\viz_phosphor_rest.bmp")
$i.Save("$PWD\viz_phosphor_rest.png", [System.Drawing.Imaging.ImageFormat]::Png)
```

Theme indices are `theme.cpp`'s `kPalettes` order: 0 phosphor, 1 cassette,
2 fusion, 3 anaglyph, 4 ultraviolet, 5 nostalgia, 6 acid, 7 mission,
8 drafting, 9 paper. The last two have **light grounds** — worth checking, since
a screen tuned on black can invert badly there.

## The HOW TO PLAY page (`render_help.cpp`)

The manual is a designed screen too, so it gets the same treatment: `help.cpp`
splits its draw into a pure `drawPage(canvas, top)` (guarded by
`GLIDE_HOST_BUILD` so the interactive loop stays out of the host build), and the
shim carries the REAL Font0 glyph table (`shim/glcdfont.h`, vendored from
M5GFX) so text renders with the shipping shapes and metrics. One panel per page
of scroll:

```
g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I support/viz_render/shim -I src \
    support/viz_render/render_help.cpp src/ui/theme.cpp -o render_help
./render_help        # phosphor
./render_help 9      # paper (light ground)
```

Writes `help_<theme>.bmp`. First run already earned its keep: the widest key
token (`fn+shift+q..p`) sat flush against its description column until the
render showed it.

## Extending it

The perform screen's eight scope modes are **not** renderable this way:
`perform_screen.cpp` pulls in M5Unified, the keyboard, storage and the audio
engine, and its viz state lives in a union sized to the scope rect. Only
host-clean sources work — today that is `ui/screensaver.cpp` and the pure
`drawPage` half of `ui/help.cpp`.

To point the harness at another screen: swap the `#include` near the top of
`render.cpp`, stub whatever hardware namespaces it reads (as `audio::lead()` is
stubbed there), and add any missing primitives to `shim/M5Cardputer.h`. Keep the
shim's semantics matching LovyanGFX — endpoint-inclusive `drawLine` especially —
or the renders will quietly lie.

Frame sampling lives in `kWant[]`. Sample widely: the screen's cycles are
deliberately non-dividing, so nearby frames all look the same.
