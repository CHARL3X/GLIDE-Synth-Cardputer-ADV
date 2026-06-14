#include "led.h"

#include <Arduino.h>  // neopixelWrite()
#include <cmath>

#include "../config.h"

namespace led {

namespace {

// smoothed state, so fast playing doesn't strobe the LED
float gHue = 0.f;       // degrees, 0..360
float gVal = 0.f;       // 0..1 brightness envelope
float gSparkle = 0.f;   // 0..1 decaying white accent
uint8_t gLastR = 1, gLastG = 1, gLastB = 1;  // != 0 so the first dark write lands

// pitch class -> hue: C maps to red (0deg) and each semitone rotates 30deg
// around the wheel, so an octave is one full rotation and a glide paints a
// smooth color sweep.
float pitchHue(float midi) {
    float pc = fmodf(midi, 12.f);
    if (pc < 0.f) pc += 12.f;
    return pc * 30.f;
}

void hsv2rgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    const float c = v * s;
    const float hp = h / 60.f;
    const float x = c * (1.f - fabsf(fmodf(hp, 2.f) - 1.f));
    float rr = 0.f, gg = 0.f, bb = 0.f;
    if (hp < 1.f)      { rr = c; gg = x; }
    else if (hp < 2.f) { rr = x; gg = c; }
    else if (hp < 3.f) { gg = c; bb = x; }
    else if (hp < 4.f) { gg = x; bb = c; }
    else if (hp < 5.f) { rr = x; bb = c; }
    else               { rr = c; bb = x; }
    const float m = v - c;
    r = (uint8_t)((rr + m) * 255.f + 0.5f);
    g = (uint8_t)((gg + m) * 255.f + 0.5f);
    b = (uint8_t)((bb + m) * 255.f + 0.5f);
}

}  // namespace

void begin() {
    if (!cfg::kLedEnabled) return;
    // Stamp S3A: power the LED rail before any data reaches it.
    if (cfg::kLedPowerPin != 255) {
        pinMode(cfg::kLedPowerPin, OUTPUT);
        digitalWrite(cfg::kLedPowerPin, HIGH);
    }
    gHue = gVal = gSparkle = 0.f;
    neopixelWrite(cfg::kLedPin, 0, 0, 0);  // start dark
    gLastR = gLastG = gLastB = 0;
}

void update(bool active, float pitchMidi, float intensity, bool accent) {
    if (!cfg::kLedEnabled) return;

    // brightness envelope: snap up on a note, ease down on release — the LED
    // breathes with the voice instead of blinking on/off.
    const float tgt = active ? (intensity < 0.f ? 0.f : (intensity > 1.f ? 1.f : intensity)) : 0.f;
    gVal += (tgt - gVal) * (tgt > gVal ? 0.55f : 0.12f);

    if (active) {
        const float tgtHue = pitchHue(pitchMidi);
        float d = tgtHue - gHue;
        if (d > 180.f) d -= 360.f;
        else if (d < -180.f) d += 360.f;
        // snap to the new pitch's color when the LED was dark (a fresh note),
        // but glide the hue while it's already lit so slides sweep smoothly.
        gHue += d * (gVal < 0.12f ? 1.f : 0.18f);
        if (gHue < 0.f) gHue += 360.f;
        else if (gHue >= 360.f) gHue -= 360.f;
    }

    if (accent) gSparkle = 1.f;
    gSparkle *= 0.78f;  // ~0.5 s tail at 30 fps
    if (gSparkle < 0.02f) gSparkle = 0.f;

    // sparkle desaturates toward white and lifts brightness
    const float s = 1.f - gSparkle * 0.85f;
    float v = gVal + gSparkle * (1.f - gVal) * 0.6f;
    if (v > 1.f) v = 1.f;

    uint8_t r, g, b;
    hsv2rgb(gHue, s, v, r, g, b);
    const float scale = cfg::kLedMaxBright / 255.f;
    r = (uint8_t)(r * scale);
    g = (uint8_t)(g * scale);
    b = (uint8_t)(b * scale);

    // skip the RMT write when nothing changed (idle dark, mostly) so the LED
    // driver stays off the bus unless the color is actually moving
    if (r == gLastR && g == gLastG && b == gLastB) return;
    neopixelWrite(cfg::kLedPin, r, g, b);
    gLastR = r; gLastG = g; gLastB = b;
}

void off() {
    if (!cfg::kLedEnabled) return;
    neopixelWrite(cfg::kLedPin, 0, 0, 0);
    gLastR = gLastG = gLastB = 0;
}

}  // namespace led
