#include "looper.h"

#include <Arduino.h>
#include <cstring>

#include "audio_engine.h"

namespace looper {

namespace {

constexpr int kMaxEvents = 1024;        // 1024 * ~20 B = 20 KB — minutes of playing
constexpr uint32_t kMinLoopMs = 300;    // shorter than this was a fumble, not a take
constexpr uint32_t kLookaheadMs = 45;   // schedule ahead of the ~33 ms UI frame
constexpr uint8_t kIdOffset = 128;      // playback ids: 128..183 (live: 0..55,
                                        // drone partners: 64..119, chime: 250)
constexpr uint8_t kLaneOffset = 4;      // playback lanes 4..7 — legato hand-offs
                                        // inside the loop can only grab loop voices

struct LoopEvent {
    uint32_t tMs;        // offset from loop start
    dsp::NoteEvent ev;   // as the live hand played it (pre-remap)
    uint8_t layer;       // 0 = base take, 1.. = overdub passes
};

LoopEvent gEv[kMaxEvents];
int gCount = 0;
State gState = State::Empty;
uint32_t gLenMs = 0;
uint32_t gAnchorMs = 0;  // Recording: when rec began. Playing/Overdub: start of cycle.
int gPlayIdx = 0;        // next event to schedule this cycle
uint64_t gOpenRec = 0;   // live ids (0..55) with a recorded On awaiting its Off
uint64_t gOpenPlay = 0;  // live-id bits whose playback voice is sounding
uint8_t gPlayLayer[56];  // layer of each currently-sounding playback voice
bool gOverflow = false;

// Layer bookkeeping. The base take is layer 0 and is permanent until clear();
// overdubs are 1..gTopLayer. gLiveLayer is the highest layer currently
// audible — peel/restore walk it between 0 and gTopLayer.
int gTopLayer = 0;       // highest layer recorded
int gLiveLayer = 0;      // highest layer audible (0 = base only)
int gDubLayer = 0;       // layer being recorded during overdub
bool gDubHasEvents = false;
int gPeelDir = -1;       // -1 = next hold peels (undo), +1 = next hold restores

void insertEvent(uint32_t t, const dsp::NoteEvent& ev, uint8_t layer) {
    if (gCount >= kMaxEvents) {
        gOverflow = true;
        return;
    }
    int i = gCount;
    while (i > 0 && gEv[i - 1].tMs > t) --i;
    if (i < gCount) memmove(&gEv[i + 1], &gEv[i], sizeof(LoopEvent) * (gCount - i));
    gEv[i].tMs = t;
    gEv[i].ev = ev;
    gEv[i].layer = layer;
    ++gCount;
    if (i < gPlayIdx) ++gPlayIdx;  // landed behind the playhead: next cycle's problem
}

void schedulePlayback(const LoopEvent& le, uint32_t dueMs) {
    dsp::NoteEvent ev = le.ev;
    ev.id = (uint8_t)(ev.id + kIdOffset);
    if (ev.lane != 0xFF) ev.lane = (uint8_t)(ev.lane + kLaneOffset);
    ev.backing = true;
    ev.drone = false;
    if (le.ev.type == dsp::NoteEvent::On) {
        gOpenPlay |= 1ULL << le.ev.id;
        gPlayLayer[le.ev.id] = le.layer;
    } else if (le.ev.type == dsp::NoteEvent::Off) {
        gOpenPlay &= ~(1ULL << le.ev.id);
    }
    audio::pushEventAt(ev, dueMs);
}

// Stop sound coming from the loop layer: invalidate everything still queued,
// release everything already sounding.
void silencePlayback() {
    audio::flushScheduled();
    for (int id = 0; id < 56; ++id)
        if ((gOpenPlay >> id) & 1ULL)
            audio::pushEvent(
                dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)(id + kIdOffset)));
    gOpenPlay = 0;
}

