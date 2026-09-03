#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (C) 2026 Charles Tobin (CHARL3X)
# Emit a QR code as the inline <svg> snippet the guides paste into their HTML.
#
#   python3 support/guides/qr.py https://example.com/thing.pdf
#
# The guides carry their QRs as literal SVG paths (no runtime library, no image
# to lose), which makes them look like magic blobs. This is the tool that made
# them: same input, same path, so a URL that changes can be re-cut rather than
# hand-patched. Output format matches the existing tiles exactly — one stroked
# path of horizontal runs, module grid = the viewBox, so CSS sizes the tile.
#
# Needs `segno` (pure python, no build step):  pip install segno
import sys

try:
    import segno
except ImportError:
    sys.exit("needs segno:  pip install segno")


def svg(data: str, ecc: str = "m") -> str:
    m = [[bool(v) for v in row] for row in segno.make(data, error=ecc).matrix]
    n = len(m)
    d, cx, cy = [], 0, 0          # pen position, in modules
    for y in range(n):
        x = 0
        while x < n:
            if not m[y][x]:
                x += 1
                continue
            run = 0
            while x + run < n and m[y][x + run]:
                run += 1
            if not d:
                d.append("M%d %g" % (x, y + 0.5))
            elif y == cy:
                d.append("m%d 0" % (x - cx))
            else:
                d.append("m%d %d" % (x - cx, y - cy))
            d.append("h%d" % run)
            cx, cy = x + run, y
            x += run
    return ('<svg viewBox="0 0 %d %d" shape-rendering="crispEdges">'
            '<path stroke="#000" d="%s"/></svg>' % (n, n, "".join(d)))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: qr.py <text-to-encode> [ecc: l|m|q|h]")
    print(svg(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "m"))
