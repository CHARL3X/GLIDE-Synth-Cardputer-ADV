#include "key_detect.h"

#include <cmath>

#include "scales.h"

namespace dsp {
namespace {

constexpr int kOctaves = 4;
constexpr float kLowA = 110.f;      // A2; top target G#6 ~1661 Hz < Nyquist@16k
constexpr int kCyclesPerTarget = 16;  // first null on the neighbouring semitone
constexpr float kSilenceMeanAbs = 30.f;  // int16 counts, ~0.1% FS

// Krumhansl-Schmuckler tonal-hierarchy profiles (probe-tone ratings).
constexpr float kProfMajor[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
                                  2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
constexpr float kProfMinor[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
                                  2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};

// Mean Goertzel magnitude at freq over the buffer, hopping half a frame.
// Two normalizations keep octaves comparable: magnitude / frame length
// cancels the coherent gain of longer frames, and total / frame count
// cancels the extra hops short frames get (without it, accumulated energy
// scales with frequency and harmonics swamp their fundamentals).
float goertzelEnergy(const int16_t* x, int n, float freq, float sr) {
    const int frame = (int)(kCyclesPerTarget * sr / freq + 0.5f);
    if (frame < 8 || frame > n) return 0.f;
    const float coeff = 2.f * cosf(6.2831853f * freq / sr);
    const float invFrame = 1.f / (float)frame;
    float total = 0.f;
    int frames = 0;
    const int hop = frame / 2;
    for (int start = 0; start + frame <= n; start += hop) {
        float s1 = 0.f, s2 = 0.f;
        const int16_t* p = x + start;
        for (int i = 0; i < frame; ++i) {
            const float s0 = (float)p[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        total += sqrtf(power > 0.f ? power : 0.f) * invFrame;
        ++frames;
    }
    return frames > 0 ? total / (float)frames : 0.f;
}

// Pearson correlation of the chroma against a profile rotated to root.
float keyScore(const float* chroma, const float* prof, int root) {
    float mc = 0.f, mp = 0.f;
    for (int i = 0; i < 12; ++i) {
        mc += chroma[(root + i) % 12];
        mp += prof[i];
    }
    mc /= 12.f;
    mp /= 12.f;
    float num = 0.f, dc = 0.f, dp = 0.f;
    for (int i = 0; i < 12; ++i) {
        const float a = chroma[(root + i) % 12] - mc;
        const float b = prof[i] - mp;
        num += a * b;
        dc += a * a;
        dp += b * b;
    }
    const float den = sqrtf(dc * dp);
    return den > 1e-9f ? num / den : 0.f;
}

}  // namespace

bool segmentAudible(const int16_t* mono, int n) {
    if (!mono || n <= 0) return false;
    float meanAbs = 0.f;
    for (int i = 0; i < n; ++i) meanAbs += fabsf((float)mono[i]);
    return meanAbs / (float)n >= kSilenceMeanAbs;
}

void accumulateChroma(const int16_t* mono, int n, float sampleRate,
                      float chroma[12]) {
    if (!mono || n < (int)sampleRate / 4 || sampleRate <= 0.f) return;

    // Per-octave magnitudes. Targets start at A2: pitch class (9 + step) % 12.
    float band[kOctaves][12];
    for (int oct = 0; oct < kOctaves; ++oct) {
        for (int step = 0; step < 12; ++step) {
            const float freq =
                kLowA * powf(2.f, (float)oct + (float)step / 12.f);
            band[oct][(9 + step) % 12] = goertzelEnergy(mono, n, freq, sampleRate);
        }
    }

    // Harmonic subtraction, bottom-up. A note's 2nd harmonic lands one band
    // up at the same pitch class; its 3rd lands one band up a fifth higher
    // (bin pc has a 3rd-harmonic parent at pc+5). Without this, the dominant
    // routinely out-scores the tonic and every key reads a fifth sharp.
    constexpr float kH2 = 0.45f, kH3 = 0.25f;
    for (int oct = 1; oct < kOctaves; ++oct) {
        for (int pc = 0; pc < 12; ++pc) {
            const float v = band[oct][pc] - kH2 * band[oct - 1][pc] -
                            kH3 * band[oct - 1][(pc + 5) % 12];
            band[oct][pc] = v > 0.f ? v : 0.f;
        }
    }

    // Fold into 12 bins, bass octaves weighted up (the bass carries the key).
    constexpr float kOctWeight[kOctaves] = {1.f, 0.9f, 0.7f, 0.5f};
    for (int oct = 0; oct < kOctaves; ++oct)
        for (int pc = 0; pc < 12; ++pc)
            chroma[pc] += kOctWeight[oct] * band[oct][pc];
}

KeyGuess classifyChroma(const float chromaIn[12]) {
    KeyGuess g = KeyGuess::make();
    float chroma[12];
    for (int i = 0; i < 12; ++i) chroma[i] = chromaIn[i];

    float peak = 0.f, mean = 0.f;
    for (int i = 0; i < 12; ++i) {
        if (chroma[i] > peak) peak = chroma[i];
        mean += chroma[i];
    }
    mean /= 12.f;
    if (peak <= 1e-9f) return g;
    for (int i = 0; i < 12; ++i) g.chroma[i] = chroma[i] / peak;

    // Flatness gate: tonal music concentrates chroma energy (mean/peak well
    // under 0.6); broadband noise spreads it flat. A flat profile has no key
    // to report, whatever the correlation margin says.
    if (mean / peak > 0.7f) return g;

    // Krumhansl-Schmuckler: best of 24 rotated profiles wins.
    float best = -2.f, second = -2.f;
    for (int root = 0; root < 12; ++root) {
        for (int m = 0; m < 2; ++m) {
            const float r = keyScore(chroma, m ? kProfMinor : kProfMajor, root);
            if (r > best) {
                second = best;
                best = r;
                g.rootPc = root;
                g.minor = m != 0;
            } else if (r > second) {
                second = r;
            }
        }
    }

    g.valid = true;
    const float margin = (best - second) * 5.f;
    g.confidence = margin < 0.f ? 0.f : (margin > 1.f ? 1.f : margin);
    return g;
}

KeyGuess detectKey(const int16_t* mono, int n, float sampleRate) {
    // Silence gate first: don't hallucinate a key out of the noise floor.
    if (!mono || n < (int)sampleRate / 4 || sampleRate <= 0.f ||
        !segmentAudible(mono, n))
        return KeyGuess::make();
    float chroma[12] = {0.f};
    accumulateChroma(mono, n, sampleRate, chroma);
    return classifyChroma(chroma);
}

bool scaleIsMinorish(int scaleIdx) {
    if (scaleIdx < 0 || scaleIdx >= kScaleCount) return false;
    const Scale& parent = kScales[kScales[scaleIdx].harm];
    for (int i = 0; i < parent.len; ++i)
        if (parent.steps[i] == 3) return true;
    return false;
}

int applyRootForScale(int detectedPc, bool detectedMinor, int scaleIdx) {
    const bool minorish = scaleIsMinorish(scaleIdx);
    if (!detectedMinor && minorish) return (detectedPc + 9) % 12;
    if (detectedMinor && !minorish) return (detectedPc + 3) % 12;
    return detectedPc;
}

}  // namespace dsp
