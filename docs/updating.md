# Updating GLIDE

A new build is a file. Ten minutes, a computer, and a card reader — or two
minutes over WiFi if you set that up. Everything you've made survives it.

There's a printable version of this page — Owner's Guide No. 01, the one that
ships in the box with a device — at
[`docs/guides/GLIDE-Updating.pdf`](guides/GLIDE-Updating.pdf). It's rendered
from `support/guides/updating.html`; edit that and re-render rather than
editing the PDF.

## First, the thing worth knowing

GLIDE runs from the Cardputer's **own flash**, not from the card. You install it
once from the SD card, and from then on it starts the moment you power the
device on — that's the GLIDE tile in Launcher's menu.

Which means a newer `GLIDE.bin` sitting on the card **does nothing on its own**.
The card is where the file arrives; installing is what puts it into the device.
Every step below exists because of that one fact.

## Your sounds are not in the app

The ten slots, your saved tweaks, every setting, the two generative sounds
unique to your unit, and the odometer all live in NVS — the device's own
storage, outside the program you're replacing. Deleting and reinstalling GLIDE
cannot touch them. The `.gpat` library on your card isn't touched either.

There is nothing to back up, and nothing to restore afterwards.

## Part one — put the new file on the card

1. **Power the Cardputer off, then pop the card out.** The slot is
   spring-loaded: press the card in until it clicks and it springs back out.
   Power off first — pulling a card out of a running device is how cards get
   corrupted.
2. **Put the card in your computer**, with an SD adapter or a USB reader.
3. **Download the newest build.** The link always points at the latest:
   [`GLIDE.bin`](https://github.com/CHARL3X/GLIDE-Synth-Cardputer-ADV/releases/latest/download/GLIDE.bin).
   Some browsers are suspicious of `.bin` files and ask whether to keep it —
   keep it.
4. **Drag it into `/apps/` on the card**, replacing the file already there.
   Keep the name exactly `GLIDE.bin`; if your browser saved it as
   `GLIDE(1).bin`, rename it.
5. **Eject the card properly**, then slide it back into the Cardputer until it
   clicks. Ejecting is what guarantees the write finished — skipping it is the
   most common cause of "the new version isn't there".

## Part two — swap the copy on the device

6. **Power on and press any key immediately**, before the GLIDE splash appears.
   That keeps you in Launcher. If the synth starts instead, hold `` ` `` for
   about a second to exit back, or power-cycle and be quicker.
7. **Delete the installed GLIDE.** It's the tile on its own row at the bottom of
   Launcher's menu, drawn in red. Select it and choose *Delete*. This removes the
   program, not your sounds. **Don't skip it:** Launcher doesn't replace an
   installed app, it adds another one — install without deleting first and you
   end up with two GLIDE tiles and nothing to tell them apart.
8. **Return to that menu** — press any key at the start screen again.
9. **Install the new one:** select **SD** (its hint line reads *Launch from or
   mng SDCard*), open `apps`, select `GLIDE`, and choose *Install*. Let it
   finish; don't power off while it works.

Power-cycle. The splash shows the version top-right — if it's the number you
downloaded, you're done.

## The shortcut

Launcher can fetch and install a build over WiFi with no computer and no card
shuffling: press a key at the start screen, open **OTA**, join your network,
pick GLIDE. Same result. The card method above stays as the fallback, and is
the only one that works with no network.

## If something goes sideways

| What you see | What it is |
| --- | --- |
| GLIDE starts before you can press a key | Power off fully, then tap a key repeatedly as it comes up. Or let it start and hold `` ` `` for a second. |
| No GLIDE in the card's `apps` folder on the device | Wrong folder or a renamed file. It must sit inside `apps`, named `GLIDE.bin`. |
| The version on the splash didn't change | Either the file never finished copying (eject properly), or part two didn't take. Repeating part two is safe. |
| Deleted GLIDE, nothing to play | Nothing is lost — the program is on the card, your sounds are in the device. Go to step 9. |
| The computer can't see the card | Try another adapter, reader, or port. If it offers to "repair" or "format" the card, decline — that erases your patch library. |
| Two GLIDE tiles in the menu | You installed without deleting first — Launcher duplicates rather than replaces. Delete both, then install once from `SD` → `apps`. Your sounds don't care. |
| The menus don't match the pictures | Launcher moves its labels between versions. The job is the same: remove the installed GLIDE, then install from `SD` → `apps`. |

Screens in the printable guide are drawn from **Launcher 2.7.2**.
