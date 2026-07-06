# 13 — Chirp share: send a sound to another GLIDE *as sound* (moonshot)

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination. **Moonshot: Task 1 is a hardware go/no-go probe in the Phase-0 spirit — do not build past it on hope.**

**Goal:** Two Cardputers on a table. One plays a five-second chirp through its speaker; the other listens on its mic and the patch appears in its library. A synthesizer whose sounds travel *as sound* — no cables, no cards, no cloud.

**Architecture:** A pure, host-tested acoustic modem in `dsp/` (16-FSK encoder + Goertzel-bank decoder with preamble sync, length/CRC framing, and Hamming(7,4) FEC). The encoder renders float samples the audio engine already knows how to play; the decoder eats mic blocks. `io/` contributes only the mic capture glue; `ui/` one LIBRARY row opening a send/receive card. **The entire modem — both directions — is testable on the host by piping encoder output (plus noise, gain warp, resampling error) straight into the decoder.** Hardware risk is isolated to one probe task.

**Tech stack:** `src/dsp/chirp_codec.{h,cpp}` (pure), the existing `src/io/listen.{h,cpp}` half-duplex capture (see CLAUDE.md rule 7 — the speaker↔mic handoff lives *solely* there; do NOT roll a second `Mic.begin()` path), one settings row + a small card UI.

**Effort:** L (3–5 days, half of it acoustics tuning). **Risk:** high — and contained: the probe gates it, the host tests de-risk everything but physics.

**UI-cost budget (the simplicity rule):** zero new gestures, **1 settings row** (LIBRARY: `Share sound` → a card with two actions, send / receive). Within budget — the card is modal UI behind one row, the same shape as the SD browser.

## Why this

