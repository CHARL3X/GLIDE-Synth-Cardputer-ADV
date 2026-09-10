// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// dsp/formant.h — a vocal tract: the mouth of the TALK macro. Pure C++.
#pragma once
#include <cmath>

namespace dsp {

// A MOUTH, NOT A WAH. The distinction is the whole design, and it is easy to
// lose: a wah is ONE resonance moving monotonically, while a vowel is two
// resonances moving in OPPOSITE directions, with the relationship between them
// carrying the identity. Going "ee" to "ah", F1 rises 280 -> 730 while F2 falls
// 2250 -> 1100. No single sweep can do that, and anything that scales the
// formants together — however it is dressed up — is a wah again. Two earlier
// builds failed exactly there: one drove a 1D vowel path with a common-mode
// "mouth size" on the second axis, and a hardware verdict of "very similar to
// wah" was correct about both halves of it.
//
//  - The control surface is the F1/F2 PLANE. Every corner of it is a real
//    vowel (oo / aw / aa / ee), so there is nowhere to steer that is not
//    speech, and the two axes genuinely oppose one another.
//  - The resonators are a CASCADE, not a parallel bank. Klatt's cascade is
//    what real vocal synthesis uses for vowels because the relative formant
//    AMPLITUDES fall out of the topology: when F1 rises the upper formants
//    swell, and when two formants converge they both do. A parallel bank has
//    to fake that with a per-vowel gain table and a power normalisation, and
//    it still reads as an EQ being pushed around.
//  - Bandwidths are CONSTANT IN Hz (70 / 100 / 170), not constant Q. Constant
//    Q is a filter habit, not a vocal tract: it made F2 wah-narrow at 78 Hz
//    down at "ah" and smeared it to 164 Hz up at "ee", which is backwards.
//  - Formants are ABSOLUTE Hz and never track the played note. A mouth does
//    not transpose when you sing higher.
class Formant {
public:
    void init(float sr) {
        sr_ = sr;
        r1_.reset(); r2_.reset(); r3_.reset(); r4_.reset(); r5_.reset();
        smF1_ = 500.f; smF2_ = 1500.f;
        env_ = 0.f; lipZ_ = 0.f; tractEnv_ = 0.f;
        set(0.f, 0.f);
    }
    void reset() {
        r1_.reset(); r2_.reset(); r3_.reset(); r4_.reset(); r5_.reset();
        env_ = 0.f; lipZ_ = 0.f; tractEnv_ = 0.f;
    }

    // x: -1..+1 -> F1 (jaw: closed 270 .. open 730)
    // y: -1..+1 -> F2 (tongue: back 800 .. front 2300)
    // The two really are independent here — that is the point.
    // `closure` 0..1 pulls the tract shut for a consonant (see Talk in synth).
    void set(float x, float y, float closure = 0.f) {
        const float jaw = norm01(x), tongue = norm01(y);
        float tF1 = 270.f + jaw * 460.f;
        const float tF2 = 800.f + tongue * 1500.f;
        // A closing tract drops F1 toward its floor — that is what makes a stop
        // read as a stop rather than as a volume dip.
        tF1 = tF1 + (220.f - tF1) * closure;
        // F3 belongs to the front/back axis too: front vowels carry it higher.
        const float tF3 = 2400.f + tongue * 600.f;
        // Articulator INERTIA. A tongue and a jaw have mass, and raw sensor
        // straight onto a cutoff sounds like a servo. ~50 ms.
        const float k = 1.f - expf(-1.f / (0.05f * sr_ / 64.f));
        smF1_ += (tF1 - smF1_) * k;
        smF2_ += (tF2 - smF2_) * k;
        smF3_ += (tF3 - smF3_) * k;
        // F1's bandwidth is NOT constant: yielding-wall losses dominate when
        // F1 is low, so it widens toward /i/ and /u/. F2/F3 are near enough
        // constant to treat as such.
        float bw1 = 50.f + 30000.f / smF1_;
        if (bw1 > 160.f) bw1 = 160.f;
        r1_.set(smF1_, bw1, sr_);
        r2_.set(smF2_, kB2, sr_);
        r3_.set(smF3_, kB3, sr_);
        // F4/F5 are fixed and wide. They carry no vowel information at all, and
        // they are what makes the tract sound like SOMEONE IS THERE rather than
        // like three resonators. They cost two biquads and never change.
        r4_.set(3500.f, 250.f, sr_);
        r5_.set(4500.f, 300.f, sr_);
    }

