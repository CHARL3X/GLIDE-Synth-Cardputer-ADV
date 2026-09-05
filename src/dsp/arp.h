// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// The arpeggiator: a pure sequencer that breaks the backing chord into one
// note at a time. It is a TEXTURE on the chord the jam row already holds, not
// a second chord engine: the UI hands it the progression's sounding triad and
// it walks root-3rd-5th-octave (1-3-5-8) in a pattern, at a division of the
// jam tempo. Stepped per render block on the audio thread — sixteenths cannot
// live on the 30 fps UI frame — so it is sample-block accurate, and like every
// other backing layer its notes are cap-exempt and steal-proof. Pure C++.
#pragma once
#include <cstdint>
#include "params.h"

namespace dsp {

constexpr uint8_t kArpPatternCount = 3;  // up, down, up/down
constexpr uint8_t kArpMaxSpan = 2;       // octaves the walk covers
constexpr int     kArpMaxSteps = 7;      // 3 chord tones x 2 octaves + the top root
// The arp's voice ids alternate so a release tail overlaps the next attack.
// Clear of lead keys (0..55), drone partners (64..119), the progression's
// block chord (120..122), loop playback (128..183) and the boot chime (250).
constexpr uint8_t kArpIdA = 123;
constexpr uint8_t kArpIdB = 124;
constexpr uint8_t kArpDefaultRate = 3;   // 1/8 in the delaySync vocabulary

inline const char* arpPatternName(uint8_t p) {
    switch (p) {
        case 1:  return "down";
        case 2:  return "up/down";
        default: return "up";
    }
}

// What the UI publishes (double-buffered through the audio engine, like
// SynthParams). `gen` is bumped on every CHORD change so the walk restarts on
// the new chord's downbeat; pattern/span/rate edits leave it alone and take
// effect at the next step. n == 0 (no chord) or on == 0 = silence.
// C++11: default member initializers, so build it field by field.
struct ArpConfig {
    float   pitches[3] = {0.f, 0.f, 0.f};  // the chord, fractional MIDI
    uint8_t n = 0;                          // chord size 0..3
    uint8_t on = 0;
    uint8_t pattern = 0;                    // 0 up, 1 down, 2 up/down
    uint8_t span = 1;                       // octaves, 1..kArpMaxSpan
    uint8_t rate = kArpDefaultRate;         // delaySyncBeats() index (0/bad -> 1/8)
    uint16_t gen = 0;
};

class Arp {
public:
    void set(const ArpConfig& c) { cfg_ = c; }
    // Advance one render block of `n` samples. Writes the block's note events
    // (at most one Off and one On) into `out`, returns how many. bpm is the
    // jam tempo the synth already publishes (SynthParams::tempoBpm).
    int advance(int n, float sampleRate, float bpm, NoteEvent* out, int cap);
    bool running() const { return running_; }

private:
    int  buildSteps(float* steps) const;   // the walk's pitch list, ascending
    float stepPitch(int idx, const float* steps, int count) const;
    int  emitOff(NoteEvent* out, int cap, int k);
    int  emitOn(NoteEvent* out, int cap, int k, float pitch);

    ArpConfig cfg_;
    bool     running_ = false;
    uint16_t gen_ = 0;
    float    count_ = 0.f;      // samples into the current step
    int      idx_ = 0;          // position in the pattern walk
    int16_t  soundId_ = -1;     // the sounding note's id, -1 = none
    bool     useB_ = false;     // which of the two ids the next note takes
};

}  // namespace dsp
