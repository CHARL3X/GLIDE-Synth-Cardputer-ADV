// The loop pedal. Records the live NoteEvent stream (a performance, not
// audio) and replays it as a protected backing layer: playback notes carry
// id+128 / lane+4 / backing=true, so they can never collide with the live
// hands, never count against the voice cap, never get stolen, and survive
// sound switches — record a CELLO bassline, flip to LEAD, solo over it.
// Playback is scheduled onto the audio thread (audio::pushEventAt), so loop
// timing is block-accurate (~4 ms), not UI-frame-accurate (~33 ms).
//
//   alt tap  : record -> play (closes the loop) -> overdub -> play -> ...
//   alt hold : clear
//   panic    : silences playback, keeps the take (alt tap plays it again)
#pragma once
#include <cstdint>
#include "../dsp/params.h"

namespace looper {

enum class State : uint8_t { Empty, Recording, Playing, Overdub, Stopped };

// Feed every live note event through here right after audio::pushEvent.
// Ignores everything that isn't loop content (drones, backing, non-note
// types, Offs/Retargets for notes it never saw the On of).
void record(const dsp::NoteEvent& ev);

State tap(uint32_t nowMs);   // the pedal press; returns the new state
void clear();                // stop playback and erase the take
void stop();                 // panic: silence playback, keep the take
void tick(uint32_t nowMs);   // call every UI frame; schedules due playback

State state();
uint32_t lengthMs();
uint32_t positionMs(uint32_t nowMs);  // 0..lengthMs while playing/overdubbing
bool overflowed();                    // the take hit the event-buffer cap

}  // namespace looper
