// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "keys.h"

#include <M5Cardputer.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../dsp/arp.h"
#include "../dsp/pitch.h"
#include "../dsp/scales.h"
#include "../storage/glide_config.h"
#include "../ui/coach.h"
#include "../ui/hud.h"
#include "../ui/morph.h"
#include "../ui/sound_card.h"
#include "audio_engine.h"
#include "demo.h"
#include "looper.h"

namespace keys {

namespace {

// ---- key codes: y*14 + x on the physical 4x14 matrix --------------------
constexpr int kCode = 14;
inline int code(int y, int x) { return y * kCode + x; }

constexpr int kKeyExit = 0;        // `
constexpr int kKeyOctDown = 11;    // -
constexpr int kKeyOctUp = 12;      // =
constexpr int kKeyPanic = 13;      // backspace
constexpr int kKeyTab = 14;        // tab
constexpr int kKeyBendDown = 25;   // [
constexpr int kKeyBendUp = 26;     // ]
constexpr int kKeyTapTempo = 27;   // backslash — tap the jam tempo, live
constexpr int kKeyFn = 28;         // fn
constexpr int kKeyShift = 29;      // shift
constexpr int kKeyKeyCycle = 37;   // k — fn+K cycles the root key (live retune)
constexpr int kKeyScaleCycle = 31; // s — fn+S cycles the scale (live)
constexpr int kKeyArp = 30;        // a — fn+A cycles the arpeggiator (off/up/down/up-down)
constexpr int kKeyArpRate = 45;    // z — fn+Z steps the arp rate (the key under a)
constexpr int kKeyArpSpan = 46;    // x — fn+X flips the arp span (1 / 2 octaves)
constexpr int kKeyTilt = 41;       // enter
constexpr int kKeyCtrl = 42;       // ctrl  (octave down, left thumb)
constexpr int kKeyOpt = 43;        // opt   (octave up, left thumb)
constexpr int kKeyAlt = 44;        // alt   (the loop pedal, left thumb —
                                   // space already covers sustain)
constexpr int kKeySpace = 55;      // space (sustain)

// The ADV's TCA8418 keyboard controller buffers key events in a ~10-deep FIFO,
// and M5Cardputer.update() pulls only ONE per call. At 30 fps a fast chord
// (press + release = 2 events/key) can overflow the FIFO faster than it drains;
// a dropped *release* event leaves a key stuck down, and an overflow can wedge
// the controller's IRQ so input freezes with a note ringing. Draining the whole
// queue every frame keeps the pressed-set honest. 12 > FIFO depth; once the
// queue is empty updateKeyList() is a near-free no-op.
constexpr int kKbDrainMax = 12;

// note grid: code -> (string, col). string 0 = bottom row = lowest.
// Physical x offsets per row give the fretboard stagger:
//   y=0 (1..0) x1..10 | y=1 (q..p) x1..10 | y=2 (a..;) x2..11 | y=3 (z../) x3..12
int8_t gGridString[56];
int8_t gGridCol[56];

void buildGridTables() {
    memset(gGridString, -1, sizeof gGridString);
    memset(gGridCol, -1, sizeof gGridCol);
    const int xStart[4] = {1, 1, 2, 3};   // physical x of col 0, by matrix row y
    const int stringOf[4] = {3, 2, 1, 0}; // matrix row y -> musical string
    for (int y = 0; y < 4; ++y)
        for (int colI = 0; colI < dsp::kGridCols; ++colI) {
            const int cd = code(y, xStart[y] + colI);
            gGridString[cd] = (int8_t)stringOf[y];
            gGridCol[cd] = (int8_t)colI;
        }
}

// ---- held-note bookkeeping ----------------------------------------------
struct HeldNote {
    bool physical = false;   // finger still on the key
    bool sustained = false;  // released but held by sustain/latch
    bool drone = false;      // jam-row latch: rings until tapped again
    int8_t string = -1;
    int8_t col = -1;
    bool chromAtPress = false;  // captured at press: shift never repitches
    float pitch = 0.f;
    int8_t droneVoicing = 0;    // semitone of the drone's partner voice
                                // (0 = single note, 7 = fifth, 12 = octave)
};
HeldNote gNotes[56];

// A fat drone is two voices on one key: the primary (id = cd) plus a partner
// (id = cd + this offset). Key codes are 0..55, so +64 can never collide with
// another key's primary id, and stays inside uint8_t.
constexpr int kDroneStackOffset = 64;
inline int droneIntervalFor(uint8_t voicing) {
    return voicing == 2 ? 7 : (voicing == 1 ? 12 : 0);  // 0=single 1=oct 2=fifth
}

inline bool sounding(const HeldNote& n) {
    return n.string >= 0 && (n.physical || n.sustained || n.drone);
}

// per-lane press-order stack for hammer-on / pull-off (string mode)
uint8_t gLaneStack[4][16];
int gLaneDepth[4];

uint64_t gPrevMask = 0;
uint32_t gLastPollMs = 0;
uint32_t gLastActivityMs = 0;  // most recent touch (idle-dim / screensaver timer)

float gBendCents = 0.f;
bool gSustainHeld = false;

// The G0 boot button (GPIO 0) doubles as a performance trigger at runtime: an
// assignable macro (the "trigger throw" — muffle/brighten/dive/grit, by default
// a filter muffle). This layer only reports the raw held level; perform_screen
// owns the action, depth and momentary-vs-latch decision. Idle is HIGH
// (pull-up) so the fail-safe is "no effect" — a misread can never wedge the
// sound. Level-read each poll.
constexpr int kBootBtnPin = 0;
bool gTriggerHeld = false;

// TCA8418 keyboard-controller wedge recovery. The vendor reader (M5Cardputer's
// TCA8418KeyboardReader) only processes events when an edge-triggered INT-pin
// ISR has fired, and it clears that flag whenever INT_STAT bit0 reads 0 — but a
// FIFO *overflow* sets a different INT bit it never clears, leaving INT asserted
// (LOW) with the flag false and no further edge coming. The reader then goes
// permanently deaf: a note rings on (a real held voice — it shows in the trail),
// no key registers (not even backspace), yet the raw-GPIO G0 trigger still
// responds. Draining can't fix it (the drain is gated on the same flag); only a
// reader rebuild does. We watchdog the INT line and rebuild when it's wedged.
// INT is active-low/open-drain (idle HIGH; deasserts as soon as events are read,
// even while a key is physically held), so a sustained LOW means undrained
// events — and kKbWedgeMs is far longer than any real drain gap (we pull up to
// kKbDrainMax events/frame at ~30 fps), so normal play never trips it.
constexpr int kKbIntPin = 11;          // TCA8418 INT (DEFAULT_TCA8418_INT_PIN)
constexpr uint32_t kKbWedgeMs = 400;   // INT held asserted this long = wedged

// tilt key (enter): short tap cycles off->single->dual->off, long-press
// toggles the mod-latch. Fired on RELEASE so the two gestures don't collide.
bool gTiltLatched = false;
uint32_t gTiltPressMs = 0;
bool gTiltLatchFired = false;            // long-press consumed this hold
constexpr uint32_t kTiltLongMs = 350;

// loop pedal (alt): tap = rec/play/overdub cycle, long-press = clear the whole
// loop, fn+alt = peel/undo one layer (redo bounces at the ends). Same release-
// fired gesture split as the tilt key. Clear is destructive, so its hold sits a
// touch past a tap (700 ms, like the exit hold) — a lingering overdub press
// must not nuke the take.
uint32_t gLoopPressMs = 0;
bool gLoopHoldFired = false;
constexpr uint32_t kLoopHoldMs = 700;

// restart (`): reboots the device — and the ADV boots straight back into
// GLIDE, so this is a restart, not an exit (Launcher needs BTN RST). It sits
// top-left where it gets brushed
// constantly — accidental reboots (and, via the boot splash, wiped sessions).
// Require a deliberate hold; a tap just shows a hint.
uint32_t gExitDownMs = 0;
bool gExitFired = false;
constexpr uint32_t kExitHoldMs = 700;

// fn+k: short tap cycles the root key (fired on RELEASE), long-press fires
// LISTEN — auto key detection off the mic. Same gesture split as the tilt key.
// +shift reverses the tap (fn+shift+k = key DOWN); the direction is captured at
// the PRESS because in a three-finger chord shift is often let go first, so
// reading it at release would flip a deliberate "down" into "up".
uint32_t gKeyCyclePressMs = 0;
int8_t gKeyCyclePending = 0;    // a fn+k press is in flight: +1 up, -1 down, 0 none
bool gKeyListenFired = false;   // long-press consumed this hold
constexpr uint32_t kListenHoldMs = 500;

// jam motion: a millis-paced beat clock on the UI thread re-strikes the
// latched drones, turning the static bed into a living backing. Pure event
// pushes through the existing queue — the dsp/ engine stays untouched.
uint32_t gLastBeatMs = 0;
int gArpIdx = 0;
int gArpPrevCd = -1;        // the arp note currently sounding (released before
                           // the next strikes) so the run stays one-at-a-time
constexpr uint8_t kJamArp = 2;  // jamMotion: arp (re-strike one per beat)
uint32_t gBeatFlashAt = 0;  // last re-strike, for the grid-map beat blink
int gBeatFlashCd = -1;      // which drone key fired (-1 = the whole chord)
constexpr uint32_t kBeatFlashMs = 120;

// auto chord progression (jamMotion == kJamProg): the easy backing. A jam-row
// tap APPENDS a chord step — no timing, repeats allowed — and the beat clock
// then walks the steps one diatonic chord per bar, gliding from chord to chord,
// as a protected drone-grade layer you solo over. Backing voice ids 120..122
// are clear of lead (0..55), drone partners (64..119), loop playback (128..183)
// and the boot chime (250).
constexpr uint8_t kJamProg = 3;
constexpr uint8_t kProgIdBase = 120;
constexpr int kProgVoices = 3;       // a diatonic triad
constexpr int kMaxProgSteps = 16;    // a progression is short by nature
struct ProgStep { int8_t string; int8_t col; bool chrom; };
ProgStep gProg[kMaxProgSteps];
int gProgLen = 0;
int gProgIdx = 0;
bool gProgSounding = false;
uint32_t gProgLastAdv = 0;  // beat clock for the chord advance (0 = re-arm)
uint32_t gProgDueMs = 0;    // arpeggiated start: the first strike waits until here
// The layout (octave/root/scale) snapshotted when the progression was built, so
// shifting octave or changing key while soloing leaves the backing in place.
dsp::Layout gProgLayout;

// The arpeggiator: SESSION state, never persisted — a mode you can't find your
// way out of is the old chromatic-toggle sin, so every boot starts with it off
// (the metronome precedent). It is a texture on the progression, not a second
// chord engine: while on, the jam row keeps its tap-a-chord meaning and the
// sounding chord is handed to dsp::Arp (on the audio thread) instead of being
// struck as a block chord. Whatever the persisted Jam motion says, the arp
// rides the progression engine (jamMotionNow).
bool gArpOn = false;
uint8_t gArpPattern = 0;                  // 0 up, 1 down, 2 up/down
uint8_t gArpRate = dsp::kArpDefaultRate;  // delaySync division (1/8)
uint8_t gArpSpan = 1;                     // octaves
uint16_t gArpGen = 0;                     // bumped per CHORD change: restarts the walk
dsp::ArpConfig gArpCfg;                   // the last published chord — republished
                                          // unchanged when only pattern/rate/span move
constexpr uint8_t kArpRates[4] = {3, 4, 5, 1};  // 1/8 -> 1/8T -> 1/16 -> 1/4 (fn+z)

inline uint8_t jamMotionNow() { return gArpOn ? kJamProg : store::get().jamMotion; }

void publishArp() {
    gArpCfg.on = gArpOn ? 1 : 0;
    gArpCfg.pattern = gArpPattern;
    gArpCfg.rate = gArpRate;
    gArpCfg.span = gArpSpan;
    audio::setArp(gArpCfg);
}

// quick-edit
bool gQuickEdit = false;
int gQuickParam = 0;  // 0..9 selected slot
uint32_t gRepeatStart = 0, gRepeatLast = 0;
int gRepeatDir = 0;
bool gRepeatCoarse = false;

// Volume (left-thumb ctrl/opt) auto-repeat: hold to ramp instead of tapping.
// Same DAS/ARR timing as the other repeating keys (cfg::kRepeat*).
uint32_t gVolRepeatStart = 0, gVolRepeatLast = 0;
int gVolRepeatDir = 0;

inline bool held(uint64_t mask, int cd) { return (mask >> cd) & 1ULL; }

float pitchFor(const HeldNote& n) {
    // drones sit in the backing register (settings: Jam octave), not on the
    // grid — the old fixed "octave under" was mud on the speaker
    const dsp::Layout& lay = store::get().layout;
    return dsp::gridToMidi(lay, n.string, n.col, n.chromAtPress) +
           (n.drone ? dsp::backingShift(lay) : 0.f);
}

// ---- lane stack helpers ---------------------------------------------------
void laneRemove(int lane, uint8_t cd) {
    int d = gLaneDepth[lane];
    for (int i = 0; i < d; ++i) {
        if (gLaneStack[lane][i] == cd) {
            for (int j = i; j < d - 1; ++j) gLaneStack[lane][j] = gLaneStack[lane][j + 1];
            gLaneDepth[lane] = d - 1;
            return;
        }
    }
}

int laneTop(int lane) {
    return gLaneDepth[lane] > 0 ? gLaneStack[lane][gLaneDepth[lane] - 1] : -1;
}

void lanePush(int lane, uint8_t cd) {
    if (gLaneDepth[lane] < 16) gLaneStack[lane][gLaneDepth[lane]++] = cd;
}

// ---- note on/off -----------------------------------------------------------
void sendOff(int cd);
void sendDroneOff(int cd);
void restrikeDrone(int cd);
void clearProg();      // defined with the progression engine, used by panic()

// Append a chord step to the progression (the jam-row tap and demo mode share
// this). Arms the beat clock and freezes the key/register on the first step.
bool progAppend(int string, int col, bool chrom) {
    if (gProgLen >= kMaxProgSteps) return false;
    gProg[gProgLen].string = (int8_t)string;
    gProg[gProgLen].col = (int8_t)col;
    gProg[gProgLen].chrom = chrom;
    ++gProgLen;
    if (gProgLen == 1) {
        gProgIdx = 0;
        gProgLastAdv = 0;                  // arm the clock
        gProgLayout = store::get().layout; // freeze the key/register here
        // Arpeggiated, the first chord takes a one-beat count-in instead of
        // striking under the tap. An arp is a rhythm: step 0 landing at
        // whatever phase the finger had made a quick three-tap start play
        // "on keypress" (measured on hardware). A block chord's swell hides
        // that phase, so the plain progression still strikes at once.
        const uint16_t bpm = store::get().jamBpm < 20 ? 20 : store::get().jamBpm;
        gProgDueMs = gArpOn ? (millis() + 60000u / bpm) | 1u : 0;  // never 0
    }
    return true;
}

void notePress(int cd, bool shiftHeld) {
    auto& cfgr = store::get();
    HeldNote& n = gNotes[cd];

    // jam rows: tap to latch a drone, tap again to let it fade
    if (gGridString[cd] < cfgr.jamRows) {
        // progression mode: a jam-row tap APPENDS a chord step (no timing,
        // repeats allowed) — spell the loop, then solo on the rows above.
        if (jamMotionNow() == kJamProg) {
            const bool chrom = shiftHeld || !cfgr.layout.scaleLock;
            if (!progAppend(gGridString[cd], gGridCol[cd], chrom)) {
                hud::showError("PROG", "full (16)");
                return;
            }
            float pp[kProgVoices];
            dsp::chordPitches(gProgLayout, gGridString[cd], gGridCol[cd], chrom, pp, kProgVoices);
            char v[20];
            snprintf(v, sizeof v, "%d: %s", gProgLen, dsp::pitchClassName(pp[0]));
            hud::show("PROG", v, -1.f);
            store::odoNote();  // a chord-step tap is played, not configured
            coach::notify(coach::Ev::ProgStep);
            return;
        }
        if (n.drone && n.string >= 0) {  // second tap: release the drone
            sendDroneOff(cd);
            return;
        }
        n.physical = true;
        n.sustained = false;
        n.drone = true;
        n.string = gGridString[cd];
        n.col = gGridCol[cd];
        n.chromAtPress = shiftHeld || !cfgr.layout.scaleLock;
        n.pitch = pitchFor(n);
        store::odoNote();  // the latch tap counts once; jam re-strikes never do
        if (cfgr.jamMotion == kJamArp) {
            // arp: this key joins the pattern but stays SILENT until the beat
            // clock voices it one-at-a-time — single notes, so the run is clean
            n.droneVoicing = 0;
        } else {
            // sustained / pulse: ring now; a fuller backing adds a fifth/octave
            n.droneVoicing = (int8_t)droneIntervalFor(cfgr.droneVoicing);
            restrikeDrone(cd);  // fires primary (+ partner) as drone voices
        }
        return;
    }

    n.physical = true;
    n.sustained = false;
    n.drone = false;
    n.string = gGridString[cd];
    n.col = gGridCol[cd];
    n.chromAtPress = shiftHeld || !cfgr.layout.scaleLock;
    n.pitch = pitchFor(n);

    bool legato = false;
    if (cfgr.stringMode) {
        // the lane already sings if anything is on its stack
        legato = gLaneDepth[n.string] > 0;
        lanePush(n.string, (uint8_t)cd);
        if (legato) coach::notify(coach::Ev::Slide);  // the slide, performed
    }
    const dsp::NoteEvent ev = dsp::NoteEvent::make(dsp::NoteEvent::On, (uint8_t)cd,
                                                   (uint8_t)n.string, legato, n.pitch);
    audio::pushEvent(ev);
    looper::record(ev);
    store::odoNote();
}

void sendOff(int cd) {
    const dsp::NoteEvent ev = dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)cd);
    audio::pushEvent(ev);
    looper::record(ev);  // no-op unless this id's On was recorded
    gNotes[cd].physical = false;
    gNotes[cd].sustained = false;
    gNotes[cd].drone = false;
    gNotes[cd].droneVoicing = 0;
    gNotes[cd].string = -1;
}