Every sharing story GLIDE has needs hardware ceremony (pull the SD card) or a future ecosystem (doc 12's web page). But the product's soul is "no WiFi, no accounts, no setup… play" — and it has a speaker and a microphone. Data-over-sound is proven tech (ggwave/Chirp built businesses on it) and *no instrument has ever shipped it as the way you trade patches*. Two kids after school leaning their synths together to swap sounds they rolled is the single most GLIDE-shaped social feature that can exist. It also makes a killer demo video, which this project demonstrably cares about.

And the payload is *made* for it: a `.gpat` is ~230 tagged bytes. This is a text-message-sized transfer, not a file transfer.

## Design

### Framing

`[len u16][patch bytes ≤ 300][crc32 u32]`, all Hamming(7,4)-encoded (2 code-symbols per byte → ×1.75 overhead), preceded by a **preamble**: 300 ms rising sync sweep + 2 known calibration symbols (they give the decoder symbol-clock phase and per-run frequency offset).

### Modulation

- 16-FSK: 4 bits/symbol. Symbol rate **50 baud** (20 ms symbols — long enough to survive small-room reverb smear; Goertzel over a 20 ms window has 50 Hz bins at any rate: 640 samples at the 32 kHz Tx rate, 320 at the 16 kHz mic rate).
- Tones: 16 frequencies, 2000–5000 Hz, 200 Hz spacing (integer multiples of the 50 Hz bin width; inside the small speaker's competent band, above most HVAC/voice hum).
- Throughput: 200 bits/s → a 236-byte frame ≈ 413 Hamming symbols ≈ **8.3 s + preamble ≈ 9 s**. Slow enough to be robust, short enough to be a party trick. (If BER allows after tuning, 25 ms→12.5 ms symbols halves it — tune from measurements, not optimism.)
- Encoder API renders at the engine's sample rate with a raised-cosine amplitude ramp per symbol boundary (clickless, reverb-friendlier).

```cpp
// dsp/chirp_codec.h — pure C++, host-tested. Sound in, bytes out — and back.
namespace dsp {
class ChirpTx {  // feed frames; pull samples until done
public:
    bool begin(const uint8_t* payload, int len, float sr);  // len <= 300
    int  render(float* out, int n);   // samples written; 0 = complete
};
class ChirpRx {  // feed mic blocks; poll for a frame
public:
    void begin(float sr);
    void feed(const float* in, int n);
    // >0 = frame length (payload copied to out), 0 = still listening,
    // -1 = frame detected but CRC failed (UI: "again?")
    int  poll(uint8_t* out, int cap);
    float sync01() const;   // 0..1 sync confidence — drives the UI meter
};
}
```

### Device glue

- **Send:** the card pauses the synth voice (panic + a HUD note `SENDING ♪`), streams `ChirpTx::render` blocks through the existing 3-buffer playRaw path (it's just another render source; do NOT touch the engine — feed it via the same buffer hand-off the synth uses; read `audio_engine.cpp` for where a source callback could be swapped, or simplest: a mode flag the render task checks).
- **Receive:** through `io/listen.cpp` — the codec is **half-duplex** (settled: CLAUDE.md rule 7) and `listen::capture()` already owns the `Speaker.end() → Mic.begin() → Mic.end() → Speaker.begin()` handoff, parked render task included. Mic rate is `listen::kRateHz = 16000` — run `ChirpRx` at 16 k (it's rate-parameterized; tones at 2–5 kHz sit safely under the 8 kHz Nyquist). Receiving silences the synth while listening — inherent to half-duplex, and fine.
  **The open problem `capture()` doesn't solve:** it records in rounds of ≤3 s (heap-sized, possibly 0.5 s) with analysis gaps between rounds, up to ~9 s total — a chirp frame that straddles a round boundary is corrupted, and a full frame barely fits 9 s. Extend `listen` with a *streaming* mode (feed each ~100 ms chunk to a callback as it arrives, no round gaps, caller-controlled duration up to ~20 s) rather than buffering rounds — `ChirpRx::feed` is exactly that consumer, and symbol-wise processing needs no big buffer at all. This extension stays inside listen.cpp (rule 7).
- Received patch → `decodePatch` → audition lick + "keep? (save to SD / slot)" via the existing save flows. CRC fail → red flash, stay listening.

## Global constraints

- `src/dsp/` pure C++ / C++11 / env:native green: `pio run -e native && .pio/build/native/program`. **All modem logic host-tested — no exceptions**; io/ is capture/playback glue only.
- Audio path law: M5Unified only, playRaw 3-buffer rotation untouched; the speaker↔mic handoff lives solely in `io/listen.cpp` (CLAUDE.md rule 7 — the codec is half-duplex and the render task must be parked first).
- Failure-visible: sync meter while listening, explicit CRC-fail flash, timeout after 30 s of nothing (`heard nothing`).
- The instrument must remain fully playable if mic init fails (amber `MIC ✗`, row still offers send).

## Tasks

### Task 1: hardware probe (go/no-go gate)

**Files:** scratch app under `support/spike_chirp/` (not shipped).

Mic capture itself is already proven — `io/listen.cpp` records on real hardware (half-duplex handoff, 16 kHz; CLAUDE.md rule 7). The probe is now physics-only:

- [ ] Physics probe (capture via `listen::capture`, tone via the normal audio path): play a 3 kHz tone from device A at jam volume; device B's captured audio shows it ≥ 20 dB above room noise at 30 cm. Repeat at 2 k/4 k/5 kHz to map the usable band; adjust the Design's tone plan to the measured band.
- [ ] Measure the inter-round gap of `capture()` rounds (timestamps in the segment callback) — this quantifies why the streaming extension in Design is needed and how big a symbol-loss a naive approach would suffer. Document in this doc's margin.
- [ ] **If the band or SNR isn't there, stop; file the findings; this doc stays a design.**

### Task 2: the modem, host-first (the bulk of the work)

**Files:** Create `src/dsp/chirp_codec.h`, `src/dsp/chirp_codec.cpp`. Modify `src/test_dsp.cpp`.

- [ ] TDD in this order, each red→green→commit:
  1. Hamming(7,4) encode/decode incl. single-bit-error correction (table-driven, 16-entry syndromes).
  2. Framing: len+payload+crc32 round-trip; corrupted CRC → reject.
  3. Tx renders: correct symbol count, band-limited (assert per-symbol dominant Goertzel bin matches the sent symbol).
  4. **Clean loopback:** Tx → Rx at 32 kHz recovers a 230-byte payload byte-exact.
  5. **Abuse loopback:** + white noise at −10 dB SNR margin steps, ×0.3 gain, 32 k→16 k decimation (the mic path), ±0.5% sample-rate skew, 200-sample random offset. Find the failure floor; assert comfortable margins survive.
  6. Sync confidence behaves (0 on noise, →1 on preamble).
- [ ] This task is DONE when a simulated "two devices with a bad microphone between them" passes. Native green. Commit(s).

### Task 3: io glue

**Files:** Modify `src/io/listen.{h,cpp}` (streaming capture mode per Design — the handoff stays solely here, rule 7). Modify `src/io/audio_engine.{h,cpp}` (minimal: a send-source hook).

- [ ] Add `listen::captureStream(chunkCb, user, maxMs)` — same handoff and failure `Result`s as `capture()`, but chunks flow to the callback continuously (no round gaps, no big buffer) for up to `maxMs` or until the callback returns false. `capture()` keeps working unchanged.
- [ ] Send path: the smallest possible audio-engine seam (e.g. `audio::setOverrideSource(fillFn)`) — the 3-buffer rotation code itself untouched; the render task calls the override instead of the synth while set.
- [ ] `pio run`; commit.

### Task 4: the card UI

**Files:** Modify `src/ui/settings_screen.cpp`; create `src/ui/share_card.{h,cpp}` (mirror `sd_browser`'s modal pattern).

- [ ] LIBRARY row `Share sound` → card: **send** (plays the chirp for the live sound, progress bar over the ~9 s, ` ` cancels) / **receive** (sync meter from `sync01()`, then the audition+keep flow). All failure states per Global constraints.
- [ ] `pio run`; commit.

### Task 5: the real thing

- [ ] Two devices, 30 cm, quiet room: 10 transfers, ≥ 8 must land (CRC-clean). Then: noisy room, 1 m, volume 50% — record the success matrix in this doc.
- [ ] The received patch *sounds identical* (it's the same bytes — verify with a saved .gpat diff).
- [ ] One-device fallback: send from a phone playing a recorded chirp WAV (made via doc 05 or the host tests' file output) → device receives. This doubles as the "share sounds over the internet" story: a chirp is a *shareable audio file*.

## Acceptance criteria

- Host loopback suite green including the abuse matrix (this is the bulk of "done").
- ≥ 80% transfer success at 30 cm/quiet on real hardware; every failure mode visible.
- One row + one modal card; zero gestures; audio law intact; playable with no mic.

## Risks

- **Physics** — gated by Task 1 before any real investment.
- ~~Speaker/mic codec exclusivity~~ — settled since this doc was written: half-duplex, handled by `io/listen.cpp` (CLAUDE.md rule 7). The residual risk is the round-gap problem, addressed by the streaming capture extension in Task 3.
- Room acoustics variance — the 50-baud/20 ms symbol choice is deliberately conservative; tune with the Task 5 matrix, never below the measured margin.
- Cuteness trap: if Task 5 lands under 80%, ship it as "experimental" behind the row's label (`Share sound (beta)`) or don't ship — a flaky party trick is worse than none.
