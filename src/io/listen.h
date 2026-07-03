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

// Records mono int16 at kRateHz in ROUNDS of ~3 s (shorter rounds if heap
// is tight — down to 1.5 s), up to ~9 s total. After each round,
// segment(user, samples, n) analyzes the audio and returns true to keep
// listening or false when it has heard enough — a 3 s capture can land on
// one chord and name ITS key, so the caller accumulates evidence and stops
// early only once it's confident. progress(user, frac) runs every ~100 ms
// chunk (frac spans the 9 s maximum); return false from it to cancel.
// Audio is always resumed before this returns, except on ResumeFailed.
Result capture(bool (*progress)(void* user, float frac), void* user,
               bool (*segment)(void* user, const int16_t* mono, int n));

}  // namespace listen
