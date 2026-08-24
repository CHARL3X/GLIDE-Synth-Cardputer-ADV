# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (C) 2026 Charles Tobin (CHARL3X)
# Assemble dist/NOTICES.txt — the attribution that has to travel with the
# compiled firmware. GLIDE's own licence is PolyForm Noncommercial, but the
# binary also carries MIT, FreeBSD/BSD, Apache-2.0 and LGPL-2.1 code, and every
# one of those says "reproduce this notice when you redistribute the binary".
# Selling a flashed device IS redistributing the binary, so this file ships
# with it — in the download, on the SD card, and linked from the listing.
#
# Run standalone:  python support/gen_notices.py
# Runs itself as a post-action of support/copy_dist.py on every `pio run`.
#
# Licence TEXT comes from the real library on disk when a build has fetched it
# (.pio/libdeps, ~/.platformio/packages) — that is ground truth for what got
# linked. When it isn't there (a fresh clone, a machine with no toolchain) the
# vendored copy in support/licenses/ is used and the section says so.
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(HERE)
VENDORED = os.path.join(HERE, "licenses")

# Libraries PlatformIO may install that are NOT linked into GLIDE. M5Cardputer
# declares them in its library.json, so they land in .pio/libdeps even though
# nothing in src/ includes them and the LDF never compiles them. Known-absent
# from the binary = no notice needed; the unlisted-library guard below skips
# them rather than crying wolf on every build.
NOT_LINKED = {"IRremote", "LibSSH-ESP32", "libssh-esp32"}

PREAMBLE = """\
================================================================================
GLIDE {version} — third-party notices
================================================================================

This file lists the software inside GLIDE.bin that was written by other people,
and reproduces the licences that require it. Keep it with the firmware: if you
pass the binary on — a download, a copy on an SD card, or a device with GLIDE
flashed on it — this file goes with it.

GLIDE itself is a separate matter from everything below.

  GLIDE — a continuous-pitch slide synthesiser for the M5Stack Cardputer ADV
  Copyright (C) 2026 Charles Tobin (CHARL3X). All rights reserved.
  SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
  Required Notice: Copyright (C) 2026 Charles Tobin (CHARL3X)
  (https://github.com/CHARL3X/GLIDE-Synth-Cardputer-ADV)

  Source: https://github.com/CHARL3X/GLIDE-Synth-Cardputer-ADV
  Terms:  LICENSING.md in that repository, and the LICENSE file beside it.

  GLIDE is free to run, study, modify and share for any NONCOMMERCIAL purpose.
  Commercial use — selling devices with it loaded, selling a fork, shipping it
  inside a paid product — is the author's alone and needs a separate commercial
  licence. Releases v2.5 and earlier were published under the GNU GPL v3 and
  stay under it; this binary is not one of those.

  If you BOUGHT a device with GLIDE already on it from CHARL3X, you are
  licensed for it: play it, modify it, reflash it, resell that unit. What you
  did not buy is the right to manufacture and sell more of them.

Nothing in GLIDE's licence restricts the rights the licences below grant you in
their own code.

"""

TRADEMARKS = """\
================================================================================
Trademarks
================================================================================

M5Stack, Cardputer and their logos belong to M5Stack Technology CO LTD. GLIDE is
an independent, unaffiliated firmware; it is named here only to say which
hardware it runs on. M5Stack has not endorsed, sponsored or reviewed it.

The bmorcelli Launcher, through which GLIDE is usually installed, is a separate
project under its own licence. GLIDE contains none of its code.

The GLIDE name, logo, icon and illustrations are (C) 2026 Charles Tobin, all
rights reserved, and are not covered by the code licence — see section 3 of
LICENSING.md.
"""

LGPL_OFFER = """\
--------------------------------------------------------------------------------
Your rights in this component, and how to exercise them
--------------------------------------------------------------------------------

The Arduino core for ESP32 is LGPL-2.1-or-later and is statically linked into
GLIDE.bin. Section 6 of that licence gives you the right to modify the core and
relink it into a working GLIDE binary. To make that real:

  * GLIDE's own complete source is public, permanently, at
    https://github.com/CHARL3X/GLIDE-Synth-Cardputer-ADV
    Its build is `pio run` against the exact versions pinned in
    platformio.ini, which is what produced the binary you have.

  * The core's own source is at https://github.com/espressif/arduino-esp32
    at the version named above.

  * If you would rather have the linkable object files than rebuild from
    source, ask and you will get them, for any binary released in the past
    three years: morphstudioshd@gmail.com, subject "GLIDE LGPL relink".
    That offer is open to anyone who holds a copy of the binary, at no charge
    beyond the cost of delivery.

  * You may reverse-engineer and debug your own modifications to this
    component. Nothing in GLIDE's licence forbids it, and no term of sale for
    a device carrying GLIDE will be read to forbid it.

This applies to the LGPL component. GLIDE's own code stays under the terms at
the top of this file.
"""


