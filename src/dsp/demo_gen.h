// Demo-mode melody generator: seeded, deterministic PHRASES built around ONE
// rhythmic hook per run — real music repeats itself. Each 4-bar phrase (locked
// to the chord loop) is: the hook, the hook answered a step away, a resolution
// bar that slides down onto the root and rings, a bar of air. Phrases cycle
// A A' B A, where B lifts the hook an octave — the arc of a solo. Slides carry
// the line: the instrument's whole point, on display.
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
    uint8_t motif[5];  // THE hook: onset positions (16ths), rolled once per seed
    int8_t degs[5];    // the hook's base degrees
    uint8_t nOn = 0;   // onsets in the hook; 0 = not rolled yet
    uint8_t bar = 0;   // 0,1 = hook bars, 2 = resolution, 3 = the rest bar
    uint8_t idx = 0;   // onset index within the bar
    uint8_t phrase = 0;// counts phrases; %4 picks the variant (A A' B A)
    int8_t shift1 = 0; // this phrase's answer shift (bar 1 sits a step away)

    void seed(uint32_t s) {
        rng = s ? s : 1;
        nOn = 0;
        bar = 0;
        idx = 0;
        phrase = 0;
    }
    DemoNote next(int scaleLen);
};

}  // namespace dsp