    // Fully wet: TALK has no dry path, the way a real talkbox has none.
    //
    // The source is NORMALISED, buzzed, then re-enveloped. Formants can only
    // sculpt harmonics that already exist, so a sine or a dark patch came out
    // silent and motionless (measured 0.2 dB of vowel movement) — the same
    // failure as the notch wah. Dividing by a slow amplitude follower makes the
    // buzz level-INDEPENDENT so the mouth always has something to shape;
    // multiplying the follower back afterwards hands the note its dynamics
    // back, so the instrument still plays soft when you play soft.
    inline float process(float x) {
        env_ += (fabsf(x) - env_) * kEnvCoef;
        const float norm = x / (env_ + 1e-4f);
        // No source lowpass. A cascade already falls off as the formants stack,
        // so the buzz has to be BRIGHTER here than felt right with a parallel
        // bank — rolling it off first starves F3/F4/F5 of anything to ring on.
        const float buzz = norm / (1.f + fabsf(norm));
        // the cascade — this ordering is what couples the formant amplitudes
        const float tract = r5_.process(r4_.process(r3_.process(r2_.process(r1_.process(buzz)))));
        // Lip radiation: sound leaves a mouth differentiated, +6 dB/oct. Without
        // it a cascade is muddy, because the cascade's own tilt is -12 dB/oct.
        const float lip = tract - 0.95f * lipZ_;
        lipZ_ = tract;
        // Flatten the tract's OVERALL level, not its shape. A cascade's total
        // output swings with where the formants sit (measured 2.24x across an
        // ee-ah-oo gesture, against 1.9x for the wah), and a loudness wobble is
        // heard INSTEAD of a vowel change rather than as well as it. This is a
        // gain on the sum, so every relative formant amplitude the cascade
        // couples for free survives it untouched. Slow enough (~100 ms) that a
        // fast articulation still punches through.
        tractEnv_ += (fabsf(lip) - tractEnv_) * kTractCoef;
        const float flat = lip / (tractEnv_ + 1e-3f);
        return flat * env_ * kMakeup;
    }

private:
    // Klatt's two-pole resonator, normalised to UNITY GAIN AT DC so a cascade
    // of them does not run away. y = a*x + b*y[-1] + c*y[-2].
    struct Res {
        float a = 1.f, b = 0.f, c = 0.f, z1 = 0.f, z2 = 0.f;
        void reset() { z1 = z2 = 0.f; }
        void set(float f, float bw, float sr) {
            if (f < 90.f) f = 90.f;
            const float ny = sr * 0.45f;
            if (f > ny) f = ny;
            c = -expf(-2.f * 3.14159265f * bw / sr);
            b = 2.f * expf(-3.14159265f * bw / sr) * cosf(2.f * 3.14159265f * f / sr);
            a = 1.f - b - c;
        }
        inline float process(float x) {
            const float y = a * x + b * z1 + c * z2;
            z2 = z1;
            z1 = y;
            return y;
        }
    };

    static float norm01(float v) {
        const float u = (v + 1.f) * 0.5f;
        return u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
    }

    // Speech bandwidths, constant in Hz — see the note at the top of the class.
    static constexpr float kB2 = 100.f, kB3 = 150.f;
    static constexpr float kEnvCoef = 0.002f;    // ~16 ms source follower
    static constexpr float kTractCoef = 0.0003f;  // ~100 ms tract-level follower
    static constexpr float kMakeup = 0.7f;      // tuned against the dry level

    Res r1_, r2_, r3_, r4_, r5_;
    float sr_ = 32000.f;
    float smF1_ = 500.f, smF2_ = 1500.f, smF3_ = 2500.f;
    float env_ = 0.f, lipZ_ = 0.f, tractEnv_ = 0.f;
};

}  // namespace dsp
