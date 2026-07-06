# 15 — Line out: the sound escapes the one-watt speaker (probe-gated)

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination. **Hardware-gated: Task 1 is a pin audit + bench probe. Do not build past it.**

**Goal:** Real audio out of GLIDE — headphones, an amp, a recorder — via a tiny dongle on the GROVE port, mirroring the exact master bus the speaker plays.

**Architecture:** The ESP32-S3 has **two I2S peripherals**; M5Unified's ES8311 path occupies one. The other drives an external DAC with a *copy* of each rendered block (the same wait-free mirror-tap pattern as doc 05's WAV bounce — the 3-buffer playRaw rotation is never touched). Two dongle designs, chosen by the Task 1 pin audit: **(a)** 3 free GROVE/expansion GPIOs → a PCM5102A I2S DAC board (~$3, true line-level), or **(b)** only 1–2 pins → **PDM output** (the S3's I2S supports PDM TX) through a passive RC low-pass into a jack — three components, astonishingly listenable, and the ultimate hacker-friendly mod.

**Tech stack:** `src/io/lineout.{h,cpp}` (ESP-IDF i2s driver on the *second* peripheral only), one settings row, a soldering-free dongle spec in `docs/`.

**Effort:** L (hardware in the loop). **Risk:** high until Task 1; low after.

**UI-cost budget:** zero new gestures, **1 settings row** (`Line out: off/on` — off by default; the extra I2S DMA costs a little power and the pin may be shared).

## Why this — and why it does NOT break rule 2

The one-watt speaker is the ceiling on everything: the FX sound better than anyone can hear, doc 05's WAV bounce proves the bus is clean, and any stage/recording use is impossible. This is the single biggest *sonic* unlock available without new hardware revisions — and it's the bridge to the "real hardware someday" story: the day GLIDE drives a PA, it's an instrument, full stop.

**Rule 2 says "M5Unified playRaw only, never raw I2S" — that rule exists because M5Unified owns the ES8311 codec's undocumented power-up.** This design never touches the ES8311, its I2S port, or the Speaker class. It configures the *other, idle* I2S peripheral against an external DAC that has no power-up sequence at all. The rule's intent is fully honored; the doc says so explicitly so a reviewer checks the intent, not just the letter. If the audit finds M5Unified claims *both* peripherals (it shouldn't — verify in the vendored source), this doc stops.

## Design

- **Mirror tap:** in the render task, after a block is finalized for `playRaw`, also write it into the line-out driver's own DMA queue (`i2s_write` with `timeout=0` — if the queue is momentarily full, drop that block *for line-out only* and count it; never block the render loop). Speaker and line-out play simultaneously (speaker can be muted the usual way — volume — if undesired).
- **Path (a), PCM5102A:** second I2S peripheral, 32 kHz, 16-bit, stereo-duplicated mono (the PCM5102A wants stereo frames). BCK/LRCK/DIN = 3 GPIOs. The PCM5102A needs zero configuration (hardwired pins on the common purple boards: FLT/DEMP/XSMT/FMT to sane defaults — document the strap settings in the dongle spec).
- **Path (b), PDM:** I2S PDM TX mode on 1 data GPIO (+ optionally its clock pin internal). Dongle: GPIO → 1 kΩ → jack tip, with 10 nF to ground (fc ≈ 16 kHz), ground → sleeve. Line-level-ish, mono. Quality: 1-bit sigma-delta at MHz rate — noticeably better than the speaker, not studio; set expectations in the row's docs.
- **Pin audit targets (Task 1):** the Cardputer GROVE port exposes G1/G2 (+5 V/GND) on the original; the **ADV may differ** — audit the schematic/silkscreen AND check softness: which of G1/G2 (and any expansion pads) are free given SD (40/39/14/12), LED (21/38), keyboard, IMU, mic. 2 free pins → path (b); 3+ → path (a).
- **Settings row** `Line out`: on = init the driver (lazily, first enable); failure (pin conflict, driver error) → red flash + stays off (failure-visible).
- `dsp/` untouched. The tap is symmetric with doc 05's recorder tap; if both land, they coexist (two consumers of the same finished block).

## Global constraints

- The ES8311/M5Unified path and the 3-buffer rotation are UNTOUCHED — the only render-task change is the one non-blocking mirror write.
- Failure-visible; off by default; native suite stays green (`pio run -e native && .pio/build/native/program`).
- No library additions: the second-peripheral driver comes from ESP-IDF within the pinned platform.

## Tasks

### Task 1: audit + bench probe (go/no-go)

**Files:** findings written back into this doc; scratch sketch under `support/spike_lineout/`.

- [ ] Read the vendored M5Unified Speaker_Class source: which I2S port number does the ADV config use? Confirm the other port is untouched by M5Unified.
- [ ] Pin audit per Design (schematic + `config.h` cross-check). Record: free pins, chosen path (a)/(b).
- [ ] Scratch sketch: M5Unified speaker playing a tone AND the second I2S port driving the chosen dongle with a different tone, simultaneously, for 10 minutes. Zero speaker glitches = GO.
- [ ] **Stop here if no free pins or any speaker interference.** Findings into this doc either way.

### Task 2: the driver + tap

**Files:** Create `src/io/lineout.{h,cpp}`. Modify `src/io/audio_engine.cpp` (one mirror-write line), `src/config.h` (pin constants + `kLineOutEnabled` compile switch, mirroring the SD block's style).

- [ ] `lineout::begin()/end()/push(const int16_t*, int)/drops()` — push is `timeout=0`, drop-and-count. Driver setup per the Task 1 path.
- [ ] The engine tap, guarded by the runtime setting: `if (lineout::active()) lineout::push(buf16, n);`
- [ ] `pio run`; verify by ear: speaker + line-out simultaneously, no starvation over 10 minutes of heavy playing. Commit.

### Task 3: setting + dongle doc

**Files:** Modify `src/ui/settings_screen.cpp`, `src/storage/glide_config.cpp` (NVS `lineout`, default off). Create `docs/lineout-dongle.md`.

- [ ] The row (AUDIO group), lazy init, red-flash on failure.
- [ ] `docs/lineout-dongle.md`: the exact dongle for the chosen path — parts, wiring diagram in ASCII, GROVE connector pinout, PCM5102A strap settings or the RC values. A person with a soldering iron and $5 must succeed from this doc alone.
- [ ] Commit.

### Task 4: verification

- [ ] Record GLIDE via line-out into a computer; A/B against doc 05's WAV bounce of the same phrase — they should be near-identical (same bus).
- [ ] Battery check: measure battery drain with line-out on vs off over 15 min; note it in the row's doc if meaningful.
- [ ] Yank the dongle mid-play: nothing bad may happen (it's just a driven pin).

## Acceptance criteria

- Speaker behavior byte-identical with line-out off; zero starvation with it on.
- The dongle doc is buildable by a stranger; audio out is clean at line level.
- 1 settings row, off by default, rule-2 intent explicitly preserved (Task 1 evidence in this doc).

## Risks

- Pin availability is THE risk — hence the gate, and the PDM fallback that needs only one pin.
- DMA/interrupt contention with the render task: the probe's 10-minute soak is the evidence either way; if contention appears, pin the line-out driver's interrupt to core 1 (away from render on core 0).
