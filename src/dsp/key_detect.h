// LISTEN: musical key detection from a mono capture. Pure C++.
//
// A constant-Q Goertzel chromagram (12 pitch classes x 4 octaves, 16 cycles
// per target so a +-30-cent detuned song still lands in its bin while the
// neighbouring semitone sits on the first null) is correlated against the
// Krumhansl-Schmuckler key profiles. The winner's tonic emphasis is what
// separates a key from its relative major/minor twin.
#pragma once
#include <cstdint>

namespace dsp {

struct KeyGuess {
    bool valid;         // false = silence / no tonal content found
    int rootPc;         // 0..11, C = 0 (kNoteNames index)
    bool minor;         // detected mode
    float confidence;   // 0..1, margin of the best key over the runner-up
    float chroma[12];   // per-pitch-class energy, normalized (peak = 1)

    static KeyGuess make() {
        KeyGuess g;
        g.valid = false;
        g.rootPc = 0;
        g.minor = false;
        g.confidence = 0.f;
        for (int i = 0; i < 12; ++i) g.chroma[i] = 0.f;
        return g;
    }
};

// Detect the key of n mono int16 samples at sampleRate. Deterministic; no
// allocation. n shorter than ~1.5 s reduces accuracy but stays valid.
KeyGuess detectKey(const int16_t* mono, int n, float sampleRate);

// Whether a scale is minor-flavoured: its HARMONY PARENT (kScales[i].harm,
// the diatonic scale the backing builds triads from) contains a minor third.
bool scaleIsMinorish(int scaleIdx);

// Root to actually apply so the player's current scale lands on the detected
// key's notes: a major song under a minorish scale takes the relative minor
// root (+9), a minor song under a majorish scale the relative major (+3).
int applyRootForScale(int detectedPc, bool detectedMinor, int scaleIdx);

}  // namespace dsp
