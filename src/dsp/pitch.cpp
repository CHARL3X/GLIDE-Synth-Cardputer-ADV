#include "pitch.h"
#include <cmath>
#include <cstdio>

namespace dsp {

float midiToFreq(float midi) {
    return 440.f * exp2f((midi - 69.f) / 12.f);
}

void midiToNoteCents(float midi, char* nameOut, int nameCap, int& centsOut) {
    const int nearest = (int)lroundf(midi);
    centsOut = (int)lroundf((midi - nearest) * 100.f);
    const int n = ((nearest % 12) + 12) % 12;
    const int oct = nearest / 12 - 1;  // MIDI 60 -> C4
    snprintf(nameOut, nameCap, "%s%d", kNoteNames[n], oct);
}

}  // namespace dsp
