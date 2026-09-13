// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "joystick.h"
#ifdef GLIDE_JOYSTICK

#include <M5Cardputer.h>
#include <math.h>

namespace joystick {

namespace {

constexpr uint8_t kAddr = 0x63;
constexpr uint32_t kFreq = 100000;
constexpr uint8_t kRegButton = 0x20;
constexpr uint8_t kRegXY = 0x50;  // 4 bytes: X-L X-H Y-L Y-H, signed, -4095..4095
constexpr uint8_t kRegRgbB = 0x30;
constexpr uint8_t kRegRgbG = 0x31;
constexpr uint8_t kRegRgbR = 0x32;

constexpr uint32_t kRetryMs = 500;  // reconnect probe cadence while absent
constexpr uint32_t kWakeGapMs = 250;    // a polling gap this long gets a wake-up write
constexpr uint32_t kKeepaliveMs = 500;  // ...and one every this often regardless

// ---- feel tuning (see joystick.h) — every constant a hardware test may move
constexpr float kStretch     = 1.12f;  // full scale lands a little short of the
                                        // register's ±4095, so the rim is reachable
constexpr float kDeadzone    = 0.10f;  // radial, after centring
constexpr float kCurveLinear = 0.35f;  // out = lin*m + (1-lin)*m^3: gentle middle
constexpr float kRimAt       = 0.92f;  // shaped radius that counts as "at the rim"
constexpr float kRimRiseMs   = 700.f;  // hold at the rim this long -> push() = 1
constexpr float kRimFallMs   = 250.f;  // leave the rim -> push() empties this fast
constexpr float kGlideOutMs  = 55.f;   // time constant pushing out (flick = sweep)
constexpr float kGlideInMs   = 130.f;  // ...and coming home (a release, not a cut)
constexpr float kNormalReach = 0.70f;  // share of the effect the normal throw covers
constexpr float kZeroTrack   = 0.02f;  // per-poll centre drift correction, deadzone only

bool gAvailable = false;
uint32_t gLastAttempt = 0, gLastPoll = 0, gLastLedWrite = 0;
float gCx = 0.f, gCy = 0.f;     // tracked centre (raw units, -1..1)
float gX = 0.f, gY = 0.f;       // shaped, instant
float gGx = 0.f, gGy = 0.f;     // glided
float gPush = 0.f;
bool gHeld = false;
bool gPressedEdge = false;      // consumed (and cleared) by pressed()
bool gFreshConnect = true;      // next good read seeds the centre
bool gLedDirty = false;
uint8_t gLed[3] = {0, 0, 0};    // r g b, last requested

int16_t toInt16(uint8_t lo, uint8_t hi) { return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8)); }

void writeLed() {
    auto& i2c = M5Cardputer.Ex_I2C;
    i2c.writeRegister8(kAddr, kRegRgbB, gLed[2], kFreq);
    i2c.writeRegister8(kAddr, kRegRgbG, gLed[1], kFreq);
    i2c.writeRegister8(kAddr, kRegRgbR, gLed[0], kFreq);
    gLedDirty = false;
    gLastLedWrite = millis();
}

