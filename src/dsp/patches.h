// The factory sound bank: ten instruments, not ten waveforms. Each patch is
// a complete personality — oscillator recipe, envelope shape, filter
// behavior, glide feel, AND its tilt response. Selected live with fn+q..p;
// fn+shift+q..p saves your tweaked version over the slot. Pure C++.
#pragma once
#include "params.h"

namespace dsp {

constexpr int kPatchCount = 10;

struct Patch {
    char name[8];        // shown in the status bar and HUD
    SynthParams synth;
    TiltRoute tiltRoute;
    float tiltDepth;     // 0..1
};

// Built imperatively (C++11: no designated initializers). Each starts from
// the neutral SynthParams defaults and states only its character.
const Patch* factoryPatches();

}  // namespace dsp
