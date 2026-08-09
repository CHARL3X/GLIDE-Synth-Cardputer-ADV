// The idle screensaver: a resting oscilloscope. After a long hands-off spell
// the perform screen hands the frame to this — a mostly-black field with a slow
// phosphor Lissajous tracing itself, the instrument's identity whispered dim
// underneath. It reads the live voice, so if a loop or jam is still running the
// figure breathes with the sound; otherwise it self-animates, morphing forever.
//
// Draws into the perform screen's full-frame canvas (no allocation, a handful of
// floats of state), so it costs nothing when idle and nothing when asleep.
#pragma once
#include <cstdint>

class M5Canvas;

namespace screensaver {

void reset();                             // restart the animation cleanly on entry
void draw(M5Canvas& c, uint32_t nowMs);   // render one frame into the full canvas

}  // namespace screensaver
