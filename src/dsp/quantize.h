// dsp/quantize.h — pure C++. Snap a raw loop length to the jam clock.
// mode: 0 = off, 1 = beat, 2 = bar (4 beats flat — the progression's *Chord
// length* is a different axis and is deliberately not coupled here). Snaps to
// the NEAREST unit, minimum one unit — a tap 40% into bar 1 was "meant" as a
// 1-bar loop. This is the single source of the rounding rule (host-tested);
// io/looper.cpp only applies the result.
#pragma once
#include <cstdint>

namespace dsp {

inline uint32_t quantizeLoopMs(uint32_t rawMs, float bpm, uint8_t mode) {
    if (mode == 0 || bpm <= 0.f) return rawMs;
    const float unit = (60000.f / bpm) * (mode == 2 ? 4.f : 1.f);
    uint32_t n = (uint32_t)((float)rawMs / unit + 0.5f);
    if (n < 1) n = 1;
    return (uint32_t)((float)n * unit + 0.5f);
}

}  // namespace dsp
