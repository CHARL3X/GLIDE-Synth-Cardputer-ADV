// GLIDE visual identity: dark machined panel, amber annunciators, phosphor
// green for anything alive (the trace, held keys). Cassette futurism —
// it should read as test equipment that learned to sing.
#pragma once
#include <cstdint>

namespace theme {

constexpr uint16_t kBg       = 0x0000;  // black
constexpr uint16_t kPanel    = 0x10A2;  // near-black panel fill
constexpr uint16_t kLine     = 0x2104;  // faint rules / graticule
constexpr uint16_t kAmber    = 0xFD60;  // primary annunciator
constexpr uint16_t kAmberDim = 0x8B00;
constexpr uint16_t kGreen    = 0x07E0;  // phosphor trace / live
constexpr uint16_t kGreenDim = 0x03E0;
constexpr uint16_t kIdle     = 0xEF7D;  // bright text
constexpr uint16_t kDim      = 0x6B4D;  // secondary text
constexpr uint16_t kRed      = 0xF800;  // failures are loud
constexpr uint16_t kSteel    = 0x42BF;  // cool accent

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

}  // namespace theme