// Fire a drone's voices: the primary plus, for a fat voicing, its partner a
// fifth/octave up. Both flagged drone -> cap-exempt and steal-proof. Used by
// both the initial latch and the jam-motion re-strikes.
void restrikeDrone(int cd) {
    HeldNote& n = gNotes[cd];
    dsp::NoteEvent ev = dsp::NoteEvent::make(dsp::NoteEvent::On, (uint8_t)cd, 0xFF, false, n.pitch);
    ev.drone = true;
    audio::pushEvent(ev);
    if (n.droneVoicing > 0) {
        dsp::NoteEvent ev2 = dsp::NoteEvent::make(
            dsp::NoteEvent::On, (uint8_t)(cd + kDroneStackOffset), 0xFF, false,
            n.pitch + n.droneVoicing);
        ev2.drone = true;
        audio::pushEvent(ev2);
    }
}

// Release a latched drone and its partner voice together.
void sendDroneOff(int cd) {
    if (gNotes[cd].droneVoicing > 0)
        audio::pushEvent(
            dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)(cd + kDroneStackOffset)));
    sendOff(cd);
}

void noteRelease(int cd) {
    auto& cfgr = store::get();
    HeldNote& n = gNotes[cd];
    if (n.string < 0) return;
    n.physical = false;
    if (n.drone) return;  // drones ignore the finger leaving — tap-to-stop

    if (gSustainHeld) {
        n.sustained = true;
        if (cfgr.stringMode) laneRemove(n.string, (uint8_t)cd);
        return;
    }

    if (cfgr.stringMode) {
        const int lane = n.string;
        const bool wasOwner = (laneTop(lane) == cd);
        laneRemove(lane, (uint8_t)cd);
        if (wasOwner) {
            const int back = laneTop(lane);
            if (back >= 0) {
                // pull-off: glide the lane's voice back to the still-held key
                HeldNote& b = gNotes[back];
                const dsp::NoteEvent ev = dsp::NoteEvent::make(
                    dsp::NoteEvent::On, (uint8_t)back, (uint8_t)lane, true, b.pitch);
                audio::pushEvent(ev);
                looper::record(ev);
                n.string = -1;
                return;
            }
            sendOff(cd);
            return;
        }
        // non-owner key never owned the sounding voice — just forget it
        n.string = -1;
        return;
    }
    sendOff(cd);
}

