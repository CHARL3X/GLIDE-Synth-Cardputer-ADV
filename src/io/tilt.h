// Optional motion modulation from the BMI270. Deliberately humble: tilt is
// an assignable effects modulator (cutoff / vibrato / volume) that defaults
// to OFF and can be ignored entirely. It is NEVER pitch bend — that was
// rejected out loud ("fuck, I gotta lean it over again").
#pragma once

namespace tilt {

void begin();
bool available();
void poll();      // call once per UI frame
float value();    // smoothed -1..+1
}  // namespace tilt
