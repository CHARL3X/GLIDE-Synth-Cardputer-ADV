# Contributing to GLIDE

Bug reports, patches, ports, and sounds are all welcome. Two things to read
before you send code — the first is about how the project works, the second is
about licensing and it matters more than most projects' equivalent.

## Before you send a patch

- **The design intent lives in the docs.** [`docs/design.md`](docs/design.md)
  is the philosophy and the audio-path facts; [`docs/manual.md`](docs/manual.md)
  is every feature and the keymap. Read both before proposing a change to how
  the instrument behaves.
- **The host tests must pass.** `pio run -e native` then run
  `.pio/build/native/program`. Any change under `src/dsp/` that does not keep
  those green will not be merged.
- **Watch the RAM line.** The frame-buffer sprite boots within about a kilobyte
  of the ceiling. Compare `pio run`'s RAM figure against the previous build and
  say what it did in your pull request.
- **Some code is frozen on purpose.** The legacy and v2 sound generators are
  pinned by golden hashes because players' devices re-derive their own sounds
  through them on every boot. Changing them silently rewrites sounds people
  already have. New work goes in new code paths.

`CLAUDE.md` in the repository root carries the full working notes, including
the hard rules about audio, RAM, and the pure-C++ DSP boundary.

## Licensing of contributions

GLIDE is dual-licensed: GPL v3 for everyone, plus a commercial licence sold
separately by the author (see [`LICENSING.md`](LICENSING.md)). That second half
only works if one party can license the whole codebase, so contributions need
an explicit grant.

**By submitting a pull request or patch, you confirm that:**

1. You wrote the contribution yourself, or you have the right to submit it, and
   you are not knowingly including anyone else's copyrighted or patented work
   without saying so and identifying its licence.
2. You license your contribution under the **GPL v3**, the same terms as the
   rest of the project.
3. You additionally grant Charles Tobin (CHARL3X) a perpetual, worldwide,
   irrevocable, royalty-free right to **relicense your contribution under other
   terms**, including proprietary commercial licences, as part of GLIDE.
4. You keep your own copyright. You are not signing it away, and you can go on
   using your own code however you like, anywhere else.

Point 3 is what lets the commercial-licensing option exist at all. Without it,
a single contributed function would make the whole project unlicensable to a
commercial customer, and the copyleft protection would be all that is left. If
you are not comfortable granting it, say so in the pull request rather than
staying quiet — a description of the bug and how to fix it is genuinely useful
on its own, and there is no hard feeling in it.

Add a sign-off line to your commits to record the above:

```
Signed-off-by: Your Name <your.email@example.com>
```

(`git commit -s` writes it for you.)

## New source files

Every new source file in `src/` or `support/` starts with:

```c
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Charles Tobin (CHARL3X)
```

Use `#` instead of `//` in Python. Files that carry a third-party licence
notice keep theirs untouched — do not overwrite one with the header above.
