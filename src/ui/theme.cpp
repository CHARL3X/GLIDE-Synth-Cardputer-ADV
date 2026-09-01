// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
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

uint8_t gCurrent = 1;  // cassette — must match the boot globals below
bool gDark = true;

// The one player-editable palette, appended after the presets so no stored
// "themeid" changes meaning. Held derived (22 B) so a redraw never re-runs the
// fit loops; the five-dial recipe it came from is 5 B beside it.
Palette gCustom = kPalettes[1];
Look gLook = {24, 30, 14, 0, 14};  // only ever seen if NVS is empty AND the
                                   // player jumps straight to custom

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int absi(int v) { return v < 0 ? -v : v; }
int min2(int a, int b) { return a < b ? a : b; }
int max2(int a, int b) { return a > b ? a : b; }

// h 0..359 (wrapped), s 0..255, v 0..100 (a LEVEL, not a 0..255 value — every
// dial and every target in this file speaks in levels).
uint16_t hsv565(int h, int s, int v) {
    h = ((h % 360) + 360) % 360;
    s = clampi(s, 0, 255);
    const int vv = clampi(v, 0, 100) * 255 / 100;
    const int region = h / 60;
    const int rem = (h - region * 60) * 255 / 60;
    const int p = vv * (255 - s) / 255;
    const int q = vv * (255 - (s * rem) / 255) / 255;
    const int t = vv * (255 - (s * (255 - rem)) / 255) / 255;
    int r, g, b;
    switch (region) {
        case 0:  r = vv; g = t;  b = p;  break;
        case 1:  r = q;  g = vv; b = p;  break;
        case 2:  r = p;  g = vv; b = t;  break;
        case 3:  r = p;  g = q;  b = vv; break;
        case 4:  r = t;  g = p;  b = vv; break;
        default: r = vv; g = p;  b = q;  break;
    }
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void toHsv(uint16_t c, int& h, int& s, int& v) {
    const int r = ((c >> 11) & 0x1F) * 255 / 31;
    const int g = ((c >> 5) & 0x3F) * 255 / 63;
    const int b = (c & 0x1F) * 255 / 31;
    const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const int d = mx - mn;
    v = mx * 100 / 255;
    s = mx == 0 ? 0 : d * 255 / mx;
    if (d == 0)        h = 0;
    else if (mx == r)  h = ((g - b) * 60 / d + 360) % 360;
    else if (mx == g)  h = (b - r) * 60 / d + 120;
    else               h = (r - g) * 60 / d + 240;
}

// Walk the LEVEL away from the ground until this colour clears `sep` of
// luminance from it. Level moves first because hue survives that. Only when
// the level rail runs out does saturation give way — unavoidable, since a
// fully saturated blue cannot also be bright. Hue is never touched: the
// player's colour choice is always honoured.
uint16_t fitTo(int h, int s, int startV, int bgL, int sep, int dir) {
    int v = clampi(startV, 0, 100);
    for (int i = 0; i < 110; ++i) {
        const uint16_t c = hsv565(h, s, v);
        if (absi((int)luma(c) - bgL) >= sep) return c;
        const int nv = v + dir;
        if (nv < 0 || nv > 100) break;
        v = nv;
    }
    while (s > 0) {
        s = s > 6 ? s - 6 : 0;
        const uint16_t c = hsv565(h, s, v);
        if (absi((int)luma(c) - bgL) >= sep) return c;
    }
    return hsv565(h, s, v);
}

// Grounds and rules must be SEPARATED but not LOUD — a panel fill that reads as
// a band, or a graticule that reads as a box, both wreck the instrument's calm.
uint16_t fitBand(int h, int s, int startV, int bgL, int lo, int hi, int dir) {
    int v = clampi(startV, 0, 100);
    for (int i = 0; i < 140; ++i) {
        const uint16_t c = hsv565(h, s, v);
        const int d = absi((int)luma(c) - bgL);
        if (d >= lo && d <= hi) return c;
        const int nv = v + (d < lo ? dir : -dir);
        if (nv < 0 || nv > 100) return c;
        v = nv;
    }
    return hsv565(h, s, v);
}

// Every role is placed by LEVEL first, and only then held to a luminance
// FLOOR. That order is load-bearing. An earlier version drove each role to a
// luminance TARGET instead, and the contact sheet showed what the source could
// not: it silently desaturated every hue that is physically unable to be bright.
// Phosphor's own green refit to near-white, ultraviolet's violet to grey. The
// hue is the thing the player chose; the level is what we are allowed to move.
// The floor bites only for the genuinely impossible (a fully saturated blue on
// black), and then it gives up as little saturation as it takes.
Palette derive(const Look& L) {
    const int hue = L.hue * 5;
    const int accOff = L.accent * 5;
    const int sat = L.vivid * 255 / kLookVividMax;
    const int gv = L.ground * 2;   // 0..80
    const int con = L.contrast;

    Palette p;
    p.name = "custom";
    // The stock carries the hue at reduced saturation, so a vivid theme reads
    // as tinted stock rather than neutral grey — the warm umber and deep navy
    // of the authored palettes are ground TINT, not ground level. At ground 0
    // this is black whatever the hue, which keeps a phosphor look available.
    // Light stock is barely tinted — drafting's cool grey and paper's warm grey
    // both read as NEUTRAL paper next to their inks. Carrying the full tint up
    // there turned them into green and teal sheets.
    int gsat = sat * 40 / 100;
    p.bg = hsv565(hue, gsat, gv);
    int bgL = (int)luma(p.bg);
    const bool dark = bgL < 110;      // the same threshold setTheme() applies
    if (!dark) {
        gsat = sat * 10 / 100;
        p.bg = hsv565(hue, gsat, gv);
        bgL = (int)luma(p.bg);
    }
    const int dir = dark ? 1 : -1;

    p.panel = fitBand(hue, gsat, gv + dir * 6, bgL, 5, 22, dir);
    p.line  = fitBand(hue, gsat, gv + dir * 14, bgL, 12, 52, dir);

    // Contrast spreads the ink away from the stock. The two grounds want
    // genuinely different shapes here, which the contact sheet made obvious:
    // on a DARK stock every role is emitted light climbing off black, so an
    // offset works. On a LIGHT stock the primary is INK — it runs to near-black
    // — but the accent and backing stay MID plate colours, the way drafting's
    // acid green and paper's deep teal do. Subtracting a fixed offset there
    // drove all three inks to 0x0000 together and the palette lost two of its
    // three colours (measured: 6% of rolls, every one of them a light stock).
    const int vGreen = dark ? min2(gv + 46 + con * 2, 92) : max2(gv * 15 / 100 - con / 4, 2);
    const int vAmber = dark ? min2(gv + 52 + con * 2, 100) : max2(gv * 62 / 100 - con / 2, 12);
    const int vSteel = dark ? gv + 36 + con : max2(gv * 56 / 100 - con / 2, 10);
    const int vIdle  = dark ? 78 + con      : 8 - con / 3;
    const int vDim   = dark ? gv + 20 + con : max2(gv * 58 / 100 - con / 3, 10);

    p.green = fitTo(hue, sat, vGreen, bgL, 65, dir);
    p.amber = fitTo(hue + accOff, sat, vAmber, bgL, 58, dir);
    // The backing sits at the third point of a split complement — far enough
    // from both to read as its own layer, never a muddy near-miss of either.
    p.steel = fitTo(hue + accOff * 45 / 100 + 180, sat * 3 / 4, vSteel, bgL, 45, dir);
    p.idle  = fitTo(hue, sat * 8 / 100, vIdle, bgL, 100, dir);
    p.dim   = fitTo(hue, sat * 20 / 100, vDim, bgL, 38, dir);
    // Failures are loud in every theme, but a light-ground red must be a DARK
    // red or it vanishes into the stock (drafting and paper both are).
    p.red = fitTo(0, 240, dark ? 100 : 30, bgL, 55, dir);

    p.greenDim = blend(p.bg, p.green, 122);
    p.amberDim = blend(p.bg, p.amber, 122);
    return p;
}

}  // namespace

