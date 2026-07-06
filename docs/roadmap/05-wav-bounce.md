# 05 — Bounce: record the master output to a WAV on SD

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination.

**Goal:** One settings row starts/stops recording the exact 32 kHz master output to `/glide/rec/take-NNN.wav` — the jam you just played leaves the device as audio.

**Architecture:** The audio render task pushes each finished int16 block into a lock-free SPSC ring (the audio thread **never blocks and never touches SD**); a low-priority writer task on the UI core drains the ring to a WAV file in chunks and patches the header sizes on stop. Overflow drops samples and *says so* — it can degrade the recording, never the performance.

**Tech stack:** `src/io/audio_engine.cpp` (tap + ring), a new `src/io/recorder.{h,cpp}` (writer task + WAV header), `src/ui/settings_screen.cpp` (row), `src/ui/perform_screen.cpp` (a small ●REC indicator). Reuse `src/dsp/spsc_queue.h` if its API fits (read it first); otherwise a 16 KB power-of-two ring with atomic head/tail indices, local to recorder.

**Effort:** M (1–1.5 days). **Risk:** medium — SD throughput/latency on the unverified SD pins is the open question; the design makes every failure mode visible and non-fatal.

**UI-cost budget (the simplicity rule):** zero new gestures, **1 settings row** (`Record WAV: start/stop`). Within budget.

## Why this

Everything GLIDE produces today dies in the air above a one-watt speaker. The player who finally nails the take — the progression, the loop, the solo — has nothing. A phone recording of a tiny speaker is not the sound; the *master bus* is the sound, and it already exists as clean int16 blocks 250 times a second. Recording it is a tap and a file write. This single row turns every good jam into a shareable artifact, which is also the project's best marketing loop (players posting takes) for zero cloud, zero accounts — on brand.

Data budget: 32000 Hz × 2 bytes mono = **64 KB/s** (≈ 3.8 MB/min). Any SD card sustains this hundreds of times over; the only real risk is *latency spikes* during FAT writes, which the ring absorbs.

## Design

- **Tap point:** in `audio_engine.cpp`, immediately after a block is rendered and converted to the int16 buffer handed to `playRaw` (find the conversion; the 3-buffer rotation is load-bearing — the tap is a *copy out*, it must not touch the rotation or pacing). `recorder::push(const int16_t* block, int n)` — inline, lock-free, drops (and counts) when full.
- **Ring:** 16 KB (8192 samples = 256 ms) is ample for FAT hiccups; make it a compile-time constant in recorder.cpp.
- **Writer task:** created lazily on first record-start (`xTaskCreatePinnedToCore`, priority 1, core 1 — same core as UI, far below the audio task). Loop: drain ≥ 4 KB chunks → `file.write`; `file.flush()` once per second (bounded loss on power-pull). On stop: drain remainder, seek to bytes 4 and 40, patch RIFF/data sizes, close.
- **WAV header:** canonical 44-byte PCM header, mono, 16-bit, 32000 Hz. Write a placeholder-size header at start; patch on stop. A file that never got patched (power loss) is still playable by most tools — acceptable.
- **Naming:** `take-001.wav` counting up (scan the dir once at record-start for the next free number).
- **UI:** the settings row shows `Record WAV: start` / `stop (0:42)`. While recording, the perform screen shows a red `●` + elapsed near the looper status (top-left cluster — visually consistent with REC/LOOP). If drops occurred, the dot turns amber and the stop row reads `stop (0:42, 3 gaps)`.
- **Failure visibility:** no card → red HUD flash `NO CARD`; write error mid-take → stop cleanly, red flash `SD WRITE FAIL`, file keeps what it got. The audio path is *provably* unaffected: push is wait-free.

## Global constraints

- **Never** touch the playRaw 3-buffer rotation, block size, or pacing. The audio task may gain only the wait-free push.
- Failures visible; instrument fully playable with no card; SD pins remain hardware-unverified (inherit that caveat — verify on a real ADV before trusting).
- `dsp/` untouched (this is pure `io/`+`ui/`). Native suite must stay green anyway: `pio run -e native && .pio/build/native/program`.
- NVS: nothing persisted (recording state is not config).

## Tasks

### Task 1: recorder module (ring + WAV writer)

**Files:** Create `src/io/recorder.h`, `src/io/recorder.cpp`. Read `src/dsp/spsc_queue.h` first and reuse it if it stores raw PODs with a capacity ≥ 8192 samples.

- [ ] API:
```cpp
namespace recorder {
bool start();                 // false + reason() on failure (no card, dir, file)
void stop();                  // drains, patches header, closes
bool active();
uint32_t elapsedMs();
uint32_t drops();             // dropped-sample count (0 = clean take)
const char* reason();         // last failure, for the HUD flash
void push(const int16_t* s, int n);  // audio-thread side: wait-free, drops on full
}
```
- [ ] Implement the ring (or wrap spsc_queue), the writer task, header write/patch, `take-NNN` naming. Guard every SD call with the failure path.
- [ ] Build `pio run`. Commit: `io: recorder — wait-free master-bus tap to WAV on SD`.

### Task 2: the tap in the audio engine

**Files:** Modify `src/io/audio_engine.cpp`.

- [ ] Locate the int16 conversion feeding `playRaw`. Add exactly one line after it: `if (recorder::active()) recorder::push(buf16, n);`
- [ ] Re-read the surrounding code and confirm the 3-buffer rotation and `isPlaying()` pacing are untouched. State this in the commit message.
- [ ] Build; commit.

### Task 3: UI (row + indicator)

**Files:** Modify `src/ui/settings_screen.cpp`, `src/ui/perform_screen.cpp`.

- [ ] LIBRARY (or PERFORM — match whichever group holds Save to SD) row `Record WAV`: action row; `format` renders `start` or `stop (m:ss)` (+ `, N gaps` when `drops() > 0`); `adjust`/select toggles via `recorder::start()/stop()`, red-flashing `reason()` on failure.
- [ ] Perform screen: red `●` + elapsed while `recorder::active()`, amber when `drops() > 0`, placed beside the looper status so the recording states read as one cluster.
- [ ] Build; commit.

### Task 4: device verification (the real gate)

- [ ] Record 3 minutes of heavy playing (8 voices + progression + loop + delay/reverb). Pull the card; verify in Audacity: 32 kHz mono, no gaps (`drops() == 0`), waveform matches what was heard.
- [ ] Start with no card → red flash, nothing crashes. Yank the card mid-take → clean stop + red flash, file playable up to the yank.
- [ ] Run the phase0 mindset: while recording, listen for any audio starvation for 5 minutes. Any dropout = the tap is wrong; stop and fix (the tap must be the only audio-task change).
- [ ] Check free-heap before/after task creation; note numbers in the commit.

## Acceptance criteria

- A multi-minute take plays back clean on a computer; `drops() == 0` under normal jamming.
- Audio starvation: zero, verified by ear over 5+ minutes while recording.
- All failure modes flash a reason; no card leaves the instrument untouched.
- One settings row, zero gestures, dsp/ untouched, native green.

## Risks

- **SD latency spikes** bigger than 256 ms would drop samples — visible via the gap counter; if it happens in practice, double the ring, don't grow priorities.
- **Header-unpatched files** on power loss: acceptable (players still open them).
- Flash wear is irrelevant (SD, not NVS). File count growth: the NNN scan is O(files); fine below hundreds of takes — note, don't engineer around.
