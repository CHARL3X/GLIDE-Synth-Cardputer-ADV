// The idle screensaver: the string at rest. After a long hands-off spell the
// perform screen hands the frame to this — a mostly-black field with a full-
// width standing wave holding still, a soft window of light sliding along it,
// the instrument's identity whispered dim underneath. It reads the live voice,
// so if a loop or jam is still running the wave answers it; in silence it
// simply breathes. The wave holds its shape on purpose: only the light moves.
//
// Draws into the perform screen's full-frame canvas (no allocation, a handful of
// floats of state), so it costs nothing when idle and nothing when asleep.
#pragma once
#include <M5Cardputer.h>

#include <cstdint>

namespace screensaver {

void reset();                             // restart the animation cleanly on entry
void draw(M5Canvas& c, uint32_t nowMs);   // render one frame into the full canvas

}  // namespace screensaver
