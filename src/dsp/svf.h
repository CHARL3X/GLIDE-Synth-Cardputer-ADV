// TPT state-variable lowpass (Zavalishin). Stable up to Nyquist — matters
// because cutoff is user-rangeable to 12 kHz at a 32 kHz rate, where a
// Chamberlin SVF would blow up. Pure C++.
#pragma once
#include <cmath>

namespace dsp {

class Svf {
public:
    void init(float sr) {
        sr_ = sr;
        reset();
    }

    // res 0..0.95; called once per block with the smoothed cutoff.
    void set(float cutoffHz, float res) {
        if (cutoffHz < 40.f) cutoffHz = 40.f;
        const float ny = sr_ * 0.49f;
        if (cutoffHz > ny) cutoffHz = ny;
        const float g = tanf(3.14159265f * cutoffHz / sr_);
        const float k = 2.f - 1.9f * res;
        a1_ = 1.f / (1.f + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    inline float process(float x) {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.f * v1 - ic1_;
        ic2_ = 2.f * v2 - ic2_;
        return v2;  // lowpass tap
    }

    void reset() { ic1_ = ic2_ = 0.f; }

private:
    float sr_ = 32000.f;
    float a1_ = 0.f, a2_ = 0.f, a3_ = 0.f;
    float ic1_ = 0.f, ic2_ = 0.f;
};

}  // namespace dsp
