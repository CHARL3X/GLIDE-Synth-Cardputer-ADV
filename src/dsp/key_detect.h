// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
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

// classifyChroma, but confidence is the margin over the best rival whose
// PITCH SET differs from the winner's. The usual runner-up is the relative
// twin (C maj vs A min): every note in common, and the landing's pentatonic
// retreat makes that ambiguity harmless to the ear — it must not deflate the
// lock confidence, while a genuinely different key (a fifth off) still does.
// Winner and chroma are identical to classifyChroma.
KeyGuess classifyChromaSong(const float chroma[12]);

// Whether a segment rises above the silence floor at all (detectKey's gate,
// exposed so a round-based listener can refuse a silent room honestly).
bool segmentAudible(const int16_t* mono, int n);

// ---- the LISTEN verdict: the song is the czar ------------------------------
// The Krumhansl profiles answer major-or-minor at some root; real rooms are
// full of modal music where that's the wrong question — an Am7-D9 Dorian vamp
// reads "D major" as honestly as "A minor". landListen turns the raw guess
// into a refined (tonic, mode) and lands the song's OWN scale at its tonic.
// It takes no player scale: what the instrument was in before the hold is
// not evidence about the song (field verdict, 2026-09-22 — "do not use the
// scale you're previously in to decide what the next one should be"; the old
// family mapping kept a Blues player in Blues for every song, and its
// relative-root rule put an exotic-scale player a third off the tonic).
//
//   1. Degree evidence upgrades the mode: strong b7 over the major 7 makes a
//      major verdict Mixolydian; a natural 6 over the b6 makes a minor one
//      Dorian. Weak evidence keeps the plain side (Ionian / Aeolian).
//   2. The tonic tiebreak: a Mixolydian-flavoured major verdict at X shares
//      its pitch set with Dorian at X+7 and plain Ionian at X+5. If either
//      rival's profile score sits within kTiebreakEps of the winner, the
//      tonic moves there — the Oye Como Va fix, and the G-major-heard-D-first
//      fix.
//   3. The landing is the mode's seven-note canvas at the tonic (Major,
//      Natural minor, Dorian, Mixolydian), rootPc == tonicPc always.
//
// Two safety layers guard that canvas, because a wrong seven-note landing
// plays a sour note where the pentatonic would simply omit it (measured on
// 47 field listens: the canvas soured 18, the pentatonic 9):
//   - CONFLICTED DEGREE: the song audibly plays BOTH versions of the deciding
//     degree (borrowed chords, melodic-minor lines) — any canvas has a
//     coin-flip sour note baked in, so it lands the side's pentatonic at the
//     tonic instead.
//   - SOURNESS: the canvas is scored by the energy heard at the out-of-set
//     semitone neighbours of the degrees it ASSERTS; if it is clearly sourer
//     than the side's pentatonic it retreats to that pentatonic. This is
//     what catches the modes the four-mode vocabulary can't name (a Lydian
//     #4 against the canvas P4, a Phrygian b2 against the canvas 2).
// chroma is KeyGuess::chroma (peak-normalized; correlation is scale-invariant).
enum ListenMode : uint8_t { LM_ION = 0, LM_DOR = 1, LM_MIXO = 2, LM_AEO = 3 };

const char* listenModeName(uint8_t mode);  // "MAJ" "DOR" "MIX" "MIN"

struct ListenApply {
    int rootPc;     // root to apply (== tonicPc for the primary landing)
    int scaleIdx;   // scale to apply
    uint8_t mode;   // the song's refined mode (ListenMode)
    int tonicPc;    // the song's tonic under that mode (HUD/card truth)
    bool modal;     // degree evidence upgraded the plain verdict
    bool tiebreak;  // the tonic moved off the raw K-S winner
    bool safe;      // the canvas retreated to the pentatonic (a guard fired)
};

ListenApply landListen(const KeyGuess& g);

// The space walk: the plausible landings for a verdict, primary first
// (== landListen), each a distinct (scaleIdx, rootPc), in this order:
//   0  the primary landing
//   1  the relative twin's canvas — same notes, the other home (never sour)
//   2  the detector's best runner-up KEY, plain major/minor at its root
//   3  the second runner-up key
//   4  the side's pentatonic at the tonic (or, when the primary already
//      retreated to it, the mode's full canvas — the seven notes on offer)
//   5  Blues at the minor home (the tonic on the minor side, the relative
//      minor on the major side — the boxes trick)
// Slots 1-3 are how a wrong verdict gets fixed; 4-5 are flavours of the
// same key. Measured on 47 field listens: the twin and each runner-up
// rescued five songs apiece, the flavours one, so "right within two presses"
// is 31/47 this way round against 22/47 flavours-first. Runner-ups skip the
// twin (it has its own slot) and the primary's own reading, so both are
// genuinely different pitch sets. Returns the count (<= cap).
int listenAlternates(const KeyGuess& g, ListenApply* out, int cap);

}  // namespace dsp
