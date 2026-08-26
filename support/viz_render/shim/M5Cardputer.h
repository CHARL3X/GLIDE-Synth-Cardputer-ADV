// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
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
#include <cstring>

// Font0 (the classic 5x7 GLCD font) — the REAL glyph table vendored from
// M5GFX, so host-rendered text has the shipping shapes and metrics
// (6 px advance, 8 px cell).
#ifndef PROGMEM
#define PROGMEM
#endif
#include "glcdfont.h"

// Just enough of LovyanGFX's loose symbols for UI sources that draw text.
enum TextDatum : uint8_t {
    top_left = 0, top_center = 1, top_right = 2,
    middle_left = 3, middle_center = 4, middle_right = 5,
};
namespace fonts {
static const int Font0 = 0, Font2 = 2, Font4 = 4;
}

class M5Canvas {
public:
    static const int W = 240, H = 135;  // the Cardputer panel, cfg::kScreenW/H
    uint16_t px[W * H];
    uint8_t datum_ = top_left;
    int scale_ = 1;                       // 1/2/3 for Font0/Font2/Font4
    uint16_t fg_ = 0xFFFF, bg_ = 0xFFFF;  // bg == fg -> transparent text

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
    void fillRect(int x, int y, int w, int h, uint16_t c) {
        for (int j = 0; j < h; ++j) drawFastHLine(x, y + j, w, c);
    }
    void drawRect(int x, int y, int w, int h, uint16_t c) {
        drawFastHLine(x, y, w, c);
        drawFastHLine(x, y + h - 1, w, c);
        drawFastVLine(x, y, h, c);
        drawFastVLine(x + w - 1, y, h, c);
    }

    // Rounded rects. The arcs are computed per-row from the radius rather than
    // by LovyanGFX's own rasteriser, so a corner pixel may land one position
    // differently; that is fine for judging a layout and a palette, which is
    // all this shim is for.
    static int cornerInset(int dy, int r) {
        int dx = r;
        while (dx > 0 && dx * dx + dy * dy > r * r) --dx;
        return r - dx;
    }
    static int clampRadius(int w, int h, int r) {
        const int maxr = ((w < h ? w : h) - 1) / 2;
        return r < 0 ? 0 : (r > maxr ? maxr : r);
    }
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
        r = clampRadius(w, h, r);
        for (int j = 0; j < h; ++j) {
            int dy = 0;
            if (j < r) dy = r - j;
            else if (j >= h - r) dy = j - (h - 1 - r);
            const int in = dy ? cornerInset(dy, r) : 0;
            drawFastHLine(x + in, y + j, w - 2 * in, c);
        }
    }
    void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
        r = clampRadius(w, h, r);
        drawFastHLine(x + r, y, w - 2 * r, c);
        drawFastHLine(x + r, y + h - 1, w - 2 * r, c);
        drawFastVLine(x, y + r, h - 2 * r, c);
        drawFastVLine(x + w - 1, y + r, h - 2 * r, c);
        for (int j = 0; j < r; ++j) {
            const int in = cornerInset(r - j, r);
            drawPixel(x + in, y + j, c);
            drawPixel(x + w - 1 - in, y + j, c);
            drawPixel(x + in, y + h - 1 - j, c);
            drawPixel(x + w - 1 - in, y + h - 1 - j, c);
            // the arc also advances horizontally; fill the gap to its neighbour
            const int prev = j ? cornerInset(r - j + 1, r) : r;
            for (int k = in + 1; k < prev; ++k) {
                drawPixel(x + k, y + j, c);
                drawPixel(x + w - 1 - k, y + j, c);
                drawPixel(x + k, y + h - 1 - j, c);
                drawPixel(x + w - 1 - k, y + h - 1 - j, c);
            }
        }
    }
    void pushSprite(int, int) {}  // the harness reads px[] directly

    // ---- text — semantics match LovyanGFX's drawString: with a background
    // colour set, each glyph cell is painted opaque, and the datum aligns the
    // string at (x, y).
    //
    // Only Font0's real glyph table is vendored, so Font2 and Font4 are
    // APPROXIMATED by scaling Font0 2x (12x16) and 3x (18x24). The shipping
    // fonts are proportional and 16 / 26 px tall, so the approximation runs a
    // little WIDE and a little short: a layout that fits here fits on the
    // device, which is the direction a check should err. Do not trust it for
    // letterforms — only for whether things collide.
    void setFont(const void* f) {
        scale_ = f == &fonts::Font4 ? 3 : (f == &fonts::Font2 ? 2 : 1);
    }
    void setTextDatum(uint8_t d) { datum_ = d; }
    int textWidth(const char* s) const { return (int)std::strlen(s) * 6 * scale_; }
    void setTextColor(uint16_t fg, uint16_t bg) {
        fg_ = fg;
        bg_ = bg;
    }
    void drawString(const char* s, int x, int y) {
        const int n = (int)std::strlen(s), adv = 6 * scale_;
        if (datum_ == top_right || datum_ == middle_right) x -= n * adv;
        else if (datum_ == top_center || datum_ == middle_center) x -= n * adv / 2;
        if (datum_ >= middle_left) y -= 4 * scale_;  // half the 8 px cell
        for (int i = 0; i < n; ++i, x += adv) {
            const unsigned char ch = (unsigned char)s[i];
            for (int col = 0; col < 6; ++col) {
                const uint8_t bits = col < 5 ? font[(size_t)ch * 5 + col] : 0;
                for (int row = 0; row < 8; ++row) {
                    const bool on = (bits >> row) & 1;
                    if (on) fillRect(x + col * scale_, y + row * scale_, scale_,
                                     scale_, fg_);
                    else if (bg_ != fg_)
                        fillRect(x + col * scale_, y + row * scale_, scale_,
                                 scale_, bg_);
                }
            }
        }
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
