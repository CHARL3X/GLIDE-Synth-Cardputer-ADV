# Owner's guides

Printable PDFs that go in the box with a device, and live in
[`docs/guides/`](../../docs/guides) for anyone who finds the repo. Each one has
a plain-text twin in `docs/` — the PDF is a rendering, never the only copy.

| Guide | Source | Rendered | Text twin |
| --- | --- | --- | --- |
| No. 01 — Updating GLIDE | `updating.html` | `docs/guides/GLIDE-Updating.pdf` | [`docs/updating.md`](../../docs/updating.md) |
| No. 02 — Playing GLIDE | `playing.html` | `docs/guides/GLIDE-Playing.pdf` | [`docs/manual.md`](../../docs/manual.md) |
| The card — one sheet | `card.html` | `docs/guides/GLIDE-Card.pdf` | both of the above |

No. 02 is the everyday sheet: the four rows, the four gestures that make it a
slide instrument, the ten sounds and how to roll your own, and the three ways
to back yourself. It is a curated read, not a keymap dump — the manual is the
complete reference and the device carries its own help screen. Its `<style>`
block is lifted verbatim from `updating.html` plus a No. 02 tail, so the two
sheets in the box can't drift apart; keep it that way.

The card is the sheet you actually want to print: **one side of one page**, no
cropping and nothing to staple. It carries what a new owner needs in the first
five minutes — the charging switch, the four rows, the three gestures, the
keymap — and then hands off to the long guides through three QR codes at the
foot of the page. Those QRs point at the **CDN copies**, not at GitHub, because
a scanning phone should land on a PDF and not on a repo:

```
https://cdn.shopify.com/s/files/1/0998/9868/7777/files/GLIDE-Updating.pdf
https://cdn.shopify.com/s/files/1/0998/9868/7777/files/GLIDE-Playing.pdf
```

Re-uploading a guide to Shopify Files under the same name keeps that URL, so
the codes stay good — but Shopify will silently save a second upload as
`GLIDE-Playing_1.pdf` if the old one wasn't deleted first, and *that* breaks
every card already in a box. Delete, then upload.

It has to stay one page. `overflow:hidden` on `.page` means an overflowing card
does not error — it silently loses its footer, which is exactly the failure the
render check catches:

```
python3 support/guides/build.py card
pdftoppm -png -r 150 docs/guides/GLIDE-Card.pdf /tmp/card   # then look at it
```

No. 01 opens with the power/charging page (the switch must be ON to charge —
the single most common "it's broken" report) before the update walkthrough, and
closes with QR codes to the repo and the Discord. It is the sheet that goes in
the box, so it has to answer day one as well as the update.

## Rendering

```
python3 support/guides/build.py updating
```

Needs a Chromium or Chrome binary (`CHROME=/path/to/chrome` if it isn't found)
and, the first time, network access to fetch the two typefaces. Fonts cache in
`.fonts/` and are never committed; the logo is inlined from `assets/`.

## QR codes

The guides carry their QRs as literal SVG paths — no runtime library, nothing
to lose — which makes them look like magic blobs. `qr.py` is the tool that cut
them, so a URL that changes can be re-cut instead of hand-patched:

```
pip install segno            # pure python, not a project dependency
python3 support/guides/qr.py https://example.com/thing.pdf
```

It reproduces the existing repo and Discord tiles byte-for-byte. Paste the
output into a `.qrtile`, and leave the URL in an HTML comment beside it. Keep a
tile at **0.7 in or larger**: the CDN URLs need 37 modules, and below that the
modules land under half a millimetre on paper. Decode the *rendered PDF* before
committing — `cv2.QRCodeDetector` on a 150 dpi `pdftoppm` raster is enough, and
it is the only check that proves what a phone will see.

**Edit the HTML, re-render, commit both.** A PDF that no longer matches its
source is worse than no PDF — same rule as `support/viz_render`.

## House style

The guides use the instrument's own palette: drafting-paper ground, the amber
annunciator for section rules, and device screens drawn as real black panels
(Launcher's tile grid, GLIDE's splash) rather than invented UI. If you draw a
screen, draw the one the device actually shows — that mistake has already been
made once and caught by a photograph.