// Boot in CASSETTE (the fresh-unit default since v2.8) so the splash and any
// pre-config screen match a new device with no palette flash; a unit with a
// saved theme restyles the moment store::begin() applies it.
uint16_t kBg       = kPalettes[1].bg;
uint16_t kPanel    = kPalettes[1].panel;
uint16_t kLine     = kPalettes[1].line;
uint16_t kAmber    = kPalettes[1].amber;
uint16_t kAmberDim = kPalettes[1].amberDim;
uint16_t kGreen    = kPalettes[1].green;
uint16_t kGreenDim = kPalettes[1].greenDim;
uint16_t kIdle     = kPalettes[1].idle;
uint16_t kDim      = kPalettes[1].dim;
uint16_t kRed      = kPalettes[1].red;
uint16_t kSteel    = kPalettes[1].steel;

int presetCount() { return kCount; }
uint8_t customIndex() { return (uint8_t)kCount; }

// The custom slot rides at the END of the cycle, so every stored "themeid"
// keeps the palette it always named.
int count() { return kCount + 1; }

const char* name(uint8_t idx) {
    if (idx == (uint8_t)kCount) return "custom";
    return kPalettes[idx < kCount ? idx : 0].name;
}

uint8_t current() { return gCurrent; }

bool darkGround() { return gDark; }

