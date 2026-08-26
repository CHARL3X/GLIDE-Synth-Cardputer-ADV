# 22 — The low-battery warning stops lying

> **For agentic workers:** Execute task-by-task. Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination — this doc claims **no codec tag and no enum**, and only one optional NVS key.

**Goal:** A quiet, non-interrupting battery warning that a player can trust — steps at ~25% and ~10%, never flickers on gauge noise, never appears while the unit is charging, and gets louder (a blink) only when the situation is actually urgent.

**Architecture:** This is a *hardening* of an existing feature, not a new one. `drawBattery` already lives in `src/ui/perform_screen.cpp:1571`. It is nine lines and it has three defects the field has now named. The work is to replace its naive poll with a load-aware estimator, latch its thresholds, and infer the charge state the ADV refuses to report.

**Tech stack:** `src/ui/perform_screen.cpp` only (plus `src/config.h` for the tunables). Nothing in `dsp/`. Not host-testable — review carefully and verify on hardware.

**Effort:** S (half a day, most of it sitting with a draining unit). **Risk:** low — the failure mode of a bug here is a cosmetic badge, never audio.

**UI-cost budget (the simplicity rule):** **zero new gestures, zero new settings rows.** The indicator already exists in the corner; this makes it honest. A `Power warnings: off/on` row is specified as an *optional* trim at the end and is **not** recommended — an indicator you have to remember to enable is not a safety net.

## Why this

A pocket instrument that dies mid-jam without warning is a broken promise, and the current warning is close enough to useless that it teaches players to ignore it. Worse, in the one situation where the reading is *most* wrong — plugged in with a deeply discharged cell, where the charger floats the ADC rail and the gauge reads full — the current code will happily paint a red blinking `BAT 8%` at a player whose unit is charging fine, or paint nothing at all while it dies. See the flat-battery field diagnosis in `docs/updating.md` and commit `8615f68`.

## What exists today (read this before designing anything)

```cpp
void drawBattery(M5Canvas& c, uint32_t now) {
    static int level = -1;
    static uint32_t lastPoll = 0;
    if (level < 0 || now - lastPoll > 5000) { lastPoll = now; level = M5.Power.getBatteryLevel(); }
    if (level < 0 || level > 20 || keys::quickEditActive()) return;
    if (level <= 10 && (now >> 9) & 1) return;   // blink when critical
    ...
}
```

Three defects, in order of how much they hurt:

1. **It shows while charging.** There is no charge check at all — because there is nothing to check (see the hardware facts below). A player on USB whose cell is still low gets a warning about a problem that is actively being solved.
2. **It has no hysteresis.** A single raw sample every 5 s, compared against a bare `> 20`. A gauge sitting near a threshold makes the badge appear and vanish every five seconds — the "sets off over and over" complaint.
3. **One step, at the wrong place.** 20% is late for a warning and 10% is late for urgency. 25% / 10% is the ask.

## The hardware facts that constrain the design

Verified in the pinned M5Unified source (`.pio/libdeps/cardputer-adv/M5Unified/src/utility/Power_Class.cpp`) — do not re-derive, but do re-check if the library pin ever moves:

- The ADV is `pmic_t::pmic_adc`: **no PMIC**, just a 2:1 divider on GPIO10. There is no charger IC to interrogate.
- **`M5.Power.isCharging()` returns `charge_unknown` on this board.** The `board_M5CardputerADV` case simply is not in the switch; it falls to the `default: return is_charging_t::charge_unknown`. *The API cannot tell you whether the unit is charging.* Any design that assumes otherwise is dead on arrival — this is the single most important line in this doc.
- `getBatteryLevel()` is `level = (mv - 3300) * 100 / 800` off one ADC read. **1% ≈ 8 mV.** That is why the gauge "sucks": it is not a state-of-charge estimate, it is an instantaneous voltage reading scaled hard. Under a 1 W speaker pulling transients, rail sag of 50–100 mV is ordinary — 6–12 points of apparent battery, correlated with *how hard you are playing*.
- `getBatteryVoltage()` returns the same reading in mV, unquantised. **Work in millivolts, not percent** — the percent conversion throws away exactly the resolution the filter needs.

## Design

