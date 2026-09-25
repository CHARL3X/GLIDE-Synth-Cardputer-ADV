# Third-party notices

GLIDE itself is licensed as described in [`LICENSING.md`](LICENSING.md). This
file lists code written by others that the GLIDE firmware links against.
Nothing here is affected by GLIDE's own licence, and each item stays under the
terms its own authors set.

**Shipping the binary?** This page is the map; the thing that has to travel with
`GLIDE.bin` is [`dist/NOTICES.txt`](dist/NOTICES.txt), which reproduces every
licence below in full plus the LGPL relink offer. It is regenerated with every
build from the licence files of the libraries that build actually linked, and
every release carries its own copy. Include it in any download, on the SD card of any device sold or given away, and link it from any
listing. That is not politeness; MIT, BSD, Apache-2.0 and LGPL-2.1 each require
it of anyone distributing the compiled result.

## Linked into the firmware

These are linked into the firmware binary in `dist/` and on every release, so
their terms apply to that binary as well as GLIDE's own.

| Component | Version pinned | Licence as published by its authors | Where that licence is stated |
| --- | --- | --- | --- |
| M5Cardputer | 1.1.1 | MIT | Per-file `SPDX-License-Identifier: MIT` headers — the repository has no root `LICENSE` file |
| M5Unified | 0.2.17 | MIT | `LICENSE` (© 2021 M5Stack) |
| M5GFX | 0.2.24 | MIT, with LovyanGFX-derived portions under FreeBSD (BSD 2-Clause) | `LICENSE` + `license.txt` (LovyanGFX, TFT_eSPI, Adafruit) |
| Arduino core for ESP32 (`espressif32@6.12.0`) | 6.12.0 | LGPL-2.1-or-later | `LICENSE.md` in `framework-arduinoespressif32` |
| ESP-IDF components beneath that core | as shipped with the platform | Apache-2.0, plus some third-party components under their own terms | Per-component notices under `tools/sdk/` |

Each library's authoritative licence text is the one its authors publish; that
text governs, not this table. If you redistribute the compiled binary, ship
[`dist/NOTICES.txt`](dist/NOTICES.txt) with it — that is those texts, gathered.

M5Cardputer declares **IRremote** and **LibSSH-ESP32** as dependencies, but
GLIDE uses neither, so neither is compiled into `GLIDE.bin`.

### The LGPL component, specifically

The Arduino core is statically linked, and LGPL-2.1 section 6 gives whoever
holds the binary the right to modify that core and relink it. `dist/NOTICES.txt`
carries the standing offer that makes that right usable — the core's source
location, linkable object files on request for three years, and an explicit statement that
debugging your own modifications is permitted. Selling a device with GLIDE on it
means that offer is being made to the buyer, so it stays in the file.

## Referenced, not included

- **bmorcelli Launcher** — the SD-card loader GLIDE is installed through. A
  separate project under its own licence; GLIDE contains none of its code and
  is not affiliated with it.
- **M5Stack** — the hardware vendor. GLIDE is an independent, unaffiliated
  firmware for their devices. Their names and marks belong to them.