void flushSustained() {
    for (int cd = 0; cd < 56; ++cd)
        if (gNotes[cd].sustained && !gNotes[cd].physical) sendOff(cd);
}

void clearAllNotes() {
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::AllOff, 0));
    for (auto& n : gNotes) {
        n.physical = n.sustained = n.drone = false;
        n.string = -1;
    }
    for (int l = 0; l < 4; ++l) gLaneDepth[l] = 0;
    gArpPrevCd = -1;
    gArpIdx = 0;
    gBendCents = 0.f;
    store::get().synth.bendCents = 0.f;
}

// Clear the solo layer only — latched drones keep ringing. Used by sound
// switching and settings trips so the backing never dies under you.
void clearLeadNotes() {
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::LeadsOff, 0));
    for (auto& n : gNotes) {
        if (n.drone && n.string >= 0) continue;  // the jam survives
        n.physical = n.sustained = n.drone = false;
        n.string = -1;
    }
    for (int l = 0; l < 4; ++l) gLaneDepth[l] = 0;
    gBendCents = 0.f;
    store::get().synth.bendCents = 0.f;
}

void panic() {
    demo::stop();    // panic means EVERYTHING stops — clearing the bed alone
                     // left the demo melody soloing over silence until a grid
                     // key delivered the takeover nobody was asking for
    looper::stop();  // silence the loop layer too — the take survives
    clearProg();     // the auto-progression clears with panic, like the drones
    clearAllNotes();
    store::unlockBacking();  // backing gone -> the split is no longer in effect
    hud::show("PANIC", "all notes off", -1.f);
}

// ---- octave / layout controls ----------------------------------------------
void retuneHeldNotes() {
    for (int cd = 0; cd < 56; ++cd) {
        HeldNote& n = gNotes[cd];
        if (!sounding(n)) continue;
        n.pitch = pitchFor(n);
        const dsp::NoteEvent ev = dsp::NoteEvent::make(
            dsp::NoteEvent::Retarget, (uint8_t)cd, (uint8_t)n.string, false, n.pitch);
        audio::pushEvent(ev);
        looper::record(ev);  // octave sweeps of recorded notes loop too
        if (n.drone && n.droneVoicing > 0)  // sweep the drone's partner too
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Retarget,
                                                  (uint8_t)(cd + kDroneStackOffset), 0xFF, false,
                                                  n.pitch + n.droneVoicing));
    }
}

void octaveShift(int dir) {
    auto& cfgr = store::get();
    const int next = cfgr.layout.octave + dir;
    if (next < 1 || next > 7) {
        hud::showError("OCTAVE", dir > 0 ? "ceiling (7)" : "floor (1)");
        return;
    }
    cfgr.layout.octave = (int8_t)next;
    if (cfgr.octaveGlide) {
        retuneHeldNotes();  // Retarget glides: a fat-finger becomes a sweep
    } else {
        // instant: re-strike held notes at the new octave
        for (int cd = 0; cd < 56; ++cd) {
            HeldNote& n = gNotes[cd];
            if (!sounding(n)) continue;
            n.pitch = pitchFor(n);
            if (n.drone) {  // keep the drone (and its partner) a drone
                if (n.droneVoicing > 0)
                    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off,
                                                          (uint8_t)(cd + kDroneStackOffset)));
                audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)cd));
                restrikeDrone(cd);
                continue;
            }
            const dsp::NoteEvent off = dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)cd);
            const dsp::NoteEvent on = dsp::NoteEvent::make(dsp::NoteEvent::On, (uint8_t)cd,
                                                           (uint8_t)n.string, false, n.pitch);
            audio::pushEvent(off);
            looper::record(off);
            audio::pushEvent(on);
            looper::record(on);
        }
    }
    char v[16];
    snprintf(v, sizeof v, "%d", next);
    hud::show("OCTAVE", v, (next - 1) / 6.f);
    store::markDirty();
}

// Master volume on the left-thumb keys (ctrl/opt). Octave stays on -/=, so no
// control is lost — the thumb pair was a redundant second octave mapping.
// Volume rides the SOLO layer only. When a jam is locked, the backing keeps the
// level it was frozen at — so you can set a quiet backing, swap to a new synth,
// and ride the new sound's volume independently over the held bed. With no split
// (backing unlocked), the solo IS the whole sound, so this is the master volume.
void adjustVolume(int dir) {
    auto& g = store::get();
    const float step = dir * 0.01f;  // 1% taps; hold ctrl/opt to ramp
    auto clamp01 = [](float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); };
    g.synth.masterVol = clamp01(g.synth.masterVol + step);
    store::markDirty();
    char v[12];
    snprintf(v, sizeof v, "%d%%", (int)(g.synth.masterVol * 100));
    hud::show(g.backingLocked ? "SOLO VOL" : "VOLUME", v, g.synth.masterVol);
}

// fn+ctrl / fn+opt: the metronome's own level, on the same thumb keys as the
// master volume — the fn layer turns "volume" into "click volume". 5% taps
// with no hold-to-ramp (0..100 is 20 taps; the ramp stays master-volume-only).
void adjustMetroVol(int dir) {
    auto& g = store::get();
    int v = (int)g.metroVol + dir * 5;
    g.metroVol = (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v));
    store::markDirty();
    char t[12];
    snprintf(t, sizeof t, "%d%%", (int)g.metroVol);
    hud::show("METRO VOL", t, g.metroVol / 100.f);
}

// fn+K: walk the root key up a semitone, wrapping B->C; fn+shift+K walks it
// down (A -> G# -> G instead of eleven taps round the ring). Built for the
// audition loop — step the key, play a phrase against a song, clash, step
// again — with no trip to settings. Held notes keep their pitch; new notes play
// in the new key.
void cycleRootKey(int dir) {
    auto& g = store::get();
    g.layout.rootSemis = (uint8_t)((g.layout.rootSemis + 12 + dir) % 12);
    store::markDirty();
    hud::show("KEY", dsp::kNoteNames[g.layout.rootSemis], -1.f);
    coach::notify(coach::Ev::KeyCycle);
}

// fn+S: walk the scale table — the same audition loop as fn+K but for mode
// color; +shift walks it backwards. Held notes keep sounding; new notes land in
// the new scale. The HUD names it in full so "phdom" never has to be decoded
// mid-jam.
void cycleScale(int dir) {
    auto& g = store::get();
    g.layout.scaleIdx = (uint8_t)((g.layout.scaleIdx + dsp::kScaleCount + dir) % dsp::kScaleCount);
    store::markDirty();
    hud::show("SCALE", dsp::kScales[g.layout.scaleIdx].name, -1.f);
    coach::notify(coach::Ev::ScaleCycle);
}

