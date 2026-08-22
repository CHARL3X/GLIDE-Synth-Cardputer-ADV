// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "sound_card.h"

#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../dsp/sound_gen.h"  // Archetype + archetypeName for the roll tag
#include "../storage/glide_config.h"
#include "sound_viz.h"
#include "theme.h"

namespace soundcard {

namespace {
uint32_t gUntil = 0;
uint32_t gShownAt = 0;
int gTagArch = -1;  // rolled-character tag (dsp::Archetype); -1 = none

// card geometry — sized to sit inside the scope area, hint line stays clear
constexpr int kW = 204, kH = 88;
constexpr int kX = (240 - kW) / 2, kY = 22;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// One hue per character, chosen to evoke it (acid = acid green, brass = gold,
// organ = the church violet, wobble = dub green...) and to keep neighbours in
// the roll pool tellable apart — the word does the naming, the colour lets a
// heavy roller read provenance at a glance. Indexed by dsp::Archetype; these
// are dark-ground (phosphor) values, dimmed to ink on the paper palette.
constexpr uint16_t kArchColor[(int)dsp::Archetype::Count] = {
    rgb(255, 150, 40),   // pluck  - orange
    rgb(200, 235, 255),  // bell   - ice
    rgb(205, 160, 255),  // pad    - lavender
    rgb(255, 80, 70),    // bass   - red
    rgb(180, 255, 40),   // acid   - acid green
    rgb(40, 200, 255),   // lead   - cyan
    rgb(255, 210, 70),   // brass  - gold
    rgb(255, 90, 220),   // chip   - magenta
    rgb(235, 235, 235),  // wild   - white
    rgb(170, 255, 210),  // whistle- mint (air)
    rgb(150, 110, 255),  // organ  - violet
    rgb(255, 200, 160),  // keys   - cream (felt)
    rgb(50, 220, 120),   // wobble - dub green
    rgb(255, 120, 140),  // strings- rosin rose
};
}  // namespace

void show(uint32_t holdMs) {
    gShownAt = millis();
    gUntil = gShownAt + holdMs;
    gTagArch = -1;  // only a fresh roll carries a character tag
}

void showRolled(uint8_t archetype, uint32_t holdMs) {
    show(holdMs);
    if (archetype < (uint8_t)dsp::Archetype::Count) gTagArch = archetype;
}

void dismiss() { gUntil = 0; }

bool active(uint32_t nowMs) { return nowMs < gUntil; }

void draw(M5Canvas& c, uint32_t nowMs) {
    if (!active(nowMs)) return;

    // fade toward background over the last 250 ms, like the HUD
    const uint32_t remain = gUntil - nowMs;
    const uint8_t fade = remain < 250 ? (uint8_t)(255 - remain * 255 / 250) : 0;
    const uint16_t panel = theme::blend(theme::kPanel, theme::kBg, fade);
    const uint16_t frame = theme::blend(theme::kAmber, theme::kBg, fade);
    const uint16_t hot   = theme::blend(theme::kGreen, theme::kBg, fade);
    const uint16_t txt   = theme::blend(theme::kIdle, theme::kBg, fade);
    const uint16_t dim   = theme::blend(theme::kDim, theme::kBg, fade);

    const auto& g = store::get();
    const auto& s = g.synth;

    c.fillRoundRect(kX, kY, kW, kH, 5, panel);
    c.drawRoundRect(kX, kY, kW, kH, 5, frame);

    // header: the sound's name (exactly what Save writes) + unsaved-edit star
    char buf[32];
    snprintf(buf, sizeof buf, "%s%s", store::liveName(), store::liveDirty() ? "*" : "");
    c.setFont(&fonts::Font2);
    c.setTextDatum(top_left);
    // the right zone must fit the SOLO badge and/or the rolled-character tag
    while (buf[0] && c.textWidth(buf) > kW - (gTagArch >= 0 ? 96 : 60)) buf[strlen(buf) - 1] = '\0';
    c.setTextColor(frame, panel);
    c.drawString(buf, kX + 8, kY + 3);
    if (store::backingLocked()) {  // the bed holds its own sound — this is the solo
        c.setFont(&fonts::Font0);
        c.setTextDatum(top_right);
        c.setTextColor(theme::blend(theme::kSteel, theme::kBg, fade), panel);
        c.drawString("SOLO", kX + kW - 8, kY + 6);
        c.setTextDatum(top_left);
    }
    if (gTagArch >= 0) {  // the character this roll committed to — left of SOLO
        // family colour, dimmed to ink on a light (paper) ground, and fading
        // toward the ground with the card like every other element
        uint16_t tagC = kArchColor[gTagArch];
        if (!theme::darkGround()) tagC = theme::scale(tagC, 120);
        c.setFont(&fonts::Font0);
        c.setTextDatum(top_right);
        c.setTextColor(theme::blend(tagC, theme::kBg, fade), panel);
        c.drawString(dsp::archetypeName((dsp::Archetype)gTagArch),
                     kX + kW - (store::backingLocked() ? 40 : 8), kY + 6);
        c.setTextDatum(top_left);
        c.setFont(&fonts::Font2);
    }

    // the face: wave | envelope | filter, labels underneath
    const int vy = kY + 24, vh = 24;
    viz::drawWave(c, kX + 8, vy, 56, vh, s.wave, hot);
    viz::drawEnv(c, kX + 74, vy, 62, vh, s.attackS, s.decayS, s.sustain, s.releaseS, hot);
    viz::drawFilter(c, kX + 146, vy, 50, vh, (dsp::FilterMode)s.filterMode, s.cutoffHz,
                    s.resonance, hot);

    c.setFont(&fonts::Font0);
    c.setTextColor(dim, panel);
    c.drawString(dsp::waveformName(s.wave), kX + 8, vy + vh + 3);
    c.drawString("env", kX + 74, vy + vh + 3);
    static const char* kModeShort[4] = {"LP", "HP", "BP", "NT"};
    if (s.cutoffHz >= 1000.f)
        snprintf(buf, sizeof buf, "%s %.1fk", kModeShort[s.filterMode & 3], s.cutoffHz / 1000.f);
    else
        snprintf(buf, sizeof buf, "%s %d", kModeShort[s.filterMode & 3], (int)s.cutoffHz);
    c.drawString(buf, kX + 146, vy + vh + 3);

    // the signature parameter gets the widest gauge
    const int gy = kY + 64;
    c.setTextColor(txt, panel);
    c.drawString("GLIDE", kX + 8, gy);
    viz::drawGauge(c, kX + 44, gy + 1, 110, 6, s.glideS / 2.f, frame);
    snprintf(buf, sizeof buf, "%dms", (int)(s.glideS * 1000));
    c.setTextColor(dim, panel);
    c.drawString(buf, kX + 160, gy);

    // the space: chorus / delay / reverb sends
    const int sy = kY + 76;
    struct Send { const char* tag; float v; };
    const Send sends[3] = {{"CHO", s.chorusDepth}, {"DLY", s.delayMix}, {"REV", s.reverbMix}};
    for (int i = 0; i < 3; ++i) {
        const int sx = kX + 8 + i * 66;
        c.setTextColor(dim, panel);
        c.drawString(sends[i].tag, sx, sy);
        viz::drawGauge(c, sx + 22, sy + 1, 36, 6, sends[i].v, frame);
    }
}

}  // namespace soundcard
