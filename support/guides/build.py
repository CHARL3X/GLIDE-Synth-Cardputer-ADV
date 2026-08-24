#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (C) 2026 Charles Tobin (CHARL3X)
# Render an owner's guide (support/guides/<name>.html) to docs/guides/<Name>.pdf.
#
#   python3 support/guides/build.py updating
#
# Same spirit as support/viz_render: the printable is a BUILD PRODUCT, not a
# binary someone has to take on faith. The committed PDF must always be
# reproducible from the .html beside this script.
#
# What it does: fetches Inter + JetBrains Mono from Google Fonts (SIL OFL,
# embedded in the PDF, never committed as .ttf), inlines assets/glide-logo.png
# for the cover, then drives headless Chromium's print-to-PDF at US Letter with
# zero margins — the page geometry lives in the HTML's @page/.page rules.
#
# Needs: a Chromium/Chrome binary. Set CHROME=/path/to/chrome if the guesses
# below miss. Network is needed only the first time (fonts are cached in
# support/guides/.fonts/, which is gitignored).
#
# Reproducible in APPEARANCE, not byte-for-byte. Chrome decides per run whether
# to subset the embedded faces or carry them whole, so the same .html yields a
# ~575 KB or a ~1015 KB PDF at random, and the internal ids differ every time.
# Measured: both variants rasterise pixel-identical at 150 dpi across every
# page, so either is safe to print — prefer committing the smaller one. Don't
# chase a stable hash here; compare rendered pages instead (pdftoppm + magick
# compare -metric AE) when you need to know whether a change is visual.
import base64
import os
import re
import shutil
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.join(ROOT, "support", "guides")
CACHE = os.path.join(HERE, ".fonts")
OUTDIR = os.path.join(ROOT, "docs", "guides")

FONT_CSS = ("https://fonts.googleapis.com/css2"
            "?family=Inter:wght@400;600;700&family=JetBrains+Mono:wght@400;700")

CHROME_GUESSES = [
    os.environ.get("CHROME"),
    "/opt/pw-browsers/chromium-1194/chrome-linux/chrome",
    shutil.which("chromium"), shutil.which("chromium-browser"),
    shutil.which("google-chrome"), shutil.which("chrome"),
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
]


def chrome() -> str:
    for c in CHROME_GUESSES:
        if c and os.path.exists(c):
            return c
    sys.exit("no Chromium/Chrome found — set CHROME=/path/to/chrome")


def fonts() -> dict:
    """{'Inter-400': b'...ttf'} — cached on disk, fetched once."""
    os.makedirs(CACHE, exist_ok=True)
    req = urllib.request.Request(FONT_CSS, headers={"User-Agent": "Mozilla/5.0"})
    out = {}
    need = ["Inter-400", "Inter-600", "Inter-700",
            "JetBrainsMono-400", "JetBrainsMono-700"]
    if all(os.path.exists(os.path.join(CACHE, n + ".ttf")) for n in need):
        for n in need:
            out[n] = open(os.path.join(CACHE, n + ".ttf"), "rb").read()
        return out
    css = urllib.request.urlopen(req).read().decode()
    seen = set()
    for fam, wt, url in re.findall(
            r"font-family: '([^']+)';.*?font-weight: (\d+);.*?src: url\((https://[^)]+)\)",
            css, re.S):
        name = f"{fam.replace(' ', '')}-{wt}"
        if name in seen or name not in need:
            continue
        seen.add(name)
        data = urllib.request.urlopen(url).read()
        open(os.path.join(CACHE, name + ".ttf"), "wb").write(data)
        out[name] = data
    missing = [n for n in need if n not in out]
    if missing:
        sys.exit("google fonts did not serve: " + ", ".join(missing))
    return out


def build(name: str) -> str:
    src = os.path.join(HERE, name + ".html")
    if not os.path.exists(src):
        sys.exit("no such guide source: " + src)
    html = open(src, encoding="utf-8").read()

    logo = base64.b64encode(
        open(os.path.join(ROOT, "assets", "glide-logo.png"), "rb").read()).decode()
    html = html.replace("LOGO_B64", logo)
    for fname, data in fonts().items():
        html = html.replace(f"url('fonts/{fname}.ttf')",
                            "url(data:font/ttf;base64," + base64.b64encode(data).decode() + ")")
    if "LOGO_B64" in html or "url('fonts/" in html:
        sys.exit("a placeholder went unfilled — check the template")

    os.makedirs(OUTDIR, exist_ok=True)
    staged = os.path.join(HERE, ".build-" + name + ".html")
    open(staged, "w", encoding="utf-8").write(html)
    pdf = os.path.join(OUTDIR, "GLIDE-" + name.capitalize() + ".pdf")
    subprocess.run([chrome(), "--headless", "--disable-gpu", "--no-sandbox",
                    "--font-render-hinting=none",
                    "--run-all-compositor-stages-before-draw",
                    "--virtual-time-budget=9000", "--print-to-pdf-no-header",
                    "--print-to-pdf=" + pdf,
                    "file://" + staged.replace(os.sep, "/")],
                   check=True, capture_output=True)
    os.remove(staged)
    return pdf


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "updating"
    out = build(which)
    print("%s  (%d KB)" % (out, os.path.getsize(out) // 1024))