// ---- tilt mode cycle --------------------------------------------------------
// enter short-tap: off -> single (axis A) -> dual (A + roll B) -> off. Entering
// an active state self-heals a missing route/depth so it's never a dead toggle.
void cycleTilt() {
    auto& cfgr = store::get();
    if (!cfgr.tiltOn) {
        cfgr.tiltOn = true;
        cfgr.tiltDual = false;
    } else if (!cfgr.tiltDual) {
        cfgr.tiltDual = true;
    } else {
        cfgr.tiltOn = false;
        cfgr.tiltDual = false;
        gTiltLatched = false;  // a frozen mod is meaningless once tilt is off
    }
    if (cfgr.tiltOn) {
        // don't auto-fill a physical route on an axis the global morph flag is
        // already driving — it would silently change the patch's personality
        if (!cfgr.tiltMorphA && cfgr.tiltRoute == store::TiltRoute::Off)
            cfgr.tiltRoute = store::TiltRoute::Cutoff;
        if (cfgr.tiltDepth < 0.05f) cfgr.tiltDepth = 0.6f;
        if (cfgr.tiltDual) {
            if (!cfgr.tiltMorphB && cfgr.tiltRouteB == store::TiltRoute::Off)
                cfgr.tiltRouteB = store::TiltRoute::Cutoff;
            if (cfgr.tiltDepthB < 0.05f) cfgr.tiltDepthB = 0.6f;
        }
    }
    // HUD names the EFFECTIVE routes (the morph flag masks the patch route)
    const store::TiltRoute effA = store::effectiveTiltRoute(cfgr.tiltMorphA, cfgr.tiltRoute);
    const store::TiltRoute effB = store::effectiveTiltRoute(cfgr.tiltMorphB, cfgr.tiltRouteB);
    char v[24];
    if (!cfgr.tiltOn) {
        hud::show("TILT", "off", -1.f);
    } else if (!cfgr.tiltDual) {
        snprintf(v, sizeof v, "%s %d%%", store::tiltRouteName(effA),
                 (int)(cfgr.tiltDepth * 100));
        hud::show("TILT", v, cfgr.tiltDepth);
    } else {
        snprintf(v, sizeof v, "%s + %s", store::tiltRouteName(effA),
                 store::tiltRouteName(effB));
        hud::show("TILT 2D", v, cfgr.tiltDepthB);
    }
    store::markDirty();
}

// ---- auto chord progression -------------------------------------------------
// Release the progression's backing voices (mode change / panic).
void silenceProg() {
    if (gArpCfg.n > 0) {  // the arp has no chord to walk: it lets its note go
        gArpCfg.n = 0;
        publishArp();
    }
    if (!gProgSounding) return;
    for (int i = 0; i < kProgVoices; ++i)
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)(kProgIdBase + i)));
    gProgSounding = false;
}

// Erase the whole progression (panic / start over).
void clearProg() {
    silenceProg();
    gProgLen = 0;
    gProgIdx = 0;
    gProgLastAdv = 0;
    gProgDueMs = 0;
}

// Fire step `idx` as a diatonic chord on the fixed backing ids. Re-firing the
// same ids makes the engine glide+re-attack each voice from the previous chord
// (legato hand-off in noteOn) — the progression slides between changes, soft
// on a pad. Flagged drone: cap-exempt, steal-proof, ignores bend/tilt, and
// re-voices through whatever sound is selected.
void strikeProgChord(int idx) {
    float p[kProgVoices];
    // frozen layout: the backing keeps its key/register while the solo roams
    const int n = dsp::chordPitches(gProgLayout, gProg[idx].string, gProg[idx].col,
                                    gProg[idx].chrom, p, kProgVoices);
    if (gArpOn) {
        // arpeggiated: the chord goes to the sequencer, not to the block-chord
        // voices — a fresh gen restarts the walk on this downbeat
        for (int i = 0; i < 3; ++i) gArpCfg.pitches[i] = i < n ? p[i] : 0.f;
        gArpCfg.n = (uint8_t)(n > 3 ? 3 : n);
        gArpCfg.gen = ++gArpGen;
        publishArp();
    } else {
        for (int i = 0; i < kProgVoices; ++i) {
            if (i < n) {
                dsp::NoteEvent ev = dsp::NoteEvent::make(dsp::NoteEvent::On,
                                                         (uint8_t)(kProgIdBase + i), 0xFF, false, p[i]);
                ev.drone = true;
                audio::pushEvent(ev);
            } else if (gProgSounding) {
                audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)(kProgIdBase + i)));
            }
        }
    }
    // a chord change is the bar's downbeat: phase-lock the metronome to it so
    // the click and the backing land in the same render block
    if (store::get().metroOn)
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::MetroSync, 0));
    gProgSounding = true;
}

// Advance the progression on the beat clock: one chord every jamChordBeats
// beats. Millis-paced on the UI thread — chord changes are bars apart, so
// frame jitter is musically invisible (unlike the loop, which needs the audio
// thread). Steady-phase advance with a resync if a blocking screen stalled us.
void progTick(uint32_t nowMs) {
    auto& cfgr = store::get();
    if (gProgLen == 0) { gProgSounding = false; return; }
    const uint32_t beatMs = 60000u / (cfgr.jamBpm < 20 ? 20 : cfgr.jamBpm);
    const uint32_t stepMs = beatMs * (cfgr.jamChordBeats < 1 ? 1 : cfgr.jamChordBeats);
    if (gProgLastAdv == 0) {  // first downbeat (or re-arm): strike now
        if (gProgDueMs && (int32_t)(nowMs - gProgDueMs) < 0) return;  // arp count-in
        gProgDueMs = 0;
        gProgIdx %= gProgLen;
        strikeProgChord(gProgIdx);
        gProgLastAdv = nowMs;
        gBeatFlashAt = nowMs;
        gBeatFlashCd = -1;
        return;
    }
    if (nowMs - gProgLastAdv < stepMs) return;
    gProgLastAdv += stepMs;                                   // steady phase, no drift
    if (nowMs - gProgLastAdv > stepMs) gProgLastAdv = nowMs;  // resync if we fell behind
    gProgIdx = (gProgIdx + 1) % gProgLen;
    strikeProgChord(gProgIdx);
    gBeatFlashAt = nowMs;
    gBeatFlashCd = -1;
}

// ---- the arpeggiator gestures ----------------------------------------------
void arpHud() {
    char v[24];
    if (!gArpOn) snprintf(v, sizeof v, "off");
    else snprintf(v, sizeof v, "%s  %s  %doct", dsp::arpPatternName(gArpPattern),
                  dsp::delaySyncName(gArpRate), gArpSpan);
    hud::show("ARP", v, -1.f);
}

// fn+a: off -> up -> down -> up/down -> off; fn+shift+a walks the same ring the
// other way (off <- up <- down <- up/down <- off), so "up" back to "off" is one
// tap, not three. Arming takes the jam row over from any latched drones (the
// row is the arp's now) and hands the sounding chord to the sequencer;
// disarming strikes it back as a pad. The progression itself is untouched
// either way — the arp is how it sounds, not what it is.
void cycleArp(int dir) {
    auto& cfgr = store::get();
    const int next = gArpOn ? (int)gArpPattern + dir : -1;  // -1: arming below
    if (!gArpOn) {
        if (cfgr.jamRows == 0) {  // nothing to arpeggiate: say so, name the fix
            hud::showError("ARP", "needs jam rows", "settings > Jam rows");
            return;
        }
        for (int cd = 0; cd < 56; ++cd)
            if (gNotes[cd].drone && gNotes[cd].string >= 0) sendDroneOff(cd);
        if (gArpPrevCd >= 0) {  // the old per-beat arp motion's held note
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)gArpPrevCd));
            gArpPrevCd = -1;
        }
        gArpOn = true;
        // stepping backwards INTO the ring lands on its last pattern
        gArpPattern = dir < 0 ? (uint8_t)(dsp::kArpPatternCount - 1) : 0;
        if (gProgSounding) {
            for (int i = 0; i < kProgVoices; ++i)  // the pad yields to the walk
                audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)(kProgIdBase + i)));
            strikeProgChord(gProgIdx);
        } else {
            publishArp();
        }
    } else if (cfgr.jamRows > 0 && next >= 0 && next < dsp::kArpPatternCount) {
        gArpPattern = (uint8_t)next;
        publishArp();
    } else {  // walked off either end of the ring -> off
        gArpOn = false;
        gArpPattern = 0;
        publishArp();  // the sequencer lets its note go
        // the pads come back — unless the persisted motion isn't progression,
        // in which case jamTick retires the chord on its next frame anyway
        if (gProgSounding && cfgr.jamMotion == kJamProg && cfgr.jamRows > 0)
            strikeProgChord(gProgIdx);
    }
    arpHud();
    coach::notify(coach::Ev::ArpCycle);  // found on their own: the tip retires
}

