# 06 — USB MIDI out: GLIDE as an expressive controller (MPE-lite)

> **For agentic workers:** Execute task-by-task (superpowers:subagent-driven-development or superpowers:executing-plans). Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination. **Hardware-gated:** Task 1 is a go/no-go spike on a real ADV.

**Goal:** Plug GLIDE into a laptop and it enumerates as a USB-MIDI device; every glide, hammer-on, chord slide and bend arrives in the DAW as per-string pitch-bent MIDI (MPE-style: one channel per string), driving any soft synth with GLIDE's fretless feel.

**Architecture:** A **pure, host-tested** `dsp::MidiMap` translates the existing NoteEvent stream + per-lane pitch snapshots into MIDI bytes (this is the hard part, and it runs on a PC). A thin `io/` layer feeds those bytes to the USB stack. Shipped as a **separate build env** (`GLIDE-MIDI.bin`) until the USB-mode change is proven not to disturb flashing/Launcher/serial — then folded into the main binary behind a settings row.

**Tech stack:** ESP32-S3 native USB via the Arduino core's TinyUSB (`ARDUINO_USB_MODE=0`) — **version-sensitive; verify inside the pinned `espressif32@6.12.0`** (Task 1). No new pinned libraries unless the spike demands Adafruit TinyUSB.

**Effort:** L (2–4 days, hardware in the loop). **Risk:** high on the USB-stack config, low on the mapping (host-tested).

**UI-cost budget (the simplicity rule):** zero new gestures, **1 settings row** (`USB MIDI: off/on`), and that only lands in phase 2 when the envs merge. Within budget.

## Why this

The README's long game is "this instrument grows into real hardware." The shortest path to GLIDE mattering *outside* its speaker is to let its unique input vocabulary — legato lane hand-offs, chord slides, continuous pitch — drive real studio synths. Nothing else with a $30 street price speaks fretless-MPE. Crucially, the *engine already computes everything needed*: per-voice `currentPitch()` is fractional MIDI, updated per block, glides included. The work is a translator, not a feature.

**Scope guard:** MIDI **out** only. MIDI-in (GLIDE as a sound module) is a separate future doc — it drags clock-sync, voice-allocation and local-off questions in with it. YAGNI.

## Design

### Channel + bend scheme (MPE-lite)

- Channel 1 = global (unused for notes). Channels 2–5 = string lanes 0–3. Backing/drones/loop → channel 6 (mono-summarized: note-ons only, no bend tracking — a DAW can mute it).
- Per lane: `NoteEvent::On (legato=false)` → NoteOn at `round(pitchMidi)`, velocity 100, preceded by a pitch-bend positioning the fractional offset. `legato=true` hand-offs and `Retarget` do **not** send NoteOn — the *bend* carries the glide (this is what makes it feel fretless in the DAW).
- Continuous: each UI frame (~33 ms), for each sounding lane, send 14-bit pitch bend = `(currentPitch - anchorNote) / kBendRangeSemis`, where `anchorNote` is the NoteOn's integer note. `kBendRangeSemis = 24` (an MPE-common default; document that the user sets ±24 in their synth, and send RPN 0,0 = 24 semitones on connect for synths that listen).
- If a glide exceeds ±24 st from the anchor (octave sweeps can): re-anchor — NoteOff old, NoteOn at the current nearest semitone with bend continuing from there. Rare, correct, and testable.
- Tilt (when routed) → CC74 on the lane channels (the MPE "brightness" lane control — semantically right for a cutoff/vibrato macro). Never pitch from tilt (**tilt is never pitch bend**, rule 5 — CC74 is not pitch).
- `AllOff`/panic → CC123 (All Notes Off) on channels 2–6.

### The pure mapper

