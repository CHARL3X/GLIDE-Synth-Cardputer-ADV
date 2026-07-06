# 19 — MIDI in: GLIDE as a sound module (and a clock follower)

> **For agentic workers:** Execute task-by-task. Steps use `- [ ]` checkboxes. Read `CLAUDE.md` and `README.md` first. See `docs/roadmap/00-INDEX.md` for cross-doc coordination. **Depends on doc 06** (the USB-mode spike and the `cardputer-adv-midi` env) — execute 06 through its Task 1 gate first; this doc reuses its findings and infrastructure.

**Goal:** Plug GLIDE into a laptop or a MIDI keyboard (via USB) and play its engine from outside: notes drive voices through the same NoteEvent path the keys use, pitch bend bends, MIDI clock disciplines the jam tempo — the generative sound engine becomes a desk synth between pocket jams.

**Architecture:** The mirror image of doc 06's pure mapper: a host-tested `dsp::MidiParse` turns incoming USB-MIDI bytes into `NoteEvent`s + control values; the io glue pushes them through `audio::pushEvent` exactly like a key press (free-poly allocation on the no-lane path, `lane = 0xFF`). Local keys keep working — external notes are just more hands. Clock: MIDI clock (0xF8, 24 ppq) low-pass-filtered into `tempoBpm`, so the progression, delay sync, LFO sync and groove all follow the DAW.

**Tech stack:** `src/dsp/midi_parse.{h,cpp}` (pure), extension of doc 06's `src/io/usb_midi.{h,cpp}` (RX callback), settings integration by *upgrading doc 06's row* rather than adding one.