def read_first(paths):
    """Return (text, path) for the first readable path, else (None, None)."""
    for p in paths:
        try:
            with open(p, "r", encoding="utf-8", errors="replace") as f:
                return f.read().strip("\n"), p
        except OSError:
            continue
    return None, None


def platformio_home():
    return os.environ.get(
        "PLATFORMIO_CORE_DIR", os.path.join(os.path.expanduser("~"), ".platformio")
    )


def libdep_dirs():
    """Every .pio/libdeps/<env>/ that a build has populated."""
    root = os.path.join(PROJECT_DIR, ".pio", "libdeps")
    if not os.path.isdir(root):
        return []
    return [os.path.join(root, d) for d in sorted(os.listdir(root))]


def libdep_paths(*relative):
    return [os.path.join(d, *relative) for d in libdep_dirs()]


def firmware_version():
    """cfg::kVersion is the single source of truth — don't let NOTICES drift."""
    cfg = os.path.join(PROJECT_DIR, "src", "config.h")
    try:
        with open(cfg, "r", encoding="utf-8") as f:
            m = re.search(r'kVersion\s*=\s*"([^"]+)"', f.read())
            if m:
                return m.group(1)
    except OSError:
        pass
    return "(version unknown)"


# Each section: what it is, why it's in the binary, where its text comes from.
COMPONENTS = [
    {
        "title": "M5Cardputer 1.1.1, M5Unified 0.2.17, M5GFX 0.2.24 — MIT",
        "intro": """\
The board, device and graphics libraries. GLIDE calls into all three: the
keyboard matrix, the ES8311 codec (audio out and the LISTEN microphone path),
the display, and the frame-buffer sprite the whole UI is drawn into.

  M5Cardputer  Copyright (c) 2025 M5Stack Technology CO LTD
  M5Unified    Copyright (c) 2021 M5Stack
  M5GFX        Copyright (c) 2021 M5Stack

All three are MIT. M5Cardputer ships no root LICENSE file; its licence is
declared per-file, as `SPDX-License-Identifier: MIT` at the top of every
source. The MIT text below is M5Unified's and governs all three.""",
        "sources": libdep_paths("M5Unified", "LICENSE")
        + libdep_paths("M5GFX", "LICENSE"),
        "fallback": "MIT-M5Stack.txt",
    },
    {
        "title": "LovyanGFX-derived portions of M5GFX — FreeBSD (BSD 2-Clause)",
        "intro": """\
M5GFX is built on LovyanGFX, which is itself derived from TFT_eSPI and
Adafruit's GFX/ILI9341 libraries. Those portions keep their original terms,
reproduced here in full as LovyanGFX ships them.

  LovyanGFX   Copyright (c) 2020 lovyan03
  TFT_eSPI    Copyright (c) 2020 Bodmer
  Adafruit    Copyright (c) 2012 Adafruit Industries""",
        "sources": libdep_paths("M5GFX", "license.txt")
        + libdep_paths("M5GFX", "LICENSE.txt"),
        "fallback": "FreeBSD-LovyanGFX.txt",
    },
    {
        "title": "Arduino core for ESP32 6.12.0 — LGPL-2.1-or-later",
        "intro": """\
The Arduino framework GLIDE is built against (espressif32@6.12.0). Statically
linked into the binary. Copyright (c) Espressif Systems and the Arduino core
contributors. Read the relink offer under the licence text — it is the part of
this file that gives you something to do, not just something to read.""",
        "sources": [
            os.path.join(
                platformio_home(),
                "packages",
                "framework-arduinoespressif32",
                "LICENSE.md",
            ),
            os.path.join(
                platformio_home(), "packages", "framework-arduinoespressif32", "LICENSE"
            ),
        ],
        "fallback": "LGPL-2.1.txt",
        "after": LGPL_OFFER,
    },
    {
        "title": "ESP-IDF and its components — Apache-2.0",
        "intro": """\
Beneath the Arduino core sits ESP-IDF: FreeRTOS, the Wi-Fi and Bluetooth
stacks, NVS (where your sounds are stored), the I2S driver under the audio
path. Copyright (c) Espressif Systems (Shanghai) CO LTD, and contributors.

Most of it is Apache-2.0, reproduced below. Some bundled third-party
components inside IDF carry their own terms (BSD, MIT, and others); those
notices ship in the component directories of the SDK, under
  <platformio>/packages/framework-arduinoespressif32/tools/sdk/
and each governs its own component.""",
        "sources": [],
        "fallback": "Apache-2.0.txt",
    },
]

