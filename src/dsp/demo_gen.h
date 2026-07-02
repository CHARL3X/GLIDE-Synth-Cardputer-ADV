// Demo-mode melody generator: seeded, deterministic PHRASES, not a random
// walk. Each phrase is four bars locked to the chord loop: a one-bar rhythmic
// motif, the same motif repeated (repetition is what makes it music), a
// resolution bar that slides down onto the root and rings, then a bar of air.
// Slides carry the line — the instrument's whole point, on display.
// Pure C++, host-tested. The driver maps degrees to pitch with the live
// Layout (always in key) and 16th-steps to ms with the jam tempo.
#pragma once
#include <cstdint>

namespace dsp {

struct DemoNote {
    enum Type : uint8_t { Rest, Attack, Slide };
    Type type;
    int degree;      // scale-degree index (0 = low root, scaleLen = mid root)
    uint8_t steps16; // duration in 16th-notes (16 = one 4/4 bar)
};

struct DemoMelody {
    uint32_t rng = 1;
    uint8_t motif[5];  // onset positions (16ths) of the one-bar motif
    int8_t degs[5];    // the motif's degrees (bar 1 replays them shifted)
    uint8_t nOn = 0;   // onsets in the motif; 0 = roll a new phrase
    uint8_t bar = 0;   // 0,1 = motif bars, 2 = resolution, then the rest bar
    uint8_t idx = 0;   // onset index within the bar
    int8_t shift1 = 0; // bar 1's degree shift (the "answer" sits a step away)

    void seed(uint32_t s) {
        rng = s ? s : 1;
        nOn = 0;
        bar = 0;
        idx = 0;
    }
    DemoNote next(int scaleLen);
};

}  // namespace dsp