// fn+z steps the rate (1/8 -> 1/8T -> 1/16 -> 1/4), fn+x flips the span. Both
// adjust even while the arp is off — the HUD says what they set and fn+a then
// arrives already tuned; no dead keys in the layer.
void adjustArp(bool span) {
    if (span) {
        gArpSpan = (uint8_t)(gArpSpan >= dsp::kArpMaxSpan ? 1 : gArpSpan + 1);
    } else {
        int i = 0;
        while (i < 4 && kArpRates[i] != gArpRate) ++i;
        gArpRate = kArpRates[(i + 1) % 4];
    }
    publishArp();
    char v[24];
    const char* tail = gArpOn ? "" : " (arp off)";
    if (span) snprintf(v, sizeof v, "%d octave%s%s", gArpSpan, gArpSpan > 1 ? "s" : "", tail);
    else      snprintf(v, sizeof v, "%s%s", dsp::delaySyncName(gArpRate), tail);
    hud::show(span ? "ARP SPAN" : "ARP RATE", v, -1.f);
}

}  // namespace

// Is any protected backing layer currently alive? Drives the solo/backing
// sound split: switching sound while this is true freezes the backing. Public
// (keys.h): the perform loop's quiet-moment gate reads it too.
bool backingActive() {
    if (gProgLen > 0) return true;
    if (looper::state() != looper::State::Empty) return true;
    for (int cd = 0; cd < 56; ++cd)
        if (gNotes[cd].drone && gNotes[cd].string >= 0) return true;
    return false;
}

namespace {

// ---- jam motion -------------------------------------------------------------
// Collect the latched drones, sorted low->high so an arp rolls musically.
int collectDrones(int* out) {
    int n = 0;
    for (int cd = 0; cd < 56; ++cd)
        if (gNotes[cd].drone && gNotes[cd].string >= 0) out[n++] = cd;
    for (int i = 1; i < n; ++i) {  // insertion sort by pitch (n is small)
        const int key = out[i];
        const float kp = gNotes[key].pitch;
        int j = i - 1;
        while (j >= 0 && gNotes[out[j]].pitch > kp) { out[j + 1] = out[j]; --j; }
        out[j + 1] = key;
    }
    return n;
}

void jamTick(uint32_t nowMs) {
    auto& cfgr = store::get();

    // progression: the chord sequencer. Silence its backing if the mode or the
    // jam rows turned off, so the prog voice ids don't ring on with no driver.
    static uint8_t prevMotion = 0;
    const uint8_t motion = jamMotionNow();  // the arpeggiator rides the progression
    const bool progMode = motion == kJamProg && cfgr.jamRows > 0;
    if (!progMode && (prevMotion == kJamProg || gProgSounding)) silenceProg();
    // leaving arp: drop the single note it was holding so it doesn't ring on
    if (prevMotion == kJamArp && motion != kJamArp && gArpPrevCd >= 0) {
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)gArpPrevCd));
        gArpPrevCd = -1;
    }
    prevMotion = motion;
    if (progMode) { progTick(nowMs); gLastBeatMs = 0; return; }

    if (motion == 0 || cfgr.jamRows == 0) { gLastBeatMs = 0; return; }
    int drones[56];
    const int nd = collectDrones(drones);
    if (nd == 0) {
        gLastBeatMs = 0;
        gArpIdx = 0;
        if (gArpPrevCd >= 0) {  // pattern emptied: stop the hanging arp note
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)gArpPrevCd));
            gArpPrevCd = -1;
        }
        return;
    }

    const uint32_t beatMs = 60000u / (cfgr.jamBpm < 20 ? 20 : cfgr.jamBpm);
    if (gLastBeatMs == 0) { gLastBeatMs = nowMs; return; }  // start the clock cleanly
    if (nowMs - gLastBeatMs < beatMs) return;
    gLastBeatMs += beatMs;                                  // steady phase, no drift
    if (nowMs - gLastBeatMs > beatMs) gLastBeatMs = nowMs;  // resync if we fell far behind

    if (motion == 1) {                    // pulse: re-strike the whole chord
        for (int i = 0; i < nd; ++i) restrikeDrone(drones[i]);
        gBeatFlashCd = -1;
    } else {                              // arp: ONE note per beat, low->high —
                                          // release the last before the next so
                                          // it's a clean run, never a cluster
        gArpIdx %= nd;
        const int cd = drones[gArpIdx];
        if (gArpPrevCd >= 0 && gArpPrevCd != cd)
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)gArpPrevCd));
        dsp::NoteEvent ev = dsp::NoteEvent::make(dsp::NoteEvent::On, (uint8_t)cd, 0xFF, false,
                                                 gNotes[cd].pitch);
        ev.backing = true;  // protected + own bus, but releases at the normal
                            // rate (not the long drone tail) so notes don't smear
        audio::pushEvent(ev);
        gArpPrevCd = cd;
        gBeatFlashCd = cd;
        gArpIdx = (gArpIdx + 1) % nd;
    }
    gBeatFlashAt = nowMs;  // the grid map blinks the struck key(s)
    // keep the click herded onto the pulse/arp beat it plays under
    if (cfgr.metroOn)
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::MetroSync, 1));
}

// ---- quick-edit layer (hold fn, top row selects, [ ] adjusts) ---------------
struct QuickParam {
    const char* name;
};
const QuickParam kQuick[10] = {
    {"GLIDE"},   {"ATTACK"}, {"DECAY"},      {"SUSTAIN"}, {"RELEASE"},
    {"WAVEFORM"}, {"CUTOFF"}, {"VOICES"},     {"BEND RANGE"}, {"VOLUME"},
};

float quickFill(int idx) {
    auto& cfgr = store::get();
    auto& s = cfgr.synth;
    switch (idx) {
        case 0: return s.glideS / 2.f;
        case 1: return s.attackS / 2.f;
        case 2: return s.decayS / 2.f;
        case 3: return s.sustain;
        case 4: return s.releaseS / 3.f;
        case 6: return log10f(s.cutoffHz / 80.f) / log10f(12000.f / 80.f);
        case 7: return s.voiceCount / 8.f;
        case 8: return cfgr.bendRange / 12.f;
        case 9: return s.masterVol;
        default: return -1.f;
    }
}

void hudQuickValue(int idx) {
    char v[20];
    quickParamValue(idx, v, sizeof v);
    hud::show(kQuick[idx].name, v, quickFill(idx));
}

template <typename T>
T clampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void adjustQuickParam(int idx, int dir, bool coarse) {
    auto& cfgr = store::get();
    auto& s = cfgr.synth;
    switch (idx) {
        case 0: s.glideS = clampT(s.glideS + dir * (coarse ? 0.1f : 0.01f), 0.f, 2.f); break;
        case 1: s.attackS = clampT(s.attackS + dir * (coarse ? 0.05f : 0.005f), 0.f, 2.f); break;
        case 2: s.decayS = clampT(s.decayS + dir * (coarse ? 0.1f : 0.01f), 0.001f, 2.f); break;
        case 3: s.sustain = clampT(s.sustain + dir * (coarse ? 0.2f : 0.05f), 0.f, 1.f); break;
        case 4: s.releaseS = clampT(s.releaseS + dir * (coarse ? 0.1f : 0.01f), 0.001f, 3.f); break;
        case 5: {
            int w = ((int)s.wave + dir + (int)dsp::Waveform::Count) % (int)dsp::Waveform::Count;
            s.wave = (dsp::Waveform)w;
            break;
        }
        case 6: s.cutoffHz = clampT(s.cutoffHz * (dir > 0 ? (coarse ? 1.3f : 1.06f)
                                                          : (coarse ? 1.f / 1.3f : 1.f / 1.06f)),
                                    80.f, 12000.f); break;
        case 7: s.voiceCount = (uint8_t)clampT((int)s.voiceCount + dir, 1, (int)dsp::kMaxVoices); break;
        case 8: cfgr.bendRange = (uint8_t)clampT((int)cfgr.bendRange + dir, 1, 12); break;
        case 9: s.masterVol = clampT(s.masterVol + dir * (coarse ? 0.2f : 0.05f), 0.f, 1.f); break;
        default: break;
    }
    store::markDirty();
    hudQuickValue(idx);
}

}  // namespace

void begin() {
    buildGridTables();
    memset(gLaneDepth, 0, sizeof gLaneDepth);
    pinMode(kBootBtnPin, INPUT_PULLUP);  // G0 as a momentary gate (idle HIGH)
}

void resync() {
    // a blocking screen consumed key events; rebuild edge state from scratch
    M5Cardputer.update();
    for (int i = 0; i < kKbDrainMax; ++i) M5Cardputer.Keyboard.updateKeyList();
    gPrevMask = 0;
    for (const auto& p : M5Cardputer.Keyboard.keyList()) gPrevMask |= 1ULL << code(p.y, p.x);
    // enter is settings' increment key; if it's held across the exit there's no
    // fresh press edge, so mark the long-press already consumed — it won't
    // latch (or cycle on release) until enter is released and pressed anew.
    gTiltLatchFired = true;
    gLoopHoldFired = true;  // same guard for the loop pedal
    gExitFired = true;      // and the exit hold — needs a fresh press to fire
    // fn+k may still be held coming back from the LISTEN modal it fired —
    // don't let its release read as a fresh root-cycling tap.
    gKeyCyclePending = 0;
    gKeyListenFired = false;
    // The jam clock is NOT reset here: settings now ticks the backing every
    // frame (keys::tickBacking), so the progression/arp/drones play straight
    // through and their clocks stay live — re-arming would re-strike a chord
    // on return. progTick/jamTick self-resync if a frame was ever dropped.
    clearLeadNotes();  // drones, loop, and progression ride through settings
    gLastActivityMs = millis();  // interacting with a modal is activity — return awake
}