// One poll attempt: two bounded I2C transactions, no blocking retry. Returns
// false immediately if the unit doesn't answer (unplugged, or never was).
bool pollOnce(float dtMs) {
    auto& i2c = M5Cardputer.Ex_I2C;
    uint8_t xy[4];
    if (!i2c.readRegister(kAddr, kRegXY, xy, sizeof xy, kFreq)) return false;
    uint8_t btn;
    if (!i2c.readRegister(kAddr, kRegButton, &btn, 1, kFreq)) return false;

    float rx = (float)toInt16(xy[0], xy[1]) * (kStretch / 4095.f);
    float ry = (float)toInt16(xy[2], xy[3]) * (kStretch / 4095.f);

    // Centre: seeded on connect if the stick is plausibly at rest, then nudged
    // toward any reading that sits inside the deadzone — so a unit whose
    // spring settles a hair off zero stops leaning on the filter, while a
    // deliberate push (outside the deadzone) never drags the centre with it.
    if (gFreshConnect) {
        gFreshConnect = false;
        const bool resting = rx * rx + ry * ry < 0.2f * 0.2f;
        gCx = resting ? rx : 0.f;
        gCy = resting ? ry : 0.f;
    }
    float vx = rx - gCx, vy = ry - gCy;
    float mag = sqrtf(vx * vx + vy * vy);
    if (mag < kDeadzone) {
        gCx += vx * kZeroTrack;
        gCy += vy * kZeroTrack;
    }

    // Radial deadzone + curve, applied to the length so diagonals shape the
    // same as the axes (a per-axis curve squashes a diagonal toward the axes).
    float m = (mag - kDeadzone) / (1.f - kDeadzone);
    if (m < 0.f) m = 0.f;
    if (m > 1.f) m = 1.f;
    const float curved = kCurveLinear * m + (1.f - kCurveLinear) * m * m * m;
    const float k = mag > 1e-4f ? curved / mag : 0.f;
    gX = vx * k;
    gY = vy * k;

    // Rim push: grows while held at the rim, drains quickly once off it.
    if (m >= kRimAt) gPush += dtMs / kRimRiseMs;
    else             gPush -= dtMs / kRimFallMs;
    if (gPush < 0.f) gPush = 0.f;
    if (gPush > 1.f) gPush = 1.f;

    // Glide toward the shaped position: out fast, home a little slower.
    const bool outward = gX * gX + gY * gY > gGx * gGx + gGy * gGy;
    const float tau = outward ? kGlideOutMs : kGlideInMs;
    const float a = 1.f - exp(-dtMs / tau);
    gGx += (gX - gGx) * a;
    gGy += (gY - gGy) * a;

    const bool wasHeld = gHeld;
    gHeld = (btn == 0);  // 1 = released, 0 = pressed
    if (gHeld && !wasHeld) gPressedEdge = true;  // latched until pressed() reads it
    return true;
}

}  // namespace

void begin() {
    M5Cardputer.Ex_I2C.begin();  // Port.A pins already mapped by the board table
}

bool available() { return gAvailable; }

void poll() {
    const uint32_t now = millis();
    const uint32_t gapMs = now - gLastPoll;
    float dtMs = (float)gapMs;
    gLastPoll = now;
    if (dtMs > 100.f) dtMs = 100.f;  // a long modal away must not jump the glide
    // Wake-up write. Seen on hardware once the LED went write-on-change: after
    // a sound change or a settings trip (both leave the stick unpolled for a
    // while — a modal loop, or the 1.5-2 s flash save) the stick stopped
    // moving anything while its click still worked, and any one click revived
    // it. The click's only I2C difference is the mode's LED write, and the
    // earlier build that wrote the LED every frame never showed this. So:
    // rewrite the LED after any gap in polling, before reading, and as a slow
    // keepalive besides. Three register writes; nothing audible.
    if (gAvailable && (gapMs > kWakeGapMs || now - gLastLedWrite > kKeepaliveMs))
        gLedDirty = true;
    if (gAvailable && gLedDirty) writeLed();
    if (!gAvailable) {
        if (now - gLastAttempt < kRetryMs) return;
        gLastAttempt = now;
        gFreshConnect = true;
        gAvailable = pollOnce(dtMs);
        if (gAvailable) gLedDirty = true;  // a replugged unit boots dark
        return;
    }
    if (!pollOnce(dtMs)) {
        gAvailable = false;
        gX = gY = gGx = gGy = gPush = 0.f;
        gHeld = false;
        return;
    }
    if (gLedDirty) writeLed();  // a mode change requested this frame
}

float x() { return gX; }
float y() { return gY; }
float ex() {
    const float s = kNormalReach + (1.f - kNormalReach) * gPush;
    return gGx * s;
}
float ey() {
    const float s = kNormalReach + (1.f - kNormalReach) * gPush;
    return gGy * s;
}
float push() { return gPush; }
bool pressed() {
    if (!gPressedEdge) return false;
    gPressedEdge = false;
    return true;
}

void setLed(float r, float g, float b) {
    auto to255 = [](float v) -> uint8_t {
        if (v < 0.f) v = 0.f; else if (v > 1.f) v = 1.f;
        return (uint8_t)(v * 255.f + 0.5f);
    };
    const uint8_t c[3] = {to255(r), to255(g), to255(b)};
    if (c[0] == gLed[0] && c[1] == gLed[1] && c[2] == gLed[2]) return;
    gLed[0] = c[0]; gLed[1] = c[1]; gLed[2] = c[2];
    gLedDirty = true;  // poll() writes it while the unit is present
}

}  // namespace joystick

#endif  // GLIDE_JOYSTICK