// Release only the sounding playback notes whose layer is now peeled away —
// the surviving layers keep ringing without a gap. (A handful of peeled-layer
// events already inside the ~45 ms schedule window may still blip; their Offs
// are scheduled too, so nothing sticks.)
void killAbove(int layer) {
    for (int id = 0; id < 56; ++id)
        if (((gOpenPlay >> id) & 1ULL) && gPlayLayer[id] > layer) {
            audio::pushEvent(
                dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)(id + kIdOffset)));
            gOpenPlay &= ~(1ULL << id);
        }
}

// Drop every event above `layer` (discards the redo stack when fresh material
// is recorded over peeled layers).
void truncateAbove(int layer) {
    int w = 0;
    for (int r = 0; r < gCount; ++r)
        if (gEv[r].layer <= layer) gEv[w++] = gEv[r];
    gCount = w;
    gTopLayer = layer;
}

// Notes still held when a take/pass closes get their Off written at `t`, so
// the loop never replays a stuck note.
void closeOpenRecNotes(uint32_t t, uint8_t layer) {
    for (int id = 0; id < 56; ++id)
        if ((gOpenRec >> id) & 1ULL)
            insertEvent(t, dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)id), layer);
    gOpenRec = 0;
}

// Position inside the current cycle. Signed: tick() advances the anchor up to
// kLookaheadMs early, so (now - anchor) can be briefly negative.
uint32_t cyclePos(uint32_t nowMs) {
    if (gLenMs == 0) return 0;
    int32_t d = (int32_t)(nowMs - gAnchorMs);
    while (d < 0) d += (int32_t)gLenMs;
    return (uint32_t)d % gLenMs;
}

void seekTo(uint32_t pos) {
    gPlayIdx = 0;
    while (gPlayIdx < gCount && gEv[gPlayIdx].tMs < pos) ++gPlayIdx;
}

}  // namespace

void record(const dsp::NoteEvent& ev) {
    if (gState != State::Recording && gState != State::Overdub) return;
    if (ev.drone || ev.backing) return;  // only the live solo hand is loop content
    if (ev.id >= 56) return;  // grid keys only (drone partners ride at 64+,
                              // the boot chime at 250 — and the open-note
                              // bitmasks are sized for 56)

    switch (ev.type) {
        case dsp::NoteEvent::On:
            gOpenRec |= 1ULL << ev.id;
            break;
        case dsp::NoteEvent::Off:
            // an Off for a note this take never opened (e.g. a drone's
            // release, or a note from before recording began) is not content
            if (!((gOpenRec >> ev.id) & 1ULL)) return;
            gOpenRec &= ~(1ULL << ev.id);
            break;
        case dsp::NoteEvent::Retarget:  // octave sweeps of recorded notes
            if (!((gOpenRec >> ev.id) & 1ULL)) return;
            break;
        default:
            return;  // AllOff/LeadsOff are live state, not performance
    }

    uint8_t layer = 0;
    if (gState == State::Overdub) {
        layer = (uint8_t)gDubLayer;
        if (!gDubHasEvents) {  // first material in this pass: commit the layer
            gDubHasEvents = true;
            gLiveLayer = gDubLayer;
            gTopLayer = gDubLayer;
        }
    }

    const uint32_t now = millis();
    const uint32_t t = (gState == State::Recording) ? now - gAnchorMs : cyclePos(now);
    insertEvent(t, ev, layer);
}

State tap(uint32_t nowMs) {
    switch (gState) {
        case State::Empty:
            gCount = 0;
            gOpenRec = 0;
            gOverflow = false;
            gTopLayer = gLiveLayer = 0;
            gAnchorMs = nowMs;
            gState = State::Recording;
            break;
        case State::Recording: {
            const uint32_t len = nowMs - gAnchorMs;
            if (len < kMinLoopMs || gCount == 0) {  // a fumble, not a take
                gCount = 0;
                gOpenRec = 0;
                gState = State::Empty;
                break;
            }
            closeOpenRecNotes(len - 1, 0);
            gLenMs = len;
            gAnchorMs = nowMs;  // first cycle starts on the pedal press
            gPlayIdx = 0;
            gTopLayer = gLiveLayer = 0;
            gPeelDir = -1;
            gState = State::Playing;
            break;
        }
        case State::Playing:
            // start a fresh overdub pass on top of what's currently audible;
            // recording over peeled layers discards the redo stack
            truncateAbove(gLiveLayer);
            seekTo(cyclePos(nowMs));
            gDubLayer = gLiveLayer + 1;
            gDubHasEvents = false;
            gState = State::Overdub;
            break;
        case State::Overdub:
            closeOpenRecNotes(cyclePos(nowMs), (uint8_t)gDubLayer);
            gPeelDir = -1;  // a fresh layer is now the top of the stack
            gState = State::Playing;
            break;
        case State::Stopped:
            gAnchorMs = nowMs;
            gPlayIdx = 0;
            gState = State::Playing;
            break;
    }
    return gState;
}