void consumeDismissHold() {
    // The same two flags resync() sets, and only those: the ` hold (exit) and
    // the enter hold (tilt latch), whose cleared state also suppresses the
    // route-cycle enter fires on release. Notes, edge mask and the backing
    // clocks are deliberately left ALONE — that is the whole point of having
    // this instead of resync().
    gExitFired = true;
    gTiltLatchFired = true;
}

void tickBacking(uint32_t nowMs) {
    jamTick(nowMs);  // keep the living backing advancing while settings owns the loop
}

// --- solo/backing split on a sound change (fn+q..p AND SD load/preview) ------
// Changing the live (solo) sound over a running jam freezes the backing on the
// sound it was playing, so only the solo changes — the bed holds its OG sound.
// Call begin() BEFORE applying the new sound, end() AFTER. Shared so the slot
// path and the SD path behave identically. begin() returns true if it locked the
// backing on this call (the caller may want to undo that on a cancel).
bool soundSwitchBegin() {
    bool lockedNow = false;
    if (!store::backingLocked() && backingActive()) {
        store::lockBacking();
        lockedNow = true;
    }
    clearLeadNotes();  // the new sound starts clean; drones/loop/progression survive
    return lockedNow;
}

void soundSwitchEnd() {
    auto& g = store::get();
    audio::setParams(g.synth, g.backingLocked ? g.backingSynth : g.synth);
}

