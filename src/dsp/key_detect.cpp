#include "key_detect.h"

#include <cmath>

#include "scales.h"

namespace dsp {
namespace {

constexpr int kOctaves = 4;
constexpr float kLowA = 110.f;      // A2; top target G#6 ~1661 Hz < Nyquist@16k
constexpr int kCyclesPerTarget = 16;  // first null on the neighbouring semitone
// Audibility floor: int16 counts, ~0.03% FS. Deliberately LOW — this gate only
// refuses a genuinely silent room (the tiny-noise regression test sits at mean
// ~4). Quiet-but-tonal music must pass; deciding whether sound has a KEY is
// the flatness gate's job in classifyChroma, not a loudness test. (Was 30,
// which made LISTEN demand a loud room and discard honest quiet rounds.)
constexpr float kSilenceMeanAbs = 10.f;

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

    // Broadband-floor subtraction, per octave, BEFORE harmonic subtraction:
    // percussion and noise raise all 12 bins of an octave roughly together, a
    // pedestal that dilutes the tonal peaks and pushes the flatness gate
    // toward "no key" — the reason drum-heavy passages used to need an
    // "especially melodic part". The lower-quartile bin tracks the pedestal
    // (tonal content occupies well under 9 of 12 bins); subtract it, clamp at
    // 0. Pearson correlation is offset-invariant, so on clean signal (floor
    // ~0) this is a no-op — it only acts through the clamp, on noisy rounds.
    // Track how much (fold-weighted) energy the subtraction removes: that
    // ratio is the noise detector below.
    constexpr float kOctWeight[kOctaves] = {1.f, 0.9f, 0.7f, 0.5f};
    float rawSum = 0.f, cleanSum = 0.f;
    for (int oct = 0; oct < kOctaves; ++oct) {
        float s[12];
        for (int pc = 0; pc < 12; ++pc) s[pc] = band[oct][pc];
        for (int i = 1; i < 12; ++i) {  // insertion sort, 12 values
            const float v = s[i];
            int j = i - 1;
            while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; --j; }
            s[j + 1] = v;
        }
        const float floorV = s[3];  // lower quartile
        for (int pc = 0; pc < 12; ++pc) {
            const float raw = band[oct][pc];
            const float v = raw - floorV;
            band[oct][pc] = v > 0.f ? v : 0.f;
            rawSum += kOctWeight[oct] * raw;
            cleanSum += kOctWeight[oct] * band[oct][pc];
        }
    }

    // Pedestal-dominated capture: pure broadband noise loses most of its
    // energy to the floor subtraction (every bin sits near the quartile),
    // while real music — even heavily contaminated — keeps most of its
    // (peak-carried) energy. What noise leaves behind is its systematic
    // constant-Q tilt, which correlates with SOME key profile and would
    // otherwise classify confidently. No tonal survivors -> no evidence:
    // contribute nothing and let the caller's silence/flatness handling
    // report "no key" honestly.
    if (cleanSum < 0.30f * rawSum) return;

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

    // Fold into 12 bins, bass octaves weighted up (the bass carries the key;
    // kOctWeight is declared with the floor subtraction above).
    for (int oct = 0; oct < kOctaves; ++oct)
        for (int pc = 0; pc < 12; ++pc)
            chroma[pc] += kOctWeight[oct] * band[oct][pc];
}