int peel(uint32_t nowMs) {
    if (gState != State::Playing && gState != State::Overdub) return 0;
    if (gTopLayer == 0) return 0;  // base only — nothing to peel

    int next = gLiveLayer + gPeelDir;
    if (next < 0) { gPeelDir = 1; next = gLiveLayer + 1; }        // bounce off the base
    else if (next > gTopLayer) { gPeelDir = -1; next = gLiveLayer - 1; }  // bounce off the top
    if (next < 0) next = 0;
    if (next > gTopLayer) next = gTopLayer;
    if (next == gLiveLayer) return 0;

    const bool undid = next < gLiveLayer;
    gLiveLayer = next;
    if (undid)
        killAbove(gLiveLayer);  // surviving layers keep ringing; only peeled notes stop
    // redo: the restored layer simply re-voices when its events next come due
    return undid ? 1 : 2;
}

void clear() {
    silencePlayback();
    gCount = 0;
    gLenMs = 0;
    gOpenRec = 0;
    gOverflow = false;
    gTopLayer = gLiveLayer = gDubLayer = 0;
    gPeelDir = -1;
    gState = State::Empty;
}

void stop() {
    switch (gState) {
        case State::Recording:  // panic mid-take: discard it
            gCount = 0;
            gOpenRec = 0;
            gState = State::Empty;
            break;
        case State::Overdub:
            closeOpenRecNotes(cyclePos(millis()), (uint8_t)gDubLayer);
            // fall through to silence
        case State::Playing:
            silencePlayback();
            gState = State::Stopped;  // the take survives; alt tap replays it
            break;
        default:
            break;
    }
}

void tick(uint32_t nowMs) {
    if (gState != State::Playing && gState != State::Overdub) return;
    if (gLenMs == 0) return;

    // A long UI stall (blocking settings action) would otherwise make the
    // loop burst-replay every cycle it missed. Jump to the current cycle,
    // keeping phase, and resume from the playhead position.
    if (nowMs - gAnchorMs >= gLenMs * 2) {
        const uint32_t pos = (nowMs - gAnchorMs) % gLenMs;
        gAnchorMs = nowMs - pos;
        seekTo(pos);
    }

    for (;;) {
        if (gPlayIdx >= gCount) {
            const uint32_t cycleEnd = gAnchorMs + gLenMs;
            if ((int32_t)(nowMs + kLookaheadMs - cycleEnd) >= 0) {
                gAnchorMs = cycleEnd;  // exact: cycles never drift
                gPlayIdx = 0;
                continue;
            }
            break;
        }
        const uint32_t due = gAnchorMs + gEv[gPlayIdx].tMs;
        if ((int32_t)(nowMs + kLookaheadMs - due) >= 0) {
            if (gEv[gPlayIdx].layer <= gLiveLayer)  // peeled layers stay silent
                schedulePlayback(gEv[gPlayIdx], due);
            ++gPlayIdx;
            continue;
        }
        break;
    }
}

State state() { return gState; }
uint32_t lengthMs() { return gLenMs; }

uint32_t positionMs(uint32_t nowMs) {
    if (gState == State::Recording) return nowMs - gAnchorMs;
    if (gState == State::Playing || gState == State::Overdub) return cyclePos(nowMs);
    return 0;
}

int liveLayers() { return gLiveLayer; }
int topLayers() { return gTopLayer; }
bool overflowed() { return gOverflow; }

}  // namespace looper
