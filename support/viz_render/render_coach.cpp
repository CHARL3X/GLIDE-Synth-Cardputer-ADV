// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Host render harness for the coach — the playable-tour banner, its GOT IT
// celebration frame, the three contextual tips, and the one-time offer card.
// Same idea as render_help.cpp: textually #include the REAL ui/coach.cpp over
// the stub M5Canvas so every teaching frame is reviewed as pixels. The banner
// paints over live perform-screen ground, so each panel first lays down a
// stand-in scope region + status bar to judge it in situ, not on a void.
//
// Build + run (any host c++, the shim is portable):
//   g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I support/viz_render/shim -I src \
//       support/viz_render/render_coach.cpp src/ui/theme.cpp -o render_coach
//   ./render_coach        # phosphor
//   ./render_coach 9      # theme index 9 (paper — the LIGHT ground)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

#include "ui/theme.h"

#include "ui/coach.cpp"

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

// A stand-in perform screen under the overlay: status bar, a pitch-trail-ish
// curve in the scope, the readout/grid ground the banner will actually cover.
// Approximate on purpose — the judgement calls here are the OVERLAY's contrast
// and fit against realistic clutter, not the perform screen itself.
static void drawGround(M5Canvas& c) {
    c.fillScreen(theme::kBg);
    c.fillRect(0, 0, M5Canvas::W, 12, theme::kPanel);
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString("GLIDE", 4, 2);
    c.setTextColor(theme::kIdle, theme::kPanel);
    c.drawString("C maj", 56, 2);
    c.drawString("OCT4", 108, 2);
    c.setTextColor(theme::kDim, theme::kPanel);
    c.setTextDatum(top_right);
    c.drawString("vox 0/4  TILT", M5Canvas::W - 4, 2);
    c.setTextDatum(top_left);
    for (int x = 4; x < 236; ++x) {  // a wandering trail across the scope
        const float t = (float)x / 235.f;
        const int y = 54 + (int)(22.f * sinf(t * 9.f) * sinf(t * 2.2f));
        c.drawPixel(x, y, theme::kGreen);
        c.drawPixel(x, y + 1, theme::kDim);
    }
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString("GLD 80ms  saw  legato", 4, 98);
    c.drawString("CUT 2.4k VOL 80 BND 2", 4, 108);
    for (int s = 0; s < 4; ++s)  // the mini grid map's footprint
        for (int col = 0; col < 10; ++col)
            c.fillRect(166 + col * 7 + 2, 98 + s * 6 + 2, 2, 2, theme::kLine);
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString("fn edit  tab setup  shift chrom  ` exit", 2, 125);
}

int main(int argc, char** argv) {
    int themeIdx = argc > 1 ? atoi(argv[1]) : 0;
    if (themeIdx < 0 || themeIdx >= theme::count()) themeIdx = 0;
    theme::setTheme((uint8_t)themeIdx);

    // panels: the 7 tour steps, one celebration frame, the 3 tips, the offer
    const int panels = coach::kTutSteps + 1 + coach::kTipCount + 1;
    const int cols = 3, rows = (panels + cols - 1) / cols;
    const int cw = M5Canvas::W * SCALE, ch = M5Canvas::H * SCALE;
    Sheet sheet(cols * cw + (cols + 1) * GAP, rows * ch + (rows + 1) * GAP);

    M5Canvas canvas;
    int i = 0;
    auto place = [&](void) {
        sheet.blit(canvas, GAP + (i % cols) * (cw + GAP), GAP + (i / cols) * (ch + GAP));
        ++i;
    };

    for (int s = 0; s < coach::kTutSteps; ++s) {
        drawGround(canvas);
        coach::drawBanner(canvas, s, false);
        place();
    }
    drawGround(canvas);
    coach::drawBanner(canvas, 1, true);  // the GOT IT frame (the slide landing)
    place();
    for (int t = 0; t < coach::kTipCount; ++t) {
        drawGround(canvas);
        coach::drawTip(canvas, t);
        place();
    }
    drawGround(canvas);
    coach::drawOffer(canvas);
    place();

    char path[128];
    snprintf(path, sizeof path, "coach_%s.bmp", theme::name((uint8_t)themeIdx));
    sheet.save(path);
    printf("wrote %s (%d panels)\n", path, panels);
    return 0;
}