Actions poll(uint32_t nowMs) {
    Actions act;
    auto& cfgr = store::get();

    M5Cardputer.update();
    // drain the rest of the TCA8418 FIFO this frame (see kKbDrainMax) so a fast
    // chord can't overflow it into a stuck key / frozen controller
    for (int i = 0; i < kKbDrainMax; ++i) M5Cardputer.Keyboard.updateKeyList();
    gTriggerHeld = (digitalRead(kBootBtnPin) == LOW);  // G0 trigger macro, raw level
    uint64_t cur = 0;
    for (const auto& p : M5Cardputer.Keyboard.keyList()) cur |= 1ULL << code(p.y, p.x);

    // --- keyboard wedge watchdog (see kKbIntPin) ---------------------------
    // INT asserted (LOW) means the controller has events the reader isn't
    // draining; sustained past kKbWedgeMs, the reader is deaf — rebuild it (a
    // fresh reader = empty key list + re-inited controller + re-attached ISR),
    // kill any stuck note, and resync from the now-empty keyboard.
    {
        static uint32_t intLowSince = 0;
        if (digitalRead(kKbIntPin) == LOW) {
            if (intLowSince == 0) intLowSince = nowMs ? nowMs : 1;  // 0 = "not low"
            else if (nowMs - intLowSince > kKbWedgeMs) {
                detachInterrupt(digitalPinToInterrupt(kKbIntPin));  // old ISR arg dies with the reader
                M5Cardputer.Keyboard.begin();  // rebuild: empty list, re-init controller, new ISR
                clearAllNotes();               // stuck id is unknown — silence everything
                gPrevMask = 0;                 // shadow now matches the empty keyboard
                cur = 0;                       // derive no presses/releases this frame
                intLowSince = 0;
                hud::showError("KEYBOARD", "recovered");
            }
        } else {
            intLowSince = 0;
        }
    }

    const uint64_t pressed = cur & ~gPrevMask;
    const uint64_t released = gPrevMask & ~cur;
    const float dtMs = gLastPollMs ? (float)(nowMs - gLastPollMs) : 16.f;
    gLastPollMs = nowMs;

    // Idle timer: any held key, any edge, or the G0 button is "a touch". Seed it
    // on the first poll so a fresh boot doesn't read as already-idle. Set before
    // the intro-card early-return so dismissing the card also counts as activity.
    if (gLastActivityMs == 0) gLastActivityMs = nowMs ? nowMs : 1;
    if (cur || pressed || released || gTriggerHeld) gLastActivityMs = nowMs ? nowMs : 1;

    // First-run intro card is modal: ANY key dismisses it, silently, and the
    // key is consumed — no note at full volume, no accidental exit, just gone.
    // (The perform loop still auto-times-out the card as a fallback.)
    if (!cfgr.seenIntro) {
        if (pressed) {
            cfgr.seenIntro = true;
            store::markDirty();
        }
        gPrevMask = cur;
        return act;  // nothing else acts while the card is up
    }

    // The one-time tour offer (existing devices) is modal the same way: enter
    // accepts, any other key declines, and the key is consumed either way.
    // coach ignores answers inside its grace window — a key mashed before the
    // card could be read is swallowed, never counted as "no thanks" forever.
    if (coach::offerActive()) {
        if (pressed) {
            if (held(pressed, kKeyTilt)) coach::offerAccept();  // enter
            else coach::offerDecline();
        }
        gPrevMask = cur;
        return act;
    }

    const bool shiftHeld = held(cur, kKeyShift);
    gSustainHeld = held(cur, kKeySpace);  // alt is the loop pedal now
    const bool wasQuickEdit = gQuickEdit;
    gQuickEdit = held(cur, kKeyFn);
    if (wasQuickEdit && !gQuickEdit) store::markDirty();  // persist on fn release

    // ---- presses ---------------------------------------------------------
    for (int cd = 0; cd < 56 && pressed; ++cd) {
        if (!held(pressed, cd)) continue;

        if (gGridString[cd] >= 0) {
            if (gQuickEdit) {
                if (cd == kKeyKeyCycle) {
                    // fn+K: decided on release — tap retunes the root key
                    // (live, +shift = down), a ~0.5 s hold fires LISTEN instead.
                    gKeyCyclePending = shiftHeld ? -1 : 1;
                    gKeyListenFired = false;
                    gKeyCyclePressMs = nowMs;
                    continue;
                }
                if (cd == kKeyScaleCycle) {  // fn+S: next scale (+shift: previous), immediately
                    cycleScale(shiftHeld ? -1 : 1);
                    continue;
                }
                if (cd == kKeyArp) { cycleArp(shiftHeld ? -1 : 1); continue; }  // fn+A: arpeggiator (+shift: back)
                if (cd == kKeyArpRate) { adjustArp(false); continue; }  // fn+Z: rate
                if (cd == kKeyArpSpan) { adjustArp(true); continue; }   // fn+X: span
                if (gGridString[cd] == 3) {
                    // top row selects the quick-edit parameter
                    gQuickParam = gGridCol[cd];
                    hudQuickValue(gQuickParam);
                } else if (gGridString[cd] == 2) {
                    // q..p row = the ten sounds; +shift saves over the slot
                    const int slot = gGridCol[cd];
                    char v[20];
                    if (shiftHeld) {
                        if (store::savePatch(slot)) {
                            snprintf(v, sizeof v, "saved -> %s", store::patchName(slot));
                            hud::show("SOUND", v, -1.f);
                        } else {
                            // the detail line names the way out (storage full ->
                            // "save to SD, then BKSP at boot") — see lastSaveHint
                            hud::showError("SOUND", store::lastSaveError(),
                                           store::lastSaveHint());
                        }
                    } else {
                        // switching sound over a running jam keeps the backing
                        // on its own sound: freeze it, then this patch is the
                        // solo voice only (own register + own effect on top)
                        soundSwitchBegin();
                        store::applyPatch(slot);
                        soundSwitchEnd();
                        // the identity card replaces the plain name flash — the
                        // sound you just summoned gets a face (it carries its
                        // own SOLO tag when the backing is locked)
                        soundcard::show();
                        morph::kick();  // arrive by glide, not by snap
                        coach::notify(coach::Ev::SlotLoad);
                    }
                }
                continue;  // grid is muted while editing
            }
            act.gridPressed = true;
            notePress(cd, shiftHeld);
            coach::notify(coach::Ev::Grid);
            // deliberately off-scale under scale lock: the player is fighting
            // the key/scale — exactly what fn+k / fn+s exist for
            if (shiftHeld && cfgr.layout.scaleLock) coach::notify(coach::Ev::ShiftNote);
            continue;
        }

        switch (cd) {
            case kKeyExit:
                // don't exit on the press — reboot is destructive and ` is too
                // easy to brush. Stamp it; the hold check below fires the exit.
                gExitDownMs = nowMs;
                gExitFired = false;
                // while the tour banner is up, a ` tap skips the tour instead
                // of hinting (the deliberate hold still exits as always)
                if (coach::tutorialActive()) coach::skip();
                else hud::show("RESTART", "hold ` to restart", -1.f);
                break;
            case kKeyTab:
                act.openSettings = true;
                break;
            case kKeyPanic:
                panic();
                break;
            case kKeyOctDown:
                if (gQuickEdit) adjustQuickParam(gQuickParam, -1, true);
                else octaveShift(-1);
                break;
            case kKeyOctUp:
                if (gQuickEdit) adjustQuickParam(gQuickParam, +1, true);
                else octaveShift(+1);
                break;
            case kKeyCtrl:
                if (gQuickEdit) { adjustMetroVol(-1); break; }  // fn layer: click level
                adjustVolume(-1);  // left-thumb volume (octave stays on -/=)
                gVolRepeatDir = -1;
                gVolRepeatStart = gVolRepeatLast = nowMs;  // arm hold-to-ramp
                break;
            case kKeyOpt:
                if (gQuickEdit) { adjustMetroVol(+1); break; }
                adjustVolume(+1);
                gVolRepeatDir = +1;
                gVolRepeatStart = gVolRepeatLast = nowMs;
                break;
            case kKeyBendDown:
                if (gQuickEdit) {
                    adjustQuickParam(gQuickParam, -1, false);
                    gRepeatDir = -1;
                    gRepeatCoarse = false;
                    gRepeatStart = gRepeatLast = nowMs;
                }
                break;
            case kKeyBendUp:
                if (gQuickEdit) {
                    adjustQuickParam(gQuickParam, +1, false);
                    gRepeatDir = +1;
                    gRepeatCoarse = false;
                    gRepeatStart = gRepeatLast = nowMs;
                }
                break;
            case kKeyTapTempo: {
                if (gQuickEdit) {
                    // fn+\ = metronome on/off: the click lives on the tempo key.
                    // The synth edge-detects metroOn and clicks the instant the
                    // params land — no event race, instant confirmation.
                    auto& g = store::get();
                    g.metroOn = !g.metroOn;
                    char v[20];
                    if (g.metroOn) snprintf(v, sizeof v, "on  %u bpm", (unsigned)g.jamBpm);
                    else           snprintf(v, sizeof v, "off");
                    hud::show("METRO", v, -1.f);
                    break;
                }
                // Tap the tempo in, live, without leaving the instrument. The
                // first tap of a series only reads the tempo back, so a stray
                // press can't change anything — there is nothing here to undo.
                char v[20];
                snprintf(v, sizeof v, "%u bpm", (unsigned)tapTempo(nowMs));
                hud::show("TEMPO", v, -1.f);
                break;
            }
            case kKeyTilt:
                // decide short-tap (cycle) vs long-press (latch) on release /
                // hold-threshold, not here — just stamp the press
                gTiltPressMs = nowMs;
                gTiltLatchFired = false;
                break;
            case kKeyAlt:
                if (held(cur, kKeyFn)) {  // fn+alt = peel/undo one loop layer
                    const int r = looper::peel(nowMs);
                    if (r != 0) {
                        char v[20];
                        snprintf(v, sizeof v, "%s  x%d", r == 1 ? "undo" : "redo",
                                 looper::liveLayers() + 1);
                        hud::show("LOOP", v, -1.f);
                    } else if (looper::state() != looper::State::Empty) {
                        hud::show("LOOP", "no layers", -1.f);
                    }
                    gLoopHoldFired = true;  // consume: no tap on release, no hold-clear
                } else {
                    // loop pedal: tap vs hold decided on release / threshold
                    gLoopPressMs = nowMs;
                    gLoopHoldFired = false;
                }
                break;
            default:
                break;
        }
    }

    // ---- releases ----------------------------------------------------------
    for (int cd = 0; cd < 56 && released; ++cd) {
        if (!held(released, cd)) continue;
        if (gGridString[cd] >= 0) {
            // fn+k released: if the LISTEN hold didn't consume it, it was a
            // short tap -> cycle the root. (pending is only ever set on the
            // quick-edit path, where the press never entered gNotes.)
            if (cd == kKeyKeyCycle && gKeyCyclePending != 0) {
                if (!gKeyListenFired) cycleRootKey(gKeyCyclePending);
                gKeyCyclePending = 0;
            }
            if (gNotes[cd].string >= 0) noteRelease(cd);
            continue;
        }
        if (cd == kKeyBendDown || cd == kKeyBendUp) gRepeatDir = 0;
        if (cd == kKeyCtrl || cd == kKeyOpt) gVolRepeatDir = 0;
        // tilt key released: if the long-press latch didn't fire, it was a
        // short tap -> cycle the tilt mode
        if (cd == kKeyTilt && !gTiltLatchFired) cycleTilt();
        // loop pedal released: short tap steps the rec -> play -> overdub cycle
        if (cd == kKeyAlt && !gLoopHoldFired) {
            const looper::State prev = looper::state();
            // the close tap snaps the take to the jam clock (Loop snap setting);
            // same tempo source that feeds SynthParams::tempoBpm each frame
            const looper::State s = looper::tap(nowMs, (float)cfgr.jamBpm, cfgr.loopSnap);
            char v[20];
            switch (s) {
                case looper::State::Recording:
                    hud::show("LOOP", "recording...", -1.f);
                    break;
                case looper::State::Playing:
                    if (prev == looper::State::Recording) {
                        snprintf(v, sizeof v, "%lu.%lus loop",
                                 (unsigned long)(looper::lengthMs() / 1000),
                                 (unsigned long)(looper::lengthMs() % 1000 / 100));
                        hud::show("LOOP", v, -1.f);
                    } else {
                        hud::show("LOOP", "play", -1.f);
                    }
                    break;
                case looper::State::Overdub:
                    hud::show("LOOP", "overdub", -1.f);
                    break;
                default:  // a too-short take was discarded
                    if (prev == looper::State::Recording)
                        hud::showError("LOOP", "too short");
                    break;
            }
        }
    }

    // tilt key held past the long-press threshold -> toggle the mod-latch
    // (fires once per hold; release then skips the cycle)
    if (held(cur, kKeyTilt) && !gTiltLatchFired && nowMs - gTiltPressMs >= kTiltLongMs) {
        gTiltLatched = !gTiltLatched;
        gTiltLatchFired = true;
        // a latch needs tilt running to hold anything — turn it on (single)
        if (!cfgr.tiltOn) {
            cfgr.tiltOn = true;
            if (cfgr.tiltRoute == store::TiltRoute::Off) cfgr.tiltRoute = store::TiltRoute::Cutoff;
            if (cfgr.tiltDepth < 0.05f) cfgr.tiltDepth = 0.6f;
            store::markDirty();
        }
        hud::show("TILT", gTiltLatched ? "latched" : "live", -1.f);
    }

    // loop pedal held past the threshold -> clear the whole loop (fires once per
    // hold). The take is performance state, so no confirm — just re-record; the
    // 700 ms hold keeps it clear of a normal overdub tap. fn+alt peels one layer.
    if (held(cur, kKeyAlt) && !gLoopHoldFired && nowMs - gLoopPressMs >= kLoopHoldMs) {
        gLoopHoldFired = true;
        if (looper::state() != looper::State::Empty) {
            looper::clear();
            hud::show("LOOP", "cleared", -1.f);
        }
    }

    // ` held past the threshold -> exit (reboot). A tap only showed the hint,
    // so a stray brush can no longer reboot you (and can't reach the boot-splash
    // reset that was wiping sessions).
    if (held(cur, kKeyExit) && !gExitFired && nowMs - gExitDownMs >= kExitHoldMs) {
        gExitFired = true;
        act.exitApp = true;
    }

    // fn+k held past the threshold -> LISTEN (fires once; the release is then
    // a no-op tap). The modal takes over the loop, so poll never sees the
    // release — resync() clears the pending flag on the way back.
    if (gKeyCyclePending != 0 && !gKeyListenFired && held(cur, kKeyKeyCycle) &&
        nowMs - gKeyCyclePressMs >= kListenHoldMs) {
        gKeyListenFired = true;
        act.listen = true;
        coach::notify(coach::Ev::KeyCycle);  // the hold proves fn+k is known too
    }

    // sustain pedal lifted -> let go of everything not physically held
    static bool prevSustain = false;
    if (prevSustain && !gSustainHeld) flushSustained();
    prevSustain = gSustainHeld;

    // ---- quick-edit auto-repeat ---------------------------------------------
    if (gQuickEdit && gRepeatDir != 0 &&
        (held(cur, kKeyBendDown) || held(cur, kKeyBendUp))) {
        if (nowMs - gRepeatStart >= cfg::kRepeatDelayMs &&
            nowMs - gRepeatLast >= cfg::kRepeatRateMs) {
            gRepeatLast = nowMs;
            adjustQuickParam(gQuickParam, gRepeatDir, gRepeatCoarse);
        }
    } else if (!gQuickEdit) {
        gRepeatDir = 0;
    }

    // ---- volume auto-repeat -------------------------------------------------
    // Hold ctrl/opt to ramp the master volume rather than tapping each step.
    if (gVolRepeatDir != 0 && (held(cur, kKeyCtrl) || held(cur, kKeyOpt))) {
        if (nowMs - gVolRepeatStart >= cfg::kRepeatDelayMs &&
            nowMs - gVolRepeatLast >= cfg::kRepeatRateMs) {
            gVolRepeatLast = nowMs;
            adjustVolume(gVolRepeatDir);
        }
    } else {
        gVolRepeatDir = 0;
    }

    // ---- bend ramp (level-sensitive: sampled every frame) --------------------
    float bendTarget = 0.f;
    if (!gQuickEdit) {
        if (held(cur, kKeyBendUp)) bendTarget += cfgr.bendRange * 100.f;
        if (held(cur, kKeyBendDown)) bendTarget -= cfgr.bendRange * 100.f;
    }
    const float rate = (cfgr.bendRange * 100.f) / (float)cfgr.bendMs;  // cents per ms
    // a released string snaps back faster than it bends up (Gilmour physics)
    const bool releasing = fabsf(bendTarget) < fabsf(gBendCents);
    const float step = rate * dtMs * (releasing ? 2.f : 1.f);
    if (gBendCents < bendTarget) {
        gBendCents += step;
        if (gBendCents > bendTarget) gBendCents = bendTarget;
    } else if (gBendCents > bendTarget) {
        gBendCents -= step;
        if (gBendCents < bendTarget) gBendCents = bendTarget;
    }
    cfgr.synth.bendCents = gBendCents;

    jamTick(nowMs);  // living backing: re-strike latched drones on the beat

    // the solo/backing split only holds while a backing exists — once the jam
    // is cleared, the next sound switch affects everything again
    if (store::backingLocked() && !backingActive()) store::unlockBacking();

    gPrevMask = cur;
    return act;
}

