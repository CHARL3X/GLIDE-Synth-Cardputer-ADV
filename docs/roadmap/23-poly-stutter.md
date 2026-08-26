# 23 — The high-polyphony stutter on the headphone jack: diagnose, then fix

> **For agentic workers:** Execute task-by-task. Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. **This doc is diagnosis-gated** — it follows the doc 06 / 13 / 15 pattern: Tasks 1–3 measure, and only their result picks which of the Task 4 remedies gets built. Building a fix before the measurement is a plan violation.

**Goal:** Find out what a player is actually hearing when dense chords break up through the headphone jack, and fix that thing — rather than the thing it sounds like.

**Effort:** S to diagnose, S–M to fix depending on which hypothesis wins. **Risk:** low for the diagnosis; the leading remedies are all small and reversible.

**UI-cost budget:** the diagnosis costs **zero**. The most likely remedy (an output level trim) costs **one settings row** and no gestures; the fallback remedies cost zero. The voice-limit control the reporter asked for **already exists** — see below.

## The report

From Discord, relayed 2026-08-26 (paraphrased, kept in the reporter's terms):

> Listening through the aux cable into headphones, a stutter can be heard when high polyphony is happening. I think the CPU gets overloaded by multiple voices and can't keep up. It's the same stutter you can hear in FL Studio when you max out the notes and it starts clipping. If it's a hardware limit I understand, but if it's possible to add some temp option to limit the number of voices it could help avoid the CPU time overload. Might be some audio driver issue also, but it does sound like CPU overload.

**Not yet reproduced by the maintainers** — no headphones or aux cable on hand at the time of writing. Nothing below is confirmed. See also the `headphone-jack-findings` note: the jack drives headphones fine and reads dead into a line input, and there is no `HP_DET` for the ADV in M5Unified.

## Answer the reporter first — the voice limit already ships

Before any work happens, this needs saying, because the requested feature is already in their hands:

**Hold `fn`, press top-row key 8 (`VOICES`), adjust with `[` and `]`.** Range 1–8, default 6, shown live in the corner as `vox held/cap`, saved with the patch and across reboots. It is `p_.voiceCount`, quick-edit slot 7 in `src/io/keys.cpp:789`.

One caveat to pass on honestly: **the cap governs held *lead* voices only.** Drones, loop playback and the auto-progression are deliberately cap-exempt (`docs/manual.md:128`), and released voices keep rendering through their release tails. So the total number of voices actually being rendered can still reach `kMaxVoices = 8` with the cap set to 2. If turning `vox` down makes the artifact go away, that is real evidence — but it is *not* proof the ceiling is voice count, and it does not mean the cap is a hard load limiter.

## What the source says before anyone measures

Read these before forming an opinion; several of them argue *against* the reporter's own theory, which is exactly why the measurement comes first.

**Arguing against raw CPU overload:**

- Per-sample work is lean by design. Every transcendental in the voice path is hoisted to block rate — `expf`, `sinf`, `exp2f` all appear once per block in `src/dsp/voice.cpp`, never inside the sample loop. Oscillators are band-limited **wavetable** reads, not computed waveforms.
- The filter is **shared per bus**, not per voice: `svf_` runs once over the summed lead bus in `Synth::render`, not eight times.
- The budget is generous. 32 kHz × 128-sample blocks = 4 ms per block; at 240 MHz that is ~960 000 cycles per block, ~7 500 cycles per output sample for everything — eight voices, two filters, and the whole FX room.
- The worst case is nowhere near that. `FatSaw` renders three detuned oscillators per voice, so eight FatSaw voices is 24 table reads and interpolations per sample — hundreds of cycles, not thousands.

**Arguing for a level/headroom problem instead:**

- **Voices sum without any per-voice normalisation.** `Voice::render` accumulates into the bus; there is no `1/N`. Eight voices at full envelope are eight times the amplitude of one.
- The mix then hits `softLimit` in `src/io/audio_engine.cpp:79` — linear to ±0.9, soft knee above. Each bus is *separately* soft-clipped to ~±0.93 first, and the two sum. Push that hard and you get intermodulation, which on a dense chord is precisely the "FL Studio maxed out and clipping" sound the reporter describes.
- **The digital bus therefore arrives near full scale with nothing downstream to soften it.** `spk.setVolume(255)` (`audio_engine.cpp:201`) is unity — not boost, so it is not *adding* distortion — but it also means the summed, soft-limited mix reaches the DAC as hot as the DSP made it, and on the jack there is no amplifier between that and your ears. Whatever the limiter does under a dense chord is what a headphone user hears, undisguised.
- **There is no headphone amplifier, and nothing reroutes when you plug in.** M5's page says *"When a 3.5mm headphone jack is inserted, the speaker amplifier will be disabled"*, which is easy to misread as a second output stage. It is not. Per the [Cardputer-Adv schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf) page 3, ES8311 pin 12 OUTP feeds one net, `DAC_P`, and that single net fans out to **both** destinations at once: through R16/C13 into the **NS4150B** class-D amp and on to the 1 W speaker, *and* through R25 (10 Ω) straight to the jack. Pin 13 OUTN is left unconnected. Inserting a plug closes the jack's own switch → `HP_DET` → Q4 → `AMP_EN`, muting the NS4150B in pure hardware. **The jack is fed identically whether the plug is in or not; all that changes is whether the speaker amp is running.**
  - Consequence, and it matters for how this gets debugged: **the jack has no analog gain stage of its own to clip.** If anything it is the quieter path — single-ended off one leg of a differential part is ~6 dB below what the codec could deliver. So "the headphone amp is running out of rails" is not available as an explanation, and neither is any routing or codec-config theory: one net feeds both, so speaker-audible implies jack-has-signal.
  - Which pushes the cause back **inside the firmware**, into the digital mix and the limiter — where the summing evidence above already points. Also note there is no hidden gain to blame: `spk.setVolume(255)` is *unity*, not boost, and M5Unified's ES8311 `0x32 = 0xBF` is exactly 0 dB.
- **So masking is the leading explanation for "jack only," by elimination.** Since both outputs carry the same signal, anything audible on the jack is present in the speaker's feed too — the speaker just cannot reproduce it. It is small, band-limited and current-limited, and the class-D amp driving it compresses hard on its own; between them they hide both clip crunch and short gaps. Headphones reproduce `DAC_P` honestly. That makes this most likely a long-standing condition surfaced by a better listening path, **not a regression** — worth saying out loud to the reporter, who may assume an update caused it.

**Arguing for timing after all:**

- `blocksDone > 16 && isPlaying(ch) == 0` (`audio_engine.cpp:113`) is the starvation detector, and it only counts a **fully drained** queue. A block that arrives late but still leaves one buffer in flight is a near-miss that goes uncounted. The true late-block rate is therefore **≥** whatever `starvedBlocks()` reports; treat that counter as a floor, never a clean bill of health.
- Flash writes stall both cores. Per debt D1 in `00-INDEX.md`, `persistNow` has been measured at 1.5–2 s of NVS work, and SPI-flash operations disable the instruction cache — the render task on core 0 is not immune to what core 1 does. This would produce a real dropout, but a *one-off* one, not one correlated with chord density. Rule it in or out early so it stops muddying the traces.

## Global constraints

- CLAUDE.md rule 2 stands throughout: **M5Unified `playRaw` only, never raw I2S**, and the 3-buffer rotation in `audio_engine.cpp` does not shrink. Whatever the diagnosis, the cure is not a rewrite of the audio path.
- RAM rule 7: any instrumentation added here is temporary, and anything that stays (a diagnostics readout) must justify its bytes against the `pio run` RAM line.
- If a remedy touches `dsp/`, `pio run -e native && .pio/build/native/program` must be green before it lands.

## Tasks

### Task 1: make the existing instrument readable

**Files:** Modify wherever the settings/about screen lives (`src/ui/settings_screen.cpp`) — read first; consider whether this is temporary or permanent before spending a row.

`audio::starvedBlocks()` already exists and is already counting. It is currently printed **once, at boot, over serial** (`src/main.cpp:146`) — the least useful possible moment. It needs to be readable after playing something.

- [ ] Surface the starve count somewhere a player or tester can see it without a computer. Cheapest honest option: append it to an existing diagnostics/about line rather than adding a row.
- [ ] Add a **block-time meter** alongside it (temporary is fine): wrap the render body in `esp_timer_get_time()`, keep a rolling max and a mean over ~2 s, and report both as a percentage of the 4 ms budget. This is the number that settles the CPU question outright.
- [ ] `pio run`; check RAM.

### Task 2: the discriminating tests (on hardware, with headphones)

Each row isolates one hypothesis. Run them all before concluding — several can be true at once.

| # | test | if the artifact... | then |
|---|------|--------------------|------|
| 1 | Reproduce a dense chord that stutters. Read the block-time meter. | max block time < ~60% of budget | it is **not** CPU. Stop blaming voices. |
| 2 | Same passage, `VOLUME` quick-edit dropped to ~40%. | disappears | it is **headroom/clipping**, digital or analog. Go to Task 4a. |
| 3 | Same passage, `vox` from 8 down to 3. | disappears *and* block time was already low | consistent with headroom, not CPU — fewer voices means a smaller sum, not just less work. |
| 4 | Same passage on the **internal speaker**. | is inaudible but the meter reads the same | the speaker path is hiding it — remember it is a *different amplifier*, not just a worse one. |
| 4b | Record the jack output into a computer and look at the waveform where it breaks up. | shows flat-topping | digital clipping — the answer is level, and it is visible rather than arguable. |
| 5 | Same passage with all FX sends at 0. | disappears | the FX room is the load or the level source; measure block time with and without. |
| 6 | Watch `starvedBlocks()` across the passage. | climbs | genuine late blocks — go to Task 4b, and remember the counter undercounts. |
| 7 | Trigger a settings-close or explicit save mid-passage. | a one-off dropout, unrelated to density | that is debt D1's flash stall, not this bug. Log it separately. |
| 8 | Different cable / different headphones / a different device in the chain. | changes or disappears | it is the reporter's chain. Say so kindly and close. |

- [ ] Record every result in this doc — including the ones that came back negative. The negatives are the valuable half.

### Task 3: pick the hypothesis, in writing

- [ ] State the winning hypothesis and the evidence for it in one paragraph here, and name what would falsify it. Only then continue.

### Task 4: the remedy (build exactly one)

**4a — Output level trim (expected winner).** A global rig-level output gain applied in `audio_engine.cpp` just before the `int16` conversion, or via `spk.setVolume()`, defaulting to today's behaviour so the speaker is unchanged and only a jack user reaches for it.
- Settings row `Output level` (speaker / headphone / line), NVS key `outtrim`. One row, no gesture.
- **Coordinate with doc 15 (line-out)** — that doc contemplates the same output stage and reserves `lineout`. Whichever lands first owns the row and the other extends it; do not ship two output-level controls.
- Rig state, never a patch field: it must not enter `SynthParams`, the codec, or a `.gpat`. Follow the `bendCents` precedent for exclusion hygiene.

**4b — Real load relief (only if Task 1's meter proves it).** In order of cost: raise the render task's headroom before touching the DSP (check `kRenderPrio` vs the M5 spk task and what else runs on core 0); then consider a *rendered*-voice ceiling as distinct from today's held-lead cap, remembering that stealing must stay glide-based — the nearest-pitch legato steal in `src/dsp/synth.cpp:156` is what makes chord slides work, and a hard cut-off steal would trade a stutter for a click.

**4c — Level discipline in the mix (if 4a helps but does not cure).** Investigate a gentle polyphony-aware bus gain so eight voices do not arrive eight times louder than one. **This changes the sound of every dense patch**, so it is a human decision at review, not an agent's — write the proposal, render before/after WAVs, and stop.

### Task 5: close the loop with the reporter

- [ ] Post the actual finding to Discord, including a negative result if that is what it is ("we measured, it is not CPU" is a good answer). They gave a careful, well-observed report and named a plausible mechanism; they have earned the real answer.

## Acceptance criteria

- The block-time meter number is written into this doc, whatever it says.
- Every Task 2 row has a recorded result.
- Exactly one remedy is built, and it is the one the evidence picked.
- No change to the `playRaw` path or the 3-buffer rotation.

## Risks

- **The most likely outcome is that the reporter's stated mechanism is wrong and their observation is right.** Handle that gracefully — a stutter is real whether or not it is CPU.
- Chasing this on the internal speaker will waste a day: hypothesis 4 says the speaker cannot hear the thing being debugged. Get headphones and a cable before starting Task 2.
- A "temp option to limit voices" is tempting to ship as reassurance. Don't — it already exists (see above), and adding a second one would be a settings row that fixes nothing.
