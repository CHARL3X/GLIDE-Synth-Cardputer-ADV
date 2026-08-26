// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Host render harness for the two LISTEN screens — same idea as render.cpp and
// render_help.cpp: textually #include the REAL ui/listen_screen.cpp over the
// stub M5Canvas (its hardware half compiles out under GLIDE_HOST_BUILD), link
// the real ui/theme.cpp and the real dsp key/scale tables, and write a contact
// sheet of every state that matters.
//
// It exists because both screens have states that only appear in conditions you
// cannot conjure on demand — a silent room, a weak verdict, a clash that
// retreats to the pentatonic — and the empty one is exactly the state that was
// wrong: with nothing heard, the live view drew twelve 2 px stubs on bare
// ground and read as a hole in the screen.
//
// Build + run:
//   g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I support/viz_render/shim -I src \
//       support/viz_render/render_listen.cpp src/ui/theme.cpp \
//       src/dsp/key_detect.cpp -o render_listen
//   ./render_listen        # phosphor
//   ./render_listen 9      # paper (a LIGHT ground)
//   ./render_listen all    # every palette, one sheet each
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ui/theme.h"

#include "ui/listen_screen.cpp"

// ---- contact sheet (same as render.cpp / render_help.cpp) -------------------
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

// ---- the states worth looking at -------------------------------------------
// A plausible A-minor chroma: tonic strongest, then the fifth and the third.
static void chromaAmin(float* ch) {
    static const float k[12] = {0.55f, 0.06f, 0.34f, 0.12f, 0.62f, 0.28f,
                                0.08f, 0.71f, 0.10f, 1.00f, 0.14f, 0.40f};
    for (int i = 0; i < 12; ++i) ch[i] = k[i];
}

// A wave that looks like something is being played, so the strip can be judged
// full as well as flat.
static void waveBusy(float* w, int n) {
    for (int i = 0; i < n; ++i) {
        const float t = (float)i / (float)n;
        w[i] = 0.75f * __builtin_sinf(t * 18.8f) * (0.35f + 0.65f * t);
    }
}

int main(int argc, char** argv) {
    const bool all = argc > 1 && strcmp(argv[1], "all") == 0;
    int first = 0, last = theme::count() - 1;
    if (!all) {
        first = last = argc > 1 ? atoi(argv[1]) : 0;
        if (first < 0 || first >= theme::count()) first = last = 0;
    }

    float ch0[12] = {0.f}, chA[12];
    chromaAmin(chA);
    float flat[112] = {0.f}, busy[112];
    waveBusy(busy, 112);

    dsp::KeyGuess quiet = dsp::KeyGuess::make();
    dsp::KeyGuess forming = dsp::KeyGuess::make();
    forming.valid = true; forming.rootPc = 9; forming.minor = true;
    forming.confidence = 0.62f;
    for (int i = 0; i < 12; ++i) forming.chroma[i] = chA[i];
    dsp::KeyGuess weak = forming;
    weak.confidence = 0.18f;

    dsp::ListenApply apA{9, 1, dsp::LM_AEO, 9, false, false, false};
    dsp::ListenApply apDor{9, 1, dsp::LM_DOR, 9, true, false, false};
    dsp::ListenApply safeA = apA;
    safeA.safe = true;

    for (int t = first; t <= last; ++t) {
        theme::setTheme((uint8_t)t);

        const int cols = 3;
        M5Canvas canvas;
        std::vector<M5Canvas*> shots;

        auto snap = [&]() {
            M5Canvas* s = new M5Canvas();
            memcpy(s->px, canvas.px, sizeof canvas.px);
            shots.push_back(s);
        };

        // --- live view -------------------------------------------------------
        // 1. the silent room: the state that was broken
        listen_screen::drawListening(canvas, 0.05f, ch0, quiet, 0, false);   snap();
        // 2. hearing something, no verdict yet
        listen_screen::drawListening(canvas, 0.35f, chA, quiet, 3, false);   snap();
        // 3. a round just landed (pulse) with a forming verdict
        listen_screen::drawListening(canvas, 0.72f, chA, forming, 5, true);  snap();

        // --- verdict card ----------------------------------------------------
        // 4. the ordinary landing: nothing to warn about, so it invites you to play
        listen_screen::drawResult(canvas, forming, apA, apA, 9, 1, 0, 1, 0,
                                  flat, 112);                                snap();
        // 5. retuned + tempo locked + alternates available, and being played
        listen_screen::drawResult(canvas, forming, apDor, apDor, 2, 0, 0, 3, 124,
                                  busy, 112);                                snap();
        // 6. nudged onto the second guess
        listen_screen::drawResult(canvas, forming, apDor, apA, 2, 0, 1, 3, 124,
                                  busy, 112);                                snap();
        // 7. the pentatonic retreat
        listen_screen::drawResult(canvas, forming, apA, safeA, 9, 1, 0, 1, 0,
                                  flat, 112);                                snap();
        // 8. a weak verdict, no tempo
        listen_screen::drawResult(canvas, weak, apA, apA, 9, 1, 0, 1, 0,
                                  flat, 112);                                snap();

        const int rows = ((int)shots.size() + cols - 1) / cols;
        const int cw = M5Canvas::W * SCALE, chh = M5Canvas::H * SCALE;
        Sheet sheet(cols * cw + (cols + 1) * GAP, rows * chh + (rows + 1) * GAP);
        for (int i = 0; i < (int)shots.size(); ++i) {
            sheet.blit(*shots[i], GAP + (i % cols) * (cw + GAP),
                       GAP + (i / cols) * (chh + GAP));
            delete shots[i];
        }
        char path[128];
        snprintf(path, sizeof path, "listen_%s.bmp", theme::name((uint8_t)t));
        sheet.save(path);
        printf("wrote %s\n", path);
    }
    return 0;
}
