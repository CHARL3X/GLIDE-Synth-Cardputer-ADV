// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Grid -> pitch mapping: the isomorphic layout, degree mapping, key/octave
// math, and note-name helpers. Pure C++.
#pragma once
#include <cstdint>
#include "scales.h"

namespace dsp {

// The musical layout of the 4x10 note grid. Rows are "strings" (string 0 =
// bottom = lowest), columns step right. With scaleLock on, every key is the
// NEXT SCALE DEGREE (degree mapping: zero dead keys, sliding a chord shape
// is a diatonic transposition — "you can't really hit a wrong note").
// With lock off (or shift held = momentary chromatic) columns are semitones.
struct Layout {
    uint8_t rootSemis = 9;        // A
    uint8_t scaleIdx = kDefaultScale;
    int8_t  octave = 3;           // base octave of string 0, col 0 (a touch low
                                  // so there's room to solo up over a backing)
    uint8_t rowIntervalSemis = 5; // string-to-string interval (a fourth)
    bool    scaleLock = true;
    int8_t  jamOctave = 1;        // where the BACKING (jam-row drones and the
                                  // progression's chords) sits, in octaves
                                  // relative to the grid: -2..+2. +1 by
                                  // default: an octave UNDER the grid (the old
                                  // fixed voicing) was bass-pad territory on
                                  // headphones and inaudible mud on the 1 W
                                  // speaker whenever the solo rows were in a
                                  // playable register — players were shifting
                                  // up two octaves to tap chords, then back.
};

constexpr int kGridStrings = 4;
constexpr int kGridCols = 10;

// The backing register never turns into a dog whistle: if the grid's base
// note plus the jam-octave shift would sit above this (C7), the shift folds
// down an octave at a time (the tempo-synced delay's fold, applied to pitch).
// One decision per LAYOUT, not per cell, so every chord in a progression
// shares one register and I-IV-V never inverts across the fold.
constexpr float kBackingCeilMidi = 96.f;

// Semitone offset the backing adds to a grid pitch: 12 * jamOctave, folded
// under the ceiling. Going LOW is never folded — a player who wants the
// backing an octave or two under the grid asked for exactly that.
inline float backingShift(const Layout& l) {
    const float base = 12.f * (l.octave + 1) + l.rootSemis;  // string 0, col 0
    float off = 12.f * l.jamOctave;
    while (off > -12.f && base + off > kBackingCeilMidi) off -= 12.f;
    return off;
}

// Row offset in *scale degrees*, derived from the semitone row interval so
// "a fourth between strings" survives the degree mapping for any scale size
// (pent: 5*5/12 -> 2 degrees = A->D; major: 5*7/12 -> 3 degrees = perfect 4th).
inline int rowDegrees(const Layout& l) {
    const int len = kScales[l.scaleIdx].len;
    int d = (l.rowIntervalSemis * len + 6) / 12;  // rounded
    return d < 1 ? 1 : d;
}

// Fractional MIDI note for a grid position. chromaticOverride = shift held.
inline float gridToMidi(const Layout& l, int string, int col, bool chromaticOverride) {
    const float base = 12.f * (l.octave + 1) + l.rootSemis;  // A oct4 -> 69 (A440)
    if (l.scaleLock && !chromaticOverride) {
        const Scale& sc = kScales[l.scaleIdx];
        const int deg = string * rowDegrees(l) + col;
        const int oct = deg / sc.len;
        const int idx = deg % sc.len;
        return base + 12.f * oct + sc.steps[idx];
    }
    return base + string * l.rowIntervalSemis + col;
}

// True if the chromatic pitch at (string, col) lands on a scale tone — used
// by the UI to highlight in-scale keys when lock is OFF.
inline bool chromaticInScale(const Layout& l, int string, int col) {
    const Scale& sc = kScales[l.scaleIdx];
    const int semis = (string * l.rowIntervalSemis + col) % 12;
    for (int i = 0; i < sc.len; ++i)
        if (sc.steps[i] == semis) return true;
    return false;
}

// A diatonic chord rooted at a grid position — the backing for the auto
// progression. With scale lock it stacks thirds into a real major/minor/dim
// triad that is ALWAYS in key, the same "you can't hit a wrong note" guarantee
// the melody gets — but the triad is built from the scale's HARMONY PARENT, not
// its own degrees. For 7-note scales the parent is the scale itself, so diatonic
// progressions are unchanged. For pentatonic/blues the parent is the diatonic
// scale they're carved from, so the backing stays consonant minor/major triads
// while the solo keeps the literal scale: the blues ♭5 "blue note" stays a
// melodic color over the chords and never lands as a chord tone (it snaps onto
// the nearest harmony tone if you tap it as a step). Chromatic (lock off) falls
// back to a power voicing (root, fifth, octave). Voiced in the backing register
// (backingShift: the jam octave over the grid, like the drones). Writes up to
// maxOut fractional MIDI notes (root first) and returns the count.
// The 0-based degree in the HARMONY PARENT scale that the chord at (string,
// col) is rooted on — the Roman-numeral index (0 = I). Assumes scale lock and
// no chromatic override; this is the tapped tone snapped onto the nearest
// harmony-scale degree (the same snap chordPitches voices, factored out so the
// triad and its label can never disagree).
inline int chordDegree(const Layout& l, int string, int col) {
    const Scale& sc = kScales[l.scaleIdx];
    const Scale& hsc = kScales[sc.harm];      // 7-note triad source
    const int deg0 = string * rowDegrees(l) + col;
    const int pc = sc.steps[deg0 % sc.len];   // tapped tone, semitones over tonic
    int hd = 0, bestDiff = 99;
    for (int i = 0; i < hsc.len; ++i) {
        const int diff = pc > hsc.steps[i] ? pc - hsc.steps[i] : hsc.steps[i] - pc;
        if (diff < bestDiff) { bestDiff = diff; hd = i; }
    }
    return hd;
}

inline int chordPitches(const Layout& l, int string, int col, bool chromatic,
                        float* out, int maxOut) {
    if (maxOut <= 0) return 0;
    const float base = 12.f * (l.octave + 1) + l.rootSemis + backingShift(l);
    int n = 0;
    if (l.scaleLock && !chromatic) {
        const Scale& sc = kScales[l.scaleIdx];
        const Scale& hsc = kScales[sc.harm];      // 7-note triad source
        const int deg0 = string * rowDegrees(l) + col;
        const float octBase = base + 12.f * (deg0 / sc.len);  // tonic at this octave
        const int hd = chordDegree(l, string, col);
        const int thirds[3] = {0, 2, 4};  // stacked diatonic thirds = a triad
        for (int k = 0; k < 3 && n < maxOut; ++k) {
            const int deg = hd + thirds[k];
            out[n++] = octBase + 12.f * (deg / hsc.len) + hsc.steps[deg % hsc.len];
        }
    } else {
        const float root = base + string * l.rowIntervalSemis + col;
        const float voicing[3] = {0.f, 7.f, 12.f};  // power: root, fifth, octave
        for (int k = 0; k < 3 && n < maxOut; ++k) out[n++] = root + voicing[k];
    }
    return n;
}

// The diminished-fifth marker appended to a lowercase numeral (vii°). Font0 is
// the classic 5x7 glyph set where 0xF8 is the degree sign; if hardware shows a
// blank box instead, swap this one constant to 'o'.
constexpr char kDegreeGlyph = '\xF8';

// Roman numeral of the diatonic triad at a grid cell: uppercase = major,
// lowercase = minor, ° = diminished, + = augmented (harmonic-minor parents
// really produce III+). False when there is no diatonic degree to name —
// scale lock off, or the step was struck chromatic (power voicing). Pure and
// allocation-free; needs cap >= 5 ("vii°" + NUL).
inline bool chordRomanNumeral(const Layout& l, int string, int col,
                              bool chromatic, char* out, int cap) {
    if (cap < 5 || !l.scaleLock || chromatic) return false;
    const Scale& hsc = kScales[kScales[l.scaleIdx].harm];
    const int hd = chordDegree(l, string, col);
    // Interval of the stacked third/fifth over the root, octave-unwrapped.
    const int s0 = hsc.steps[hd];
    const int iv2 = 12 * ((hd + 2) / hsc.len) + hsc.steps[(hd + 2) % hsc.len] - s0;
    const int iv4 = 12 * ((hd + 4) / hsc.len) + hsc.steps[(hd + 4) % hsc.len] - s0;
    static const char* const kNumerals[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    const char* rn = kNumerals[hd % 7];
    const bool minor = iv2 == 3;  // minor third -> lowercase
    int n = 0;
    for (const char* c = rn; *c && n < cap - 2; ++c)
        out[n++] = minor ? (char)(*c + 32) : *c;
    if (iv4 == 6) out[n++] = kDegreeGlyph;  // diminished fifth
    else if (iv4 == 8) out[n++] = '+';      // augmented fifth
    out[n] = '\0';
    return true;
}

// Pitch-class name (no octave) of a fractional MIDI note — the progression
// step labels. Integer-rounds without <cmath> (kept out of this hot header).
inline const char* pitchClassName(float midi) {
    const int m = (int)(midi + 0.5f);
    return kNoteNames[((m % 12) + 12) % 12];
}

float midiToFreq(float midi);

// Nearest note name + signed cents, e.g. "A4", -50..+50.
void midiToNoteCents(float midi, char* nameOut, int nameCap, int& centsOut);

}  // namespace dsp
