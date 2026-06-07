// GLIDE dsp core — parameter and event types shared between the UI thread
// and the audio render thread. PURE C++: no Arduino/M5/IDF includes anywhere
// under dsp/. This is the porting boundary for future dedicated hardware.
#pragma once
#include <cstdint>

namespace dsp {

constexpr int kMaxVoices = 8;

enum class Waveform : uint8_t { Sine, Triangle, Saw, Square, FatSaw, Count };
enum class GlideMode : uint8_t {
    LegatoOnly,  // glide only when notes overlap — the skill-gap default
    Always,      // every transition portamentos (dreamier)
    Count
};

inline const char* waveformName(Waveform w) {
    switch (w) {
        case Waveform::Sine:     return "sine";
        case Waveform::Triangle: return "tri";
        case Waveform::Saw:      return "saw";
        case Waveform::Square:   return "sqr";
        case Waveform::FatSaw:   return "fat";
        default:                 return "?";
    }
}

// Everything the audio thread needs each block. POD and trivially copyable:
// the UI writes the inactive copy of a double buffer and flips an atomic
// index, so the render thread always sees a coherent set.
struct SynthParams {
    Waveform  wave       = Waveform::Saw;
    GlideMode glideMode  = GlideMode::LegatoOnly;
    float attackS  = 0.005f;
    float decayS   = 0.12f;
    float sustain  = 0.70f;   // 0..1
    float releaseS = 0.25f;
    float glideS   = 0.12f;   // THE core parameter — portamento time
    float cutoffHz = 4000.f;
    float resonance = 0.30f;  // 0..0.95
    float masterVol = 0.70f;  // 0..1
    float detuneCents = 12.f; // fat-saw spread
    uint8_t voiceCount = 6;   // held-voice cap (1..kMaxVoices)

    // live modulation, pre-summed by the UI thread each frame
    float bendCents    = 0.f;  // bend keys, ramped by UI
    float vibratoCents = 0.f;  // tilt->vibrato depth (0 = off)
    float cutoffModOct = 0.f;  // tilt->cutoff offset in octaves (-2..+2)
    float volMod       = 1.f;  // tilt->volume multiplier (0.25..1)
};

struct NoteEvent {
    enum Type : uint8_t {
        On,        // fresh note (legato=false) or lane hand-off (legato=true)
        Off,       // release by id
        Retarget,  // re-aim a sounding note's pitch with an explicit glide
        AllOff     // panic
    };
    Type    type   = On;
    uint8_t id     = 0;     // physical key code — identity for Off/Retarget
    uint8_t lane   = 0xFF;  // grid row ("string") 0..3, 0xFF = no lane
    bool    legato = false; // On: hand the lane's sounding voice this id+pitch
    float   pitchMidi = 60.f;  // fractional MIDI note number

    // C++11-safe construction helper (NoteEvent has default member
    // initializers, so aggregate init isn't portable to the device std)
    static NoteEvent make(Type t, uint8_t id, uint8_t lane = 0xFF, bool legato = false,
                          float pitch = 60.f) {
        NoteEvent e;
        e.type = t;
        e.id = id;
        e.lane = lane;
        e.legato = legato;
        e.pitchMidi = pitch;
        return e;
    }
};

}  // namespace dsp
