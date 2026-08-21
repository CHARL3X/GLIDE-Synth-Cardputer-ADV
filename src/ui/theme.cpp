// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "theme.h"

namespace theme {

namespace {

struct Palette {
    const char* name;
    uint16_t bg, panel, line, amber, amberDim, green, greenDim, idle, dim, red, steel;
};

// Preset palettes. Index 0 is the original identity and the default; the rest
// keep the same role structure (live / accent / hot / grounds) so every screen
// stays legible in all of them. Order is append-only — the index persists in
// NVS ("themeid"), so a reorder would silently restyle existing devices.
constexpr Palette kPalettes[] = {
    // PHOSPHOR — the original: black panel, phosphor green, amber annunciators
    {"phosphor", 0x0000, 0x10A2, 0x2104, 0xFD60, 0x8B00, 0x07E0, 0x03E0, 0xEF7D,
     0x6B4D, 0xF800, 0x42BF},
    // CASSETTE — the SUPERCASSETTE label: mint-green ink on black stock,
    // hot-pink stripe accents, VHS cyan backing
    {"cassette", 0x0000, 0x10A2, 0x19E7, 0xFA70, 0x894A, 0x3710, 0x13C9,
     0xEFFE, 0x53CC, 0xF800, 0x4E9C},
    // FUSION — the COMPULSIVE dossier: teal-mint wireframe vs blood-red data,
    // black ground
    {"fusion", 0x0000, 0x0862, 0x1965, 0xE1E5, 0x7903, 0x3E94, 0x1B6A,
     0xE7FE, 0x4B8C, 0xF800, 0x95B5},
    // ANAGLYPH — the CENTAURI plate: hot pink vs cyan-blue stereo pair; the
    // STRING weave literally splits into its 3D channels here
    {"anaglyph", 0x0000, 0x100A, 0x2887, 0x3D1F, 0x1AD1, 0xF9F4, 0x88EB,
     0xFF3E, 0x7ACD, 0xF800, 0x6B7F},
    // ULTRAVIOLET — cyberpunk vector-pack purple: electric violet, hot-pink
    // accent, teal backing
    {"ultraviolet", 0x0822, 0x1046, 0x28AA, 0xF96F, 0x88C8, 0x89FF, 0x4111,
     0xF75F, 0x62B1, 0xF800, 0x2F3A},
    // NOSTALGIA — the VHS box: neon pink-red on deep navy, terminal-green
    // annotations
    {"nostalgia", 0x0843, 0x10A5, 0x294A, 0x2F2B, 0x1BC6, 0xF96B, 0x88E7,
     0xFEFC, 0x6331, 0xF800, 0x4DBF},
    // ACID — the dither-pattern plate: chartreuse on deep moss, ember accent
    {"acid", 0x0081, 0x0902, 0x19E5, 0xFC65, 0x8A63, 0xCF27, 0x5BC3,
     0xF7FB, 0x4B68, 0xF800, 0x6659},
    // MISSION — the launch-badge sheet: cream ink on warm umber, orange
    // burn accent, gold backing
    {"mission", 0x20C2, 0x2923, 0x41C5, 0xF2C3, 0x89C2, 0xE6D7, 0x8C0D,
     0xFF9B, 0x7B4A, 0xF800, 0xE505},
    // DRAFTING — the No.023 print: black ink on cool gray stock with an
    // acid-green plate accent (the colorful light theme). The accent is a
    // DARK acid: the poster's bright chartreuse matched the stock's luminance
    // and vanished on hardware.
    {"drafting", 0xDEB9, 0xCE58, 0xAD33, 0x6CC1, 0x9D8D, 0x18C3, 0x6B6C,
     0x0000, 0x8C4F, 0xB102, 0x4B71},
    // PAPER — vellum's dimmer sibling: warm mid-gray stock at roughly half the
    // luminance (vellum at full backlight was "bright as FUCK" — the user),
    // near-black ink, deep-green annotations, pure-black hot
    {"paper", 0xAD12, 0x9C90, 0x736C, 0x1A86, 0x5BEC, 0x1081, 0x4A27, 0x0000,
     0x5288, 0x88A1, 0x2A4C},
};
constexpr int kCount = (int)(sizeof(kPalettes) / sizeof(kPalettes[0]));

uint8_t gCurrent = 0;
bool gDark = true;

}  // namespace

// Boot in PHOSPHOR so the splash and any pre-config failure screen are styled
// before store::begin() applies the saved palette.
uint16_t kBg       = kPalettes[0].bg;
uint16_t kPanel    = kPalettes[0].panel;
uint16_t kLine     = kPalettes[0].line;
uint16_t kAmber    = kPalettes[0].amber;
uint16_t kAmberDim = kPalettes[0].amberDim;
uint16_t kGreen    = kPalettes[0].green;
uint16_t kGreenDim = kPalettes[0].greenDim;
uint16_t kIdle     = kPalettes[0].idle;
uint16_t kDim      = kPalettes[0].dim;
uint16_t kRed      = kPalettes[0].red;
uint16_t kSteel    = kPalettes[0].steel;

int count() { return kCount; }

const char* name(uint8_t idx) { return kPalettes[idx < kCount ? idx : 0].name; }

uint8_t current() { return gCurrent; }

bool darkGround() { return gDark; }

uint16_t stack565(uint16_t a, uint16_t b) {
    if (gDark) return addSat565(a, b);  // light sums on a dark ground
    // ink pools on a light ground: per-channel densities (distance below the
    // page colour) add, clamped at black — overlap darkens, never glows
    const int pr = (kBg >> 11) & 0x1F, pg = (kBg >> 5) & 0x3F, pb = kBg & 0x1F;
    int r = pr - ((pr - (int)((a >> 11) & 0x1F)) + (pr - (int)((b >> 11) & 0x1F)));
    int g = pg - ((pg - (int)((a >> 5) & 0x3F)) + (pg - (int)((b >> 5) & 0x3F)));
    int l = pb - ((pb - (int)(a & 0x1F)) + (pb - (int)(b & 0x1F)));
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (l < 0) l = 0;
    if (r > pr) r = pr;  // pooled ink can't be lighter than the page
    if (g > pg) g = pg;
    if (l > pb) l = pb;
    return (uint16_t)((r << 11) | (g << 5) | l);
}

void setTheme(uint8_t idx) {
    if (idx >= kCount) idx = 0;
    gCurrent = idx;
    const Palette& p = kPalettes[idx];
    // classify the ground once: perceptual luminance of bg, 0..255 scale
    {
        const int r8 = ((p.bg >> 11) & 0x1F) * 255 / 31;
        const int g8 = ((p.bg >> 5) & 0x3F) * 255 / 63;
        const int b8 = (p.bg & 0x1F) * 255 / 31;
        gDark = ((r8 * 54 + g8 * 183 + b8 * 18) >> 8) < 110;
    }
    kBg = p.bg;
    kPanel = p.panel;
    kLine = p.line;
    kAmber = p.amber;
    kAmberDim = p.amberDim;
    kGreen = p.green;
    kGreenDim = p.greenDim;
    kIdle = p.idle;
    kDim = p.dim;
    kRed = p.red;
    kSteel = p.steel;
}

}  // namespace theme
