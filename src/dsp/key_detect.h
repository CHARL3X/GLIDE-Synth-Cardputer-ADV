// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Charles Tobin (CHARL3X)
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

// The scale to actually play under a detected key: the four vanilla scales
// swap to their opposite-mode sibling when the detected mode disagrees
// (Major<->Natural minor, Maj pent<->Min pent), so the keyboard's home key
// lands on the song's true tonic instead of its relative twin. Every other
// scale — an exotic, deliberate flavor choice — is returned unchanged (and
// keeps the relative-root behavior of applyRootForScale). Feed the result to
// applyRootForScale: for swapped/matching scales it returns the tonic as-is.
int applyScaleForKey(int scaleIdx, bool detectedMinor);

// applyScaleForKey, refined by the heard chroma: the Krumhansl profiles only
// ever answer major-or-minor, but half of what a mic meets is MODAL — a
// Dorian vamp lands "minor" and Natural minor's b6 then plays sour against
// the song's natural 6 (the "right key, wrong scale" failure). Within the
// plain seven-note canvases (Major / Mixolydian on the major side, Natural
// minor / Dorian on the minor side) the distinguishing degree's own energy
// decides the mode: a natural 6 clearly out-powering the b6 names Dorian, a
// b7 out-powering the major 7 names Mixolydian. Weak or absent evidence
// keeps the player's current mode if it's already on the detected side
// (an evidence-free switch would be a coin flip), else that side's plain
// default. Pentatonics keep the frozen swap (they omit the clash degrees —
// that's why they're the safe default), and every other scale keeps the
// deliberate-flavor behavior unchanged. chroma is KeyGuess::chroma
// (peak-normalized); detectedPc is the guess's rootPc.
int applyScaleForKeyChroma(int scaleIdx, bool detectedMinor,
                           const float chroma[12], int detectedPc);

// ---- the full LISTEN verdict: mode, tonic, and a landing for EVERY scale --
// The Krumhansl profiles answer major-or-minor at some root; real rooms are
// full of modal music where that's the wrong question — an Am7-D9 Dorian vamp
// reads "D major" as honestly as "A minor", and a flavor-scale player then
// lands home on the wrong note (B blues over an A Dorian song: nothing sour,
// everything off-centre). applyListen turns the raw guess into a refined
// (tonic, mode) and maps it to the player's scale FAMILY:
//
//   1. Degree evidence upgrades the mode: strong b7 over the major 7 makes a
//      major verdict Mixolydian; a natural 6 over the b6 makes a minor one
//      Dorian (same gates as applyScaleForKeyChroma — weak evidence changes
//      NOTHING and every output collapses to the frozen behavior).
//   2. The tonic tiebreak: a Mixolydian-flavoured major verdict at X shares
//      its pitch set with Dorian at X+7. If the profile score of (X+7 minor)
//      sits within kTiebreakEps of the winner, the tonic moves there and the
//      mode becomes Dorian — the Oye Como Va fix.
//   3. Family mapping, tonic-home wherever the set allows it:
//      - plain canvases play the mode itself at the tonic;
//      - pentatonics keep their documented flavor swap, at the tonic (maj
//        pent for Ionian/Mixolydian, min pent for Aeolian/Dorian — all four
//        land fully inside the song's pitch set);
//      - Blues stays Blues (spice is chosen, never imposed) and re-centres:
//        tonic over Dorian/Aeolian/Mixolydian (dominant blues is the canon
//        move), relative minor over plain Ionian (the boxes trick);
//      - every other scale keeps the frozen relative-root behavior, fed the
//        REFINED tonic and side.
//
// Scale/tonic corrections sit behind stricter gates than the key retune
// itself: presence + ratio for the mode, kTiebreakEps for the tonic. chroma
// is KeyGuess::chroma (peak-normalized; correlation is scale-invariant).
//
// Two safety layers guard the plain-canvas landing (and ONLY it — pents are
// already the safe subset, flavor scales are chosen spice):
//   - CONFLICTED DEGREE: when a song audibly plays BOTH versions of the
//     deciding degree (borrowed chords, melodic-minor lines — the case the
//     ratio gate can only shrug at), any seven-note landing has a coin-flip
//     sour note baked in. The landing demotes to the side's pentatonic at
//     the tonic, which omits the clash degree entirely.
//   - SOURNESS: the chosen canvas is scored by the energy heard at the
//     out-of-set semitone neighbours of the degrees it ASSERTS (a scale tone
//     the song contradicts is what actually plays sour; a degree the song
//     merely omits is silent, not sour). If the canvas is clearly sourer
//     than the side's pentatonic, it demotes — this is what catches the
//     modes the four-mode vocabulary can't name (a Lydian song's #4 against
//     the canvas P4, a Phrygian song's b2 against the canvas 2; the pent
//     omits both). Canvas-to-canvas moves stay behind the measured degree
//     gates — sourness only ever retreats to fewer notes, never re-picks
//     among sevens (the phantom-b7 trap stays fenced).
enum ListenMode : uint8_t { LM_ION = 0, LM_DOR = 1, LM_MIXO = 2, LM_AEO = 3 };

const char* listenModeName(uint8_t mode);  // "MAJ" "DOR" "MIX" "MIN"

struct ListenApply {
    int rootPc;     // root to apply
    int scaleIdx;   // scale to apply (== input scale unless a canvas moved)
    uint8_t mode;   // the song's refined mode (ListenMode)
    int tonicPc;    // the song's tonic under that mode (HUD/card truth)
    bool modal;     // degree evidence upgraded the plain verdict
    bool tiebreak;  // the tonic moved off the raw K-S winner
    bool safe;      // a canvas landing retreated to the pentatonic (the
                    // conflicted-degree or sourness guard fired)
};

ListenApply applyListen(int scaleIdx, const KeyGuess& g);

// The plausible landings for a verdict, primary first (== applyListen), each
// a distinct (scaleIdx, rootPc): the side's other canvases and pentatonic at
// the tonic for canvas/pent players, then the relative twin. Flavor scales
// keep their scale and offer root alternates only (their scale is chosen).
// Returns the count (<= cap). Feeds the result card's one-tap second guess —
// a near-miss verdict is fixed in a keypress instead of a full re-listen.
int listenAlternates(int scaleIdx, const KeyGuess& g, ListenApply* out, int cap);

}  // namespace dsp
