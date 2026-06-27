// The factory sound bank: ten instruments, not ten waveforms. Each patch is
// a complete personality — oscillator recipe, envelope shape, filter
// behavior, glide feel, AND its tilt response. Selected live with fn+q..p;
// fn+shift+q..p saves your tweaked version over the slot. Pure C++.
#pragma once
#include "params.h"

namespace dsp {

constexpr int kPatchCount = 10;

// The bank is CURATED, not random: q (slot 0) is GLIDE, w (slot 1) is ACID,
// and slots 2..kFirstGenSlot-1 are hand-picked preset instruments. Only the
// slots from kFirstGenSlot up are the per-device generative slots — they ignore
// their factory definition below (which is just a fallback) and are regenerated
// from the unit's seed on demand (see glide_config loadPatchData). So o,p are
// the unique-per-device "roll" slots; everything else is the same on every unit.
constexpr int kFirstGenSlot = 8;  // o,p (8,9) are generative; q..i are curated

struct Patch {
    char name[16];       // shown in the status bar and HUD (room for the SD
                         // preset names, e.g. "Fatter square")
    SynthParams synth;
    TiltRoute tiltRoute;       // axis A (forward/back) route
    float tiltDepth;           // axis A depth, 0..1
    // Axis B (left/right roll). Defaulted so a patch that states no roll
    // personality leaves the roll axis inert (Off) — dual mode then does
    // nothing on that sound until the player assigns a route in settings.
    TiltRoute tiltRouteB = TiltRoute::Off;
    float tiltDepthB = 0.6f;   // axis B depth, 0..1
};

// Built imperatively (C++11: no designated initializers). Each starts from
// the neutral SynthParams defaults and states only its character.
const Patch* factoryPatches();

}  // namespace dsp