HOST_ONLY = """\
================================================================================
Not in the binary
================================================================================

One third-party file lives in the GLIDE repository but is NOT compiled into the
firmware: support/viz_render/shim/glcdfont.h, the Adafruit GFX 5x7 font glyph
table (BSD 3-Clause, Copyright (c) 2012 Adafruit Industries). It belongs to the
host-side render harness used to preview screens on a PC. Its licence text is
reproduced at the top of that file, and also appears in the LovyanGFX section
above. It is listed here for completeness only — no obligation attaches to the
device you are holding.
"""


def unlisted_libraries():
    """Anything in libdeps we have no notice for. A future lib_deps line must
    not slip into the binary without attribution — this is the tripwire."""
    known = {"M5Cardputer", "M5Unified", "M5GFX"}
    found = set()
    for d in libdep_dirs():
        try:
            entries = os.listdir(d)
        except OSError:
            continue
        for name in entries:
            if not os.path.isdir(os.path.join(d, name)):
                continue
            # PlatformIO can duplicate a library as "Name@x.y.z" while
            # resolving version pins — same library, same attribution.
            base = name.split("@", 1)[0]
            if base in known or base in NOT_LINKED or name.startswith("."):
                continue
            found.add(name)
    return sorted(found)


def rule(char, title=None):
    line = char * 80
    return line if title is None else "%s\n%s\n%s" % (line, title, line)


def build():
    out = [PREAMBLE.replace("{version}", firmware_version())]
    fallbacks = []

    for c in COMPONENTS:
        text, path = read_first(c["sources"])
        if text is None:
            text, path = read_first([os.path.join(VENDORED, c["fallback"])])
            origin = (
                "Text below: the copy vendored at support/licenses/%s\n"
                "(no build tree on this machine to read the installed library from)."
                % c["fallback"]
            )
            fallbacks.append(c["fallback"])
        else:
            where = (
                os.path.relpath(path, PROJECT_DIR)
                if path.startswith(PROJECT_DIR)
                else "the installed toolchain"
            )
            origin = "Text below: read at build time from %s." % where
        if text is None:
            sys.stderr.write("[notices] MISSING licence text for %s\n" % c["title"])
            text = "*** LICENCE TEXT MISSING — DO NOT SHIP THIS FILE ***"

        out.append(rule("=", c["title"]))
        out.append("")
        out.append(c["intro"])
        out.append("")
        out.append(origin)
        out.append("")
        out.append(rule("-"))
        out.append(text)
        out.append(rule("-"))
        out.append("")
        if c.get("after"):
            out.append(c["after"])

    out.append(HOST_ONLY)
    out.append("")
    out.append(TRADEMARKS)

    extra = unlisted_libraries()
    if extra:
        out.append("")
        out.append(rule("=", "UNLISTED LIBRARIES — attribution needed"))
        out.append("")
        out.append(
            "These libraries were installed into .pio/libdeps but have no section\n"
            "in this file. If any of them is linked into the binary, add it to\n"
            "support/gen_notices.py and THIRD-PARTY-NOTICES.md before shipping:\n"
        )
        for name in extra:
            out.append("  - %s" % name)
        out.append("")
        sys.stderr.write(
            "[notices] WARNING: no attribution section for: %s\n" % ", ".join(extra)
        )

    return "\n".join(out).rstrip("\n") + "\n", fallbacks


def main():
    text, fallbacks = build()
    dist_dir = os.path.join(PROJECT_DIR, "dist")
    os.makedirs(dist_dir, exist_ok=True)
    dst = os.path.join(dist_dir, "NOTICES.txt")
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("[notices] %s (%d bytes)" % (dst, len(text.encode("utf-8"))))
    if fallbacks:
        print(
            "[notices] used vendored text for: %s (no installed copy found)"
            % ", ".join(fallbacks)
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
