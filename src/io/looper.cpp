#include "looper.h"

#include <Arduino.h>
#include <cstring>

#include "audio_engine.h"

namespace looper {

namespace {

constexpr int kMaxEvents = 1024;        // 1024 * 16 B = 16 KB — minutes of playing
constexpr uint32_t kMinLoopMs = 300;    // shorter than this was a fumble, not a take
constexpr uint32_t kLookaheadMs = 45;   // schedule ahead of the ~33 ms UI frame
constexpr uint8_t kIdOffset = 128;      // playback ids: 128..183 (live: 0..55,
                                        // drone partners: 64..119, chime: 250)
constexpr uint8_t kLaneOffset = 4;      // playback lanes 4..7 — legato hand-offs
                                        // inside the loop can only grab loop voices

struct LoopEvent {
    uint32_t tMs;        // offset from loop start
    dsp::NoteEvent ev;   // as the live hand played it (pre-remap)
};

LoopEvent gEv[kMaxEvents];
int gCount = 0;
State gState = State::Empty;
uint32_t gLenMs = 0;
uint32_t gAnchorMs = 0;  // Recording: when rec began. Playing/Overdub: start of cycle.
int gPlayIdx = 0;        // next event to schedule this cycle
uint64_t gOpenRec = 0;   // live ids (0..55) with a recorded On awaiting its Off
uint64_t gOpenPlay = 0;  // live-id bits whose playback voice is sounding
bool gOverflow = false;

// Keep gEv sorted by tMs. Recording appends in O(1); overdub inserts behind
// the playhead in O(n) (n <= 1024 — fine at UI rate).
void insertEvent(uint32_t t, const dsp::NoteEvent& ev) {
    if (gCount >= kMaxEvents) {
        gOverflow = true;
        return;
    }
    int i = gCount;
    while (i > 0 && gEv[i - 1].tMs > t) --i;
    if (i < gCount) memmove(&gEv[i + 1], &gEv[i], sizeof(LoopEvent) * (gCount - i));
    gEv[i].tMs = t;
    gEv[i].ev = ev;
    ++gCount;
    if (i < gPlayIdx) ++gPlayIdx;  // landed behind the playhead: next cycle's problem
}

void schedulePlayback(const LoopEvent& le, uint32_t dueMs) {
    dsp::NoteEvent ev = le.ev;
    ev.id = (uint8_t)(ev.id + kIdOffset);
    if (ev.lane != 0xFF) ev.lane = (uint8_t)(ev.lane + kLaneOffset);
    ev.backing = true;
    ev.drone = false;
    if (le.ev.type == dsp::NoteEvent::On)
        gOpenPlay |= 1ULL << le.ev.id;
    else if (le.ev.type == dsp::NoteEvent::Off)
        gOpenPlay &= ~(1ULL << le.ev.id);
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

// Notes still held when a take closes get their Off written at `t`, so the
// loop never replays a stuck note.
void closeOpenRecNotes(uint32_t t) {
    for (int id = 0; id < 56; ++id)
        if ((gOpenRec >> id) & 1ULL)
            insertEvent(t, dsp::NoteEvent::make(dsp::NoteEvent::Off, (uint8_t)id));
    gOpenRec = 0;
}

// Position inside the current cycle. Signed: tick() advances the anchor up
// to kLookaheadMs early, so (now - anchor) can be briefly negative.
uint32_t cyclePos(uint32_t nowMs) {
    if (gLenMs == 0) return 0;
    int32_t d = (int32_t)(nowMs - gAnchorMs);
    while (d < 0) d += (int32_t)gLenMs;
    return (uint32_t)d % gLenMs;
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

    const uint32_t now = millis();
    const uint32_t t = (gState == State::Recording) ? now - gAnchorMs : cyclePos(now);
    insertEvent(t, ev);
}

State tap(uint32_t nowMs) {
    switch (gState) {
        case State::Empty:
            gCount = 0;
            gOpenRec = 0;
            gOverflow = false;
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
            closeOpenRecNotes(len - 1);
            gLenMs = len;
            gAnchorMs = nowMs;  // first cycle starts on the pedal press
            gPlayIdx = 0;
            gState = State::Playing;
            break;
        }
        case State::Playing:
            gState = State::Overdub;  // keep playing, start layering
            break;
        case State::Overdub:
            closeOpenRecNotes(cyclePos(nowMs));
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

void clear() {
    silencePlayback();
    gCount = 0;
    gLenMs = 0;
    gOpenRec = 0;
    gOverflow = false;
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
            closeOpenRecNotes(cyclePos(millis()));
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
        gPlayIdx = 0;
        while (gPlayIdx < gCount && gEv[gPlayIdx].tMs < pos) ++gPlayIdx;
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

bool overflowed() { return gOverflow; }

}  // namespace looper
