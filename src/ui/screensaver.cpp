// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "screensaver.h"

#include <M5Cardputer.h>
#include <cmath>

#include "../config.h"
#include "../io/audio_engine.h"
#include "theme.h"

namespace screensaver {

namespace {

// The whole animation is a handful of floats — no buffers, so it costs nothing
// to carry and nothing to wake from. This matters more here than it looks: the
// perform screen's VizState union already sits within ~1 KB of the RAM ceiling
// (see perform_screen.cpp), so the resting screen must budget in floats, never
// in arrays. They PERSIST across idle cycles on purpose — the figure resumes
// where it left off instead of snapping back to a cold start.
float gPhase = 0.f;     // the helix's rotation — one full turn ~17 s
float gEnvPhase = 0.f;  // the beat envelope's drift — the waists migrate ~30 s
float gPulse = 0.f;     // 0..1 sweep of the traveling light (left to right)
float gPulse2 = 1.f;    // the counter-traveling one, slower — they interfere
float gK = 0.f;         // eased turn rate (how many twists span the screen)
float gAmp = 0.f;       // eased radius — swells with a running loop, else rests
float gRich = 0.f;      // eased envelope depth — how pinched the waists get

constexpr float kTau = 6.2831853f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

void reset() {
    // Nothing to tear down — the persistent clocks keep the figure continuous.
    // Kept as a hook so the entry point is explicit at the call site.
}

// The string at rest, seen as a helix.
//
// Two strands wind around a shared axis across the full screen, rungs between
// them, with a soft window of light sliding along the length. Everything moves
// slowly and nothing jumps: one turn of the helix takes ~17 s, one pass of the
// light ~14 s, and the beat envelope that pinches the strands into waists drifts
// on a ~30 s cycle of its own. Because those three periods don't divide evenly,
// the figure never quite repeats — the complexity comes from the interference,
// not from anything moving fast.
//
// Depth is the trick that sells it: each strand's brightness and colour track
// its z (cos of the winding angle), so the strand in front reads hot green and
// the one behind cools to steel and dims. That alternation, twice per turn, is
// what makes two mirrored sine waves read as a single object in space.
//
// If a loop or jam is still sounding the helix answers it — pitch sets the turn
// rate, timbre sets how deeply the waists pinch — but eased hard, so a glide
// winds the figure over seconds instead of snapping.
void draw(M5Canvas& c, uint32_t nowMs) {
    (void)nowMs;
    c.fillScreen(theme::kBg);

    // ~30 fps, so these are per-frame steps. Deliberately slower than anything
    // the perform screen does — this screen's job is to be ignorable.
    gPhase += 0.012f;
    if (gPhase > kTau) gPhase -= kTau;
    gEnvPhase += 0.0035f;
    if (gEnvPhase > kTau) gEnvPhase -= kTau;
    // Both lights wrap only once they are FULLY off the edge (±0.30 of a screen
    // is ~2.1 sigma out, where the gaussian is ~1% — wrapping at ±0.15 left ~30%
    // of the glow still lit, so the light teleported across the panel). Travel
    // speed is unchanged; the light just rests off-screen a little longer, which
    // makes the crossing flare land as an event rather than a constant state.
    gPulse += 1.f / 420.f;
    if (gPulse > 1.30f) gPulse -= 1.60f;
    gPulse2 -= 1.f / 570.f;  // the other way, slower: 14 s and 19 s don't
    if (gPulse2 < -0.30f) gPulse2 += 1.60f;  // divide, so they meet somewhere
                                             // new every single time

    // live voice: a running loop or jam feeds the figure; silence lets it rest
    const audio::Lead l = audio::lead();
    const float energy = l.active ? clampf(l.level, 0.f, 1.f) : 0.f;

    // pitch -> turn rate, as the STRING scope mode maps pitch to half-waves,
    // but eased ~7x harder. At rest it settles on a plain 3.5-turn figure.
    const float turns = l.active ? clampf(1.2f + (l.pitchMidi - 40.f) * 0.07f, 1.2f, 4.0f) : 2.4f;
    const float kTgt = turns * kTau / (float)cfg::kScreenW;
    if (gK <= 0.f) gK = kTgt;  // first frame ever — start settled, not winding
    gK += (kTgt - gK) * 0.02f;

    // radius fills the screen — a resting screen should still be a COMPOSITION,
    // not a thin ribbon floating in black (measured on the host renderer: at
    // 0.46 the figure used a third of the panel and read as empty)
    gAmp += ((0.80f + 0.16f * energy) - gAmp) * 0.03f;
    // a bright patch pinches the waists harder — the figure gets more sinuous
    gRich += ((l.active ? 0.42f + 0.38f * l.brightness : 0.50f) - gRich) * 0.02f;

    const float cy = cfg::kScreenH * 0.5f;
    const float amp = gAmp * (cfg::kScreenH * 0.44f);
    // the beat: a second, much slower standing wave riding the radius. Its
    // waists drift against the helix's own turns, so crossings keep migrating.
    const float kEnv = 1.5f * 3.14159265f / (float)cfg::kScreenW;

    // The two lights, summed. Where they overlap the figure FLARES (the sum is
    // allowed past 1.0 before clamping) — that flare is the one event on this
    // screen, and it lands somewhere new every pass.
    const float px = gPulse * (float)cfg::kScreenW;
    const float px2 = gPulse2 * (float)cfg::kScreenW;
    auto glowAt = [px, px2](float xf) {
        const float d1 = (xf - px) / 34.f, d2 = (xf - px2) / 34.f;
        const float g = expf(-d1 * d1) + expf(-d2 * d2);
        return g > 1.35f ? 1.35f : g;
    };

    // A very faint column wash under the light. This is what gives the pulse
    // VOLUME — without it the light is just a stripe of brighter pixels on the
    // curve; with it, it reads as something illuminating the figure.
    // Dithered per column, not faded: at this depth the wash only spans ~11 of
    // 255 levels, so a straight fade quantises into visible vertical BANDS with
    // hard edges (measured on the host renderer — it read as a grey box around
    // the light). Perturbing the level by the Bayer threshold dissolves the step
    // into interleaved columns instead.
    // Dithered per PIXEL in 2D, not per column: at this depth the wash spans
    // only ~9 of 255 levels. A straight fade quantises into vertical bands with
    // hard edges; dithering by column alone turns those bands into visible
    // STRIPES (both measured on the host renderer). Only the full 4x4 Bayer
    // scatters it into dust. It costs ~16k pixel writes inside the lit window,
    // which is affordable at 30 fps and only while the screen is asleep.
    for (int x = 0; x < cfg::kScreenW; ++x) {
        const int w = (int)(9.f * glowAt((float)x));
        if (w <= 0) continue;
        for (int y = 0; y < cfg::kScreenH; ++y)
            c.drawPixel(x, y, theme::fadeDither(theme::kGreenDim, w, x, y));
    }

    // the sparse '+' field the STRING scope mode uses — the resting screen
    // should still read as GLIDE's test-equipment world, not a bare curve. It
    // never moves; it just catches the light on the way past.
    for (int gx = 26; gx < cfg::kScreenW - 6; gx += 53)
        for (int gy = 16; gy < cfg::kScreenH - 10; gy += 31) {
            const int b = (int)(110.f + 130.f * glowAt((float)gx));
            const uint16_t mc = theme::fade(theme::kLine, (uint8_t)(b > 255 ? 255 : b));
            c.drawFastHLine(gx - 2, gy, 5, mc);
            c.drawFastVLine(gx, gy - 2, 5, mc);
        }

    // the axis the helix winds around — whispered, and it catches the light as
    // the pulse passes over it
    for (int x = 0; x < cfg::kScreenW; ++x)
        c.drawPixel(x, (int)cy,
                    theme::fadeDither(theme::kLine, (int)(110.f + 120.f * glowAt((float)x)), x,
                                      (int)cy));

    int prevA = 0, prevB = 0;
    float prevS = 0.f;
    for (int x = 0; x < cfg::kScreenW; ++x) {
        const float t = gK * (float)x + gPhase;
        const float s = sinf(t), z = cosf(t);  // z = depth: +1 toward the viewer
        const float env = (1.f - gRich) + gRich * fabsf(sinf(kEnv * (float)x + gEnvPhase));
        const float r = amp * env;
        const int yA = (int)(cy - r * s + 0.5f);
        const int yB = (int)(cy + r * s + 0.5f);

        // the traveling windows: gaussians of light ~34 px wide sliding along
        // the helix. They light what's already there — never moving the figure.
        const float glow = glowAt((float)x);

        // the ribbon between the strands: rungs that fade out at the crossings,
        // so the figure reads as one twisted RIBBON and not two loose wires
        if ((x % 12) == 0) {
            const int lo = yA < yB ? yA : yB, hi = yA < yB ? yB : yA;
            const float open = fabsf(s) * env;  // 0 at a crossing, 1 at the widest
            const int rb = (int)((5.f + 22.f * open) * (0.45f + 1.10f * glow) + 8.f * energy);
            if (rb > 3 && hi > lo)
                c.drawFastVLine(x, lo, hi - lo + 1, theme::fadeDither(theme::kSteel, rb, x, lo));
        }

        if (x > 0) {
            // Depth is the whole illusion: two mirrored sines only read as one
            // object in space if the near strand is unmistakably nearer. Three
            // cues, stacked — it is THICKER, much brighter (squared, so the far
            // half genuinely recedes), and drawn last so it occludes. Brightness
            // alone measured as a flat ribbon on the host renderer.
            for (int pass = 0; pass < 2; ++pass) {
                // draw the far strand first so the near one overlaps it
                const bool aFar = (z <= 0.f);
                const bool doA = (pass == 0) ? aFar : !aFar;
                const float dep = doA ? z : -z;          // -1 (far) .. +1 (near)
                const float f01 = dep * 0.5f + 0.5f;     //  0 (far) ..  1 (near)
                const float lit = 0.42f + 0.78f * glow;
                const int y0 = doA ? prevA : prevB;
                const int y1 = doA ? yA : yB;
                const int bright = (int)((20.f + 185.f * f01 * f01) * lit + 24.f * energy);
                const uint16_t base = theme::blend(theme::kSteel, theme::kGreen,
                                                   (uint8_t)(f01 * 255.f));
                // clamped: where the two lights overlap glow runs past 1, which
                // would wrap this blend factor and invert the flare to black
                const uint16_t col =
                    (glow > 0.55f && f01 > 0.6f)
                        ? theme::blend(base, theme::kIdle,
                                       (uint8_t)clampf((glow - 0.55f) * 330.f, 0.f, 255.f))
                        : base;
                c.drawLine(x - 1, y0, x, y1, theme::fadeDither(col, bright, x, y1));
                // the strand thickens as it comes toward you — the cue that
                // actually sells the twist at this pixel pitch
                if (f01 > 0.55f) {
                    const int hb = (int)(bright * (0.30f + 0.45f * (f01 - 0.55f) / 0.45f));
                    c.drawLine(x - 1, y0 + 1, x, y1 + 1, theme::fadeDither(col, hb, x, y1 + 1));
                    c.drawLine(x - 1, y0 - 1, x, y1 - 1,
                               theme::fadeDither(col, (int)(hb * 0.6f), x, y1 - 1));
                }
            }
        }
        // the crossings: where the two strands trade places, a bead marks the
        // waist of the twist. The eye needs fixed points to read a rotation —
        // without them a slow helix reads as a wave that merely wobbles.
        if (x > 0 && ((s <= 0.f) != (prevS <= 0.f))) {
            const float bb = 90.f + 150.f * glow;
            c.drawPixel(x, (int)cy, theme::fade(theme::kIdle, (uint8_t)(bb > 255.f ? 255 : bb)));
            c.drawPixel(x, (int)cy - 1, theme::fade(theme::kGreen, (uint8_t)(bb * 0.45f)));
            c.drawPixel(x, (int)cy + 1, theme::fade(theme::kGreen, (uint8_t)(bb * 0.45f)));
        }

        prevA = yA;
        prevB = yB;
        prevS = s;
    }
}

}  // namespace screensaver
