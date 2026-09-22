// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
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
    // The 7th harmonic lands TWO bands up a minor seventh higher (7f = two
    // octaves + 969 cents; -31 cents off the bin centre, well inside the
    // 16-cycle window), so bin pc also has a 7th-harmonic parent two bands
    // down at pc+2 — without that term, a bright tonic paints its own b7
    // and guitar-heavy major music leans Mixolydian.
    constexpr float kH2 = 0.45f, kH3 = 0.25f, kH7 = 0.15f;
    for (int oct = 1; oct < kOctaves; ++oct) {
        for (int pc = 0; pc < 12; ++pc) {
            float v = band[oct][pc] - kH2 * band[oct - 1][pc] -
                      kH3 * band[oct - 1][(pc + 5) % 12];
            if (oct >= 2) v -= kH7 * band[oct - 2][(pc + 2) % 12];
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

KeyGuess classifyChromaSong(const float chromaIn[12]) {
    KeyGuess g = classifyChroma(chromaIn);  // gates + winner + normalized chroma
    if (!g.valid) return g;
    // Re-derive the margin, excluding the relative twin: it shares every
    // note with the winner, and the landing's pentatonic retreat means the
    // ear meets nothing sour either way — its closeness is not uncertainty.
    // A parallel-mode rival (A maj vs A min) or a fifth-off key still counts.
    const int twinRoot = (g.rootPc + (g.minor ? 3 : 9)) % 12;
    float best = -2.f, bestRival = -2.f;
    for (int root = 0; root < 12; ++root) {
        for (int m = 0; m < 2; ++m) {
            const bool minor = m != 0;
            const float r = keyScore(chromaIn, minor ? kProfMinor : kProfMajor, root);
            if (root == g.rootPc && minor == g.minor) {
                best = r;
                continue;
            }
            if (minor != g.minor && root == twinRoot) continue;
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

const char* listenModeName(uint8_t mode) {
    switch (mode) {
        case LM_ION:  return "MAJ";
        case LM_DOR:  return "DOR";
        case LM_MIXO: return "MIX";
        default:      return "MIN";
    }
}

namespace {

// The mode-evidence gates (landListen's judgment):
// the deciding degree must be PRESENT (vs the chroma peak) and clearly
// out-power its rival, or the evidence is treated as absent. The b7 bin
// answers to a HIGHER floor: it's the phantom-prone one (the tonic's own
// 7th harmonic lands there — measured on hardware as a G-major song locking
// G MIXO), while a genuine mixo groove plays its b7 as a whole loud chord
// and clears 0.30 without trying.
constexpr float kModeRatio = 1.8f;
constexpr float kModePresence = 0.20f;
constexpr float kModePresenceB7 = 0.30f;
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
// the gates were cleared at all (in EITHER direction). conflict fires when
// BOTH versions of the deciding degree are audibly present and neither wins
// the ratio — the song plays both (borrowed chords, melodic-minor lines), so
// any seven-note landing has a coin-flip sour note baked in. The b7 side of
// a conflict answers to the same phantom-aware floor as the Mixolydian gate.
uint8_t modeFromChroma(bool minor, const float chroma[12], int tonicPc,
                       bool& hasEvidence, bool& conflict) {
    float peak = 0.f;
    for (int i = 0; i < 12; ++i)
        if (chroma[i] > peak) peak = chroma[i];
    hasEvidence = false;
    conflict = false;
    if (peak <= 1e-9f) return minor ? LM_AEO : LM_ION;
    const float presence = kModePresence * peak;
    if (minor) {
        const float nat6 = chroma[(tonicPc + 9) % 12];
        const float fl6 = chroma[(tonicPc + 8) % 12];
        if (nat6 >= presence && nat6 >= kModeRatio * fl6) {
            hasEvidence = true;
            return LM_DOR;
        }
        if (fl6 >= presence && fl6 >= kModeRatio * nat6) {
            hasEvidence = true;
            return LM_AEO;
        }
        conflict = nat6 >= presence && fl6 >= presence;
        return LM_AEO;
    }
    const float maj7 = chroma[(tonicPc + 11) % 12];
    const float fl7 = chroma[(tonicPc + 10) % 12];
    if (fl7 >= kModePresenceB7 * peak && fl7 >= kModeRatio * maj7) {
        hasEvidence = true;
        return LM_MIXO;
    }
    if (maj7 >= presence && maj7 >= kModeRatio * fl7) {
        hasEvidence = true;
        return LM_ION;
    }
    conflict = maj7 >= presence && fl7 >= kModePresenceB7 * peak;
    return LM_ION;
}

inline bool modeMinorish(uint8_t mode) { return mode == LM_AEO || mode == LM_DOR; }

inline bool isCanvas(int scaleIdx) {
    return scaleIdx == SC_MAJOR || scaleIdx == SC_MINOR ||
           scaleIdx == SC_DORIAN || scaleIdx == SC_MIXO;
}

// The seven-note canvas that plays a mode itself, tonic-home.
inline int canvasForMode(uint8_t mode) {
    return (mode == LM_DOR)    ? SC_DORIAN
           : (mode == LM_MIXO) ? SC_MIXO
           : (mode == LM_AEO)  ? SC_MINOR
                               : SC_MAJOR;
}

inline int pentForSide(bool minorSide) { return minorSide ? SC_MIN_PENT : SC_MAJ_PENT; }

// Sourness of a landing: energy heard at the out-of-set semitone neighbours
// of the degrees the scale ASSERTS — chroma[n] beyond chroma[s] for each
// in-set pc s and out-of-set neighbour n. A scale tone the song contradicts
// is what actually plays sour; a degree the song merely omits is silent.
// chroma is peak-normalized, so the gates below are absolute.
float scaleSourness(int scaleIdx, int rootPc, const float chroma[12]) {
    if (scaleIdx < 0 || scaleIdx >= kScaleCount) return 0.f;
    bool inSet[12] = {false};
    const Scale& sc = kScales[scaleIdx];
    for (int i = 0; i < sc.len; ++i) inSet[(rootPc + sc.steps[i]) % 12] = true;
    float sour = 0.f;
    for (int pc = 0; pc < 12; ++pc) {
        if (!inSet[pc]) continue;
        for (int d = -1; d <= 1; d += 2) {
            const int n = (pc + d + 12) % 12;
            if (inSet[n]) continue;
            const float excess = chroma[n] - chroma[pc];
            if (excess > 0.f) sour += excess;
        }
    }
    return sour;
}

// The sourness guard's gates: a canvas landing must be audibly sour at all
// (floor) AND clearly sourer than the side's pentatonic (margin) before it
// retreats — a demote on a coin flip would trade the player's seven-note
// canvas for nothing. This is what catches the modes the four-mode
// vocabulary can't name: a Lydian song's #4 indicts the canvas P4, a
// Phrygian song's b2 the canvas 2, and the pentatonic omits both.
constexpr float kSourFloor = 0.15f;
constexpr float kSourMargin = 0.10f;

// Retreat a canvas landing to the side's pentatonic (same root) when it is
// clearly sourer. Canvas-to-canvas moves stay behind the measured degree
// gates — this guard only ever plays FEWER notes, never re-picks among
// sevens, so the phantom-b7 trap stays fenced.
void guardCanvasSourness(ListenApply& out, const float chroma[12]) {
    if (!isCanvas(out.scaleIdx)) return;
    const int pent = (out.scaleIdx == SC_MAJOR || out.scaleIdx == SC_MIXO)
                         ? SC_MAJ_PENT
                         : SC_MIN_PENT;
    const float sc = scaleSourness(out.scaleIdx, out.rootPc, chroma);
    if (sc < kSourFloor) return;
    const float sp = scaleSourness(pent, out.rootPc, chroma);
    if (sp + kSourMargin > sc) return;
    out.scaleIdx = pent;
    out.safe = true;
}

}  // namespace

ListenApply landListen(const KeyGuess& g) {
    ListenApply out;
    out.mode = g.minor ? (uint8_t)LM_AEO : (uint8_t)LM_ION;
    out.tonicPc = g.rootPc;
    out.rootPc = g.rootPc;
    out.scaleIdx = canvasForMode(out.mode);
    out.modal = false;
    out.tiebreak = false;
    out.safe = false;

    bool hasEvidence = false, conflict = false;
    const uint8_t mode =
        modeFromChroma(g.minor, g.chroma, g.rootPc, hasEvidence, conflict);

    // Conflicted deciding degree: the song audibly plays BOTH 6ths (or both
    // 7ths), so either seven-note canvas would assert a note the song
    // contradicts half the time. Land the side's pentatonic at the tonic —
    // it omits the clash degree entirely.
    if (conflict) {
        out.scaleIdx = pentForSide(g.minor);
        out.safe = true;
        return out;
    }

    if (hasEvidence) {
        out.mode = mode;
        out.modal = (mode == LM_DOR || mode == LM_MIXO);

        // Tonic tiebreak: "X major with a strong b7" shares its pitch set
        // with BOTH Dorian at X+7 and plain Ionian at X+5 (D mixo == A dorian
        // == G major's notes). If either reading's profile score runs neck
        // and neck with the winner, the song's true home is there — re-seat
        // the tonic on the better-scoring rival (the Am7-D9 vamp goes
        // Dorian; a G-major song heard D-first goes home to G).
        if (mode == LM_MIXO) {
            const float win = keyScore(g.chroma, kProfMajor, g.rootPc);
            const float dorTwin = keyScore(g.chroma, kProfMinor, (g.rootPc + 7) % 12);
            const float ionParent = keyScore(g.chroma, kProfMajor, (g.rootPc + 5) % 12);
            const float rival = dorTwin > ionParent ? dorTwin : ionParent;
            if (rival >= win - kTiebreakEps) {
                if (dorTwin > ionParent) {
                    out.tonicPc = (g.rootPc + 7) % 12;
                    out.mode = LM_DOR;
                } else {
                    out.tonicPc = (g.rootPc + 5) % 12;
                    out.mode = LM_ION;
                    out.modal = false;  // the song was plain major all along
                }
                out.tiebreak = true;
            }
        }
    }

    // The mode's own canvas, tonic-home — then the sourness guard (a clash
    // can live on a degree the 6th/7th evidence never looks at: a Lydian
    // #4, a Phrygian b2), which only ever retreats to fewer notes.
    out.scaleIdx = canvasForMode(out.mode);
    out.rootPc = out.tonicPc;
    guardCanvasSourness(out, g.chroma);
    return out;
}

int listenAlternates(const KeyGuess& g, ListenApply* out, int cap) {
    if (!out || cap <= 0) return 0;
    const ListenApply primary = landListen(g);
    const int tonic = primary.tonicPc;
    const bool minorSide = modeMinorish(primary.mode);
    const int twinRoot = (tonic + (minorSide ? 3 : 9)) % 12;
    const int twinScale = minorSide ? SC_MAJOR : SC_MINOR;

    // The five candidates behind the primary, computed first so the emit
    // order below is one table and nothing else.
    enum { C_FLAVOR, C_BLUES, C_RUNNER1, C_RUNNER2, C_TWIN, C_COUNT };
    struct Cand {
        int scale;
        int root;
        uint8_t mode;
        bool ok;
    };
    Cand c[C_COUNT];
    for (int i = 0; i < C_COUNT; ++i) c[i].ok = false;
    // Fewer notes — or, when the primary already retreated, the full seven
    // on offer for a player who wants them anyway.
    c[C_FLAVOR] = isCanvas(primary.scaleIdx)
                      ? Cand{pentForSide(minorSide), tonic, primary.mode, true}
                      : Cand{canvasForMode(primary.mode), tonic, primary.mode, true};
    // The blues, at its minor home.
    c[C_BLUES] = Cand{SC_BLUES, minorSide ? tonic : twinRoot, primary.mode, true};
    // The relative twin's canvas — same notes, the other home.
    c[C_TWIN] = Cand{twinScale, twinRoot,
                     minorSide ? (uint8_t)LM_ION : (uint8_t)LM_AEO, true};
    // Runner-up KEYS: rescore the 24 profiles and take the two best whose
    // landing is neither the primary's own reading nor the twin (it has its
    // own slot) — so both are genuinely different pitch sets. Landings are
    // plain major/minor at the rival root: the mode refinements were judged
    // at the primary tonic and don't transfer.
    float peak = 0.f;
    for (int i = 0; i < 12; ++i)
        if (g.chroma[i] > peak) peak = g.chroma[i];
    if (peak > 1e-9f) {
        for (int k = 0; k < 2; ++k) {
            float bestR = -2.f;
            int bestRoot = -1;
            bool bestMinor = false;
            for (int root = 0; root < 12; ++root) {
                for (int m = 0; m < 2; ++m) {
                    const bool minor = m != 0;
                    if (root == tonic && minor == minorSide) continue;
                    if (root == twinRoot && minor != minorSide) continue;
                    if (k == 1 && c[C_RUNNER1].ok && root == c[C_RUNNER1].root &&
                        (minor ? SC_MINOR : SC_MAJOR) == c[C_RUNNER1].scale)
                        continue;
                    const float r =
                        keyScore(g.chroma, minor ? kProfMinor : kProfMajor, root);
                    if (r > bestR) {
                        bestR = r;
                        bestRoot = root;
                        bestMinor = minor;
                    }
                }
            }
            if (bestRoot < 0) break;
            c[C_RUNNER1 + k] = Cand{bestMinor ? SC_MINOR : SC_MAJOR, bestRoot,
                                    bestMinor ? (uint8_t)LM_AEO : (uint8_t)LM_ION,
                                    true};
        }
    }

    // The walk. Measured on 47 field listens (2026-09-22): the twin and
    // each runner-up key rescued five songs apiece, the flavour slots one —
    // "right within two presses" is 31/47 with the rescuers first against
    // 22/47 with the flavours first. So the rescuers ride right behind the
    // primary, the twin leading because it can never play sour (same notes,
    // the other home), and the flavours follow.
    static const int kOrder[C_COUNT] = {C_TWIN, C_RUNNER1, C_RUNNER2, C_FLAVOR, C_BLUES};

    int n = 0;
    out[n++] = primary;
    for (int i = 0; i < C_COUNT && n < cap; ++i) {
        const Cand& cd = c[kOrder[i]];
        if (!cd.ok) continue;
        bool dup = false;
        for (int j = 0; j < n; ++j)
            if (out[j].scaleIdx == cd.scale && out[j].rootPc == cd.root) dup = true;
        if (dup) continue;
        ListenApply a = primary;  // tonic/tiebreak stay the card's truth
        a.scaleIdx = cd.scale;
        a.rootPc = cd.root;
        a.mode = cd.mode;
        a.modal = false;
        a.safe = false;
        out[n++] = a;
    }
    return n;
}

}  // namespace dsp
