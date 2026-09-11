// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Optional modulation from M5Stack's Unit JoyStick2 (Hall-effect stick, I2C
// @0x63 over Port.A). PERSONAL BUILD ONLY — gated on GLIDE_JOYSTICK, which
// the public cardputer-adv env never defines, so this is dead code (and
// dead NVS keys, dead ModSource entries) in every shipped binary. See
// support/viz_render's sibling reasoning: verified on hardware first via
// src/joystick_probe.cpp before this ever touched the real synth.
//
// Deliberately humble like tilt: an assignable mod-matrix source (JoyX/JoyY
// in dsp/params.h) that does nothing if the unit is never plugged in, and
// nothing bad if it's unplugged mid-session — poll() just stops updating and
// available() drops, exactly like tilt losing its IMU.
//
// The click is NOT a hold-to-freeze gesture: mounted on the case back, it's
// too fiddly to hold down mid-phrase while playing two-handed. It's a plain
// button press instead, consumed once per press by perform_screen.cpp to
// cycle which destinations JoyX/JoyY drive (see applyJoystick) — a gesture
// you make between phrases, not one you ride during a note.
#pragma once
#ifdef GLIDE_JOYSTICK

namespace joystick {

void begin();       // configures Port.A I2C; safe even if nothing answers
bool available();    // true once the unit has answered at least one poll
void poll();         // call once per UI frame: cheap re-probe w/ backoff when
                      // absent, one two-transaction read when present
float x();           // -1..+1, 0 if centered/absent — always live, never frozen
float y();           // -1..+1, 0 if centered/absent
bool held();         // the stick's own click, physically down right now
bool pressed();      // one-frame rising edge of held() — the "click" gesture

// Drive the unit's own WS2812. r/g/b are 0..1 targets; internally EMA-
// smoothed toward them (see kLedSmooth) so a jumpy caller (raw stick
// position, a mode switch) still glides instead of snapping — call this
// every frame with whatever the CURRENT target color/brightness is, not
// just on change. A no-op while the unit is absent.
void setLed(float r, float g, float b);

}  // namespace joystick

#endif  // GLIDE_JOYSTICK
