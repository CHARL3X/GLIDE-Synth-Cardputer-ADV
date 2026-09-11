// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// GLIDE Unit JoyStick2 probe (pio run -e joystick-probe -t upload).
//
// Standalone test rig, NOT part of the instrument build: reads M5Stack's
// Hall-effect "Unit JoyStick2" (STM32G030, I2C @0x63) over Port.A and draws
// a live crosshair + button state, and drives the unit's own WS2812 LED back
// from the stick position so both read and write paths get exercised.
//
// Register map (Unit JoyStick2, addr 0x63):
//   0x20        button, 1 byte: 1 = released, 0 = pressed
//   0x30/31/32  RGB LED, 1 byte each: B, G, R (write)
//   0x50..0x53  X-L,X-H,Y-L,Y-H: signed 16-bit centered offset, -4095..4095
//   0xFE        firmware version, 1 byte
//
// Safe unplugged: every I2C call is a bounded start/stop transaction (no
// blocking retry loop), so a probe with nothing on Port.A just reports
// NOT CONNECTED and keeps polling at a slow backoff instead of hanging.
#include <M5Cardputer.h>

namespace {

constexpr uint8_t kAddr = 0x63;
constexpr uint32_t kFreq = 100000;
constexpr uint8_t kRegButton = 0x20;
constexpr uint8_t kRegRgbB = 0x30;
constexpr uint8_t kRegRgbG = 0x31;
constexpr uint8_t kRegRgbR = 0x32;
constexpr uint8_t kRegXY = 0x50;  // 4 bytes: X-L X-H Y-L Y-H
constexpr uint8_t kRegVersion = 0xFE;

constexpr uint32_t kRetryMs = 500;  // reconnect probe cadence while absent

M5Canvas gCanvas(&M5Cardputer.Display);

bool gConnected = false;
uint32_t gLastAttempt = 0;
int16_t gX = 0, gY = 0;
bool gButtonDown = false, gButtonWasDown = false;
uint32_t gPressCount = 0;
uint8_t gFwVersion = 0;

int16_t toInt16(uint8_t lo, uint8_t hi) { return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8)); }

uint8_t clampByte(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

// Push the stick's own reading back to its LED — cheap two-way proof the
// I2C link works both directions, not just read.
void updateLed() {
    uint8_t r = clampByte(128 + (gX * 127) / 4095);
    uint8_t g = clampByte(128 + (gY * 127) / 4095);
    uint8_t b = 60;
    if (gButtonDown) { r = g = b = 255; }
    auto& i2c = M5Cardputer.Ex_I2C;
    i2c.writeRegister8(kAddr, kRegRgbB, b, kFreq);
    i2c.writeRegister8(kAddr, kRegRgbG, g, kFreq);
    i2c.writeRegister8(kAddr, kRegRgbR, r, kFreq);
}

// One poll attempt. Returns false immediately (no blocking retry) if the
// unit doesn't answer, so the caller can fall back to a slow reconnect scan.
bool pollOnce() {
    auto& i2c = M5Cardputer.Ex_I2C;
    uint8_t xy[4];
    if (!i2c.readRegister(kAddr, kRegXY, xy, sizeof xy, kFreq)) return false;
    uint8_t btn;
    if (!i2c.readRegister(kAddr, kRegButton, &btn, 1, kFreq)) return false;

    gX = toInt16(xy[0], xy[1]);
    gY = toInt16(xy[2], xy[3]);
    gButtonWasDown = gButtonDown;
    gButtonDown = (btn == 0);
    if (gButtonDown && !gButtonWasDown) gPressCount++;
    return true;
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    Serial.begin(115200);

    M5Cardputer.Ex_I2C.begin();  // Port.A pins already mapped by board table
    gCanvas.createSprite(240, 135);
}

void loop() {
    M5Cardputer.update();

    uint32_t now = millis();
    if (!gConnected) {
        if (now - gLastAttempt >= kRetryMs) {
            gLastAttempt = now;
            if (pollOnce()) {
                gConnected = true;
                gFwVersion = M5Cardputer.Ex_I2C.readRegister8(kAddr, kRegVersion, kFreq);
            }
        }
    } else {
        if (!pollOnce()) {
            gConnected = false;
            gX = gY = 0;
            gButtonDown = false;
        }
    }

    gCanvas.fillScreen(TFT_BLACK);
    gCanvas.setFont(&fonts::Font0);
    gCanvas.setTextColor(0xFD60, TFT_BLACK);
    gCanvas.drawString("GLIDE - UNIT JOYSTICK2 PROBE", 4, 3);

    char buf[64];
    if (!gConnected) {
        gCanvas.setTextColor(TFT_RED, TFT_BLACK);
        gCanvas.setFont(&fonts::Font2);
        gCanvas.drawString("NOT CONNECTED", 4, 24);
        gCanvas.setFont(&fonts::Font0);
        gCanvas.setTextColor(0x6B4D, TFT_BLACK);
        gCanvas.drawString("plug into Port.A (Grove) - retrying...", 4, 44);
    } else {
        updateLed();

        gCanvas.setTextColor(0x07E0, TFT_BLACK);
        snprintf(buf, sizeof buf, "connected  fw:%u  presses:%lu", gFwVersion,
                 (unsigned long)gPressCount);
        gCanvas.drawString(buf, 4, 16);

        gCanvas.setTextColor(0xEF7D, TFT_BLACK);
        snprintf(buf, sizeof buf, "x:%6d  y:%6d  btn:%s", gX, gY,
                 gButtonDown ? "DOWN" : "up  ");
        gCanvas.drawString(buf, 4, 28);

        // crosshair: 80x80 box, dot position scaled from -4095..4095
        const int bx = 140, by = 24, bw = 80, bh = 80;
        gCanvas.drawRect(bx, by, bw, bh, 0x4208);
        gCanvas.drawFastHLine(bx, by + bh / 2, bw, 0x2104);
        gCanvas.drawFastVLine(bx + bw / 2, by, bh, 0x2104);
        int px = bx + bw / 2 + (gX * (bw / 2 - 4)) / 4095;
        int py = by + bh / 2 - (gY * (bh / 2 - 4)) / 4095;
        gCanvas.fillCircle(px, py, gButtonDown ? 6 : 4, gButtonDown ? TFT_WHITE : 0x07FF);

        gCanvas.setTextColor(0x6B4D, TFT_BLACK);
        gCanvas.drawString("LED mirrors stick position + click", 4, 100);
    }

    gCanvas.setTextColor(0x6B4D, TFT_BLACK);
    gCanvas.drawString("G0: reserved   ESC: reserved", 4, 124);
    gCanvas.pushSprite(0, 0);

    delay(33);
}
