// Scale tables. Pure C++.
#pragma once
#include <cstdint>

namespace dsp {

struct Scale {
    const char* name;
    const char* shortName;
    uint8_t len;
    uint8_t steps[12];  // semitone offsets within one octave
};

constexpr Scale kScales[] = {
    {"Chromatic",     "chrom", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"Major",         "maj",    7, {0, 2, 4, 5, 7, 9, 11}},
    {"Natural minor", "min",    7, {0, 2, 3, 5, 7, 8, 10}},
    {"Major pent",    "Mpent",  5, {0, 2, 4, 7, 9}},
    {"Minor pent",    "mpent",  5, {0, 3, 5, 7, 10}},
    {"Blues",         "blues",  6, {0, 3, 5, 6, 7, 10}},
    {"Dorian",        "dorian", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"Mixolydian",    "mixo",   7, {0, 2, 4, 5, 7, 9, 10}},
};
constexpr int kScaleCount = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
constexpr int kDefaultScale = 4;  // minor pentatonic — first touch sounds good

constexpr const char* kNoteNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                        "F#", "G",  "G#", "A",  "A#", "B"};

}  // namespace dsp
