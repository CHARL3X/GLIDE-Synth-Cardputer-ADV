#include "demo_gen.h"

namespace dsp {

namespace {
inline uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
}  // namespace

DemoNote DemoMelody::next(int scaleLen) {
    if (scaleLen < 2) scaleLen = 2;
    const int hi = 2 * scaleLen;  // two octaves of degrees

    if (nOn == 0) {
        // roll a fresh phrase: a one-bar motif (3..5 onsets, on the beat-ish,
        // nothing faster than 8ths) with a rising contour from the mid root
        static const uint8_t kGaps[6] = {2, 3, 4, 4, 6, 8};
        uint8_t pos = 0;
        nOn = 0;
        while (nOn < 5) {
            motif[nOn++] = pos;
            pos += kGaps[xorshift(rng) % 6];
            if (pos > 13) break;  // leave the bar's tail to ring
        }
        int d = scaleLen - (int)(xorshift(rng) % 2);  // start on/just under mid root
        for (int i = 0; i < nOn; ++i) {
            degs[i] = (int8_t)(d > hi ? hi : (d < 0 ? 0 : d));
            d += 1 + (int)(xorshift(rng) % 2);  // rise by a step or two
        }
        shift1 = (int8_t)(xorshift(rng) % 3) - 1;  // the answer sits -1..+1 away
        bar = 0;
        idx = 0;
    }

    DemoNote n;
    if (bar == 2) {  // resolution: approach, slide down onto the root, breathe
        if (idx == 0) {
            n.type = DemoNote::Attack;
            n.degree = scaleLen + 1;
            n.steps16 = 4;
            ++idx;
        } else if (idx == 1) {
            n.type = DemoNote::Slide;  // the money slide: down onto the root
            n.degree = scaleLen;
            n.steps16 = 12;            // ring out the rest of the bar
            ++idx;
        } else {
            n.type = DemoNote::Rest;   // the fourth bar is air — the bed shows
            n.degree = scaleLen;
            n.steps16 = 16;
            nOn = 0;                   // next call rolls a new phrase
        }
        return n;
    }

    // bars 0 and 1: the motif; bar 1 repeats its rhythm exactly (that's the
    // hook) with the degrees shifted a step — question, then answer
    const uint8_t at = motif[idx];
    const uint8_t nextAt = (idx + 1 < nOn) ? motif[idx + 1] : 16;
    int deg = degs[idx] + (bar == 1 ? shift1 : 0);
    if (deg < 0) deg = 0;
    if (deg > hi) deg = hi;
    n.degree = deg;
    n.steps16 = (uint8_t)(nextAt - at);
    // each bar re-articulates its downbeat; inside the bar the line SLIDES
    n.type = (idx == 0) ? DemoNote::Attack
                        : ((xorshift(rng) % 10 < 6) ? DemoNote::Slide : DemoNote::Attack);
    if (++idx >= nOn) {
        idx = 0;
        ++bar;
    }
    return n;
}

}  // namespace dsp
