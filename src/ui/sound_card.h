// The sound identity card: a transient panel giving the live sound a face —
// name, waveform, envelope, filter and sends, drawn with the shared viz
// primitives. Fired on sound switches, generative rolls and SD previews, so a
// sound you just summoned is seen as well as heard. HUD-style: timed, fading,
// never blocking play.
#pragma once
#include <M5Cardputer.h>

namespace soundcard {

void show(uint32_t holdMs = 2600);  // default rides the audition phrase length
// Randomize's variant: also names the CHARACTER the roll committed to
// ("whistle", "acid", …) in the card's corner — the roll's provenance, which
// the sound's name alone can't tell you. Plain show() clears the tag, so a
// mutate/load/init never wears a stale character.
void showRolled(const char* archetype, uint32_t holdMs = 2600);
void dismiss();                     // e.g. the player started playing — yield
bool active(uint32_t nowMs);
void draw(M5Canvas& c, uint32_t nowMs);  // reads the live sound from store::

}  // namespace soundcard
