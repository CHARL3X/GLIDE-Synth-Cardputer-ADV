#include "wavetables.h"
#include <cmath>

namespace dsp {

float gTables[TblCount][kTableSize + 1];

namespace {
constexpr float kPi = 3.14159265358979f;

// Additive band-limited builders. Normalized to +/-1 peak afterwards.
void buildSaw(float* t, int harmonics) {
    for (int i = 0; i < kTableSize; ++i) {
        const float ph = 2.f * kPi * i / kTableSize;
        float s = 0.f;
        for (int h = 1; h <= harmonics; ++h) s += sinf(h * ph) / h;
        t[i] = s;
    }
}

void buildSquare(float* t, int harmonics) {
    for (int i = 0; i < kTableSize; ++i) {
        const float ph = 2.f * kPi * i / kTableSize;
        float s = 0.f;
        for (int h = 1; h <= harmonics; h += 2) s += sinf(h * ph) / h;
        t[i] = s;
    }
}

void buildTriangle(float* t, int harmonics) {
    for (int i = 0; i < kTableSize; ++i) {
        const float ph = 2.f * kPi * i / kTableSize;
        float s = 0.f;
        float sign = 1.f;
        for (int h = 1; h <= harmonics; h += 2) {
            s += sign * sinf(h * ph) / (float)(h * h);
            sign = -sign;
        }
        t[i] = s;
    }
}

void normalize(float* t) {
    float peak = 0.f;
    for (int i = 0; i < kTableSize; ++i) {
        const float a = fabsf(t[i]);
        if (a > peak) peak = a;
    }
    if (peak > 0.f) {
        const float g = 1.f / peak;
        for (int i = 0; i < kTableSize; ++i) t[i] *= g;
    }
    t[kTableSize] = t[0];  // interpolation guard
}
}  // namespace

void initWavetables() {
    for (int i = 0; i < kTableSize; ++i)
        gTables[TblSine][i] = sinf(2.f * kPi * i / kTableSize);
    buildTriangle(gTables[TblTri], 9);
    // Lo tables are clean below kMipSplitHz at 32 kHz; hi tables trade a
    // little aliasing for brightness above it.
    buildSaw(gTables[TblSawLo], 20);
    buildSaw(gTables[TblSawHi], 6);
    buildSquare(gTables[TblSqrLo], 19);
    buildSquare(gTables[TblSqrHi], 5);
    for (int t = 0; t < TblCount; ++t) normalize(gTables[t]);
}

}  // namespace dsp
