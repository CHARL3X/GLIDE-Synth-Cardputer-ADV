// The onboard RGB LED as a second readout. Hue follows the lead voice's pitch
// (a chromatic color wheel), brightness follows note activity, and a sparkle
// fires on fresh attacks and bends. Driven once per UI frame from the perform
// loop — NEVER the audio thread. Self-disables via cfg::kLedEnabled, so the
// call sites stay clean. Pin/order live in config.h (see the caveat there).
#pragma once

namespace led {

void begin();

// Per-frame update.
//   active    : a lead voice is currently sounding
//   pitchMidi : that voice's pitch — the hue source (slides sweep the color)
//   intensity : 0..1 target brightness while active
//   accent    : momentary white sparkle (new attack / pitch bend)
void update(bool active, float pitchMidi, float intensity, bool accent);

// Force dark — used on app exit so the LED doesn't freeze lit.
void off();

}  // namespace led
