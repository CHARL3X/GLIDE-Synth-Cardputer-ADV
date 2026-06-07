// Master output stage: a ~120 Hz highpass (the Cardputer speaker can't move
// air below that — don't waste its excursion) followed by a polynomial soft
// clip so full polyphony saturates instead of crackling. Pure C++.
#pragma once

namespace dsp {

class OutputStage {
public:
    void init(float sr) {
        // one-pole RC highpass coefficient, applied twice (2nd order)
        a_ = 1.f / (1.f + 2.f * 3.14159265f * 120.f / sr);
        reset();
    }

    inline float process(float x) {
        x = hp(x, p1x_, p1y_);
        x = hp(x, p2x_, p2y_);
        return softclip(x);
    }

    void reset() { p1x_ = p1y_ = p2x_ = p2y_ = 0.f; }

    static inline float softclip(float x) {
        if (x > 1.5f) x = 1.5f;
        else if (x < -1.5f) x = -1.5f;
        return x * (27.f + x * x) / (27.f + 9.f * x * x);
    }

private:
    inline float hp(float x, float& px, float& py) {
        const float y = a_ * (py + x - px);
        px = x;
        py = y;
        return y;
    }

    float a_ = 0.98f;
    float p1x_ = 0.f, p1y_ = 0.f, p2x_ = 0.f, p2y_ = 0.f;
};

}  // namespace dsp
