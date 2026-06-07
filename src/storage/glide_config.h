// All user-adjustable state, with NVS persistence. "Nothing hardcoded":
// every parameter the synth or layout consumes lives here, is editable
// on-device, and survives reboot. Writes are debounced so a knob sweep
// doesn't hammer flash.
#pragma once
#include <cstdint>
#include "../dsp/params.h"
#include "../dsp/patches.h"
#include "../dsp/pitch.h"

namespace store {

using TiltRoute = dsp::TiltRoute;  // moved into dsp so patches carry it
using dsp::tiltRouteName;

struct GlideConfig {
    dsp::SynthParams synth;   // engine params (ADSR, glide, wave, filter...)
    dsp::Layout layout;       // key, scale, octave, row interval, lock

    bool stringMode = true;   // rows are mono "strings" with legato hand-off
                              // (guitar feel); off = free poly allocation
    bool octaveGlide = true;  // octave keys sweep held notes instead of jumping
    TiltRoute tiltRoute = TiltRoute::Cutoff;  // tilt is NEVER pitch bend
    float tiltDepth = 0.6f;   // 0..1 modulation depth
    float tiltCenter = 0.f;   // calibrated "flat" — wherever YOU hold it
    bool tiltOn = false;
    uint8_t currentPatch = 0; // active sound slot (fn+q..p)
    uint8_t jamRows = 0;      // 0=off, 1..2 bottom rows become tap-to-latch
                              // drones (-1 oct): the layering jam — backing
                              // rings underneath while you solo above
    uint16_t bendMs = 250;    // time to reach full bend range
    uint8_t bendRange = 2;    // semitones
    bool bootSound = true;
    bool seenIntro = false;
};

GlideConfig& get();

void begin();                 // load from NVS (or defaults on first boot)
void markDirty();             // schedule a debounced persist
void tick(uint32_t nowMs);    // call each frame; performs the deferred write
void persistNow();
void resetDefaults();         // restore + persist

// ---- sound slots (fn+q..p) ----------------------------------------------
// Each of the 10 slots is a factory patch plus an optional user override
// saved over it (fn+shift+q..p). Overrides are versioned NVS blobs: a
// firmware that changes SynthParams silently falls back to factory.
void applyPatch(int slot);             // load slot -> working sound + tilt
bool savePatch(int slot);              // working sound -> slot override
void clearOverride(int slot);          // back to factory
bool patchHasOverride(int slot);
const char* patchName(int slot);       // factory name

}  // namespace store
