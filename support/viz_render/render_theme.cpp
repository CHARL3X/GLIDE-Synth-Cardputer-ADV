// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Host render harness for the CUSTOM palette: see what the five dials actually
// produce, without flashing anything.
//
// The custom slot derives eleven role colours from a five-dial recipe. The
// native tests prove no dial position can make the instrument unreadable —
// they cannot tell you whether a look is any GOOD. That is what this is for,
// and it is the same reason support/viz_render exists at all: this feature IS
// a screen appearance, so reviewing it in source is reviewing it blind.
//
// It links the REAL ui/theme.cpp and textually includes the REAL
// ui/screensaver.cpp over the stub canvas, so the colours here are the shipping
// colours. Each cell also carries a strip of the eleven raw roles, because the
// screensaver alone does not exercise every one of them.
//
// Build:
//   g++ -std=gnu++14 -O2 -I support/viz_render/shim -I src \
//       support/viz_render/render_theme.cpp src/ui/theme.cpp -o render_theme
// Run:
//   ./render_theme rolls          # 24 rolled looks              -> theme_rolls.bmp
//   ./render_theme presets        # each preset vs its refit     -> theme_presets.bmp
//   ./render_theme sweep          # one dial swept, others held  -> theme_sweep_*.bmp
//   ./render_theme dials 24 30 14 0 14                           -> theme_dials.bmp
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "io/audio_engine.h"
#include "ui/theme.h"

// The live voice the screen reads. Posed so every cell shows the instrument
// mid-note: a resting screen leaves the primary/live role almost unused, which
// is exactly the role a palette lives or dies on.
namespace audio {
Lead gLead = {};
Lead lead() { return gLead; }
}  // namespace audio

#include "ui/screensaver.cpp"

static const int SCALE = 2, GAP = 6, STRIP = 10;  // STRIP = the role swatches

struct Sheet {
    int w, h;
    std::vector<uint8_t> rgb;
    Sheet(int W, int H) : w(W), h(H), rgb((size_t)W * H * 3, 18) {}

    void put(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* p = &rgb[((size_t)y * w + x) * 3];
        p[0] = r; p[1] = g; p[2] = b;
    }
    void put565(int x, int y, uint16_t v) {
        put(x, y, (uint8_t)((((v >> 11) & 0x1F) * 255) / 31),
            (uint8_t)((((v >> 5) & 0x3F) * 255) / 63),
            (uint8_t)(((v & 0x1F) * 255) / 31));
    }
    void blit(const M5Canvas& c, int ox, int oy) {
        for (int y = 0; y < M5Canvas::H; ++y)
            for (int x = 0; x < M5Canvas::W; ++x)
                for (int sy = 0; sy < SCALE; ++sy)
                    for (int sx = 0; sx < SCALE; ++sx)
                        put565(ox + x * SCALE + sx, oy + y * SCALE + sy,
                               c.px[y * M5Canvas::W + x]);
    }
    // The eleven roles, in the order theme.h declares them, so a wrong one is
    // identifiable at a glance rather than merely "off".
    void roles(int ox, int oy, int width) {
        const uint16_t r[11] = {theme::kBg,   theme::kPanel,    theme::kLine,
                                theme::kAmber, theme::kAmberDim, theme::kGreen,
                                theme::kGreenDim, theme::kIdle,  theme::kDim,
                                theme::kRed,  theme::kSteel};
        for (int i = 0; i < 11; ++i) {
            const int x0 = ox + i * width / 11, x1 = ox + (i + 1) * width / 11;
            for (int x = x0; x < x1 - 1; ++x)
                for (int y = oy; y < oy + STRIP; ++y) put565(x, y, r[i]);
        }
    }
    void save(const char* path) const {
        const int rowBytes = w * 3, pad = (4 - (rowBytes % 4)) % 4;
        const int dataSize = (rowBytes + pad) * h, fileSize = 54 + dataSize;
        uint8_t hdr[54] = {};
        hdr[0] = 'B'; hdr[1] = 'M';
        memcpy(&hdr[2], &fileSize, 4);
        const int off = 54, hs = 40, negH = -h;
        memcpy(&hdr[10], &off, 4);
        memcpy(&hdr[14], &hs, 4);
        memcpy(&hdr[18], &w, 4);
        memcpy(&hdr[22], &negH, 4);
        const uint16_t planes = 1, bpp = 24;
        memcpy(&hdr[26], &planes, 2);
        memcpy(&hdr[28], &bpp, 2);
        memcpy(&hdr[34], &dataSize, 4);
        FILE* f = fopen(path, "wb");
        if (!f) { printf("cannot write %s\n", path); return; }
        fwrite(hdr, 1, 54, f);
        const uint8_t zero[3] = {0, 0, 0};
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint8_t* p = &rgb[((size_t)y * w + x) * 3];
                const uint8_t bgr[3] = {p[2], p[1], p[0]};
                fwrite(bgr, 1, 3, f);
            }
            if (pad) fwrite(zero, 1, pad, f);
        }
        fclose(f);
    }
};

static const int CW = M5Canvas::W * SCALE, CH = M5Canvas::H * SCALE + STRIP;

