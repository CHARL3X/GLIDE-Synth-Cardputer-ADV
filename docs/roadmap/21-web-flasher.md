# 21 — Web flasher + standalone build

> **For agentic workers:** Execute task-by-task. Steps use `- [ ]` checkboxes.
> Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for
> the ledger. This doc claims **no codec tags, no enum values, no NVS keys** —
> it is packaging and distribution, not engine work.

**Goal:** Install and update GLIDE from a web page — plug the Cardputer into
USB, open the flasher page in a browser, click. One step replaces today's
whole updating guide. The instrument ships as a **standalone image** that owns
the device (its own bootloader and partition table), while the **Launcher path
stays fully supported**: every release keeps publishing the `/apps/GLIDE.bin`
app image exactly as today.

**Why standalone is more than convenience:** GLIDE under Launcher shares a
20 KB NVS partition with Launcher and every app that ever wrote to it. That
shared, chronically crowded partition is the root of the STORAGE FULL warning,
the 1–2 s save stalls, and the escalated factory reset (see D1 in the index).
A standalone image gets a **clean partition table of its own** — that entire
class of field problems does not exist on a standalone unit.

**Tech stack:** [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
(WebSerial; Chrome/Edge only — the page must say so) + a merged image produced
by `esptool merge_bin` (already pinned at 4.9.0 in the toolchain). Static
hosting; GitHub Pages is enough to start.

**UI-cost budget (the simplicity rule):** zero new gestures, zero settings
rows. Everything here is off-device.

## The two supported paths, explicitly

| Path | Asset | Who it serves |
| --- | --- | --- |
| **Launcher** (unchanged) | `GLIDE.bin` — app image for `/apps/` on the SD | Anyone already running Launcher; multi-app users; OTA through Launcher's store |
| **Web flasher** (new) | `GLIDE-full.bin` — merged bootloader + partitions + app | One-click install/update from the browser; standalone units |

Both assets ship in every release. Neither path is deprecated by this doc;
the README presents the flasher as the easy road and Launcher as the
multi-app road.

## The one hard invariant: NVS survives every update

The unit's seed, genver, saved sounds, settings, and odometer live in the NVS
partition (`0x9000`, per `partitions.csv`). Two consequences, non-negotiable:

1. **The flasher page needs two flows.** *Install* (first flash of a blank or
   foreign device) may erase everything. *Update* must write the app region
   only — never the NVS. ESP Web Tools manifests support exactly this split;
   build both and label them honestly on the page.
2. **Migration from a Launcher unit must be verified on hardware before the
   page goes public.** A Launcher-installed GLIDE and a standalone GLIDE must
   agree on the NVS offset, or crossing over wipes the player's sounds. If the
   offsets cannot be made to agree, the *install* flow must say plainly that
   crossing over resets the instrument (sounds saved to SD survive regardless
   — same story as the factory reset).

Do not enlarge or move the NVS partition in the standalone table until the
migration question is settled; a bigger NVS is tempting (see D1) but layout
changes are exactly what breaks the crossing.

## Tasks

### Task 1: the merged image (repo work, no hardware risk)

- [ ] `support/copy_dist.py`: after the app copy, produce
      `dist/GLIDE-full.bin` via `esptool merge_bin` from the build's
      bootloader, partition table, `boot_app0`, and app (the exact offsets are
      in the pio build output; record them in the script, not in prose).
- [ ] `dist/NOTICES.txt` already ships beside the bin; confirm the release
      checklist includes it for both assets.
- [ ] `.gitignore`: decide whether `GLIDE-full.bin` is committed like
      `GLIDE.bin` or attached to releases only (leaning: releases only — it is
      derivable and doubles the repo's binary churn).

### Task 2: hardware verification (gate — doc-06 spirit)

- [ ] Flash `GLIDE-full.bin` at `0x0` to a real ADV. Verify: boots straight
      into GLIDE, audio up, settings persist across power cycles.
- [ ] The migration test, on a Launcher-installed unit with known sounds:
      flash the full image over it and check seed / slots / odometer.
      **Record the result in this doc either way** — the flasher page's
      install-flow wording depends on it.
- [ ] Standalone exit gesture: with no Launcher to exit to, decide what
      `` ` `` hold does (this is D2 in the index — on standalone, "restart"
      is at least honest). Zero new gestures; only the label may change.

### Task 3: the flasher page

- [ ] Static page + `manifest.json` pair (install / update) using ESP Web
      Tools; host from the repo (GitHub Pages) to start. The page carries: the
      two-flow choice, the Chrome/Edge note, a link to the manual, and the
      same `Required Notice` + `NOTICES.txt` the releases carry.
- [ ] Wire the release flow: publishing a release updates what the page
      serves (worst case: the manifest points at
      `releases/latest/download/GLIDE-full.bin` and nothing ever needs
      editing).
- [ ] README: the flasher becomes the first install path; the Launcher path
      moves to second, unchanged in content.

### Task 4: docs follow

- [ ] `docs/updating.md` + the printable guide gain the one-click path;
      the card path stays as the fallback (and as the Launcher path).
- [ ] `docs/design.md`: note the standalone partition table beside the
      existing direct-USB section.

## Risks

- **WebSerial reach:** Chrome/Edge desktop only. Mitigated: the card/Launcher
  path remains first-class, and the page says who it's for.
- **NVS offset mismatch on migration** — the whole reason Task 2 gates
  Task 3. Building the page before the migration verdict is a plan violation.
- **Partition-table drift:** the standalone table and the Launcher-app build
  must stay in lockstep per release; the merge happens in `copy_dist.py`
  precisely so it cannot be done by hand and drift.
- **Launcher OTA listing:** units that stay on Launcher keep their OTA store
  entry (it points at the same releases). Nothing here touches it.

## Downstream idea: updating from inside GLIDE (parked, and gated on this doc)

Raised 2026-08-26: check for and install updates from within the instrument —
no card, no computer. Recorded here rather than as its own doc because it is
**not independently buildable**, and the reason is worth writing down before
someone spends a week on the wrong half.

The obvious cost is the UI, and it is enormous by this project's standards: a
WiFi scan, a scrollable network list, and a password entry field is not "one
settings row," it is a whole modal subsystem with a text-input mode, on an
instrument whose identity is that it has almost no menus. That alone would
need a human decision at review.

**But the UI is not the blocker. Partition ownership is.** As a Launcher app,
GLIDE does not own the OTA slots. Launcher writes the selected app into one;
the other holds whatever the player installed before. So a self-update has
nowhere honest to write:

- Write to the inactive slot and it clobbers an app the player deliberately
  installed — silently, from inside a synth.
- Or, if that slot already held GLIDE, the unit now carries **two identical
  GLIDE images** and a player has no way to tell them apart.
- And GLIDE cannot reliably know which case it is in, because the allocation
  is another program's business.

Writing the new image to the SD card instead sidesteps the flash question and
lands somewhere worse: the player is still running the old build and must go
back through Launcher to install it — the manual step the feature existed to
remove.

**So it is strictly downstream of this doc.** Once GLIDE ships as a standalone
image that owns its own partition table (Task 2 here), self-update becomes an
ordinary A/B OTA: two slots, both GLIDE's, write the inactive one, flip the
boot partition, restart. That is well-trodden ESP-IDF ground. It also retires
debt D2 in `00-INDEX.md` as a side effect, since a GLIDE that owns its boot
partition can answer the "exit vs restart" question honestly.

**Verdict: parked, owner's call — "not for this batch."** The one rule for
whoever revisits it: do not build the WiFi UI first. It is the expensive half
of a feature the partition table currently forbids, and it would be built
against assumptions this doc's Task 2 may overturn.
