# Vendored licence texts

These are other people's licences, kept verbatim. **Do not edit them**, and do
not add the GLIDE SPDX header to them — a licence text with a local
modification is worse than no copy at all.

`support/gen_notices.py` assembles `dist/NOTICES.txt` from these files whenever
the installed library isn't on disk to read from (a fresh clone, a machine with
no PlatformIO toolchain, CI). When a build tree *is* present, the generator
prefers the real file under `.pio/libdeps/` or `~/.platformio/packages/`,
because that is ground truth for what actually got linked. The generated file
says which source it used, section by section.

| File | Covers | Fetched from |
| --- | --- | --- |
| `MIT-M5Stack.txt` | M5Cardputer, M5Unified, M5GFX | `m5stack/M5Unified@master:LICENSE` |
| `FreeBSD-LovyanGFX.txt` | LovyanGFX / TFT_eSPI / Adafruit portions of M5GFX | `lovyan03/LovyanGFX@master:license.txt` |
| `LGPL-2.1.txt` | Arduino core for ESP32 | `espressif/arduino-esp32@master:LICENSE.md` |
| `Apache-2.0.txt` | ESP-IDF and its components | `apache.org/licenses/LICENSE-2.0.txt` |

Two notes on what isn't here:

- **M5Cardputer ships no root LICENSE file.** Its licence is declared per-file,
  as `SPDX-License-Identifier: MIT` at the top of each source. The MIT terms in
  `MIT-M5Stack.txt` are what govern it; only the copyright holder line differs
  (M5Stack Technology CO LTD, 2025), and `gen_notices.py` prints that.
- **The Adafruit BSD 3-Clause font header** (`support/viz_render/shim/glcdfont.h`)
  has no file here because it is not linked into the firmware — it is host-side
  tooling, and its notice already lives at the top of that file.

If you add a library to `lib_deps`, add its licence here and a section to
`gen_notices.py`. The generator warns about anything in `.pio/libdeps/` it has
no section for, so an unattributed library can't quietly reach a buyer.
