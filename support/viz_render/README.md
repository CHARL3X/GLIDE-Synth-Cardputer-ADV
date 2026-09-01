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

`render_theme.cpp` renders the CUSTOM palette instead of a fixed one — the
eleventh theme slot, whose eleven roles are derived from five dials. It draws
the same resting screen plus a strip of the eleven raw roles, since the
screensaver alone does not exercise all of them.

```
g++ -std=gnu++14 -O2 -I support/viz_render/shim -I src \
    support/viz_render/render_theme.cpp src/ui/theme.cpp -o render_theme

./render_theme rolls            # 24 rolled looks        -> theme_rolls.bmp
./render_theme presets          # preset vs its refit    -> theme_presets.bmp
./render_theme sweep            # one dial at a time     -> theme_sweep_*.bmp
./render_theme dials 24 30 14 0 14                       -> theme_dials.bmp
```

`presets` is the important one: the left column is the authored palette, the
right is what `recipeForPreset()` makes of it — i.e. what a player sees the
instant they cycle onto "custom". It has already earned its keep twice. The
first derivation drove every role to a luminance TARGET, which silently
desaturated every hue that cannot be bright: phosphor's green refit to
near-white and ultraviolet's violet to grey, invisible in the source and
obvious in one sheet. The second bug ranked the two colour roles by saturation
to find the palette's hue, which let fusion's blood-red data colour outvote its
teal wireframe and rotated the whole palette to red.

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

## The two LISTEN screens (`render_listen.cpp`)

```
g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I support/viz_render/shim -I src \
    support/viz_render/render_listen.cpp src/ui/theme.cpp src/dsp/key_detect.cpp \
    -o render_listen
./render_listen        # phosphor
./render_listen 9      # paper (a LIGHT ground)
./render_listen all    # every palette, one sheet each
```

Writes `listen_<theme>.bmp`: eight panels covering the live view (silent room,
hearing, a round landing) and the verdict card (plain, retuned + tempo, nudged
to a second guess, the pentatonic retreat, a weak verdict).

It exists because both screens have states you cannot conjure on demand — and
the one that was WRONG was the emptiest: with nothing heard, the live view drew
twelve 2 px stubs on bare ground and read as a hole in the screen with the title
floating oddly above it. That is obvious in a contact sheet and invisible in
source. The same sheet is how the verdict card's float was tuned, and how the
one real theme hazard here got caught: text drawn on the card must pass
`theme::kPanel` as its background colour, because `kBg` punches a
screen-coloured box behind every glyph — invisible on phosphor, where both are
black, and glaring on the two light palettes.

`ui/listen_screen.cpp` is host-clean the same way `help.cpp` is: the two draws
are pure and public, and everything with a keyboard, a microphone or a clock in
it sits behind `#ifndef GLIDE_HOST_BUILD`.

## Extending it

Font0 is the only REAL glyph table the shim carries. `Font2` and `Font4` are
approximated by scaling it 2x and 3x, which runs a little wide and a little
short against the proportional originals — a layout that fits in a render fits
on the device, but do not judge letterforms by it.

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
