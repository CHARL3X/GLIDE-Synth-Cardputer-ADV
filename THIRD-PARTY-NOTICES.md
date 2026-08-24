# Third-party notices

GLIDE itself is licensed as described in [`LICENSING.md`](LICENSING.md). This
file lists code written by others that GLIDE either includes or links against.
Nothing here is affected by GLIDE's own licence, and each item stays under the
terms its own authors set.

**Shipping the binary?** This page is the map; the thing that has to travel with
`GLIDE.bin` is [`dist/NOTICES.txt`](dist/NOTICES.txt), which reproduces every
licence below in full plus the LGPL relink offer. It is regenerated on every
`pio run` by [`support/gen_notices.py`](support/gen_notices.py) — from the
licence files of the libraries the build actually linked, when they're on disk,
and from [`support/licenses/`](support/licenses/) otherwise. Include it in any
download, on the SD card of any device sold or given away, and link it from any
listing. That is not politeness; MIT, BSD, Apache-2.0 and LGPL-2.1 each require
it of anyone distributing the compiled result.

## Included in this repository

### Adafruit GFX 5x7 font glyph table — BSD 3-Clause

`support/viz_render/shim/glcdfont.h`
Copyright (c) 2012 Adafruit Industries. All rights reserved.

The full BSD licence text is reproduced at the top of that file and must stay
there. This file is part of the host-side render harness only; it is not
compiled into the device firmware.

## Fetched at build time (not vendored here)

PlatformIO resolves these from `platformio.ini` when you build. They are not
redistributed in this repository, but they *are* linked into the firmware
binary in `dist/`, so their terms apply to that binary as well as GLIDE's own.

| Component | Version pinned | Licence as published by its authors | Where that licence is stated |
| --- | --- | --- | --- |
| M5Cardputer | 1.1.1 | MIT | Per-file `SPDX-License-Identifier: MIT` headers — the repository has no root `LICENSE` file |
| M5Unified | 0.2.17 | MIT | `LICENSE` (© 2021 M5Stack) |
| M5GFX | 0.2.24 | MIT, with LovyanGFX-derived portions under FreeBSD (BSD 2-Clause) | `LICENSE` + `license.txt` (LovyanGFX, TFT_eSPI, Adafruit) |
| Arduino core for ESP32 (`espressif32@6.12.0`) | 6.12.0 | LGPL-2.1-or-later | `LICENSE.md` in `framework-arduinoespressif32` |
| ESP-IDF components beneath that core | as shipped with the platform | Apache-2.0, plus some third-party components under their own terms | Per-component notices under `tools/sdk/` |

Each library's authoritative licence text ships in its own `LICENSE` file
inside `.pio/libdeps/` and `~/.platformio/packages/` after a build; that text
governs, not this table. If you redistribute the compiled binary, ship
[`dist/NOTICES.txt`](dist/NOTICES.txt) with it — that is those texts, gathered.

### Installed but not linked

PlatformIO resolves M5Cardputer's own declared dependencies, so **IRremote** and
**LibSSH-ESP32** may appear in `.pio/libdeps/`. Nothing under `src/` includes
either one, so the library dependency finder never compiles them and they are
not in `GLIDE.bin` — no notice is owed for them. `gen_notices.py` knows this and
skips them; anything *else* that turns up in `libdeps` without an attribution
section makes it print a warning, so a new `lib_deps` line can't reach a buyer
unattributed.

### The LGPL component, specifically

The Arduino core is statically linked, and LGPL-2.1 section 6 gives whoever
holds the binary the right to modify that core and relink it. `dist/NOTICES.txt`
carries the standing offer that makes that right usable — full source location,
linkable object files on request for three years, and an explicit statement that
debugging your own modifications is permitted. Selling a device with GLIDE on it
means that offer is being made to the buyer, so it stays in the file.

## Referenced, not included

- **bmorcelli Launcher** — the SD-card loader GLIDE is installed through. A
  separate project under its own licence; GLIDE contains none of its code and
  is not affiliated with it.
- **M5Stack** — the hardware vendor. GLIDE is an independent, unaffiliated
  firmware for their devices. Their names and marks belong to them.
