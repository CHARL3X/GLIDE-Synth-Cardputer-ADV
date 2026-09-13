// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Optional expression from M5Stack's Unit JoyStick2 (Hall-effect stick, I2C
// @0x63 over Port.A). PERSONAL BUILD ONLY — gated on GLIDE_JOYSTICK, which
// the public cardputer-adv env never defines, so this is dead code (and
// dead NVS keys, dead ModSource entries) in every shipped binary. Verified on
// hardware first via src/joystick_probe.cpp before this ever touched the synth.
//
// Deliberately humble like tilt: does nothing if the unit is never plugged
// in, and nothing bad if it's unplugged mid-session — poll() stops updating
// and available() drops, exactly like tilt losing its IMU.
//
// THE FEEL LAYER. The stick has a few millimetres of travel under a finger on
// the case back, polled at ~30 fps: raw, a flick reaches the rim in one frame
// and every effect sounded like a switch (field notes, a week of play). So
// this layer hands out three views of the same stick:
//   x()/y()     shaped: auto-zeroed centre, radial deadzone, a curve that is
//               gentle near the middle. Instant — for thresholds (bend tiers,
//               gate rate) where lag would feel like a missed input.
//   ex()/ey()   expressive: shaped, then GLIDED (a flick becomes a quick
//               sweep, a slow push still tracks the finger) and scaled so the
//               normal throw covers kNormalReach of the effect; holding at the
//               rim creeps the rest of the way (push()). This is what effects
//               should ride.
//   push()      0..1, grows while the stick is held at the rim, falls off
//               quickly when it leaves. "Past the edge, keep going."
//
// The click is a plain rising-edge press, not a hold gesture: mounted on the
// case back it's too fiddly to hold mid-phrase two-handed. perform_screen.cpp
// consumes it to cycle the mode, a gesture you make between phrases.
#pragma once
#ifdef GLIDE_JOYSTICK

namespace joystick {

void begin();       // configures Port.A I2C; safe even if nothing answers
bool available();    // true once the unit has answered at least one poll
void poll();         // call once per UI frame: cheap re-probe w/ backoff when
                      // absent, one two-transaction read when present
float x();           // shaped, instant, -1..+1 (0 if centred/absent)
float y();           // shaped, instant, +1 = up
float ex();          // expressive: glided + rim-push scaled, -1..+1
float ey();
float push();        // rim-hold accumulator, 0..1
bool pressed();      // one-frame rising edge of the stick's click

// The unit's own WS2812. Written once on change (and again after a replug),
// never per frame — it faces away from the player on the case back, so it is
// a glance-when-you-flip-it indicator, not worth three I2C writes a frame.
void setLed(float r, float g, float b);

}  // namespace joystick

#endif  // GLIDE_JOYSTICK
