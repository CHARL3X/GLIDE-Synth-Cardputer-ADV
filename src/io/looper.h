// The loop pedal. Records the live NoteEvent stream (a performance, not
// audio) and replays it as a protected backing layer: playback notes carry
// id+128 / lane+4 / backing=true, so they can never collide with the live
// hands, never count against the voice cap, never get stolen, and survive
// sound switches — record a CELLO bassline, flip to LEAD, solo over it.
// Playback is scheduled onto the audio thread (audio::pushEventAt), so loop
// timing is block-accurate (~4 ms), not UI-frame-accurate (~33 ms).
//
//   alt tap   : record -> play (closes the loop) -> overdub -> play -> ...
//   alt hold  : clear everything
//   fn + alt  : peel the last overdub layer (undo); repeating climbs back up
//               (redo). The base loop is protected — never peeled.
//   panic     : silences playback, keeps the take (alt tap plays it again)
#pragma once
#include <cstdint>
#include "../dsp/params.h"

namespace looper {

enum class State : uint8_t { Empty, Recording, Playing, Overdub, Stopped };

// Feed every live note event through here right after audio::pushEvent.
// Ignores everything that isn't loop content (drones, backing, non-note
// types, Offs/Retargets for notes it never saw the On of).
void record(const dsp::NoteEvent& ev);

// The pedal press; returns the new state. bpm/snapMode matter only on the tap
// that CLOSES a recording: the raw take length snaps to the jam clock via
// dsp::quantizeLoopMs (snapMode: 0 off, 1 beat, 2 bar), so the loop and the
// progression share one clock instead of drifting apart.
State tap(uint32_t nowMs, float bpm, uint8_t snapMode);
int peel(uint32_t nowMs);    // hold: undo/redo a layer. 0 = nothing to do,
                             // 1 = peeled a layer (undo), 2 = restored (redo)
void clear();                // erase the take entirely (fn+alt)
void stop();                 // panic: silence playback, keep the take
void tick(uint32_t nowMs);   // call every UI frame; schedules due playback

State state();
uint32_t lengthMs();
uint32_t positionMs(uint32_t nowMs);  // 0..lengthMs while playing/overdubbing
int liveLayers();                     // audible overdub layers above the base
int topLayers();                      // overdub layers recorded (>= liveLayers)
bool overflowed();                    // the take hit the event-buffer cap

}  // namespace looper