```cpp
// dsp/midi_map.h — pure C++, host-tested. NoteEvents + per-lane pitch
// snapshots in; MIDI bytes out. Owns anchors, running status per channel,
// and the re-anchor rule. Knows nothing about USB.
class MidiMap {
public:
    void onEvent(const NoteEvent& ev);              // note lifecycle
    void onFramePitch(uint8_t lane, float midi);    // sounding pitch, per UI frame
    void onTilt(float a01);                         // 0..1 -> CC74, rate-limited
    void reset();                                   // -> All Notes Off burst
    int  drain(uint8_t* out, int cap);              // pull encoded bytes
};
```
Internally a small byte FIFO (256 B), bend deduplication (don't resend an unchanged 14-bit value), CC74 rate-limited to ~30 Hz and 1-bit hysteresis.

### The io glue + build env

- `src/io/usb_midi.{h,cpp}`: init the TinyUSB MIDI interface, and a `pump()` called each UI frame: feed `MidiMap::drain` into the endpoint. Compiled only under `-DGLIDE_USB_MIDI=1`.
- `platformio.ini` gains `[env:cardputer-adv-midi]` extending the main env with `-DGLIDE_USB_MIDI=1 -DARDUINO_USB_MODE=0` → `dist/GLIDE-MIDI.bin`. The main binary is untouched in phase 1.

## Global constraints

- `dsp/midi_map` is pure C++ / C++11 / env:native-tested (`pio run -e native && .pio/build/native/program`).
- Pinned platform stays `espressif32@6.12.0` (CLAUDE rule 6). If the spike shows its TinyUSB can't do MIDI, pin `adafruit/Adafruit TinyUSB Library` at an exact version in the midi env only — never bump the platform.
- Audio rules untouched (USB work lives nowhere near the render task).
- Tilt is never pitch bend — CC74 only.
- Failure-visible: if USB-MIDI init fails at boot in the midi env, HUD shows an amber `MIDI ✗` (not fatal — the instrument still plays).

## Tasks

### Task 1: hardware spike (go/no-go gate)

**Files:** scratch sketch only (do not commit into src/) — e.g. `support/spike_usbmidi/`.

- [ ] Confirm what the pinned framework offers: `ls ~/.platformio/packages/framework-arduinoespressif32/libraries/USB` — look for `USBMIDI` (present in arduino-esp32 ≥ 2.0.9; the pinned 6.12.0 platform ships 2.0.17 — verify, don't trust this doc).
- [ ] Minimal sketch: `ARDUINO_USB_MODE=0`, init USBMIDI, send a note every second. Flash to a real ADV. Verify: (a) enumerates on macOS (`Audio MIDI Setup`), (b) notes arrive, (c) **serial monitor still works** (CDC over TinyUSB), (d) **normal flashing still works**, and (e) the G0-boot Launcher entry is unaffected.
- [ ] Record findings in this doc's margin (edit the file) — especially anything that breaks. **If (d) or (e) fails, stop and redesign around it before any src/ work.**

### Task 2: the pure mapper, test-first

**Files:** Create `src/dsp/midi_map.h`, `src/dsp/midi_map.cpp`. Modify `src/test_dsp.cpp`.

- [ ] Failing tests (byte-exact expectations):
  - fresh On lane 0 pitch 60.30 → bend-then-NoteOn on ch 2: `E1 <14bit(+0.30/24)>`, `91 3C 64`.
  - legato hand-off to 62.0 then framePitch ramps → **no** new NoteOn, bends only, deduped.
  - Off → `81 3C 40`-style NoteOff on the right channel with the *anchor* note number.
  - re-anchor: framePitch walks +25 st → NoteOff, NoteOn at the new nearest note, bend continuity (no full-scale jump between two consecutive drains).
  - reset() → CC123 on ch 2–6.
- [ ] Implement; native green; commit.

### Task 3: io glue + env

**Files:** Create `src/io/usb_midi.h/.cpp`. Modify `platformio.ini`, `src/main.cpp`, the event/frame call sites in `src/io/keys.cpp` / `src/ui/perform_screen.cpp` (wherever NoteEvents are pushed and lead pitches are read per frame — mirror how the looper taps `record()` right after `audio::pushEvent`).

- [ ] Feed `MidiMap::onEvent` at the same place `looper::record` taps the stream; feed `onFramePitch` from the per-frame lane pitch reads; `onTilt` from the tilt code. All guarded by `#ifdef GLIDE_USB_MIDI`.
- [ ] `[env:cardputer-adv-midi]` as in Design, `extra_scripts` producing `dist/GLIDE-MIDI.bin` (copy_dist naming — check `support/copy_dist.py` for how the name derives).
- [ ] `pio run -e cardputer-adv-midi`; commit.

### Task 4: hardware verification

- [ ] Into a DAW with an MPE-capable synth (Vital/Surge, free): chord slide across 3 strings → three voices glide independently. Hammer-on/pull-off → no re-attacks. Octave sweep → re-anchor is inaudible. Panic kills everything.
- [ ] 30-minute jam plugged in: no audio dropouts on the device itself, no stuck notes in the DAW.
- [ ] Main env (`pio run`) byte-unchanged behavior (build both, flash main, confirm normal).

### Task 5 (phase 2, separate PR): merge into the main binary

- [ ] Only after Task 1 findings prove USB-mode 0 is harmless on this hardware: move the flag into the main env, add the `USB MIDI: off/on` settings row (NVS key `usbmidi`, default off, takes effect on reboot — say so in the row's value: `on (reboot)`).

## Acceptance criteria

- Native suite green including byte-exact mapper tests.
- Chord slides arrive as three independent bent channels in a DAW.
- Flashing, Launcher entry, and serial all still work in the midi env (Task 1 evidence).
- Main binary untouched until phase 2; then: 1 settings row, zero gestures.

## Risks

- **USB mode 0 side-effects** (flashing/Launcher/CDC): the entire reason Task 1 is a gate and phase 1 is a separate binary.
- Framework USBMIDI API drift across core versions: pinned platform mitigates; the spike documents the exact API found.
- Bend resolution at ±24 st ≈ 0.6 cents/step — inaudible; not a risk, recorded for completeness.
