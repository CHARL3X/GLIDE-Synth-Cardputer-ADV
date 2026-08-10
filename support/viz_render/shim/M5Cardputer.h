// Host stand-in for M5GFX's M5Canvas — just enough of the drawing surface to
// compile a UI source file on a PC and render what it draws.
//
// Only the primitives the host-renderable screens actually call are here. If
// you point the harness at a source that needs more (text, fills, sprites),
// add them; keep the semantics matching LovyanGFX, especially endpoint-INCLUSIVE
// drawLine, or the renders will lie to you.
#pragma once
#include <cstdint>
#include <cstdlib>

class M5Canvas {
public:
    static const int W = 240, H = 135;  // the Cardputer panel, cfg::kScreenW/H
    uint16_t px[W * H];

    void fillScreen(uint16_t c) {
        for (int i = 0; i < W * H; ++i) px[i] = c;
    }
    void drawPixel(int x, int y, uint16_t c) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        px[y * W + x] = c;
    }
    void drawFastHLine(int x, int y, int w, uint16_t c) {
        for (int i = 0; i < w; ++i) drawPixel(x + i, y, c);
    }
    void drawFastVLine(int x, int y, int h, uint16_t c) {
        for (int i = 0; i < h; ++i) drawPixel(x, y + i, c);
    }
    void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            drawPixel(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
};
