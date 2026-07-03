// LISTEN capture: the half-duplex handoff between the synth's speaker and
// the mic, in one place. Suspends the audio engine, records a few seconds
// of the room, restores the audio engine, and hands the samples out. The
// mic is optional equipment (SD-card spirit): every failure is reported,
// none of them dents the instrument — except a speaker that won't restart,
// which the caller must treat as fatal.
#pragma once
#include <cstdint>

namespace listen {

constexpr uint32_t kRateHz = 16000;  // key info lives well under Nyquist/2 here

enum class Result : uint8_t {
    Ok,
    Cancelled,     // player backed out; nothing changed
    NoMic,         // Mic.begin() failed / mic not present — audio restored
    AllocFailed,   // no heap for the capture buffer — audio never suspended
    ResumeFailed,  // Speaker.begin() failed after capture — FATAL, show it
};

// Records up to ~3 s of mono int16 at kRateHz (shorter if heap is tight —
// down to 1.5 s). progress(user, frac) runs every ~100 ms chunk; return
// false from it to cancel. On Ok, sink(user, samples, n) is called once
// with the capture before the buffer is freed. Audio is always resumed
// before this returns, except on ResumeFailed.
Result capture(bool (*progress)(void* user, float frac), void* user,
               void (*sink)(void* user, const int16_t* mono, int n));

}  // namespace listen
