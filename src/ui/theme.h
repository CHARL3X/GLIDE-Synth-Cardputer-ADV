// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// GLIDE visual identity: dark machined panel, amber annunciators, phosphor
// green for anything alive (the trace, held keys). Cassette futurism —
// it should read as test equipment that learned to sing.
//
// The identity is now a set of ROLES, not fixed colors: kGreen is "primary /
// live", kAmber is "accent / annunciator", kIdle is "hot / bright text",
// kDim/kLine/kPanel/kBg the grounds. setTheme() repoints every role at one of
// the preset palettes (PHOSPHOR is the original). The historical names stay so
// every call site reads unchanged.
#pragma once
#include <cstdint>

namespace theme {

// The role values the whole UI draws with. Runtime — repointed by setTheme().
extern uint16_t kBg;        // screen ground
extern uint16_t kPanel;     // near-ground panel fill
extern uint16_t kLine;      // faint rules / graticule
extern uint16_t kAmber;     // accent / annunciator
extern uint16_t kAmberDim;
extern uint16_t kGreen;     // primary / live
extern uint16_t kGreenDim;
extern uint16_t kIdle;      // hot / bright text
extern uint16_t kDim;       // secondary text
extern uint16_t kRed;       // failures are loud (red-family in every theme)
extern uint16_t kSteel;     // cool accent (the backing layer's colour)

int count();                     // number of preset palettes
const char* name(uint8_t idx);   // palette name for the settings row
void setTheme(uint8_t idx);      // clamps; repoints the roles above
uint8_t current();
bool darkGround();               // is kBg dark (phosphor) or light (paper)?

// Stack two exposure colours with the medium's own algebra: emitted LIGHT
// sums toward white on a dark ground; INK pools toward black on a light one.
// Additive math on paper saturated overlaps to white — a glowing white line
// on a printed page (measured; STRING on the paper theme).
uint16_t stack565(uint16_t a, uint16_t b);

inline uint16_t scale(uint16_t c, uint8_t f) {
    uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = (r * f) / 255;
    g = (g * f) / 255;
    b = (b * f) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

inline uint16_t blend(uint16_t a, uint16_t b, uint8_t t) {  // t=0 -> a
    const int rA = (a >> 11) & 0x1F, rB = (b >> 11) & 0x1F;
    const int gA = (a >> 5) & 0x3F, gB = (b >> 5) & 0x3F;
    const int bA = a & 0x1F, bB = b & 0x1F;
    const int r = (rA * (255 - t) + rB * t) / 255;
    const int g = (gA * (255 - t) + gB * t) / 255;
    const int bl = (bA * (255 - t) + bB * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Intensity that fades toward the theme GROUND, not toward black — the only
// dimming that stays correct on a light (paper) palette. New scope modes use
// this; scale() remains for the phosphor-style glow math of the originals.
inline uint16_t fade(uint16_t c, uint8_t f) { return blend(kBg, c, f); }

// Ordered-dither fade for glow ramps. RGB565's 32/64/32 levels posterise a
// smooth ramp into visible steps — worst exactly where a glow must be
// smoothest, in the faint tail, which is why halos read as thick lines with
// a hard outer edge. Perturbing the level by a Bayer threshold before
// quantising trades a stable 4x4 pattern (invisible at this pixel pitch) for
// ~16x the perceived level count. +-8 of 255 stays under one green step.
inline uint16_t fadeDither(uint16_t c, int f, int x, int y) {
    static const uint8_t kB[16] = {0, 8, 2, 10, 12, 4, 14, 6,
                                   3, 11, 1, 9, 15, 7, 13, 5};
    f += (int)kB[(y & 3) * 4 + (x & 3)] - 8;
    return fade(c, (uint8_t)(f < 0 ? 0 : (f > 255 ? 255 : f)));
}

// RGB565 saturating add. Exposure/glow layers are EMITTED LIGHT, which sums —
// where they land on the same pixel they must brighten toward white, never
// let the last color win (five stacked exposures read as mud otherwise).
inline uint16_t addSat565(uint16_t a, uint16_t b) {
    uint16_t r = (uint16_t)(((a >> 11) & 0x1F) + ((b >> 11) & 0x1F));
    uint16_t g = (uint16_t)(((a >> 5) & 0x3F) + ((b >> 5) & 0x3F));
    uint16_t l = (uint16_t)((a & 0x1F) + (b & 0x1F));
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (l > 31) l = 31;
    return (uint16_t)((r << 11) | (g << 5) | l);
}

}  // namespace theme
