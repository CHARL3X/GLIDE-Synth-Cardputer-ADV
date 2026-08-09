#include "screensaver.h"

#include <M5Cardputer.h>
#include <cmath>

#include "../config.h"
#include "../io/audio_engine.h"
#include "../storage/glide_config.h"
#include "theme.h"

namespace screensaver {

namespace {

// The whole animation is a handful of floats — no buffers, so it costs nothing
// to carry and nothing to wake from. They PERSIST across idle cycles on purpose:
// the figure resumes where it left off instead of snapping back to a cold start.
float gT = 0.f;      // slow master clock — drives the figure's morph
float gBeam = 0.f;   // the beam's parametric position along the curve

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

void reset() {
    // Nothing to tear down — the persistent clocks keep the figure continuous.
    // Kept as a hook so the entry point is explicit at the call site.
}

// A resting oscilloscope. A slow Lissajous whose frequency ratio and phase
// drift forever, so the figure endlessly passes through lines, ellipses and
// rosettes without ever repeating. A comet beam traces it — hot white head in a
// green bloom, a phosphor ribbon fading down the tail. If a loop or jam is still
// sounding the figure breathes with the voice; in silence it animates itself.
void draw(M5Canvas& c, uint32_t nowMs) {
    (void)nowMs;
    c.fillScreen(theme::kBg);

    gT += 0.02f;
    gBeam += 0.14f;

    // live voice: a running backing breathes the figure; silence self-animates
    const audio::Lead l = audio::lead();
    const float energy = l.active ? clampf(l.level, 0.f, 1.f) : 0.f;

    // drifting ratio + phase — the source of the never-repeating morph
    const float a = 3.0f + 0.9f * sinf(gT * 0.110f);
    const float b = 2.0f + 0.9f * sinf(gT * 0.079f + 1.7f);
    const float ph = gT * 0.37f;
    const float breath = 1.0f + 0.06f * sinf(gT * 0.23f) + 0.22f * energy;

    const float cx = cfg::kScreenW * 0.5f;
    const float cy = cfg::kScreenH * 0.46f;   // a touch high — room for the label
    const float Ax = 104.f;
    const float Ay = 50.f * breath;

    // the comet: N samples trailing the beam head, brightest at the head and
    // fading toward the ground down the tail — a glowing ribbon snaking the figure
    constexpr int N = 180;
    constexpr float dt = 0.045f;
    int px = 0, py = 0;
    bool have = false;
    for (int k = 0; k < N; ++k) {
        const float t = gBeam - k * dt;
        const int x = (int)(cx + Ax * sinf(a * t + ph) + 0.5f);
        const int y = (int)(cy + Ay * sinf(b * t) + 0.5f);
        const float age = 1.0f - (float)k / N;   // 1 at the head, 0 at the tail
        const int bright = (int)((28.f + 205.f * age) * (0.72f + 0.28f * energy));
        if (have) {
            c.drawLine(px, py, x, y, theme::fadeDither(theme::kGreen, bright, x, y));
            if (bright > 150) {  // a soft halo on the recent, bright stretch
                const uint16_t g = theme::fadeDither(theme::kGreen, bright / 4, x, y - 1);
                c.drawPixel(x, y - 1, g);
                c.drawPixel(x, y + 1, g);
            }
        }
        px = x;
        py = y;
        have = true;
    }

    // the beam head: a white-hot point in a green bloom (the scope's language)
    {
        const int hx = (int)(cx + Ax * sinf(a * gBeam + ph) + 0.5f);
        const int hy = (int)(cy + Ay * sinf(b * gBeam) + 0.5f);
        c.fillCircle(hx, hy, 2, theme::fade(theme::kGreen, 200));
        c.fillCircle(hx, hy, 1, theme::kIdle);
    }

    // the instrument's identity, whispered: the live sound's name drifts slowly
    // across the lower field, dim enough to keep the screen mostly black
    const char* nm = store::liveName();
    if (nm && nm[0]) {
        const int lx = (int)(cfg::kScreenW * 0.5f + 40.f * sinf(gT * 0.031f));
        const int ly = (int)(cfg::kScreenH - 20 + 7.f * sinf(gT * 0.043f + 2.f));
        c.setFont(&fonts::Font0);
        c.setTextDatum(middle_center);
        c.setTextColor(theme::fade(theme::kDim, 130), theme::kBg);
        c.drawString(nm, lx, ly);
        c.setTextDatum(top_left);
    }
}

}  // namespace screensaver