**The estimator (this is the whole feature).** Sag is one-directional: load pulls the rail *down*, never up. So the honest estimate of open-circuit voltage is the **recent maximum**, not the recent average — averaging just tracks how loud you have been playing.

Use a decaying peak-hold, O(1) and two floats of state (see the RAM note):

```
sample mv every ~2 s
peakMv = max(mv, peakMv - kBatDecayMvPerPoll)   // ~2 mV per poll ≈ 60 mV/minute
```

The decay is what lets the estimate follow a genuinely draining battery down; it is deliberately slower than any real discharge rate, so a busy passage can never drag the estimate with it. Seed `peakMv` from the first reading rather than from zero, or the badge screams for the first minute after boot.

**Latching thresholds.** Two latches, `warnLatched` / `critLatched`, driven off the peak-held estimate:

| latch | arms at | clears at | look |
|---|---|---|---|
| warn | ≤ 25% | > 32% sustained, or on charge inference | amber `BAT 24%`, steady |
| crit | ≤ 10% | > 16% sustained, or on charge inference | red `BAT 9%`, blinking |

Once armed, a latch does not disarm on a dip back over the line — it disarms only on a *sustained* recovery, which on a discharging unit does not happen. This is what kills the flicker, and it is more important than the exact numbers.

**Charge inference (because the API won't).** With `isCharging()` unusable, infer it from the estimator's own trend: hold a `chargeRefMv` snapshot and its timestamp; if `peakMv` has risen more than ~40 mV above the reference within ~90 s, declare charging, clear both latches, and suppress the badge until the estimate falls again. Rationale:

- Discharging never produces a sustained *rise* in peak-held voltage. Recovery after a transient does, but only by tens of millivolts over seconds — the 90 s window rejects it.
- The fake-full case (empty cell, charger floating the rail to ~4.2 V) is caught for free: it is an enormous instantaneous rise, far past the threshold.
- Cost of a wrong "charging" call: a warning is suppressed for a while on a unit that is actually draining. Cost of a wrong "discharging" call: a red blink at someone on USB, which is the exact complaint being fixed. **Bias toward suppressing**, and say so in the code comment.

**Presentation — it stays non-interrupting.** No modal, no takeover, nothing that steals a keypress. Same corner slot, same `Font0`, same duck-under-the-loop-and-progression layout. The only escalation from warn to crit is colour and the existing ~0.5 s blink. Resist every temptation to make this bigger; the whole value of the feature is that a player can ignore it and lose nothing but battery.

**The functional payload (do not skip — this is the part that actually saves a player something).** Below the crit threshold, take one real action: call the existing debounced-persist flush immediately and keep it flushed, so pending settings are on flash *before* a brownout. Per debt D1 in `00-INDEX.md`, `persistNow` can take 1–2 s of flash writes; a brownout landing inside that window is how a config gets corrupted. Read how `store::markDirty()` / the debounce in `src/storage/glide_config.cpp` currently schedules flushes, and hook the crit latch to force one — do not invent a second persist path.

## Global constraints

- **RAM (CLAUDE.md rule 7).** The decaying peak-hold is the design *because* it needs no ring buffer: two floats, two bools, two `uint32_t`. Even so, per rule 7 new frame-to-frame UI state belongs inside the perform screen's `VizState` union rather than in fresh function-level statics — check what the union can host and put it there if it fits. Compare `pio run`'s RAM line to the previous build before calling this done.
- Nothing here goes in `dsp/`, the patch codec, or the `.gpat` format. The battery is rig state, not sound.
- All tunables (poll interval, decay rate, thresholds, hysteresis gaps, charge-inference window and delta) go in `src/config.h` next to the idle/screensaver block, not inline — the whole point is that the next person tunes them from measurements without reading UI code.

## Tasks

### Task 1: measure the real gauge before writing any threshold

**Files:** a temporary debug build. Nothing here ships; both traces come back out in Task 2.

**The trap — read this before reaching for `Serial.printf`.** The sag measurement has to happen **on battery**, and USB serial means the unit is plugged in, which means the charger is holding the rail up. A serial trace of the battery voltage is a trace of the *charger*, not the battery. The two measurements need two different instruments, and only one of them can use serial at all.

- [ ] **Sag trace — unplugged, logged to the SD card.** Append `millis(),mV` every 2 s to a CSV in `/glide/` (copy the write path in `src/io/sd_store.cpp`; the card is already mounted and this is a few lines). Then play the unit **normally, unplugged, for 20+ minutes**, making sure it includes both quiet stretches and at least one hard dense passage — 8-voice FatSaw pad, FX up, backlight full. Pull the card and read the file on a computer. This is unattended by design: nobody has to sit watching a number, and it captures far more of the discharge than anyone would have the patience to observe.
  - *Fallback with no card:* draw the raw mV on screen where the percentage goes and read it off the panel by eye, quiet vs. hammering. Crude, but it answers the one question that gates the design.
- [ ] From that file, record **the actual sag in mV between quiet and hard-played passages** — the whole design hinges on this one number. It sets `kBatDecayMvPerPoll` and the hysteresis gaps. The 50–100 mV figure above is a prediction, not a measurement.
- [ ] **Charge trace — plugged in, and here serial is exactly right**, because being plugged in *is* the experiment. From a genuinely flat unit (run one down on purpose — see `docs/updating.md` for what that state looks like), print `getBatteryVoltage()` every 2 s and watch the first ten minutes. Record two things: how far the reading jumps the instant the cable goes in (the fake-full float), and how fast it climbs after that. Those set the charge-inference delta and window.
- [ ] Write all three numbers into this doc before proceeding, then remove both traces.

### Task 2: the estimator and the latches

**Files:** Modify `src/ui/perform_screen.cpp`, `src/config.h`.

- [ ] Add the tunables to `config.h` with a comment block explaining the sag problem in one paragraph.
- [ ] Replace `drawBattery`'s poll with the peak-hold estimator + the two latches + charge inference. Keep the function's existing signature, its `quickEditActive()` suppression, and its layout ducking.
- [ ] Convert to percent only at the moment of drawing (`(mv - 3300) / 8`), so all logic stays in millivolts.
- [ ] `pio run`; **check the RAM line against the previous build** and record both numbers in the commit message.

### Task 3: the crit-level flush

**Files:** Modify `src/ui/perform_screen.cpp` (or wherever the persist debounce is driven from — read first).

- [ ] On the crit latch arming, force a persist and note it in the code comment as brownout hygiene, referencing debt D1.
- [ ] Verify it fires **once**, not every frame — a per-frame NVS flush at 10% battery would be a spectacular own goal.

### Task 4: device verification (the long one)

- [ ] Run a unit down from ~40% while playing. Confirm: the 25% badge appears once and **stays** — no flicker across a dense passage, which is the whole acceptance test.
- [ ] Confirm the badge does not fight the loop/progression indicators for the corner at any combination of those being active.
- [ ] Plug in at ~15%: the badge clears within the inference window and does not return while charging.
- [ ] Plug in a **fully flat** unit (the `docs/updating.md` scenario): no red blink, no warning, nothing alarming — this is the buyer-facing case.
- [ ] Confirm the crit blink is visible but not distracting while actually playing. If it pulls the eye, halve its duty cycle rather than removing it.

## Acceptance criteria

- Across a full discharge with heavy playing, the badge changes state **monotonically** — arms at 25, arms at 10, never disarms.
- No warning is ever shown while charging, including from a flat cell.
- RAM delta is at or near zero and is stated in the commit message.
- Nothing about the indicator can steal a keypress or a frame from playing.

## Risks

- **Charge inference is a heuristic, not a fact.** It will occasionally miss a slow charge from a weak USB source. Accepted: the failure is a badge that stays up too long, and the doc's bias is explicit.
- **Task 1's numbers might invalidate the design.** If measured sag turns out to be tiny (< 20 mV), the peak-hold is overkill and a slow one-pole plus the latches is enough — take the simpler code in that case. If sag turns out to be huge (> 200 mV), consider polling only when no voices are sounding (`audio::lead().sounding == 0`), which sidesteps the problem entirely and is *cheaper* than filtering. Let the measurement pick.
- **Optional trim, not recommended:** a `Power warnings: off / on` settings row, NVS key `batwarn`. Costs one row against the budget and lets a player disable the one thing that would have told them to save their sound. Only land it if a human asks for it at review.
