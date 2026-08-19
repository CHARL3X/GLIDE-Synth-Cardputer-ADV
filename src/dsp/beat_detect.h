// LISTEN: tempo estimation from the same mic capture that feeds the
// chromagram. Pure C++.
//
// A 100 Hz onset envelope (half-wave-rectified log-energy flux over 10 ms
// frames) accumulates across listening rounds, exactly like the chroma
// evidence. Autocorrelation over the 40..240 BPM lag window, shaped by a
// gentle log-normal prior centred near 110 BPM (so the beat's own octave
// multiples can't win), names the tempo. Ambient or beatless music scores
// low periodicity and reports invalid — the jam tempo must never move on
// weak evidence, the same honesty rule as NO SIGNAL.
#pragma once
#include <cstdint>

namespace dsp {

struct BeatState {
    static constexpr int kEnvRate = 100;              // envelope samples/second
    static constexpr int kMaxEnvLen = kEnvRate * 10;  // the 9 s budget + slack
    float env[kMaxEnvLen];
    int len;
    float prevE;  // previous frame's log-energy; <0 = none yet (first frame
                  // contributes no flux, so a round boundary never spikes)

    static BeatState make() {
        BeatState s;
        s.len = 0;
        s.prevE = -1.f;
        for (int i = 0; i < kMaxEnvLen; ++i) s.env[i] = 0.f;
        return s;
    }
};

// Append one captured segment's onset envelope. Deterministic; no allocation.
// Rounds are near-contiguous in time, so prevE carries across calls.
void accumulateOnsets(BeatState& st, const int16_t* mono, int n, float sampleRate);

struct TempoGuess {
    bool valid;        // false = too little envelope, or no periodicity
    float bpm;         // 40..240
    float confidence;  // 0..1, from the chosen lag's raw autocorrelation

    static TempoGuess make() {
        TempoGuess t;
        t.valid = false;
        t.bpm = 0.f;
        t.confidence = 0.f;
        return t;
    }
};

// Estimate the tempo of everything accumulated so far.
TempoGuess estimateTempo(const BeatState& st);

}  // namespace dsp
