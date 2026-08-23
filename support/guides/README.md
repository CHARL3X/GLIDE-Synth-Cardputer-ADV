# Owner's guides

Printable PDFs that go in the box with a device, and live in
[`docs/guides/`](../../docs/guides) for anyone who finds the repo. Each one has
a plain-text twin in `docs/` — the PDF is a rendering, never the only copy.

| Guide | Source | Rendered | Text twin |
| --- | --- | --- | --- |
| No. 01 — Updating GLIDE | `updating.html` | `docs/guides/GLIDE-Updating.pdf` | [`docs/updating.md`](../../docs/updating.md) |

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
