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
    // INK — pure monochrome: silver trace, white-hot accents, print texture
    {"ink", 0x0000, 0x10A2, 0x18E3, 0xFFFF, 0x8C71, 0xBDF7, 0x52AA, 0xFFFF,
     0x632C, 0xF800, 0x7C94},
    // AMBER — single-tube vintage terminal: everything burns warm
    {"amber", 0x0820, 0x1880, 0x28E1, 0xFA82, 0x7981, 0xFD60, 0x7A80, 0xFF58,
     0x7B08, 0xF800, 0xA40A},
    // ICE — the blue CRT spec-sheet: glacier blues, white-blue hot
    {"ice", 0x0001, 0x0863, 0x10C7, 0xBE5F, 0x5311, 0x6CDF, 0x29F0, 0xF7BF,
     0x4AAF, 0xF800, 0x8E9C},
    // VELLUM — dark ink on cream paper; the instrument as a printed page
    {"vellum", 0xEF3A, 0xE6D8, 0xC5F4, 0x2367, 0x8D72, 0x20E3, 0x7BCD, 0x0000,
     0x948F, 0xB102, 0x4B71},
    // ULTRAVIOLET — electric purple, neon aqua accent, magenta backing:
    // the futuristic one
    {"ultraviolet", 0x0803, 0x1046, 0x28AA, 0x0739, 0x0BCD, 0xB27F, 0x5952,
     0xF73F, 0x62B1, 0xF800, 0xFB39},
    // ORCHID — hot pink phosphor with a cyan accent (the reference post's dish)
    {"orchid", 0x0801, 0x1823, 0x3045, 0x4F3F, 0x2BD1, 0xFA7A, 0x892F, 0xFF3F,
     0x7ACE, 0xF800, 0xB498},
    // BLUEPRINT — white line-work on cyanotype blue; the instrument as an
    // engineering drawing (gold annotations, cyan backing)
    {"blueprint", 0x00C7, 0x094B, 0x1A30, 0xFE49, 0x8B85, 0xEF9F, 0x7CF9,
     0xFFFF, 0x4B73, 0xF800, 0x6F3A},
    // REDSHIFT — deep-space red receding to black, hot gold annunciators
    {"redshift", 0x0800, 0x1821, 0x3882, 0xFDA9, 0x8B25, 0xFA85, 0x78E2,
     0xFF5B, 0x7A8A, 0xF800, 0x4C7F},
};
constexpr int kCount = (int)(sizeof(kPalettes) / sizeof(kPalettes[0]));

uint8_t gCurrent = 0;

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

void setTheme(uint8_t idx) {
    if (idx >= kCount) idx = 0;
    gCurrent = idx;
    const Palette& p = kPalettes[idx];
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
