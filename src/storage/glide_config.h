// All user-adjustable state, with NVS persistence. "Nothing hardcoded":
// every parameter the synth or layout consumes lives here, is editable
// on-device, and survives reboot. Writes are debounced so a knob sweep
// doesn't hammer flash.
#pragma once
#include <cstdint>
#include "../dsp/params.h"
#include "../dsp/pitch.h"

namespace store {

enum class TiltRoute : uint8_t { Off, Cutoff, Vibrato, Volume, Count };

inline const char* tiltRouteName(TiltRoute r) {
    switch (r) {
        case TiltRoute::Cutoff:  return "cutoff";
        case TiltRoute::Vibrato: return "vibrato";
        case TiltRoute::Volume:  return "volume";
        default:                 return "off";
    }
}

struct GlideConfig {
    dsp::SynthParams synth;   // engine params (ADSR, glide, wave, filter...)
    dsp::Layout layout;       // key, scale, octave, row interval, lock

    bool stringMode = true;   // rows are mono "strings" with legato hand-off
                              // (guitar feel); off = free poly allocation
    bool octaveGlide = true;  // octave keys sweep held notes instead of jumping
    TiltRoute tiltRoute = TiltRoute::Off;  // tilt is NEVER pitch bend
    bool tiltOn = false;
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

}  // namespace store
