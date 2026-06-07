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
    int8_t  octave = 4;           // base octave of string 0, col 0
    uint8_t rowIntervalSemis = 5; // string-to-string interval (a fourth)
    bool    scaleLock = true;
};

constexpr int kGridStrings = 4;
constexpr int kGridCols = 10;

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

float midiToFreq(float midi);

// Nearest note name + signed cents, e.g. "A4", -50..+50.
void midiToNoteCents(float midi, char* nameOut, int nameCap, int& centsOut);

}  // namespace dsp
