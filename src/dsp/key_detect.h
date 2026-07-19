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

// Incremental form, for listening in rounds: zero the accumulator, feed each
// captured segment, classify whenever you want a verdict. Chroma evidence
// sums across segments, so a longer listen is strictly more informed —
// short captures risk hearing one chord and naming ITS key, not the song's.
void accumulateChroma(const int16_t* mono, int n, float sampleRate,
                      float chroma[12]);
KeyGuess classifyChroma(const float chroma[12]);

// Round accumulation with each segment's chroma normalized to unit sum before
// it lands in the accumulator: one audible round = one vote, whatever its
// level. Raw accumulateChroma weights by energy, so one loud chord out-votes
// ten quiet honest rounds — exactly wrong for a listener that meets both a
// blaring chorus and a phone across the room. (Classification itself is
// correlation-based and level-invariant; only the accumulation ever cared.)
void accumulateChromaNormalized(const int16_t* mono, int n, float sampleRate,
                                float chroma[12]);

// classifyChroma, but confidence is the margin over the best key whose
// APPLIED root (via applyRootForScale under scaleIdx) actually differs from
// the winner's. The usual runner-up is the relative twin (C maj vs A min),
// which lands on the SAME root for the player's scale — a harmless ambiguity
// that must not deflate the lock confidence, while a genuinely different key
// (a fifth off) still does. Winner and chroma are identical to classifyChroma.
KeyGuess classifyChromaForScale(const float chroma[12], int scaleIdx);

// Whether a segment rises above the silence floor at all (detectKey's gate,
// exposed so a round-based listener can refuse a silent room honestly).
bool segmentAudible(const int16_t* mono, int n);

// Whether a scale is minor-flavoured: its HARMONY PARENT (kScales[i].harm,
// the diatonic scale the backing builds triads from) contains a minor third.
bool scaleIsMinorish(int scaleIdx);

// Root to actually apply so the player's current scale lands on the detected
// key's notes: a major song under a minorish scale takes the relative minor
// root (+9), a minor song under a majorish scale the relative major (+3).
int applyRootForScale(int detectedPc, bool detectedMinor, int scaleIdx);

}  // namespace dsp
