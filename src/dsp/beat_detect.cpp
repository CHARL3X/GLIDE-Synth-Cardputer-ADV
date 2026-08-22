// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "beat_detect.h"

#include <cmath>

namespace dsp {
namespace {

// The jamBpm window, in envelope lags: 240 BPM = 25 samples, 40 BPM = 150.
constexpr float kMinBpm = 40.f;
constexpr float kMaxBpm = 240.f;
// The chosen lag must show at least this much raw periodicity to count as a
// beat at all; confidence spans from here up.
constexpr float kMinPeriodicity = 0.30f;
constexpr float kConfidenceSpan = 0.40f;
// Log-normal tempo prior: centred where players actually jam, wide enough
// (0.6 octaves sigma) that it only breaks ties between a beat and its own
// double/half — a genuinely slow or fast song still wins on periodicity.
constexpr float kPriorCentreBpm = 110.f;
constexpr float kPriorSigmaOct = 0.6f;
// Absolute onset-strength gate, in log-energy nats: a real musical attack
// lifts frame energy by tens of percent (0.25 nats ~ +28%). Without this, a
// steady tone's microscopic amplitude ripple against the frame grid — which
// IS periodic — sails through the scale-invariant autocorrelation and names
// a confident beat out of nothing.
constexpr float kMinOnsetFlux = 0.25f;

// Unbiased normalized autocorrelation of the mean-removed envelope at lag.
float autocorrAt(const float* e, int n, float mean, float r0, int lag) {
    if (lag <= 0 || lag >= n || r0 <= 1e-12f) return 0.f;
    float r = 0.f;
    for (int i = 0; i + lag < n; ++i) r += (e[i] - mean) * (e[i + lag] - mean);
    // Rescale for the shrinking overlap so long lags compete fairly.
    return (r / r0) * ((float)n / (float)(n - lag));
}

}  // namespace

void accumulateOnsets(BeatState& st, const int16_t* mono, int n, float sampleRate) {
    if (!mono || n <= 0 || sampleRate <= 0.f) return;
    const int frame = (int)(sampleRate / (float)BeatState::kEnvRate + 0.5f);
    if (frame < 8) return;
    for (int start = 0; start + frame <= n && st.len < BeatState::kMaxEnvLen;
         start += frame) {
        float e = 0.f;
        const int16_t* p = mono + start;
        for (int i = 0; i < frame; ++i) e += fabsf((float)p[i]);
        // Log compression: a chorus and a phone across the room get the same
        // vote per onset, matching the chroma path's one-round-one-vote rule.
        e = logf(1.f + e / (float)frame);
        const float flux = st.prevE < 0.f ? 0.f : e - st.prevE;
        st.prevE = e;
        st.env[st.len++] = flux > 0.f ? flux : 0.f;
    }
}

TempoGuess estimateTempo(const BeatState& st) {
    TempoGuess t = TempoGuess::make();
    const int n = st.len;
    const int minLag = (int)(60.f * BeatState::kEnvRate / kMaxBpm + 0.5f);
    int maxLag = (int)(60.f * BeatState::kEnvRate / kMinBpm + 0.5f);
    if (maxLag > n / 2) maxLag = n / 2;  // need two full periods in evidence
    if (n < 4 * minLag || maxLag <= minLag) return t;

    float mean = 0.f, peakFlux = 0.f;
    for (int i = 0; i < n; ++i) {
        mean += st.env[i];
        if (st.env[i] > peakFlux) peakFlux = st.env[i];
    }
    mean /= (float)n;
    if (peakFlux < kMinOnsetFlux) return t;  // nothing ever ATTACKED: no beat
    float r0 = 0.f;
    for (int i = 0; i < n; ++i) {
        const float d = st.env[i] - mean;
        r0 += d * d;
    }
    if (r0 <= 1e-12f) return t;  // dead-flat envelope: nothing pulsed

    int bestLag = 0;
    float bestScore = 0.f, bestR = 0.f;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const float r = autocorrAt(st.env, n, mean, r0, lag);
        const float bpm = 60.f * (float)BeatState::kEnvRate / (float)lag;
        const float x = log2f(bpm / kPriorCentreBpm) / kPriorSigmaOct;
        const float prior = expf(-0.5f * x * x);
        const float score = r * prior;
        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
            bestR = r;
        }
    }
    if (bestLag == 0 || bestR < kMinPeriodicity) return t;

    // Parabolic refinement around the winning lag: the true period rarely
    // sits exactly on a 10 ms grid point.
    const float rl = autocorrAt(st.env, n, mean, r0, bestLag - 1);
    const float rr = autocorrAt(st.env, n, mean, r0, bestLag + 1);
    const float denom = rl - 2.f * bestR + rr;
    float delta = 0.f;
    if (denom < -1e-9f) {
        delta = 0.5f * (rl - rr) / denom;
        if (delta > 0.5f) delta = 0.5f;
        if (delta < -0.5f) delta = -0.5f;
    }
    float bpm = 60.f * (float)BeatState::kEnvRate / ((float)bestLag + delta);
    if (bpm < kMinBpm) bpm = kMinBpm;
    if (bpm > kMaxBpm) bpm = kMaxBpm;

    t.valid = true;
    t.bpm = bpm;
    float conf = (bestR - kMinPeriodicity) / kConfidenceSpan;
    t.confidence = conf < 0.f ? 0.f : (conf > 1.f ? 1.f : conf);
    return t;
}

}  // namespace dsp
