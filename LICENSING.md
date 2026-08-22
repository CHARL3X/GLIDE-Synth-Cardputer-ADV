# Licensing

GLIDE is **dual-licensed**. Pick whichever of the two applies to you.

```
Copyright (C) 2026 Charles Tobin (CHARL3X). All rights reserved.
SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
Required Notice: Copyright (C) 2026 Charles Tobin (CHARL3X)
(https://github.com/CHARL3X/GLIDE-Synth-Cardputer-ADV)
```

---

## 1. Noncommercial licence — PolyForm Noncommercial 1.0.0 (default)

Everything in this repository, unless a file says otherwise, is released under
the **PolyForm Noncommercial License 1.0.0** — the full text is in
[`LICENSE`](LICENSE). The source is public and stays public; what the licence
draws is one line: **commerce with this code is the author's alone.**

Under it you may, at no cost and without asking anyone, for any
**noncommercial** purpose:

- run GLIDE on as many devices as you like;
- read, study, and learn from the source;
- modify it, fork it, port it to other hardware, experiment freely;
- share your builds and changes with anyone, on the same terms.

What it does **not** grant, to anyone, is commercial use. Selling devices with
GLIDE (or a derivative of it) loaded, selling the firmware or a fork of it,
bundling it in a paid product or service — all of that requires the commercial
licence in Section 2. Playing GLIDE at a paid gig is your performance, not a
distribution of the software; that was never the licence's business and still
isn't.

Whoever you share the software with must receive the licence terms and the
`Required Notice:` line above — credit travels with the code.

Read `LICENSE` for the binding terms; the summary above is a plain-English
description, not a substitute for it.

### What this means in practice

- A hobbyist flashing GLIDE, tweaking it, and sharing their build for free:
  fine, always. That is exactly what the licence protects.
- A tinkerer porting the slide engine to another handheld and publishing the
  port for other hobbyists: fine.
- Anyone selling flashed devices, a fork, or a product with this code inside:
  **not licensed** — that needs Section 2, negotiated with the author.
- Anyone unsure which side of the line they're on: open an issue and ask.

### Earlier releases

Releases **v2.5 and earlier** were published under the GNU GPL v3, and that
grant is irrevocable for those versions: what shipped under the GPL stays
under the GPL. Everything after v2.5 — this repository's current contents and
every future release — is PolyForm Noncommercial 1.0.0 only.

---

## 2. Commercial licence (available on request)

If your use is commercial — because you want to ship GLIDE, in whole or in
part, inside a product; sell devices with it loaded; or offer it as part of a
paid service — a **separate commercial licence is available for purchase**.

The copyright in GLIDE is held in one place, by its author, so those terms can
be negotiated directly. A commercial licence can grant, by agreement:

- the right to ship GLIDE, in whole or in part, inside a commercial product,
  closed or open;
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

The licence in Section 1 is a copyright licence for the *code*. It grants no
rights to a project's name or branding, and none are granted here.

**Not covered by the Section 1 grant:**

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

Noncommercial forks are welcome — Section 1 permits them. **Give your fork its
own name.** Rebranding a derivative is a normal, expected step, and it keeps
players from mistaking someone else's build for this one.

---

## 4. Third-party code

GLIDE links against libraries with their own licences, and one vendored file
carries a BSD notice. See [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
Nothing in this document affects those licences or their authors' rights — in
particular, nothing here restricts the rights the LGPL grants you in the
Arduino-ESP32 core the firmware links against, including modifying that
component and relinking it.

---

## 5. No warranty

GLIDE is provided **as-is, without warranty of any kind**, as set out in the
"No Liability" section of `LICENSE`. It is firmware for hardware you own and
you run it at your own risk.