// Known limit: if the synth steals a sustained note's voice at the cap
// (free mode), the key layer isn't told — that key stays lit on the grid
// map until its Off/panic. Display-only; audio is always correct.
bool noteHeld(int string, int col) {
    for (int cd = 0; cd < 56; ++cd) {
        const HeldNote& n = gNotes[cd];
        if (sounding(n) && n.string == string && n.col == col) return true;
    }
    return false;
}

int noteState(int string, int col, uint32_t nowMs) {
    for (int cd = 0; cd < 56; ++cd) {
        const HeldNote& n = gNotes[cd];
        if (!sounding(n) || n.string != string || n.col != col) continue;
        if (!n.drone) return 1;
        const bool flash = nowMs - gBeatFlashAt < kBeatFlashMs &&
                           (gBeatFlashCd < 0 || gBeatFlashCd == cd);
        return flash ? 3 : 2;
    }
    return 0;
}

float quickParamFill(int idx) { return quickFill(idx); }

bool quickEditActive() { return gQuickEdit; }
int quickEditParam() { return gQuickParam; }

const char* quickParamName(int idx) {
    return (idx >= 0 && idx < 10) ? kQuick[idx].name : "?";
}

void quickParamValue(int idx, char* out, int cap) {
    auto& cfgr = store::get();
    auto& s = cfgr.synth;
    switch (idx) {
        case 0: snprintf(out, cap, "%dms", (int)(s.glideS * 1000)); break;
        case 1: snprintf(out, cap, "%dms", (int)(s.attackS * 1000)); break;
        case 2: snprintf(out, cap, "%dms", (int)(s.decayS * 1000)); break;
        case 3: snprintf(out, cap, "%d%%", (int)(s.sustain * 100)); break;
        case 4: snprintf(out, cap, "%dms", (int)(s.releaseS * 1000)); break;
        case 5: snprintf(out, cap, "%s", dsp::waveformName(s.wave)); break;
        case 6:
            if (s.cutoffHz >= 1000.f) snprintf(out, cap, "%.1fk", s.cutoffHz / 1000.f);
            else snprintf(out, cap, "%d", (int)s.cutoffHz);
            break;
        case 7: snprintf(out, cap, "%d", s.voiceCount); break;
        case 8: snprintf(out, cap, "%dst", cfgr.bendRange); break;
        case 9: snprintf(out, cap, "%d%%", (int)(s.masterVol * 100)); break;
        default: out[0] = '\0'; break;
    }
}
float bendCentsNow() { return gBendCents; }
bool sustainActive() { return gSustainHeld; }

// Tap tempo, one state machine for both entry points (the `\` key and the
// settings row) so a series survives crossing between them. A gap longer than
// kTapSeriesMs starts fresh; the running interval is a 50/50 smooth so a
// wobbly hand settles instead of jittering, and the BPM is clamped to the same
// 40..240 the settings row has always used.
uint16_t tapTempo(uint32_t nowMs) {
    constexpr uint32_t kTapSeriesMs = 2000;
    static uint32_t lastTap = 0;
    static float avgInt = 0.f;
    auto& cfgr = store::get();
    if (lastTap != 0 && nowMs - lastTap < kTapSeriesMs) {
        const float interval = (float)(nowMs - lastTap);
        avgInt = avgInt > 0.f ? avgInt * 0.5f + interval * 0.5f : interval;
        int bpm = (int)(60000.f / avgInt + 0.5f);
        if (bpm < 40) bpm = 40;
        if (bpm > 240) bpm = 240;
        cfgr.jamBpm = (uint16_t)bpm;
        store::markDirty();
    } else {
        avgInt = 0.f;  // first tap of a fresh series: read, don't write
    }
    lastTap = nowMs;
    // every tap phase-locks the click, so the metronome lands ON your finger —
    // tap-tap-tap and the beat is already yours
    if (cfgr.metroOn)
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::MetroSync, 1));
    return cfgr.jamBpm;
}
bool tiltLatched() { return gTiltLatched; }
bool triggerHeld() { return gTriggerHeld; }  // raw G0 level (trigger macro)
uint32_t lastActivityMs() { return gLastActivityMs; }

// ---- auto chord progression view state --------------------------------------
bool progActive() {
    return jamMotionNow() == kJamProg && store::get().jamRows > 0;
}
bool arpOn() { return gArpOn; }
void arpLabel(char* out, int cap) {
    static const char* const kTags[dsp::kArpPatternCount] = {"ARP^", "ARPv", "ARP^v"};
    snprintf(out, cap, "%s", kTags[gArpPattern < dsp::kArpPatternCount ? gArpPattern : 0]);
}
int progLen() { return gProgLen; }
int progIndex() { return gProgLen ? gProgIdx : -1; }
bool progAppendStep(int string, int col, bool chrom) { return progAppend(string, col, chrom); }

// Settings moved the Jam octave. The progression's frozen layout keeps the key
// and octave it was built in but takes the new register at once, re-striking
// the sounding chord so the change auditions from inside settings (the jam
// keeps ticking there); latched drones glide to their new pitch. The lead
// voices are untouched — this is the backing's knob, not the grid's.
void backingRegisterChanged() {
    gProgLayout.jamOctave = store::get().layout.jamOctave;
    if (gProgSounding) strikeProgChord(gProgIdx);
    for (int cd = 0; cd < 56; ++cd) {
        HeldNote& n = gNotes[cd];
        if (!n.drone || n.string < 0) continue;
        n.pitch = pitchFor(n);
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Retarget, (uint8_t)cd, 0xFF,
                                              false, n.pitch));
        if (n.droneVoicing > 0)
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Retarget,
                                                  (uint8_t)(cd + kDroneStackOffset), 0xFF, false,
                                                  n.pitch + n.droneVoicing));
    }
}

void progStepName(int i, char* out, int cap) {
    if (cap <= 0) return;
    if (i < 0 || i >= gProgLen) { out[0] = '\0'; return; }
    float pp[kProgVoices];
    dsp::chordPitches(gProgLayout, gProg[i].string, gProg[i].col, gProg[i].chrom, pp, kProgVoices);
    snprintf(out, cap, "%s", dsp::pitchClassName(pp[0]));
}

// The step's Roman numeral (I..vii°), computed against the SAME frozen layout
// the triad voices from, so the label can never disagree with the sound. False
// when there is no diatonic degree to name (chromatic strike / lock off).
bool progRomanNumeral(int i, char* out, int cap) {
    if (i < 0 || i >= gProgLen) return false;
    return dsp::chordRomanNumeral(gProgLayout, gProg[i].string, gProg[i].col,
                                  gProg[i].chrom, out, cap);
}

bool progCurrentCell(int& string, int& col) {
    if (gProgLen == 0 || !gProgSounding) return false;
    string = gProg[gProgIdx].string;
    col = gProg[gProgIdx].col;
    return true;
}

// The chord sounding right now, as pitch classes — the grid map marks the keys
// that belong to it. Gated on the same condition as progCurrentCell (a chord is
// actually ringing), not on progActive(), which only says the mode is selected.
// The chromatic voicing is root/fifth/octave, so it yields two distinct classes,
// not three; dedupe rather than reporting the octave twice.
int progChordPcs(uint8_t* pcs, int cap) {
    if (cap <= 0 || gProgLen == 0 || !gProgSounding) return 0;
    float pp[kProgVoices];
    const int np = dsp::chordPitches(gProgLayout, gProg[gProgIdx].string,
                                     gProg[gProgIdx].col, gProg[gProgIdx].chrom,
                                     pp, kProgVoices);
    int n = 0;
    for (int i = 0; i < np && n < cap; ++i) {
        const int m = (int)(pp[i] + 0.5f);
        const uint8_t pc = (uint8_t)(((m % 12) + 12) % 12);
        bool dup = false;
        for (int k = 0; k < n; ++k)
            if (pcs[k] == pc) { dup = true; break; }
        if (!dup) pcs[n++] = pc;
    }
    return n;
}

}  // namespace keys