void accumulateChromaNormalized(const int16_t* mono, int n, float sampleRate,
                                float chroma[12]) {
    float round[12] = {0.f};
    accumulateChroma(mono, n, sampleRate, round);
    float sum = 0.f;
    for (int i = 0; i < 12; ++i) sum += round[i];
    if (sum <= 1e-9f) return;  // nothing tonal heard: contributes no vote
    const float inv = 1.f / sum;
    for (int i = 0; i < 12; ++i) chroma[i] += round[i] * inv;
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

KeyGuess classifyChromaForScale(const float chromaIn[12], int scaleIdx) {
    KeyGuess g = classifyChroma(chromaIn);  // gates + winner + normalized chroma
    if (!g.valid) return g;
    // Re-derive the margin, excluding every rival whose applied root equals
    // the winner's — those rivals (typically the relative twin) lead to the
    // exact same retune, so their closeness is not uncertainty. keyScore is
    // scale-invariant, so the raw accumulator values are fine here.
    const int appliedWin = applyRootForScale(g.rootPc, g.minor, scaleIdx);
    float best = -2.f, bestRival = -2.f;
    for (int root = 0; root < 12; ++root) {
        for (int m = 0; m < 2; ++m) {
            const float r = keyScore(chromaIn, m ? kProfMinor : kProfMajor, root);
            if (root == g.rootPc && (m != 0) == g.minor) {
                best = r;
                continue;
            }
            if (applyRootForScale(root, m != 0, scaleIdx) == appliedWin) continue;
            if (r > bestRival) bestRival = r;
        }
    }
    const float margin = (best - bestRival) * 5.f;
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

int applyScaleForKey(int scaleIdx, bool detectedMinor) {
    switch (scaleIdx) {
        case SC_MAJOR:
        case SC_MINOR:    return detectedMinor ? SC_MINOR : SC_MAJOR;
        case SC_MAJ_PENT:
        case SC_MIN_PENT: return detectedMinor ? SC_MIN_PENT : SC_MAJ_PENT;
        default:          return scaleIdx;  // exotic (or out of range): keep it
    }
}

int applyScaleForKeyChroma(int scaleIdx, bool detectedMinor,
                           const float chroma[12], int detectedPc) {
    // Only the plain seven-note canvases refine by ear; pentatonics and
    // deliberate flavors keep the frozen behavior exactly.
    if (scaleIdx != SC_MAJOR && scaleIdx != SC_MINOR &&
        scaleIdx != SC_DORIAN && scaleIdx != SC_MIXO)
        return applyScaleForKey(scaleIdx, detectedMinor);
    if (detectedPc < 0 || detectedPc >= 12)
        return applyScaleForKey(scaleIdx, detectedMinor);

    float peak = 0.f;
    for (int i = 0; i < 12; ++i)
        if (chroma[i] > peak) peak = chroma[i];
    if (peak <= 1e-9f) return applyScaleForKey(scaleIdx, detectedMinor);

    // The winning degree must actually be present in the song AND clearly
    // out-power its rival — a mode switch on a coin flip is worse than none.
    constexpr float kRatio = 1.8f;
    const float presence = 0.20f * peak;

    // Evidence-free fallback: stay in the player's mode if it's already on
    // the detected side, else that side's plain default.
    if (detectedMinor) {
        const int fallback = (scaleIdx == SC_DORIAN) ? SC_DORIAN : SC_MINOR;
        const float nat6 = chroma[(detectedPc + 9) % 12];
        const float fl6 = chroma[(detectedPc + 8) % 12];
        if (nat6 >= presence && nat6 >= kRatio * fl6) return SC_DORIAN;
        if (fl6 >= presence && fl6 >= kRatio * nat6) return SC_MINOR;
        return fallback;
    }
    const int fallback = (scaleIdx == SC_MIXO) ? SC_MIXO : SC_MAJOR;
    const float maj7 = chroma[(detectedPc + 11) % 12];
    const float fl7 = chroma[(detectedPc + 10) % 12];
    if (fl7 >= presence && fl7 >= kRatio * maj7) return SC_MIXO;
    if (maj7 >= presence && maj7 >= kRatio * fl7) return SC_MAJOR;
    return fallback;
}

const char* listenModeName(uint8_t mode) {
    switch (mode) {
        case LM_ION:  return "MAJ";
        case LM_DOR:  return "DOR";
        case LM_MIXO: return "MIX";
        default:      return "MIN";
    }
}

namespace {

// The mode-evidence gates, shared with applyScaleForKeyChroma's judgment:
// the deciding degree must be PRESENT (vs the chroma peak) and clearly
// out-power its rival, or the evidence is treated as absent.
constexpr float kModeRatio = 1.8f;
constexpr float kModePresence = 0.20f;
// Tonic tiebreak: how close (raw Pearson) the Dorian twin must score to the
// mixo-flavoured major winner for the tonic to move. Sized from measured
// corridors: the Am7-D9 vamp's twin gap is ~0.08 (must fire), a tonic-clear
// G7 groove's is ~0.50 (must not) — and the tiebreak's failure asymmetry
// favors eagerness, since a wrong re-seat stays inside the SAME pitch set
// (off-centre at worst), while a missed one strands a flavor-scale player
// a whole step off the vamp's home.
constexpr float kTiebreakEps = 0.12f;

// Degree evidence at a tonic: LM_DOR/LM_MIXO when the deciding degree clears
// the gates, else the plain side (LM_AEO/LM_ION); hasEvidence says whether
// the gates were cleared at all (in EITHER direction).
uint8_t modeFromChroma(bool minor, const float chroma[12], int tonicPc,
                       bool& hasEvidence) {
    float peak = 0.f;
    for (int i = 0; i < 12; ++i)
        if (chroma[i] > peak) peak = chroma[i];
    hasEvidence = false;
    if (peak <= 1e-9f) return minor ? LM_AEO : LM_ION;
    const float presence = kModePresence * peak;
    if (minor) {
        const float nat6 = chroma[(tonicPc + 9) % 12];
        const float fl6 = chroma[(tonicPc + 8) % 12];
        if (nat6 >= presence && nat6 >= kModeRatio * fl6) {
            hasEvidence = true;
            return LM_DOR;
        }
        if (fl6 >= presence && fl6 >= kModeRatio * nat6) hasEvidence = true;
        return LM_AEO;
    }
    const float maj7 = chroma[(tonicPc + 11) % 12];
    const float fl7 = chroma[(tonicPc + 10) % 12];
    if (fl7 >= presence && fl7 >= kModeRatio * maj7) {
        hasEvidence = true;
        return LM_MIXO;
    }
    if (maj7 >= presence && maj7 >= kModeRatio * fl7) hasEvidence = true;
    return LM_ION;
}

inline bool modeMinorish(uint8_t mode) { return mode == LM_AEO || mode == LM_DOR; }

}  // namespace

ListenApply applyListen(int scaleIdx, const KeyGuess& g) {
    ListenApply out;
    out.mode = g.minor ? (uint8_t)LM_AEO : (uint8_t)LM_ION;
    out.tonicPc = g.rootPc;
    out.modal = false;
    out.tiebreak = false;

    bool hasEvidence = false;
    const uint8_t mode = modeFromChroma(g.minor, g.chroma, g.rootPc, hasEvidence);

    // No readable evidence: EXACTLY the frozen behavior, whatever the scale.
    if (!hasEvidence) {
        out.scaleIdx = applyScaleForKey(scaleIdx, g.minor);
        out.rootPc = applyRootForScale(g.rootPc, g.minor, out.scaleIdx);
        return out;
    }
    out.mode = mode;
    out.modal = (mode == LM_DOR || mode == LM_MIXO);

    // Tonic tiebreak: "X major with a strong b7" shares its pitch set with
    // Dorian at X+7 (D mixo == A dorian == G major's notes). If the profile
    // score of that Dorian twin runs neck and neck with the winner, the vamp's
    // true home is the twin — re-seat the tonic there.
    if (mode == LM_MIXO) {
        const float win = keyScore(g.chroma, kProfMajor, g.rootPc);
        const float twin = keyScore(g.chroma, kProfMinor, (g.rootPc + 7) % 12);
        if (twin >= win - kTiebreakEps) {
            out.tonicPc = (g.rootPc + 7) % 12;
            out.mode = LM_DOR;
            out.tiebreak = true;
        }
    }

    const uint8_t m = out.mode;
    const int tonic = out.tonicPc;
    switch (scaleIdx) {
        // Plain canvases play the mode itself, tonic-home.
        case SC_MAJOR:
        case SC_MINOR:
        case SC_DORIAN:
        case SC_MIXO:
            out.scaleIdx = (m == LM_DOR)    ? SC_DORIAN
                           : (m == LM_MIXO) ? SC_MIXO
                           : (m == LM_AEO)  ? SC_MINOR
                                            : SC_MAJOR;
            out.rootPc = tonic;
            return out;
        // Pentatonics keep the documented flavor swap, tonic-home: all four
        // (maj pent under Ionian/Mixolydian, min pent under Aeolian/Dorian)
        // sit fully inside the song's pitch set.
        case SC_MAJ_PENT:
        case SC_MIN_PENT:
            out.scaleIdx = modeMinorish(m) ? SC_MIN_PENT : SC_MAJ_PENT;
            out.rootPc = tonic;
            return out;
        // Blues is chosen spice — never switched, only re-centred: the tonic
        // under Dorian/Aeolian (minor home) AND under Mixolydian (dominant
        // blues, the canon move); the relative-minor boxes trick under a
        // plain Ionian song, exactly as before.
        case SC_BLUES:
            out.scaleIdx = SC_BLUES;
            out.rootPc = (m == LM_ION) ? (tonic + 9) % 12 : tonic;
            return out;
        // Everything else keeps the frozen relative-root behavior, fed the
        // refined tonic and side.
        default:
            out.scaleIdx = applyScaleForKey(scaleIdx, modeMinorish(m));
            out.rootPc = applyRootForScale(tonic, modeMinorish(m), out.scaleIdx);
            return out;
    }
}

}  // namespace dsp
