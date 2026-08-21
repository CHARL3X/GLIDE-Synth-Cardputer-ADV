// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Host render harness for the HOW TO PLAY page — same idea as render.cpp
// (see its header comment): textually #include the REAL ui/help.cpp over the
// stub M5Canvas, render every scroll position that shows a fresh page, and
// write a contact sheet. The manual is a designed screen like any other; it
// gets reviewed as pixels, not prose in a source file.
//
// Build + run (any host c++, the shim is portable):
//   g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I support/viz_render/shim -I src \
//       support/viz_render/render_help.cpp src/ui/theme.cpp -o render_help
//   ./render_help        # phosphor
//   ./render_help 9      # theme index 9 (paper — the LIGHT ground)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ui/theme.h"

#include "ui/help.cpp"

// ---- contact sheet (same as render.cpp) ------------------------------------
static const int SCALE = 2, GAP = 6;

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

int main(int argc, char** argv) {
    int themeIdx = argc > 1 ? atoi(argv[1]) : 0;
    if (themeIdx < 0 || themeIdx >= theme::count()) themeIdx = 0;
    theme::setTheme((uint8_t)themeIdx);

    // one panel per full page of scroll, plus the very last position
    const int vis = help::visibleLines();
    std::vector<int> tops;
    for (int t = 0; t < help::lineCount() - vis; t += vis) tops.push_back(t);
    tops.push_back(help::lineCount() - vis);

    const int cols = 3, rows = ((int)tops.size() + cols - 1) / cols;
    const int cw = M5Canvas::W * SCALE, ch = M5Canvas::H * SCALE;
    Sheet sheet(cols * cw + (cols + 1) * GAP, rows * ch + (rows + 1) * GAP);

    M5Canvas canvas;
    for (int i = 0; i < (int)tops.size(); ++i) {
        help::drawPage(canvas, tops[i]);
        sheet.blit(canvas, GAP + (i % cols) * (cw + GAP), GAP + (i / cols) * (ch + GAP));
    }

    char path[128];
    snprintf(path, sizeof path, "help_%s.bmp", theme::name((uint8_t)themeIdx));
    sheet.save(path);
    printf("wrote %s (%d pages of %d lines)\n", path, (int)tops.size(), help::lineCount());
    return 0;
}
