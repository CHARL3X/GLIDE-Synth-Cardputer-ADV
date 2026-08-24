# Owner's guides

Printable PDFs that go in the box with a device, and live in
[`docs/guides/`](../../docs/guides) for anyone who finds the repo. Each one has
a plain-text twin in `docs/` — the PDF is a rendering, never the only copy.

| Guide | Source | Rendered | Text twin |
| --- | --- | --- | --- |
| No. 01 — Updating GLIDE | `updating.html` | `docs/guides/GLIDE-Updating.pdf` | [`docs/updating.md`](../../docs/updating.md) |
| No. 02 — Playing GLIDE | `playing.html` | `docs/guides/GLIDE-Playing.pdf` | [`docs/manual.md`](../../docs/manual.md) |

No. 02 is the everyday sheet: the four rows, the four gestures that make it a
slide instrument, the ten sounds and how to roll your own, and the three ways
to back yourself. It is a curated read, not a keymap dump — the manual is the
complete reference and the device carries its own help screen. Its `<style>`
block is lifted verbatim from `updating.html` plus a No. 02 tail, so the two
sheets in the box can't drift apart; keep it that way.

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

**Edit the HTML, re-render, commit both.** A PDF that no longer matches its
source is worse than no PDF — same rule as `support/viz_render`.

## House style

The guides use the instrument's own palette: drafting-paper ground, the amber
annunciator for section rules, and device screens drawn as real black panels
(Launcher's tile grid, GLIDE's splash) rather than invented UI. If you draw a
screen, draw the one the device actually shows — that mistake has already been
made once and caught by a photograph.
