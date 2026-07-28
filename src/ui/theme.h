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

}  // namespace theme