// One cell: the resting screen at a fixed frame, plus the role strip. The frame
// is held constant across cells so differences are the PALETTE, never the phase.
static void cell(Sheet& sheet, int col, int row, int cols) {
    (void)cols;
    M5Canvas canvas;
    screensaver::reset();
    for (int f = 0; f <= 700; ++f) screensaver::draw(canvas, (uint32_t)(f * 33));
    const int ox = GAP + col * (CW + GAP), oy = GAP + row * (CH + GAP);
    sheet.blit(canvas, ox, oy);
    sheet.roles(ox, oy + M5Canvas::H * SCALE, CW);
}

static Sheet makeSheet(int cols, int rows) {
    return Sheet(cols * CW + (cols + 1) * GAP, rows * CH + (rows + 1) * GAP);
}

static void poseActive() {
    audio::gLead.active = true;
    audio::gLead.pitchMidi = 62.f;
    audio::gLead.level = 0.8f;
    audio::gLead.brightness = 0.7f;
    audio::gLead.leads = 2;
}

static void applyLook(const theme::Look& l) {
    theme::setLook(l);
    theme::setTheme(theme::customIndex());
}

static void describe(const char* tag, const theme::Look& l) {
    printf("  %-10s hue %3d  accent %3d  vivid %2d  ground %2d  contrast %2d  "
           "-> %s ground (bg luma %d)\n",
           tag, l.hue * 5, l.accent * 5, l.vivid, l.ground, l.contrast,
           theme::darkGround() ? "dark" : "light", (int)theme::luma(theme::kBg));
}

int main(int argc, char** argv) {
    poseActive();
    const char* mode = argc > 1 ? argv[1] : "rolls";

    if (strcmp(mode, "rolls") == 0) {
        const int cols = 4, rows = 6;
        Sheet sheet = makeSheet(cols, rows);
        printf("theme_rolls.bmp — %d rolled looks\n", cols * rows);
        for (int i = 0; i < cols * rows; ++i) {
            const theme::Look l = theme::rollLook((uint32_t)(i * 2654435761u + 7u));
            applyLook(l);
            char tag[16];
            snprintf(tag, sizeof tag, "roll %d", i);
            describe(tag, l);
            cell(sheet, i % cols, i / cols, cols);
        }
        sheet.save("theme_rolls.bmp");
        return 0;
    }

    if (strcmp(mode, "presets") == 0) {
        // Left column: the authored palette. Right: what recipeForPreset()
        // makes of it — i.e. what a player SEES the instant they cycle onto
        // "custom". A big visual gap here means the fit is lying to them.
        const int rows = theme::presetCount();
        Sheet sheet = makeSheet(2, rows);
        printf("theme_presets.bmp — authored (left) vs refit as a recipe (right)\n");
        for (int i = 0; i < rows; ++i) {
            theme::setTheme((uint8_t)i);
            printf("  %-12s authored   bg luma %3d %s\n", theme::name((uint8_t)i),
                   (int)theme::luma(theme::kBg),
                   theme::darkGround() ? "dark" : "light");
            cell(sheet, 0, i, 2);
            const theme::Look l = theme::recipeForPreset((uint8_t)i);
            applyLook(l);
            describe("refit", l);
            cell(sheet, 1, i, 2);
        }
        sheet.save("theme_presets.bmp");
        return 0;
    }

    if (strcmp(mode, "sweep") == 0) {
        // One dial at a time, the rest held — this is what the player feels
        // when they hold an arrow key on a row.
        struct Dial { const char* name; int max; };
        const Dial dials[5] = {{"hue", theme::kLookHueMax},
                               {"accent", theme::kLookAccentMax},
                               {"vivid", theme::kLookVividMax},
                               {"ground", theme::kLookGroundMax},
                               {"contrast", theme::kLookContrastMax}};
        for (int d = 0; d < 5; ++d) {
            const int cols = 4, rows = 3, n = cols * rows;
            Sheet sheet = makeSheet(cols, rows);
            printf("theme_sweep_%s.bmp\n", dials[d].name);
            for (int i = 0; i < n; ++i) {
                theme::Look l = {24, 30, 14, 2, 14};
                const uint8_t v = (uint8_t)(i * dials[d].max / (n - 1));
                if (d == 0) l.hue = v;
                else if (d == 1) l.accent = v;
                else if (d == 2) l.vivid = v;
                else if (d == 3) l.ground = v;
                else l.contrast = v;
                applyLook(l);
                char tag[16];
                snprintf(tag, sizeof tag, "%d", (int)v);
                describe(tag, l);
                cell(sheet, i % cols, i / cols, cols);
            }
            char path[64];
            snprintf(path, sizeof path, "theme_sweep_%s.bmp", dials[d].name);
            sheet.save(path);
        }
        return 0;
    }

    if (strcmp(mode, "dials") == 0 && argc >= 7) {
        theme::Look l = {(uint8_t)atoi(argv[2]), (uint8_t)atoi(argv[3]),
                         (uint8_t)atoi(argv[4]), (uint8_t)atoi(argv[5]),
                         (uint8_t)atoi(argv[6])};
        l = theme::unpackLook(theme::packLook(l));  // clamp exactly as NVS would
        applyLook(l);
        Sheet sheet = makeSheet(1, 1);
        describe("dials", l);
        cell(sheet, 0, 0, 1);
        sheet.save("theme_dials.bmp");
        return 0;
    }

    printf("usage: render_theme [rolls|presets|sweep|dials h a v g c]\n");
    return 1;
}
