// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "joystick.h"
#ifdef GLIDE_JOYSTICK

#include <M5Cardputer.h>

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
constexpr float kLedSmooth = 0.12f; // per-poll EMA factor — a mode switch or a
                                     // hard flick to the corner glides over a
                                     // handful of frames instead of snapping

bool gAvailable = false;
uint32_t gLastAttempt = 0;
float gX = 0.f, gY = 0.f;       // live, normalized -1..1 — never frozen
bool gHeld = false;
bool gPressedEdge = false;      // consumed (and cleared) by pressed()
float gLedR = 0.f, gLedG = 0.f, gLedB = 0.f;  // smoothed, last-WRITTEN color

int16_t toInt16(uint8_t lo, uint8_t hi) { return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8)); }

// One poll attempt: two bounded I2C transactions, no blocking retry. Returns
// false immediately if the unit doesn't answer (unplugged, or never was).
bool pollOnce() {
    auto& i2c = M5Cardputer.Ex_I2C;
    uint8_t xy[4];
    if (!i2c.readRegister(kAddr, kRegXY, xy, sizeof xy, kFreq)) return false;
    uint8_t btn;
    if (!i2c.readRegister(kAddr, kRegButton, &btn, 1, kFreq)) return false;

    const int16_t rx = toInt16(xy[0], xy[1]);
    const int16_t ry = toInt16(xy[2], xy[3]);
    gX = (float)rx * (1.f / 4095.f);
    gY = (float)ry * (1.f / 4095.f);
    if (gX > 1.f) gX = 1.f; else if (gX < -1.f) gX = -1.f;
    if (gY > 1.f) gY = 1.f; else if (gY < -1.f) gY = -1.f;

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
    if (!gAvailable) {
        if (now - gLastAttempt < kRetryMs) return;
        gLastAttempt = now;
        gAvailable = pollOnce();
        return;
    }
    if (!pollOnce()) {
        gAvailable = false;
        gX = gY = 0.f;
        gHeld = false;
    }
}

float x() { return gX; }
float y() { return gY; }
bool held() { return gHeld; }
bool pressed() {
    if (!gPressedEdge) return false;
    gPressedEdge = false;
    return true;
}

void setLed(float r, float g, float b) {
    if (!gAvailable) return;  // no unit to write to — and no point burning the
                               // smoothing state chasing a color no one sees
    gLedR += (r - gLedR) * kLedSmooth;
    gLedG += (g - gLedG) * kLedSmooth;
    gLedB += (b - gLedB) * kLedSmooth;
    auto to255 = [](float v) -> uint8_t {
        if (v < 0.f) v = 0.f; else if (v > 1.f) v = 1.f;
        return (uint8_t)(v * 255.f + 0.5f);
    };
    auto& i2c = M5Cardputer.Ex_I2C;
    i2c.writeRegister8(kAddr, kRegRgbB, to255(gLedB), kFreq);
    i2c.writeRegister8(kAddr, kRegRgbG, to255(gLedG), kFreq);
    i2c.writeRegister8(kAddr, kRegRgbR, to255(gLedR), kFreq);
}

}  // namespace joystick

#endif  // GLIDE_JOYSTICK
