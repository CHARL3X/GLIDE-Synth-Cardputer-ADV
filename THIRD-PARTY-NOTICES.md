# Third-party notices

GLIDE itself is licensed as described in [`LICENSING.md`](LICENSING.md). This
file lists code written by others that GLIDE either includes or links against.
Nothing here is affected by GLIDE's own licence, and each item stays under the
terms its own authors set.

## Included in this repository

### Adafruit GFX 5x7 font glyph table — BSD 3-Clause

`support/viz_render/shim/glcdfont.h`
Copyright (c) 2012 Adafruit Industries. All rights reserved.

The full BSD licence text is reproduced at the top of that file and must stay
there. This file is part of the host-side render harness only; it is not
compiled into the device firmware.

### Inter and JetBrains Mono — SIL Open Font License 1.1

`docs/guides/*.pdf`

The owner's guides are typeset in **Inter** (Copyright (c) 2016 The Inter
Project Authors) and **JetBrains Mono** (Copyright (c) 2020 The JetBrains Mono
Project Authors), both under the SIL Open Font License 1.1, which permits
embedding in a document. No `.ttf` is committed here: `support/guides/build.py`
fetches them at render time and caches them in a gitignored folder. The font
data is embedded in the committed PDFs, so the OFL applies to those files.

## Fetched at build time (not vendored here)

PlatformIO resolves these from `platformio.ini` when you build. They are not
redistributed in this repository, but they *are* linked into the firmware
binary in `dist/`, so their terms apply to that binary as well as GLIDE's own.

| Component | Version pinned | Licence as published by its authors |
| --- | --- | --- |
| M5Cardputer | 1.1.1 | MIT |
| M5Unified | 0.2.17 | MIT |
| M5GFX | 0.2.24 | MIT, with LovyanGFX-derived portions under FreeBSD (BSD 2-Clause) |
| Arduino core for ESP32 (`espressif32@6.12.0`) | 6.12.0 | LGPL-2.1-or-later |
| ESP-IDF components beneath that core | as shipped with the platform | Apache-2.0, plus some third-party components under their own terms |

Each library's authoritative licence text ships in its own `LICENSE` file
inside `.pio/libdeps/` and `~/.platformio/packages/` after a build; that text
governs, not this table. If you redistribute the compiled binary, include those
notices with it.

## Referenced, not included

- **bmorcelli Launcher** — the SD-card loader GLIDE is installed through. A
  separate project under its own licence; GLIDE contains none of its code and
  is not affiliated with it.
- **M5Stack** — the hardware vendor. GLIDE is an independent, unaffiliated
  firmware for their devices. Their names and marks belong to them.
