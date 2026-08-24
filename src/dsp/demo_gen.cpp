// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
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
    const int hi = 3 * scaleLen;  // room for the lifted (B) phrase

    if (nOn == 0) {
        // roll a hook: 3..5 onsets, on the beat-ish, nothing faster than
        // 8ths, with a rising contour from the mid root. Re-rolled every
        // section (see the turnaround) so the solo keeps developing.
        static const uint8_t kGaps[6] = {2, 2, 2, 3, 4, 4};
        uint8_t pos = 0;
        nOn = 0;
        while (nOn < 5) {
            motif[nOn++] = pos;
            pos += kGaps[xorshift(rng) % 6];
            if (pos > 13) break;  // leave the bar's tail to ring
        }
        int d = scaleLen - (int)(xorshift(rng) % 2);  // start on/under mid root
        for (int i = 0; i < nOn; ++i) {
            degs[i] = (int8_t)(d > 2 * scaleLen ? 2 * scaleLen : (d < 0 ? 0 : d));
            d += 1 + (int)(xorshift(rng) % 2);  // rise by a step or two
        }
    }

    // the phrase variant: A A' B A — B lifts the hook an octave (the arc)
    const int lift = (phrase % 4 == 2) ? scaleLen : 0;

    DemoNote n;
    if (bar == 3) {  // turnaround: one short breath, then a swept pickup run —
                     // the trail never goes dark for more than a beat
        if (idx == 0) {
            n.type = DemoNote::Rest;
            n.degree = scaleLen + lift;
            n.steps16 = 4;
            ++idx;
            return n;
        }
        const int k = idx - 1;  // 0..5: six 8th-notes = the rest of the bar
        // odd phrases dive an octave from the high root; even phrases climb
        // from the low root back up to the hook's doorstep
        int deg = (phrase & 1) ? 2 * scaleLen - 2 * k : scaleLen - 5 + k;
        if (deg < 0) deg = 0;
        if (deg > hi) deg = hi;
        n.degree = deg;
        n.steps16 = 2;
        // one attack, then the run is a single swept glissando — the trail
        // draws it as a curve, which is the whole point of the instrument
        n.type = (k == 0) ? DemoNote::Attack : DemoNote::Slide;
        if (++idx >= 7) {  // breath + 6 run notes: the bar is spent
            idx = 0;
            bar = 0;
            ++phrase;
            if (phrase % 4 == 0) nOn = 0;  // new section: re-roll the hook
        }
        return n;
    }

    if (bar == 2) {  // resolution: approach, slide down onto the root, ring
        if (idx == 0) {
            n.type = DemoNote::Attack;
            n.degree = scaleLen + lift + 1;
            n.steps16 = 4;
            ++idx;
        } else {
            n.type = DemoNote::Slide;  // the money slide: down onto the root
            n.degree = scaleLen + lift;
            n.steps16 = 12;            // ring out the rest of the bar
            idx = 0;
            bar = 3;                   // next call plays the turnaround
        }
        return n;
    }

    if (bar == 0 && idx == 0)  // phrase start: roll this phrase's answer shift
        shift1 = (int8_t)(xorshift(rng) % 3) - 1;

    // bars 0 and 1: the hook; bar 1 repeats its rhythm exactly (that IS the
    // hook) with the degrees a step away — question, then answer
    const uint8_t at = motif[idx];
    const uint8_t nextAt = (idx + 1 < nOn) ? motif[idx + 1] : 16;
    int deg = degs[idx] + lift + (bar == 1 ? shift1 : 0);
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
