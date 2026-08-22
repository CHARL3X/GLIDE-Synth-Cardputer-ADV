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
enum TextDatum : uint8_t { top_left = 0, top_right = 2 };
namespace fonts {
static const int Font0 = 0;
}

class M5Canvas {
public:
    static const int W = 240, H = 135;  // the Cardputer panel, cfg::kScreenW/H
    uint16_t px[W * H];
    uint8_t datum_ = top_left;
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

    // ---- text (Font0 only) — semantics match LovyanGFX's drawString: with a
    // background colour set, each 6x8 cell is painted opaque; datum top_right
    // right-aligns the string at x.
    void setFont(const void*) {}
    void setTextDatum(uint8_t d) { datum_ = d; }
    void setTextColor(uint16_t fg, uint16_t bg) {
        fg_ = fg;
        bg_ = bg;
    }
    void drawString(const char* s, int x, int y) {
        const int n = (int)std::strlen(s);
        if (datum_ == top_right) x -= n * 6;
        for (int i = 0; i < n; ++i, x += 6) {
            const unsigned char ch = (unsigned char)s[i];
            for (int col = 0; col < 6; ++col) {
                const uint8_t bits = col < 5 ? font[(size_t)ch * 5 + col] : 0;
                for (int row = 0; row < 8; ++row) {
                    const bool on = (bits >> row) & 1;
                    if (on) drawPixel(x + col, y + row, fg_);
                    else if (bg_ != fg_) drawPixel(x + col, y + row, bg_);
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
