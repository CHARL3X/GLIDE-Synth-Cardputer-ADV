# Licensing

GLIDE is **dual-licensed**. Pick whichever of the two applies to you.

```
Copyright (C) 2026 Charles Tobin (CHARL3X). All rights reserved.
SPDX-License-Identifier: GPL-3.0-only
```

---

## 1. Open-source licence — GNU GPL v3 (default)

Everything in this repository, unless a file says otherwise, is released under
the **GNU General Public License, version 3** — the full text is in
[`LICENSE`](LICENSE). Version 3 only; no "or any later version" grant.

This is a real open-source licence. Under it you may, at no cost and without
asking anyone:

- run GLIDE on as many devices as you like, for anything, including at paid gigs;
- read, study, and learn from the source;
- modify it, fork it, port it to other hardware;
- redistribute it, **including selling it** or selling hardware with it loaded.

In exchange, the GPL asks three things of anyone who distributes GLIDE or
anything derived from it — a modified build, a port, a product that includes
this code:

1. **Ship the complete corresponding source**, including your modifications, to
   whoever receives the binary.
2. **Licence that whole work under the GPL v3 as well.** You cannot fold GLIDE
   into a closed-source product. Derivative firmware stays open, permanently.
3. **Keep the copyright and licence notices intact**, and state what you changed.
   Credit travels with the code.

Read `LICENSE` for the binding terms; the summary above is a plain-English
description, not a substitute for it.

### What this means in practice

- A hobbyist flashing GLIDE, tweaking it, and sharing their build: fine, always.
- A company shipping a device with GLIDE (or a recognisable derivative of its
  synthesis, generative-sound, or UI code) inside: allowed — but the firmware
  they ship must be published under the GPL v3, with source, with attribution.
  They do not get to take this closed.
- Anyone who wants the code **without** those obligations needs option 2.

### Why GPL v3 specifically

The Arduino-ESP32 core GLIDE builds against is LGPL-2.1-or-later and the
ESP-IDF components underneath it are Apache-2.0. Apache-2.0 code cannot be
combined with GPL **v2**, but combines cleanly under GPL **v3**, so v3 is the
version that makes a lawful, distributable binary out of this stack.

---

## 2. Commercial licence (available on request)

If the GPL's terms do not work for your product — because you need to keep your
firmware closed, because you cannot publish source, or because your legal
department will not accept copyleft — a **separate commercial licence is
available for purchase**.

The copyright in GLIDE is held in one place, by its author, so those terms can
be negotiated directly. A commercial licence can grant, by agreement:

- the right to ship GLIDE, in whole or in part, inside a closed-source product;
- release from the source-disclosure and copyleft obligations of Section 1;
- terms for use of the GLIDE name and marks (see below);
- optional support, integration work, or custom builds.

**To enquire:** open an issue on
[the repository](https://github.com/CHARL3X/GLIDE-Synth-Cardputer-ADV/issues), or
email **morphstudioshd@gmail.com** with "GLIDE commercial licence" in the
subject line.

Distributing GLIDE outside the terms of Section 1 without such an agreement is
copyright infringement, not a licensing question.

---

## 3. Name, logo, and artwork

The GPL is a copyright licence for the *code*. It grants no rights to a
project's name or branding, and none are granted here.

**Not covered by the GPL grant:**

- the name **GLIDE** as it identifies this instrument, and the CHARL3X name;
- the logo and icon (`assets/glide-logo.png`, `assets/Glide Icon.png`);
- the illustrations in `assets/` (`glide-slide.svg`, `glide-tilt.svg`,
  `glide-roll.svg`, `glide-keymap.svg`, `glide-listen.svg`,
  `glide-autokey.svg`).

These remain **© 2026 Charles Tobin, all rights reserved**. You may reproduce
them unmodified to refer to, review, document, or redistribute unmodified GLIDE
itself. You may not use them to brand a fork, a modified build, or a product,
and you may not use them in a way that suggests this project endorses or
authored something it did not.

Forks are welcome — GPL Section 1 guarantees them. **Give your fork its own
name.** Rebranding a derivative is a normal, expected step, and it keeps
players from mistaking someone else's build for this one.

---

## 4. Third-party code

GLIDE links against libraries with their own licences, and one vendored file
carries a BSD notice. See [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
Nothing in this document affects those licences or their authors' rights.

---

## 5. No warranty

GLIDE is provided **as-is, without warranty of any kind**, as set out in
Sections 15 through 17 of `LICENSE`. It is firmware for hardware you own and
you run it at your own risk.
