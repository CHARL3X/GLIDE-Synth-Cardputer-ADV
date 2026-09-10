// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// TPT state-variable lowpass (Zavalishin). Stable up to Nyquist — matters
// because cutoff is user-rangeable to 12 kHz at a 32 kHz rate, where a
// Chamberlin SVF would blow up. Pure C++.
#pragma once
#include <cmath>
#include <cstdint>

namespace dsp {

class Svf {
public:
    void init(float sr) {
        sr_ = sr;
        reset();
    }

    // Output tap: this is a multimode SVF — the TPT structure computes LP/BP/HP
    // simultaneously, so the four modes are free (just a different combination
    // of the same v1/v2 each sample). mode: 0=LP 1=HP 2=BP 3=notch.
    enum Mode : uint8_t { LP, HP, BP, Notch };

    // res 0..0.95; called once per block with the smoothed cutoff + the mode.
    // `toBp` (0..1) crossfades the NOTCH tap toward bandpass, and is ignored by
    // every other mode. It exists for the G0 wah: see the note on process().
    void set(float cutoffHz, float res, uint8_t mode = LP, float toBp = 0.f) {
        if (cutoffHz < 40.f) cutoffHz = 40.f;
        const float ny = sr_ * 0.49f;
        if (cutoffHz > ny) cutoffHz = ny;
        const float g = tanf(3.14159265f * cutoffHz / sr_);
        k_ = 2.f - 1.9f * res;
        a1_ = 1.f / (1.f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
        mode_ = mode;
        toBp_ = toBp < 0.f ? 0.f : (toBp > 1.f ? 1.f : toBp);
    }

    inline float process(float x) {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;  // bandpass state
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;  // lowpass state
        ic1_ = 2.f * v1 - ic1_;
        ic2_ = 2.f * v2 - ic2_;
        switch (mode_) {
            case HP:    return x - k_ * v1 - v2;
            case BP:    return v1;
            // Notch = HP + LP. Its width is proportional to k_, which is the
            // INVERSE of resonance -- so a notch gets narrower as Q goes up,
            // where every other mode gets more dramatic. That inversion is what
            // made the wah inaudible on a notch patch: the wah drives res to
            // 0.95, k_ falls to 0.195, and the null narrows to ~176 Hz at a
            // 900 Hz centre, so a sweep across a 220 Hz-spaced harmonic series
            // passes BETWEEN the partials and cancels nothing (measured: 0.9 dB
            // of harmonic swing over the full sweep, against 33 dB in LP).
            // toBp_ crossfades the tap to bandpass -- which is what a wah pedal
            // physically IS, and the one response that WANTS the Q the wah
            // brings. Three flops, and only on this branch.
            case Notch: {
                const float nf = x - k_ * v1;
                return toBp_ > 0.f ? nf + toBp_ * (v1 - nf) : nf;
            }
            default:    return v2;           // LP
        }
    }

    void reset() { ic1_ = ic2_ = 0.f; }

private:
    float sr_ = 32000.f;
    float a1_ = 0.f, a2_ = 0.f, a3_ = 0.f, k_ = 1.f;
    float ic1_ = 0.f, ic2_ = 0.f;
    float toBp_ = 0.f;
    uint8_t mode_ = LP;
};

}  // namespace dsp