uint8_t luma(uint16_t c) {
    const int r8 = ((c >> 11) & 0x1F) * 255 / 31;
    const int g8 = ((c >> 5) & 0x3F) * 255 / 63;
    const int b8 = (c & 0x1F) * 255 / 31;
    return (uint8_t)((r8 * 54 + g8 * 183 + b8 * 18) >> 8);
}

uint32_t packLook(const Look& l) {
    return ((uint32_t)(l.hue & 0x7F)) | ((uint32_t)(l.accent & 0x7F) << 7) |
           ((uint32_t)(l.vivid & 0x1F) << 14) | ((uint32_t)(l.ground & 0x3F) << 19) |
           ((uint32_t)(l.contrast & 0x1F) << 25);
}

Look unpackLook(uint32_t v) {
    Look l;
    l.hue      = (uint8_t)clampi((int)(v & 0x7F), 0, kLookHueMax);
    l.accent   = (uint8_t)clampi((int)((v >> 7) & 0x7F), 0, kLookAccentMax);
    l.vivid    = (uint8_t)clampi((int)((v >> 14) & 0x1F), 0, kLookVividMax);
    l.ground   = (uint8_t)clampi((int)((v >> 19) & 0x3F), 0, kLookGroundMax);
    l.contrast = (uint8_t)clampi((int)((v >> 25) & 0x1F), 0, kLookContrastMax);
    return l;
}

const Look& look() { return gLook; }

void setLook(const Look& l) {
    gLook = l;
    gCustom = derive(gLook);
    if (gCurrent == (uint8_t)kCount) setTheme(gCurrent);  // live under the finger
}

