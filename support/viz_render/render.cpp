// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Host render harness: see what a screen actually LOOKS like without flashing.
//
// It textually #includes the REAL ui/screensaver.cpp over a stub M5Canvas and
// links the REAL ui/theme.cpp, so what you review is the shipping code and the
// shipping palettes — not a mock-up that can drift. It steps the animation frame
// by frame and writes a contact sheet of chosen frames as a BMP.
//
// This exists because the screensaver was first written blind, and every single
// thing wrong with it was invisible in the source: the figure used a third of
// the panel, the depth cue read flat, the glow wash quantised into hard bands
// and then (after a naive fix) into stripes, and summed glow above 1.0 wrapped a
// uint8_t blend factor so the flare would have inverted to black. Reviewing
// renders caught all of it in minutes.
//
// Build + run: see README.md in this directory.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "io/audio_engine.h"
#include "ui/theme.h"

// The live voice the screen reads. Static here so the harness can pose it.
namespace audio {
Lead gLead = {};
Lead lead() { return gLead; }
}  // namespace audio

#include "ui/screensaver.cpp"

// ---- contact sheet ---------------------------------------------------------
static const int SCALE = 2, GAP = 6;  // 2x so dither/1px detail stays readable

struct Sheet {
    int w, h;
    std::vector<uint8_t> rgb;  // 3 bytes/px, top-down
    Sheet(int W, int H) : w(W), h(H), rgb((size_t)W * H * 3, 18) {}

    void blit(const M5Canvas& c, int ox, int oy) {
        for (int y = 0; y < M5Canvas::H; ++y)
            for (int x = 0; x < M5Canvas::W; ++x) {
                const uint16_t v = c.px[y * M5Canvas::W + x];
                const uint8_t r = (uint8_t)((((v >> 11) & 0x1F) * 255) / 31);
                const uint8_t g = (uint8_t)((((v >> 5) & 0x3F) * 255) / 63);
                const uint8_t b = (uint8_t)(((v & 0x1F) * 255) / 31);
                for (int sy = 0; sy < SCALE; ++sy)
                    for (int sx = 0; sx < SCALE; ++sx) {
                        const int qx = ox + x * SCALE + sx, qy = oy + y * SCALE + sy;
                        if (qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
                        uint8_t* p = &rgb[((size_t)qy * w + qx) * 3];
                        p[0] = r; p[1] = g; p[2] = b;
                    }
            }
    }

    void save(const char* path) const {
        const int rowBytes = w * 3, pad = (4 - (rowBytes % 4)) % 4;
        const int dataSize = (rowBytes + pad) * h, fileSize = 54 + dataSize;
        uint8_t hdr[54] = {};
        hdr[0] = 'B'; hdr[1] = 'M';
        memcpy(&hdr[2], &fileSize, 4);
        const int off = 54, hs = 40, negH = -h;  // negative height = top-down
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

// Frames chosen to sample the slow cycles at very different phases. The screen's
// periods (rotation, beat envelope, the two lights) deliberately do not divide,
// so spread the samples wide or every frame looks alike.
static const int kWant[6] = {90, 350, 700, 1050, 1400, 1750};

int main(int argc, char** argv) {
    bool active = false;
    int themeIdx = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "active") == 0) active = true;
        else if (strcmp(argv[i], "rest") == 0) active = false;
        else themeIdx = atoi(argv[i]);
    }
    if (themeIdx < 0 || themeIdx >= theme::count()) themeIdx = 0;
    theme::setTheme((uint8_t)themeIdx);

    if (active) {  // a loop or jam still sounding under the resting screen
        audio::gLead.active = true;
        audio::gLead.pitchMidi = 62.f;
        audio::gLead.level = 0.8f;
        audio::gLead.brightness = 0.7f;
        audio::gLead.leads = 2;
    }

    const int cols = 2, rows = 3;
    const int cw = M5Canvas::W * SCALE, ch = M5Canvas::H * SCALE;
    Sheet sheet(cols * cw + (cols + 1) * GAP, rows * ch + (rows + 1) * GAP);

    M5Canvas canvas;
    screensaver::reset();
    int wi = 0;
    for (int f = 0; f <= kWant[5] && wi < 6; ++f) {
        screensaver::draw(canvas, (uint32_t)(f * 33));  // ~30 fps, as on device
        if (f == kWant[wi]) {
            sheet.blit(canvas, GAP + (wi % cols) * (cw + GAP), GAP + (wi / cols) * (ch + GAP));
            ++wi;
        }
    }

    char path[128];
    snprintf(path, sizeof path, "viz_%s_%s.bmp", theme::name((uint8_t)themeIdx),
             active ? "active" : "rest");
    sheet.save(path);
    printf("%s  (%dx%d, theme '%s', frames %d..%d)\n", path, sheet.w, sheet.h,
           theme::name((uint8_t)themeIdx), kWant[0], kWant[5]);
    return 0;
}