**Effort:** M–L (1–2 days on top of 06). **Risk:** medium (rides on 06's USB findings).

**UI-cost budget:** zero new gestures, **0 new rows** — doc 06's `USB MIDI` row upgrades from `off/on` to `off / out / in / both`. Net UI cost across both docs stays one row.

## Why this

Doc 06 makes GLIDE's *playing* valuable to the studio; this makes GLIDE's *engine* valuable to it. The sound half of the product — generative patches, the morphing, the mod matrix, the FX room — currently exists only under 40 playable keys. MIDI-in opens it to sequencers, and the payoff cases are exactly GLIDE-shaped: sequence a line in the DAW, then *glide on top of it from the device keys* (external notes and local notes share the voice pool and the legato rules — chord-slide against a sequence is a genuinely new trick). And clock-follow means the whole jam machinery (progression, synced delay, groove) locks to an external session with zero taps.

Scope guard (the questions this doc answers now so they don't creep): **no MPE-in** (plain notes + global bend), **no CC-to-matrix mapping layer** (CC74 → the tilt-A mod slot input and CC64 → sustain only), **no local-off** (local keys always play; it's an instrument first). Each would be its own doc if ever wanted.

## Design

### The pure parser

```cpp
// dsp/midi_parse.h — pure C++, host-tested. USB-MIDI bytes in, engine
// intents out. Owns running status, 14-bit bend assembly, and clock
// smoothing. Knows nothing about USB or the synth.
namespace dsp {
struct MidiIntent {
    enum Type : uint8_t { None, NoteOn, NoteOff, Bend, CC, ClockTempo, Panic } type;
    uint8_t note; uint8_t value;   // note/vel or CC num/val
    float bendSemis;               // Bend: -2..+2 (standard range)
    float bpm;                     // ClockTempo: smoothed estimate
};
class MidiParse {
public:
    // Feed raw MIDI bytes (already de-framed from USB packets by the io
    // layer); poll intents. tick(nowMs) drives clock-tempo estimation.
    void feed(const uint8_t* bytes, int n);
    void tick(uint32_t nowMs);
    bool poll(MidiIntent& out);
};
}
```
- Channel policy: **omni** (accept all channels) — a sound module with a channel selector is a settings row nobody asked for.
- Clock: timestamp 0xF8 arrivals; BPM = 60000 / (24 × mean interval) over a 24-tick window, one-pole smoothed (τ ≈ 1 s); emit `ClockTempo` only when the estimate moves > 0.5 BPM (hysteresis — don't jitter the delay). Start/Stop (0xFA/0xFC): Stop emits `Panic`-lite? No — Stop emits nothing (local performance continues); document the choice.
- Velocity → level: the engine has no velocity input today; map velocity to nothing in v1 (fixed like the keys). Honest and symmetric with doc 06's fixed-100 out. (A velocity→amp path would be a `SynthParams` live-mod addition — noted as future, not built.)

### The io glue + engine feed

- External NoteOn → `NoteEvent::make(On, 200 + note % 56, 0xFF, false, note)` — ids in a 200+ range so they can never collide with key codes or the looper's `id+128` range (**check looper.h's id math and pick a provably disjoint range; document it in params.h's comment block**). No lane → free-poly allocation (the engine already handles `lane = 0xFF`; verify against synth.cpp's alloc path).
- Bend → `p.bendCents` merge: external bend *adds* to the local bend-key ramp (both are performance state; the sum clamps at the engine's existing limits — find where bendCents is written per frame and sum there).
- CC64 sustain: gate the same sustain mechanism `space` drives (read how keys.cpp implements it; reuse, don't parallel). CC74 → feed the tilt-A mod-source input (`tiltAVal`) only when tilt is disabled/flat — simplest non-conflicting rule; state it in the code.
- Clock-follow: when ClockTempo intents arrive, they set the jam tempo (same setter as tap tempo) and the HUD flashes `SYNC ♪` once on lock. Manual tempo edits while clocked flash amber (external clock wins until unplugged — one rule, no modes).
- Looper records external notes too (they flow through the same `pushEvent` tap) — that's a *feature* (sequence in, capture as a take), and it needs zero work. Note it in the docs.

## Global constraints

- All of doc 06's constraints (pinned platform, USB env separation until phase 2, failure-visible `MIDI ✗`).
- `dsp/midi_parse` pure / C++11 / native-tested: `pio run -e native && .pio/build/native/program`.
- Local keys always play; external input must never be able to wedge the instrument (a stuck external note is cleared by panic like any note — verify the 200+ id range panics correctly).

## Tasks

### Task 1: the parser, test-first

**Files:** Create `src/dsp/midi_parse.{h,cpp}`. Modify `src/test_dsp.cpp`.

- [ ] Failing tests: NoteOn/Off incl. running status and NoteOn-vel-0-as-Off; 14-bit bend assembly to ±2 semis; CC64/74 pass-through; clock: 24 ticks at exactly 120 BPM spacing → one ClockTempo ≈ 120 ± 0.5 after the window fills, and *no* re-emission while stable (hysteresis test); garbage bytes → no intents, no state corruption (feed random bytes, then a clean NoteOn still parses).
- [ ] Implement; native green; commit.

### Task 2: engine feed + id range

**Files:** Modify `src/io/usb_midi.{h,cpp}` (RX path), `src/dsp/params.h` (the id-range comment), call sites per Design (bend merge, sustain reuse, tempo setter).

- [ ] Verify the disjoint id range against keys (`y*14+x` ≤ 55) and looper (`id+128`) — pick and document; wire NoteEvents, bend merge, CC64/74, clock→tempo with the HUD flash and the amber manual-edit rule.
- [ ] `pio run -e cardputer-adv-midi`; commit.

### Task 3: the row upgrade

**Files:** Modify `src/ui/settings_screen.cpp`, `src/storage/glide_config.cpp`.

- [ ] Doc 06's `USB MIDI` row becomes `off / out / in / both` (NVS `usbmidi` u8 gains values 2, 3 — coordinate with 06 if it landed with a bool; the ledger note in 00-INDEX covers this).
- [ ] `pio run`; commit.

### Task 4: hardware verification

- [ ] DAW sequence → GLIDE plays it; switch patches mid-sequence (the sequence re-voices — the loop-pedal trick, now for external input).
- [ ] Play local keys *over* the sequence: shared voice pool behaves, chord-slide against held external notes works, panic clears everything.
- [ ] Clock-follow: DAW at 97 BPM → delay repeats and progression lock; tempo change in the DAW follows within ~2 s without delay-time zipper noise.
- [ ] Unplug mid-note: no stuck voices (USB detach → treat as panic for external ids — implement if the test fails, in the detach callback).

## Acceptance criteria

- Native green incl. parser robustness and clock hysteresis.
- Sequence + local play coexist; clock-follow locks the whole jam machinery.
- 0 new rows (the 06 row upgraded); local play can never be wedged by external input.

## Risks

- Inherits doc 06's USB-stack risk wholesale — that's why it sequences after 06's gate.
- Clock jitter from cheap hosts: the 1 s smoothing + hysteresis is the defense; if a host still wobbles the delay, widen τ — one constant.
- Id-range collision would be catastrophic and silent — hence it's a named, documented, test-checked decision, not an inline literal.