// Family first, dials second — the same shape as generateSoundV3's archetypes,
// for the same reason: unconstrained rolls regress to one mid-everything mush.
Look rollLook(uint32_t seed) {
    uint32_t s = seed ? seed : 0x9E3779B9u;
    auto next = [&s]() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    };
    auto pick = [&next](int lo, int hi) {
        return lo + (int)(next() % (uint32_t)(hi - lo + 1));
    };
    static const uint8_t kFamilyRoll[13] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4};
    const int fam = kFamilyRoll[next() % 13];

    Look l;
    l.hue = (uint8_t)pick(0, kLookHueMax);
    switch (fam) {
        case 0:  // TERMINAL — near-black stock, one live colour, hot annunciator
            l.ground = (uint8_t)pick(0, 3);   l.vivid = (uint8_t)pick(14, 20);
            l.contrast = (uint8_t)pick(13, 20); l.accent = (uint8_t)pick(24, 48);
            break;
        case 1:  // PLATE — dark stock, near-complementary pair (the dossier look)
            l.ground = (uint8_t)pick(1, 5);   l.vivid = (uint8_t)pick(10, 17);
            l.contrast = (uint8_t)pick(11, 17); l.accent = (uint8_t)pick(30, 42);
            break;
        case 2:  // NEON — lifted stock, loud, split rather than opposite
            l.ground = (uint8_t)pick(2, 7);   l.vivid = (uint8_t)pick(17, 20);
            l.contrast = (uint8_t)pick(15, 20);
            l.accent = (uint8_t)(next() & 1 ? pick(8, 20) : pick(52, 64));
            break;
        case 3:  // PRINT — light stock, ink and one plate colour
            l.ground = (uint8_t)pick(30, 40); l.vivid = (uint8_t)pick(6, 13);
            l.contrast = (uint8_t)pick(12, 19); l.accent = (uint8_t)pick(12, 30);
            break;
        default:  // STOCK — dimmer paper, quiet, for eye comfort
            l.ground = (uint8_t)pick(26, 34); l.vivid = (uint8_t)pick(4, 10);
            l.contrast = (uint8_t)pick(14, 20); l.accent = (uint8_t)pick(20, 40);
            break;
    }
    return l;
}

// Fit a recipe to a preset, so cycling onto "custom" opens as a tweakable copy
// of the palette the player was already enjoying — not a jarring default.
Look recipeForPreset(uint8_t idx) {
    const Palette& p = kPalettes[idx < kCount ? idx : 0];
    int hg, sg, vg, ha, sa, va, hb, sb, vb;
    toHsv(p.green, hg, sg, vg);
    toHsv(p.amber, ha, sa, va);
    toHsv(p.bg, hb, sb, vb);
    const int bgL = (int)luma(p.bg);
    const bool dark = bgL < 110;

    // Whichever colour role is actually COLOURED carries the hue. On a light
    // ground the primary is INK — near-black, where hue is numerical noise —
    // so reading the hue off the primary unconditionally turned drafting's cool
    // grey stock pink. Fall back to the ground itself if neither role is
    // coloured (a fully monochrome palette has no hue to recover).
    // The PRIMARY carries a palette's identity and the accent answers it, so
    // the primary's hue is the base whenever it is a real colour at a real
    // level. Ranking the two by saturation instead let fusion's blood-red data
    // colour outvote its teal wireframe and rotated the whole palette to red.
    // A near-black or near-white primary has no recoverable hue (that is what a
    // light-ground INK is), and only then does the accent take over.
    const bool primaryLeads = sg >= 60 && vg >= 12;
    int hueBase = primaryLeads ? hg : ha;
    int other = primaryLeads ? ha : hg;
    if (!primaryLeads && sa < 40) { hueBase = hb; other = hb; }

    Look l;
    l.hue = (uint8_t)clampi(hueBase / 5, 0, kLookHueMax);
    l.accent =
        (uint8_t)clampi(((((other - hueBase) % 360) + 360) % 360) / 5, 0, kLookAccentMax);
    // The MOST saturated role sets vividness. Averaging the two washed the
    // warm-stock palettes out — mission pairs a near-grey cream ink with a
    // fiercely saturated burnt accent, and the mean of those is neutral.
    const int sMax = primaryLeads ? (sg > sa ? sg : sa) : sa;
    l.vivid = (uint8_t)clampi(sMax * kLookVividMax / 255, 0, kLookVividMax);
    l.ground = (uint8_t)clampi(vb / 2, 0, kLookGroundMax);
    // Read contrast back off the hot text, inverting the level derive() places
    // it at (dark: 78 + con, light: 8 - con/3).
    int hi, si, vi;
    toHsv(p.idle, hi, si, vi);
    l.contrast = (uint8_t)clampi(dark ? vi - 78 : (8 - vi) * 3, 0, kLookContrastMax);
    return l;
}

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
    if (idx > (uint8_t)kCount) idx = 0;
    gCurrent = idx;
    // The custom slot is the only one whose colours are computed rather than
    // authored; from here down nothing else in the instrument can tell.
    const Palette& p = (idx == (uint8_t)kCount) ? gCustom : kPalettes[idx];
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
