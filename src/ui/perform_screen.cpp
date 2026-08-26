// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "perform_screen.h"

#include <M5Cardputer.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../dsp/morph.h"
#include "../dsp/pitch.h"
#include "../dsp/scales.h"
#include "../io/audio_engine.h"
#include "../io/demo.h"
#include "../io/keys.h"
#include "../io/led.h"
#include "../io/looper.h"
#include "../io/tilt.h"
#include "../storage/glide_config.h"
#include "hud.h"
#include "listen_screen.h"
#include "morph.h"
#include "screensaver.h"
#include "settings_screen.h"
#include "sound_card.h"
#include "sound_viz.h"
#include "theme.h"

namespace perform {

namespace {

// screen regions
constexpr int kStatusH = 12;
constexpr int kScopeY = 13, kScopeH = 82;          // 13..94
constexpr int kScopeMid = kScopeY + kScopeH / 2;   // 54
constexpr int kBottomY = 98;
constexpr int kHintY = 125;

constexpr int kTraceX = 4, kTraceW = 232;
bool gPrevValid = false;

float gScopeBuf[512];

// ---- shared scope-mode state ----------------------------------------------
// Exactly one scope mode draws at a time, so every mode's frame-to-frame
// history — including the original waveform afterglow and pitch-trail rings —
// lives in ONE union, sized by the largest member (the trail). Switching
// Display modes restarts the incoming mode's history. The 65 KB frame-buffer
// sprite sits within ~1 KB of the RAM ceiling (measured on hardware: +1.2 KB
// of .bss over the shipped build already boots to "UI ALLOC FAILED"), so a
// new mode must budget INSIDE this union, never beside it — and heap use is
// off the table too: LISTEN/auto-key sizes its record buffer from the largest
// free block (a resident 19.5 KB field cost the player fn+k — measured).
constexpr int kTapeCols = 58;
struct TapeCol {
    uint8_t lev, bri, vib, bl, pitch, flags;  // pitch 0 = rest; flags: bit0 bend,
                                              // bit1 transient spike
};
constexpr int kCombBins = 56;
constexpr int kHgPts = 800;      // 800 * 2 B = 1,600 B — inside the 1,624 cap
struct HgPoint {
    uint8_t x, y;  // scope rect is 232x82, so both axes fit a byte
};
union VizState {
    HgPoint harmo[kHgPts];  // harmonograph pen-trail ring
    int16_t wavePrev[kTraceW];  // waveform scope: last frame's trace (afterglow)
    struct {                    // pitch trail: lead pitch sampled once per frame,
        float pitch[kTraceW];   // scrolling right-to-left (~7.5 s across the
        uint8_t bend[kTraceW];  // screen). NAN = silence gap; bend marks pulled
        uint8_t lev[kTraceW];   // notes; lev/bri drive brightness + hue.
        uint8_t bri[kTraceW];
    } trail;
    TapeCol tape[kTapeCols];
    struct {
        float coef[kCombBins], v[kCombBins], pk[kCombBins];
        uint8_t octMark[kCombBins];
    } comb;
};
VizState gViz;

int gTrailPos = 0;
bool gTrailInit = false;
float gTrailCenter = 69.f;  // view center in MIDI, follows the lead slowly
bool gTrailCenterSet = false;
constexpr float kVibVisGain = 2.5f;  // visual exaggeration of the vibrato wobble

// Add one tilt axis's contribution into the mod accumulators. Cutoff is
// additive (octaves), vibrato additive (cents), morph additive (blend);
// volume is multiplicative (a swell), so two volume routes compound — floored
// by the caller. oneSided: axis A vibrato/morph only lean one way ("forward
// to sing" / forward into the other sound); axis B (roll) is symmetric.
void accumTilt(store::TiltRoute route, float v, float depth, bool oneSided,
               float& cutOct, float& vibCents, float& volMul, float& morphAmt) {
    switch (route) {
        case store::TiltRoute::Cutoff:  // the wah
            cutOct += v * 2.f * depth;
            break;
        case store::TiltRoute::Vibrato:
            vibCents += (oneSided ? (v > 0.f ? v : 0.f) : fabsf(v)) * 80.f * depth;
            break;
        case store::TiltRoute::Volume:  // the swell pedal
            volMul *= 1.f - depth * 0.9f * (0.5f - v * 0.5f);
            break;
        case store::TiltRoute::Morph:   // lean into the previous sound
            morphAmt += (oneSided ? (v > 0.f ? v : 0.f) : fabsf(v)) * depth;
            break;
        default:
            break;
    }
}

void applyTilt() {
    auto& c = store::get();
    auto& s = c.synth;
    float cutOct = 0.f, vibCents = 0.f, volMul = 1.f, morphAmt = 0.f;
    float rawA = 0.f, rawB = 0.f;  // raw axes, exposed as mod-matrix sources

    // Guard on enabled+available only — axis A may be Off while roll (B) is
    // active, so the per-route switch (not this guard) handles Off.
    if (c.tiltOn && tilt::available()) {
        tilt::poll();  // updates both axes in one IMU read

        // mod-latch: freeze the per-axis readings on the rising edge so the
        // player can set a timbre and then lay the device flat.
        static bool prevLatched = false;
        static float latchedA = 0.f, latchedB = 0.f;
        const bool latched = keys::tiltLatched();
        if (latched && !prevLatched) {
            latchedA = tilt::value();
            latchedB = tilt::valueB();
        }
        prevLatched = latched;

        rawA = latched ? latchedA : tilt::value();
        rawB = latched ? latchedB : tilt::valueB();  // read both, even if dual is off,
                                                      // so the matrix can route roll
        accumTilt(store::effectiveTiltRoute(c.tiltMorphA, c.tiltRoute), rawA, c.tiltDepth,
                  true, cutOct, vibCents, volMul, morphAmt);
        if (c.tiltDual)
            accumTilt(store::effectiveTiltRoute(c.tiltMorphB, c.tiltRouteB), rawB,
                      c.tiltDepthB, false, cutOct, vibCents, volMul, morphAmt);
        if (cutOct > 3.f) cutOct = 3.f;
        if (cutOct < -3.f) cutOct = -3.f;
        if (volMul < 0.1f) volMul = 0.1f;  // two volume routes can't hit silence
    }
    s.cutoffModOct = cutOct;
    s.vibratoCents = vibCents;
    s.volMod = volMul;
    morph::setTiltAmt(morphAmt);  // tilt's push on the blend fader (0 when tilt
                                  // is off/unavailable — this path always runs)
    s.tiltAVal = rawA;  // matrix sources (separate from the hardwired routes above)
    s.tiltBVal = rawB;
    s.tempoBpm = (float)c.jamBpm;  // publish the jam tempo for the synced delay
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The G0 trigger macro. Engaged is decided by the caller (momentary vs latch);
// here we just drive the chosen action into the live param copies by `depth`
// (0..1). Filter routes move both lead and backing; pitch/drive touch the lead
// only, so the backing stays a steady bed (matching the bend/tilt rule).
void applyTrigger(dsp::SynthParams& lead, dsp::SynthParams& back, uint8_t action, float depth) {
    switch ((store::TriggerAction)action) {
        case store::TriggerAction::Muffle: {  // lowpass dives shut — the classic throw
            const float oct = -5.f * depth;
            lead.cutoffModOct += oct;
            back.cutoffHz = clampf(back.cutoffHz * exp2f(oct), 60.f, 12000.f);
            break;
        }
        case store::TriggerAction::Brighten: {  // opposite throw: a resonant lift
            // Just raising the corner is nearly inaudible when the patch is
            // already open (a saw barely has top harmonics, and the 1 W speaker
            // barely reproduces them). So push the cutoff up AND sweep resonance
            // up with it — a singing peak as it opens is what you actually hear.
            const float oct = 5.f * depth;
            lead.cutoffModOct += oct;
            lead.resonance = clampf(lead.resonance + depth * 0.6f, 0.f, 0.92f);
            back.cutoffHz = clampf(back.cutoffHz * exp2f(oct), 60.f, 15000.f);
            back.resonance = clampf(back.resonance + depth * 0.6f, 0.f, 0.92f);
            break;
        }
        case store::TriggerAction::PitchDive:   // dive-bomb the lead, up to ~2 oct
            lead.bendCents -= depth * 2400.f;
            break;
        case store::TriggerAction::Drive:       // shove the lead into the soft clipper
            lead.drive = clampf(lead.drive + depth * 6.f, 1.f, 8.f);
            break;
        default: break;
    }
}

void drawStatus(M5Canvas& c) {
    auto& cf = store::get();
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextDatum(top_left);

    if (keys::quickEditActive()) {
        c.fillRect(0, 0, cfg::kScreenW, kStatusH, theme::kAmberDim);
        c.setTextColor(theme::kBg, theme::kAmberDim);
        c.drawString("-- EDIT -- 1-0 param q-p sound [ ] adj", 4, 2);  // 38ch=228px
        return;
    }

    c.fillRect(0, 0, cfg::kScreenW, kStatusH, theme::kPanel);
    char buf[32];
    // the LIVE sound owns the wordmark spot — its name is exactly what Save
    // writes (no second name). * = UNSAVED edits (the live sound differs from the
    // current slot); it clears the moment you shift-save. (The "this slot is your
    // own sound" badge lives in the fn+q..p slot list, not here.)
    snprintf(buf, sizeof buf, "%s%s", store::liveName(),
             store::liveDirty() ? "*" : "");
    // truncate to fit the cramped slot before the scale readout (x56); the full
    // name always lives in the SD library / Save dialog
    while (buf[0] && c.textWidth(buf) > 50) buf[strlen(buf) - 1] = '\0';
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString(buf, 4, 2);
    c.drawString(buf, 5, 2);  // faux bold

    c.setTextColor(theme::kIdle, theme::kPanel);
    snprintf(buf, sizeof buf, "%s %s", dsp::kNoteNames[cf.layout.rootSemis],
             dsp::kScales[cf.layout.scaleIdx].shortName);
    c.drawString(buf, 56, 2);

    snprintf(buf, sizeof buf, "OCT%d", cf.layout.octave);
    c.drawString(buf, 108, 2);  // ends 132: clears SOLO (left edge 138)

    // solo/backing split engaged: the backing holds its own sound while this
    // patch / octave / volume is the solo. Steel "SOLO" (the backing's colour)
    // so you know why a sound switch isn't changing the bed.
    if (store::backingLocked()) {
        c.setTextColor(theme::kSteel, theme::kPanel);
        c.drawString("SOLO", 138, 2);
    }

    // annunciators, right side
    int x = cfg::kScreenW - 4;
    auto l = audio::lead();
    // leads vs the cap — what the cap actually governs. Latched drones live
    // outside it (their count sits by the grid map); no more "vox 5/4".
    snprintf(buf, sizeof buf, "vox %d/%d", l.leads, cf.synth.voiceCount);
    c.setTextDatum(top_right);
    c.setTextColor(l.held > 0 ? theme::kGreen : theme::kDim, theme::kPanel);
    c.drawString(buf, x, 2);
    x -= 48;
    // TILT annunciator carries mode + latch: dim=off, green=on, amber=mod-
    // latched; a trailing "2" means the roll axis (dual) is live too.
    const bool latched = cf.tiltOn && keys::tiltLatched();
    c.setTextColor(!cf.tiltOn ? theme::kLine : (latched ? theme::kAmber : theme::kGreen),
                   theme::kPanel);
    c.drawString(cf.tiltOn && cf.tiltDual ? "TILT2" : "TILT", x, 2);
    c.setTextDatum(top_left);
}

// While fn is held the scope yields to a map of the whole layer: all ten
// parameters with live values on the left, and on the right either the ten
// sounds or — when the selected parameter has a shape — a live context
// visualization: the ADSR params draw the envelope reshaping as you adjust,
// WAVE the actual wavetable cycle, CUTOFF the filter response. fn+q..p still
// switches sounds either way (the keys don't need the list).
void drawEditPanel(M5Canvas& c) {
    static const char* kShort[10] = {"GLIDE",  "ATTACK", "DECAY",  "SUSTAIN", "RELEASE",
                                     "WAVE",   "CUTOFF", "VOICES", "BEND",    "VOLUME"};
    static const char kParamKeys[11] = "1234567890";
    static const char kPatchKeys[11] = "qwertyuiop";
    auto& cf = store::get();
    const int sel = keys::quickEditParam();
    const bool vizEnv = sel >= 1 && sel <= 4, vizWave = sel == 5, vizCut = sel == 6;
    const bool hasViz = vizEnv || vizWave || vizCut;

    c.setFont(&fonts::Font0);
    char buf[24], val[10];
    for (int i = 0; i < 10; ++i) {
        const int y = kScopeY + 1 + i * 8;
        // params (left): a value gauge behind each row — the whole sound
        // reads at a glance, like channel strips on a mixer
        keys::quickParamValue(i, val, sizeof val);
        snprintf(buf, sizeof buf, "%c %-7s %s", kParamKeys[i], kShort[i], val);
        if (i == sel) c.fillRect(2, y - 1, 116, 8, theme::kPanel);
        const float fill = keys::quickParamFill(i);
        if (fill >= 0.f) {
            const int bw = (int)(114.f * (fill > 1.f ? 1.f : fill));
            if (bw > 0)
                c.fillRect(3, y - 1, bw, 8,
                           i == sel ? theme::scale(theme::kAmber, 70) : theme::kLine);
        }
        c.setTextColor(i == sel ? theme::kAmber : theme::kDim);  // bg shows through
        c.drawString(buf, 6, y);
        if (hasViz) continue;  // right side belongs to the context viz
        // sounds (right)
        const bool cur = (i == cf.currentPatch);
        snprintf(buf, sizeof buf, "%c %s%s", kPatchKeys[i], store::patchName(i),
                 store::patchHasOverride(i) ? "*" : "");
        if (cur) {
            c.fillRect(126, y - 1, 112, 8, theme::kPanel);
            c.setTextColor(theme::kGreen, theme::kPanel);
        } else {
            c.setTextColor(theme::kDim, theme::kBg);
        }
        c.drawString(buf, 130, y);
    }

    if (!hasViz) return;
    // the context viz: what the selected knob is doing to the sound, live
    const auto& s = cf.synth;
    const int vx = 128, vy = kScopeY + 14, vw = 106, vh = 52;
    c.setTextColor(theme::kAmber, theme::kBg);
    if (vizEnv) {
        c.drawString("ENVELOPE", vx, kScopeY + 3);
        viz::drawEnv(c, vx, vy, vw, vh, s.attackS, s.decayS, s.sustain, s.releaseS,
                     theme::kGreen);
    } else if (vizWave) {
        c.drawString("WAVEFORM", vx, kScopeY + 3);
        viz::drawWave(c, vx, vy, vw, vh, s.wave, theme::kGreen);
        c.setTextColor(theme::kDim, theme::kBg);
        c.drawString(dsp::waveformName(s.wave), vx, vy + vh + 4);
    } else {
        c.drawString("FILTER", vx, kScopeY + 3);
        viz::drawFilter(c, vx, vy, vw, vh, (dsp::FilterMode)s.filterMode, s.cutoffHz,
                        s.resonance, theme::kGreen);
        c.setTextColor(theme::kDim, theme::kBg);
        c.drawString(dsp::filterModeName((dsp::FilterMode)s.filterMode), vx, vy + vh + 4);
    }
}

// The pitch trail: the lead voice's pitch drawn over time. On an instrument
// whose whole point is the space between the notes, this is the oscilloscope
// for the *other* axis — every glide, hammer-on and bend is a visible curve.
void drawPitchTrail(M5Canvas& c) {
    if (!gTrailInit) {
        for (int i = 0; i < kTraceW; ++i) gViz.trail.pitch[i] = NAN;
        memset(gViz.trail.bend, 0, sizeof gViz.trail.bend);
        memset(gViz.trail.lev, 0, sizeof gViz.trail.lev);
        memset(gViz.trail.bri, 0, sizeof gViz.trail.bri);
        gTrailPos = 0;
        gTrailCenterSet = false;
        gTrailInit = true;
    }

    auto l = audio::lead();
    const float base = l.active ? l.pitchMidi : NAN;
    float v = base;
    if (l.active) {
        if (!gTrailCenterSet) {
            gTrailCenter = base;
            gTrailCenterSet = true;
        }
        // the view drifts after the lead; snaps faster if it ran off-screen
        const float d = base - gTrailCenter;
        gTrailCenter += d * (fabsf(d) > 12.f ? 0.3f : 0.05f);
        // vibrato shimmer: a display-only LFO at the synth's ~5.5 Hz vibrato
        // rate, scaled by the live vibrato depth (tilt + patch), so the trail
        // visibly wobbles when tilt-vibrato is on. The note readout stays on
        // `base` (no wobble) so the name doesn't flicker.
        static float vibPhase = 0.f;
        vibPhase += 6.2831853f * 5.5f / 30.f;
        if (vibPhase > 6.2831853f) vibPhase -= 6.2831853f;
        const auto& s = store::get().synth;
        const float depthCents = s.vibratoCents + s.autoVibCents;
        v = base + sinf(vibPhase) * depthCents * 0.01f * kVibVisGain;
    }
    gViz.trail.pitch[gTrailPos] = v;
    gViz.trail.bend[gTrailPos] = fabsf(keys::bendCentsNow()) > 2.f ? 1 : 0;
    gViz.trail.lev[gTrailPos] = (uint8_t)(l.active ? clampf(l.level * 180.f, 0.f, 255.f) : 0.f);
    gViz.trail.bri[gTrailPos] = (uint8_t)(l.active ? clampf(l.brightness * 255.f, 0.f, 255.f) : 0.f);
    gTrailPos = (gTrailPos + 1) % kTraceW;

    // 30-semitone window: ~2.5 octaves visible
    const float pxPerSemi = (kScopeH - 4) / 30.f;
    const int yTop = kScopeY, yBot = kScopeY + kScopeH - 1;
    auto yOf = [&](float midi) {
        int y = kScopeMid - (int)((midi - gTrailCenter) * pxPerSemi + 0.5f);
        return y < yTop ? yTop : (y > yBot ? yBot : y);
    };

    // gridlines at every root pitch — the fret markers of the time axis
    auto& cf = store::get();
    const int root = cf.layout.rootSemis;
    char nm[8];
    c.setFont(&fonts::Font0);
    for (int m = (int)gTrailCenter - 16; m <= (int)gTrailCenter + 16; ++m) {
        if (((m % 12) + 12) % 12 != root) continue;
        const int y = kScopeMid - (int)((m - gTrailCenter) * pxPerSemi + 0.5f);
        if (y < yTop + 2 || y > yBot - 2) continue;
        c.drawFastHLine(kTraceX, y, kTraceW, theme::kLine);
        if (y + 10 <= yBot) {  // label sits under its line, inside the scope
            snprintf(nm, sizeof nm, "%s%d", dsp::kNoteNames[root], m / 12 - 1);
            c.setTextColor(theme::kDim, theme::kBg);
            c.drawString(nm, kTraceX + 2, y + 2);
        }
    }

    // the trace, oldest at the left edge; segments drawn while the bend keys
    // were pulling the pitch go amber — earned notes, marked. Built in layers
    // per segment: a green glow halo underneath, a punchy green body, then a
    // white-hot core filament burned down its centre near the beam — it reads
    // like a real CRT trace decaying behind the beam, not a flat painted line.
    int prevY = 0;
    bool prevValid = false;
    for (int x = 0; x < kTraceW; ++x) {
        const int idx = (gTrailPos + x) % kTraceW;
        const float m = gViz.trail.pitch[idx];
        if (m != m) {  // NAN: a rest — break the line
            prevValid = false;
            continue;
        }
        const int y = yOf(m);
        // hue = timbre: a dark filter reads green, an open one warms toward gold
        // — kept mostly green (cap 120) so the trace stays punchy, not olive.
        // Bend overrides to full amber: the deflection + colour mark a pulled note.
        const uint16_t baseCol = gViz.trail.bend[idx]
                                     ? theme::kAmber
                                     : theme::blend(theme::kGreen, theme::kAmber,
                                                    (uint8_t)((uint32_t)gViz.trail.bri[idx] * 120 / 255));
        const uint8_t age = (uint8_t)(110 + (uint32_t)x * 145 / kTraceW);  // old->new, bold floor
        const uint8_t lev = gViz.trail.lev[idx];                               // loudness here
        // brightness rides loudness on a HIGH floor, so the green stays vivid and
        // punchy well across the screen and only dims as a note releases.
        const uint8_t bright = (uint8_t)((uint32_t)age * (180 + (uint32_t)lev * 75 / 255) / 255);
        // fade (toward the theme ground) + ordered dither: fade keeps light
        // palettes honest, the dither keeps the ramp's faint end from
        // posterising into a hard halo edge (RGB565 has nowhere else to go)
        const int px2 = kTraceX + x;
        const uint16_t body = theme::fadeDither(baseCol, bright, px2, y);
        if (prevValid) {
            // green glow halo UNDER the body: a luminous tube, fat where loud and
            // recent, thinning to a single thread as it ages off to the left.
            if (bright > 150) {  // wider soft bloom on the brightest stretch
                const int f2 = bright / 4;
                c.drawLine(px2 - 1, prevY - 2, px2, y - 2,
                           theme::fadeDither(baseCol, f2, px2, y - 2));
                c.drawLine(px2 - 1, prevY + 2, px2, y + 2,
                           theme::fadeDither(baseCol, f2, px2, y + 2));
            }
            if (bright > 50) {
                const int f1 = (int)((uint32_t)bright * 100 / 255);
                c.drawLine(px2 - 1, prevY - 1, px2, y - 1,
                           theme::fadeDither(baseCol, f1, px2, y - 1));
                c.drawLine(px2 - 1, prevY + 1, px2, y + 1,
                           theme::fadeDither(baseCol, f1, px2, y + 1));
            }
            // the punchy green body
            c.drawLine(kTraceX + x - 1, prevY, kTraceX + x, y, body);
            // white-hot core: a bright filament down the CENTRE of the green that
            // fades off behind the beam (~36px), so the live end burns hot like a
            // CRT trace instead of wearing a pasted-on white cap.
            const int fromHead = kTraceW - 1 - x;
            if (fromHead < 36) {
                const uint8_t hot = (uint8_t)((36 - fromHead) * 255 / 36);  // hottest at head
                c.drawLine(kTraceX + x - 1, prevY, kTraceX + x, y,
                           theme::blend(body, theme::kIdle, hot));
            }
        } else {
            c.drawPixel(kTraceX + x, y, body);
        }
        prevY = y;
        prevValid = true;
    }
    // the beam head: a white-hot point seated in a layered green bloom (wide soft
    // halo -> mid glow -> hot inner). The core filament above already burns the
    // trace white into this point, so the head reads as a round glowing beam
    // rather than a pasted-on square.
    if (prevValid) {
        const int hx = kTraceX + kTraceW - 2;
        // the core warms toward amber as drive climbs — a gritty patch glows hot,
        // a clean one stays cool white.
        const float drv = cf.synth.drive;
        const float warm = drv <= 2.f ? 0.f : (drv >= 7.f ? 1.f : (drv - 2.f) / 5.f);
        const uint16_t core = theme::blend(theme::kIdle, theme::kAmber, (uint8_t)(warm * 130.f));
        c.drawFastVLine(hx, prevY - 6, 13, theme::fadeDither(theme::kGreen, 40, hx, prevY - 6));
        c.drawFastVLine(hx, prevY - 4, 9, theme::fadeDither(theme::kGreen, 120, hx, prevY - 4));
        c.drawFastVLine(hx, prevY - 2, 5, theme::kGreen);
        c.fillCircle(hx, prevY, 2, theme::fadeDither(theme::kGreen, 210, hx, prevY));
        c.fillCircle(hx, prevY, 1, core);                              // white-hot point
    }
}

// ---------------------------------------------------------------------------
// Generative scope modes (scopeMode 2..7), ported 1:1 from the viz-lab
// browser preview. All of them draw from the same two lock-free feeds the
// originals use — audio::lead() + audio::copyScope() — plus the tilt/vibrato/
// morph values the UI thread already owns. Nothing here touches the audio
// thread. Aesthetic family: ordered dither, stipple, monochrome duotone —
// intensity always fades toward the theme ground (theme::fade), so every
// mode survives the paper (vellum) palette.
// ---------------------------------------------------------------------------

uint32_t gVizFrame = 0;  // advanced once per drawScope

constexpr uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
inline uint8_t bayer4(int x, int y) { return kBayer[(y & 3) * 4 + (x & 3)]; }

// Deterministic per-cell hash for dither/twinkle — no rand(), no state.
inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t t) {
    uint32_t h = x * 374761393u + y * 668265263u + t * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float hash01(uint32_t x, uint32_t y, uint32_t t) {
    return (float)(hash3(x, y, t) & 0xFFFF) / 65535.f;
}

// The per-frame data every generative mode reads. tiltX/Y are the raw axes
// applyTilt() already publishes; vib01 is the live vibrato depth (tilt +
// patch); blend01 is the morph fader — so the visuals answer the same
// gestures the sound does.
struct VizFeed {
    audio::Lead l;
    bool bend;
    float tiltX, tiltY, vib01, blend01;
};
VizFeed vizFeed() {
    VizFeed f;
    f.l = audio::lead();
    f.bend = fabsf(keys::bendCentsNow()) > 2.f;
    const auto& s = store::get().synth;
    f.tiltX = clampf(s.tiltBVal, -1.f, 1.f);  // roll
    f.tiltY = clampf(s.tiltAVal, -1.f, 1.f);  // forward/back
    f.vib01 = clampf((s.vibratoCents + s.autoVibCents) / 80.f, 0.f, 1.f);
    f.blend01 = clampf(morph::pos(), 0.f, 1.f);
    return f;
}

// A peak read of the recent mix for the amplitude-history modes.
float scopePeak() {
    const int n = audio::copyScope(gScopeBuf, 512);
    float pk = 0.f;
    for (int i = 0; i < n; ++i) {
        const float a = fabsf(gScopeBuf[i]);
        if (a > pk) pk = a;
    }
    return pk;
}

// Slow AGC over the mix peak. The raw ring is far quieter than full scale
// (master volume + patch headroom) and varies per sound, so every amplitude-
// driven mode normalizes against a ~5 s running peak instead of a fixed
// gain — measured on hardware, where fixed gain drew "a flat stream of two
// squares". Short-term dynamics survive (the track moves slowly); silence
// stays silent (the floor).
float gAgcTrack = 0.05f;
inline void agcFeed(float pk) {
    // The floor only guards the division — the synth is digitally silent at
    // rest, so it can sit very low. A 0.02 floor buried low-master-volume
    // playing entirely (measured: comb barely moved at practice volume).
    gAgcTrack = fmaxf(fmaxf(pk, gAgcTrack * 0.995f), 0.004f);
}
float scopePeakNorm() {
    const float pk = scopePeak();
    agcFeed(pk);
    const float n = pk / gAgcTrack;
    return n > 1.f ? 1.f : n;
}

// ---- TAPE (mode 2): quantized-block amplitude history ---------------------
// The sound as archival tape: stacked dithered blocks, transients strike a
// full-height filament, bends print accent. Vibrato prints as wow/flutter
// undulation; the morph blend inks the blocks toward solid.
bool gTapeInit = false;
uint8_t gTapePhase = 0, gTapePrevLev = 0;
float gTapeCenter = 57.f;

void drawTape(M5Canvas& c) {
    if (!gTapeInit) {
        memset(gViz.tape, 0, sizeof gViz.tape);
        gTapePhase = 0;
        gTapePrevLev = 0;
        gTapeInit = true;
    }
    const VizFeed f = vizFeed();
    gTapePhase ^= 1;
    if (gTapePhase) {  // push a column every other frame -> ~3.8 s of history
        // AGC-normalized peak plus a shot of raw envelope, so attacks POP
        // above the running level instead of averaging into it
        float lev = scopePeakNorm() * 0.80f + fminf(f.l.level, 1.f) * 0.30f;
        if (lev > 1.f) lev = 1.f;
        const uint8_t l8 = (uint8_t)(lev * 255.f);
        const uint8_t spike = ((int)l8 - (int)gTapePrevLev > 60) ? 2 : 0;
        gTapePrevLev = l8;
        if (f.l.active) gTapeCenter += (f.l.pitchMidi - gTapeCenter) * 0.06f;
        memmove(gViz.tape, gViz.tape + 1, sizeof(TapeCol) * (kTapeCols - 1));
        TapeCol& nc = gViz.tape[kTapeCols - 1];
        nc.lev = l8;
        nc.bri = (uint8_t)(f.l.brightness * 255.f);
        nc.vib = (uint8_t)(f.vib01 * 255.f);
        nc.bl = (uint8_t)(f.blend01 * 255.f);
        // keep riding the last pitch through release tails — pitch 0 only at
        // true silence, so a note-off doesn't snap the stream back to center
        nc.pitch = (f.l.active || f.l.sounding > 0)
                       ? (uint8_t)clampf(f.l.pitchMidi, 1.f, 127.f)
                       : 0;
        nc.flags = (uint8_t)((f.bend ? 1 : 0) | spike);
    }
    constexpr int rows = 10, bh = 4, bw = 4;
    for (int ci = 0; ci < kTapeCols; ++ci) {
        const TapeCol& tc = gViz.tape[ci];
        if (tc.lev < 6) continue;
        const int px = kTraceX + ci * bw;
        const float lv = tc.lev / 255.f;
        // the stream RIDES the melody (2 px/semitone) with vibrato flutter on
        // top — the y-motion that makes the pitch trail feel alive, kept here
        int yo = tc.pitch ? (int)((gTapeCenter - (float)tc.pitch) * 2.f) : 0;
        yo += (int)(sinf(gVizFrame * 0.25f + ci * 0.55f) * (tc.vib / 255.f) * 3.f *
                    fminf(1.f, lv * 2.f));
        if (yo > 16) yo = 16;
        if (yo < -16) yo = -16;
        const int mid = kScopeMid + 4 + yo;  // seated at the optical center,
                                             // like the waveform's midline
        int extent = (int)(lv * rows + 0.5f);
        if (extent < 1) extent = 1;
        const int roomUp = (mid - (kScopeY + 1)) / bh;
        const int roomDn = (kScopeY + kScopeH - 2 - mid) / bh;
        const int maxExt = roomUp < roomDn ? roomUp : roomDn;
        if (maxExt < 1) continue;
        if (extent > maxExt) extent = maxExt;
        const float blf = tc.bl / 255.f;
        // the newest columns burn hot — a beam head, where the sound IS now
        const int fromHead = kTapeCols - 1 - ci;
        const float head = fromHead < 4 ? (float)(4 - fromHead) / 4.f : 0.f;
        for (int r = 0; r < extent; ++r) {
            float b = powf(1.f - (float)r / (extent + 0.5f), 1.4f);
            // osc blend sets the print: dithery when clean, near-solid under morph
            const float fl = hash01(ci, r, gVizFrame >> 3);
            b *= (0.55f + 0.45f * fl) * (1.f - blf) + (0.88f + 0.12f * fl) * blf;
            b = fminf(1.f, b + head * 0.30f);
            // full-strength green ground burning toward white with level ×
            // timbre; the morph blend tints toward steel — the lean into your
            // previous sound prints right onto the tape (bend stays amber)
            uint16_t base =
                (tc.flags & 1) ? theme::kAmber
                               : theme::blend(theme::kGreen, theme::kIdle,
                                              (uint8_t)clampf(b * (0.20f + tc.bri / 255.f) * 220.f,
                                                              0.f, 255.f));
            if (!(tc.flags & 1) && tc.bl > 24)
                base = theme::blend(base, theme::kSteel, (uint8_t)((tc.bl * 3) >> 2));
            else if (!(tc.flags & 1) && tc.bri > 90)  // an open filter warms the
                base = theme::blend(base, theme::kAmber,  // print toward accent
                                    (uint8_t)((tc.bri - 90) * 2 / 3));
            if (head > 0.f) base = theme::blend(base, theme::kIdle, (uint8_t)(head * 110.f));
            const float q = b > 0.75f ? 1.f : b > 0.5f ? 0.78f : b > 0.28f ? 0.52f : 0.30f;
            const uint16_t col = theme::fade(base, (uint8_t)(q * 255.f));
            c.fillRect(px, mid - (r + 1) * bh, bw - 1, bh - 1, col);
            c.fillRect(px, mid + r * bh + 1, bw - 1, bh - 1, col);
        }
        if (tc.flags & 2) {  // transient: thin bright filament, kept in-bounds
                             // (just proud of the blocks — full-height read as
                             // a wall of pillars on hardware)
            int hgt = (extent * bh * 3) / 4 + 2;
            const int mu = mid - (kScopeY + 1), md = kScopeY + kScopeH - 2 - mid;
            if (hgt > mu) hgt = mu;
            if (hgt > md) hgt = md;
            if (hgt > 0)
                c.drawFastVLine(px + 1, mid - hgt, hgt * 2, theme::fade(theme::kIdle, 230));
        }
    }
    c.drawFastHLine(kTraceX, kScopeMid + 4, kTraceW, theme::kLine);
}

// ---- CYMATIC (mode 3): a Chladni plate resonating at the lead pitch -------
// Sand gathers on nodal lines; glides morph the figure continuously. The
// white is energy: note attacks strike the plate and launch a shockwave ring
// that sweeps through the sand and cools; loudness (and tilt-vibrato)
// physically agitates the grains. Tilt moves the driving point off-center,
// warping the figure asymmetric.
float gCymM = 3.02f, gCymN = 2.0f, gCymDrift = 0.f, gCymPrevLev = 0.f;
struct Shock {
    float r, a;
};
Shock gShocks[3];
int gShockN = 0;

void drawCymatic(M5Canvas& c) {
    const VizFeed f = vizFeed();
    if (f.l.level - gCymPrevLev > 0.22f && gShockN < 3) {
        gShocks[gShockN].r = 3.f;
        gShocks[gShockN].a = 1.f;
        ++gShockN;
    }
    gCymPrevLev = f.l.level;
    for (int i = 0; i < gShockN;) {
        gShocks[i].r += 3.4f;
        gShocks[i].a *= 0.90f;
        if (gShocks[i].a < 0.06f || gShocks[i].r > 190.f)
            gShocks[i] = gShocks[--gShockN];
        else
            ++i;
    }
    if (f.l.active) {  // pitch -> plate modes, eased so glides morph the figure
        const float pc = fmodf(fmodf(f.l.pitchMidi, 12.f) + 12.f, 12.f);
        const float oct = floorf(f.l.pitchMidi / 12.f) - 2.f;
        const float tm = 1.5f + pc / 12.f * 3.5f, tn = 1.f + oct * 0.85f;
        gCymM += (tm - gCymM) * 0.12f;
        gCymN += (tn - gCymN) * 0.12f;
    } else {  // idle: the figure breathes slowly instead of freezing
        gCymDrift += 0.003f;
        gCymM += sinf(gCymDrift) * 0.0015f;
        gCymN += cosf(gCymDrift * 0.7f) * 0.0012f;
    }
    const float th = 0.05f + f.l.level * 0.13f;    // louder -> thicker sand lines
    const float glow = 0.30f + f.l.level * 0.70f;
    const float agit = f.l.level * 1.8f + f.vib01 * f.l.level * 1.6f;
    const uint32_t shT = gVizFrame >> (f.vib01 > 0.55f ? 1 : 2);
    const float ax = f.tiltX * 0.10f, ay = f.tiltY * 0.10f;
    constexpr float PI_ = 3.14159265f;
    float* cosA = gScopeBuf;            // scratch: cymatic never copyScopes,
    float* cosB = gScopeBuf + kTraceW;  // and 2x232 floats fit the 512 buffer
    for (int i = 0; i < kTraceW; ++i) {
        const float u = (float)i / (kTraceW - 1);
        cosA[i] = cosf(gCymM * PI_ * (u + ax));
        cosB[i] = cosf(gCymN * PI_ * (u - ax));
    }
    const uint16_t base =
        f.bend ? theme::kAmber
               : theme::blend(theme::kGreen, theme::kIdle,
                              (uint8_t)(25 + f.l.brightness * 76.f));
    const float ys = (float)kTraceW / kScopeH;  // elliptical shock distance
    for (int j = 0; j < kScopeH; ++j) {
        const float v = (float)j / (kScopeH - 1);
        const float ry = cosf(gCymN * PI_ * (v + ay));
        const float ry2 = cosf(gCymM * PI_ * (v - ay));
        for (int i = 0; i < kTraceW; ++i) {
            const float a = fabsf(cosA[i] * ry - cosB[i] * ry2);
            if (a >= th) continue;
            const float p = 1.f - a / th;
            float g = p * glow * (0.5f + 0.5f * hash01(i, j, shT));
            float boost = 0.f;
            if (gShockN) {
                const float dx = i - kTraceW * 0.5f, dy = (j - kScopeH * 0.5f) * ys;
                const float dist = sqrtf(dx * dx + dy * dy);
                for (int s = 0; s < gShockN; ++s) {
                    const float dd = fabsf(dist - gShocks[s].r);
                    if (dd < 11.f) {
                        const float bb = gShocks[s].a * (1.f - dd / 11.f);
                        if (bb > boost) boost = bb;
                    }
                }
            }
            g += boost * 0.9f;
            if (g > 1.f) g = 1.f;
            if (g * 255.f <= bayer4(i, j) * 13.6f) continue;  // ordered-dither gate
            // agitation: grains displace, hardest inside the shock ring
            const float jmp = agit + boost * 3.5f;
            const int oi = i + (int)((hash01(i, j, gVizFrame) - 0.5f) * 2.f * jmp);
            const int oj = j + (int)((hash01(j, i, gVizFrame) - 0.5f) * 2.f * jmp);
            if (oi < 0 || oi >= kTraceW || oj < 0 || oj >= kScopeH) continue;
            // two-tone sand: the line's core stays primary, its outer rim
            // cools toward steel — depth without a third draw pass
            const uint16_t col =
                boost > 0.28f ? theme::kIdle
                : g > 0.85f   ? theme::blend(base, theme::kIdle, 153)
                : p < 0.35f   ? theme::blend(base, theme::kSteel, 102)
                              : base;
            c.drawPixel(kTraceX + oi, kScopeY + oj,
                        theme::fade(col, (uint8_t)clampf((g + 0.15f) * 255.f, 0.f, 255.f)));
        }
    }
}

// ---- STRING (mode 4): the standing wave the synth actually drives ---------
// Replaced the reaction-diffusion "bloom" (hardware verdict: weird mold — eye
// candy, not linked to the sound). This is the instrument's string, photo-
// graphed like a long-exposure scope shot: the played pitch sets how many
// half-waves fit the screen (a glide visibly stretches/compresses the weave),
// attack energy excites harmonics by timbre (bright patch = rich weave, dark
// = one pure arc), release rings down like a real string dying out, vibrato
// shimmers the oscillation. Five phase snapshots multi-expose into the woven
// envelope; every point gets a dither-spray so the curve has a grain glow —
// dense core, sprayed edges.
float gStrA[3] = {0.f, 0.f, 0.f};  // harmonic amplitudes; ring down on release
float gStrPhase = 0.f, gStrK = 0.03f, gStrOmega = 0.11f;
uint8_t gStrPrevLeads = 0;
bool gStrPrevActive = false;

void drawStringWave(M5Canvas& c) {
    const VizFeed f = vizFeed();
    // sparse '+' field markers — long-exposure scope-photo chrome
    for (int gx = kTraceX + 22; gx < kTraceX + kTraceW - 8; gx += 47)
        for (int gy = kScopeY + 14; gy < kScopeY + kScopeH - 8; gy += 27) {
            c.drawFastHLine(gx - 2, gy, 5, theme::kLine);
            c.drawFastVLine(gx, gy - 2, 5, theme::kLine);
        }
    // pitch -> spatial frequency (half-waves on the string), eased so glides
    // stretch the weave smoothly; pitch also sets the oscillation rate
    if (f.l.active) {
        const float nAnti = clampf(0.8f + (f.l.pitchMidi - 40.f) * 0.11f, 0.8f, 4.6f);
        gStrK += (nAnti * 3.14159265f / (float)kTraceW - gStrK) * 0.15f;
        gStrOmega = 0.09f + clampf(f.l.pitchMidi - 40.f, 0.f, 40.f) * 0.0035f;
    }
    // attacks pluck the string; sustain feeds it; silence lets it ring down
    const bool newNote = (f.l.leads > gStrPrevLeads) || (f.l.active && !gStrPrevActive);
    gStrPrevLeads = f.l.leads;
    gStrPrevActive = f.l.active;
    const float lv = fminf(f.l.level, 1.f);
    const float tgt[3] = {f.l.active ? 0.45f + 0.55f * lv : 0.f,
                          f.l.active ? (0.20f + 0.80f * f.l.brightness) * lv : 0.f,
                          f.l.active ? f.l.brightness * f.l.brightness * 0.7f * lv : 0.f};
    for (int h = 0; h < 3; ++h) {
        if (newNote) gStrA[h] = fminf(1.f, fmaxf(gStrA[h], tgt[h] * 1.25f));
        gStrA[h] = tgt[h] > gStrA[h] ? gStrA[h] + (tgt[h] - gStrA[h]) * 0.5f
                                     : gStrA[h] * 0.972f;  // the ring-down
    }
    // vibrato shimmers the oscillation rate (~5.5 Hz, like the synth's LFO)
    gStrPhase += gStrOmega * (1.f + f.vib01 * 0.6f * sinf(gVizFrame * 1.1519f));
    if (gStrPhase > 6.2831853f) gStrPhase -= 6.2831853f;
    // fundamental node ticks on the axis — the physics, annotated
    c.drawFastHLine(kTraceX, kScopeMid, kTraceW, theme::kLine);
    for (float nx = 3.14159265f / gStrK; nx < kTraceW - 2.f; nx += 3.14159265f / gStrK)
        c.drawFastVLine(kTraceX + (int)nx, kScopeMid - 2, 5, theme::fade(theme::kDim, 140));
    // phase-snapshot cosines: 5 exposures across the cycle weave the envelope
    float cosTab[3][5];
    for (int h = 0; h < 3; ++h)
        for (int p = 0; p < 5; ++p)
            cosTab[h][p] = cosf((float)(h + 1) * (gStrPhase + (p - 2) * 0.55f));
    // chromatic exposures, kept SATURATED: pure steel and amber bookends, only
    // a 25% lean on the inner pair — the 50/50 mixes went olive on phosphor.
    // Where exposures land on the same pixel they ADD (saturating), so
    // crossings and the resting string brighten like stacked light instead of
    // last-color-wins mud.
    uint16_t mid = theme::blend(theme::kGreen, theme::kIdle, (uint8_t)(76 + lv * 90.f));
    if (f.blend01 > 0.1f)  // morph lean prints steel, same as tape
        mid = theme::blend(mid, theme::kSteel, (uint8_t)(f.blend01 * 191.f));
    uint16_t expCol[5] = {theme::kSteel,
                          theme::blend(theme::kGreen, theme::kSteel, 64),
                          mid,
                          theme::blend(theme::kGreen, theme::kAmber, 64),
                          theme::kAmber};
    if (f.bend) {  // a pulled note pulls the whole weave amber
        expCol[0] = expCol[1] = expCol[3] = expCol[4] = theme::kAmber;
        expCol[2] = theme::blend(theme::kAmber, theme::kIdle, 76);
    }
    // whole-weave brightness follows the string's energy — the resting string
    // is a quiet line, not five full exposures stacked to white
    const float master = 0.45f + 0.55f * fminf(1.f, gStrA[0] + gStrA[1] + gStrA[2]);
    // per-column oscillators via rotation — no sin() in the hot loop
    float s[3], cR[3], sd[3], cd[3];
    for (int h = 0; h < 3; ++h) {
        s[h] = 0.f;
        cR[h] = 1.f;
        sd[h] = sinf((float)(h + 1) * gStrK);
        cd[h] = cosf((float)(h + 1) * gStrK);
    }
    constexpr float kAmp = 34.f;
    for (int x = 0; x < kTraceW; ++x) {
        int ey[5];        // exposures landing on this column, merged additively
        uint16_t ec[5];
        int en = 0;
        for (int p = 0; p < 5; ++p) {
            const float y = (gStrA[0] * s[0] * cosTab[0][p] + gStrA[1] * s[1] * cosTab[1][p] +
                             gStrA[2] * s[2] * cosTab[2][p]) *
                            kAmp;
            int py = kScopeMid - (int)y;
            if (py < kScopeY + 1) py = kScopeY + 1;
            if (py > kScopeY + kScopeH - 2) py = kScopeY + kScopeH - 2;
            const int dp = p < 2 ? 2 - p : p - 2;  // distance from the "now" exposure
            const float I = (dp == 0 ? 1.f : dp == 1 ? 0.62f : 0.40f) * master;
            const uint16_t col = theme::fade(expCol[p], (uint8_t)(I * 235.f));
            bool merged = false;
            for (int e = 0; e < en; ++e)
                if (ey[e] == py) {  // light sums / ink pools, per the ground
                    ec[e] = theme::stack565(ec[e], col);
                    merged = true;
                    break;
                }
            if (!merged) {
                ey[en] = py;
                ec[en] = col;
                ++en;
            }
            // dither-spray: grain bleeding off the curve, denser near the core
            const int sp = 1 + (int)(hash01(x, p, gVizFrame) * 3.f);
            const int py2 = py + ((hash3(x, p + 9, gVizFrame) & 1) ? sp : -sp);
            if (py2 > kScopeY && py2 < kScopeY + kScopeH - 1)
                c.drawPixel(kTraceX + x, py2, theme::fade(expCol[p], (uint8_t)(I * 92.f)));
        }
        for (int e = 0; e < en; ++e) c.drawPixel(kTraceX + x, ey[e], ec[e]);
        for (int h = 0; h < 3; ++h) {  // rotate the column oscillators
            const float ns = s[h] * cd[h] + cR[h] * sd[h];
            cR[h] = cR[h] * cd[h] - s[h] * sd[h];
            s[h] = ns;
        }
    }
}

// ---- COMB (mode 5): Goertzel harmonic spectrum ----------------------------
// A true spectrum without an FFT: 56 Goertzel bins on a log-frequency axis
// (55 Hz .. 7 kHz). Glides slide the comb sideways; opening the filter
// ignites the upper partials; dissolving dithered tops, falling peak caps,
// fundamental runs hot, octave ruler above. The tilt-morph blend shows for
// real — the spectrum IS the sound.
bool gCombInit = false;

void drawComb(M5Canvas& c) {
    const VizFeed f = vizFeed();
    if (!gCombInit) {
        int prevOct = -1;
        for (int k = 0; k < kCombBins; ++k) {
            const float fr = 55.f * powf(128.f, (float)k / (kCombBins - 1));
            gViz.comb.coef[k] = 2.f * cosf(6.2831853f * fr / (float)cfg::kSampleRate);
            const int o = (int)floorf(log2f(fr / 55.f) + 1e-4f);
            gViz.comb.octMark[k] = (o != prevOct) ? 1 : 0;
            prevOct = o;
            gViz.comb.v[k] = gViz.comb.pk[k] = 0.f;
        }
        gCombInit = true;
    }
    const int n = audio::copyScope(gScopeBuf, 512);
    float combPk = 0.f;
    for (int i = 0; i < n; ++i) {
        const float a = fabsf(gScopeBuf[i]);
        if (a > combPk) combPk = a;
    }
    agcFeed(combPk);
    for (int k = 0; k < kCombBins; ++k) {
        const float co = gViz.comb.coef[k];
        float s1 = 0.f, s2 = 0.f;
        for (int i = 0; i < n; ++i) {
            const float s0 = gScopeBuf[i] + co * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        float mag = n > 0 ? sqrtf(fmaxf(0.f, s1 * s1 + s2 * s2 - co * s1 * s2)) / (n * 0.5f) : 0.f;
        // normalize against the AGC track — a fixed gain left the whole
        // spectrum below the visibility floor on hardware
        float v = powf(fminf(1.f, mag / (gAgcTrack * 1.15f)), 0.65f);
        if (v < 0.04f) v = 0.f;
        gViz.comb.v[k] += (v - gViz.comb.v[k]) * (v > gViz.comb.v[k] ? 0.75f : 0.30f);  // snap up
        gViz.comb.pk[k] = fmaxf(gViz.comb.v[k], gViz.comb.pk[k] - 0.016f);             // peak hold
    }
    const float hz = 440.f * exp2f((f.l.pitchMidi - 69.f) / 12.f);
    auto binOf = [](float fr) {
        return (int)(log2f(fr / 55.f) / 7.f * (kCombBins - 1) + 0.5f);  // 7 octaves
    };
    const int fBin = f.l.active ? binOf(hz) : -9;
    bool harm[kCombBins] = {};
    if (f.l.active)
        for (int h = 2; h <= 6; ++h) {
            const int b = binOf(hz * h);
            if (b >= 0 && b < kCombBins) harm[b] = true;
        }
    for (int k = 0; k < kCombBins; ++k) {  // octave ruler along the top
        const int rx = kTraceX + k * 4 + 1;
        if (gViz.comb.octMark[k])
            c.drawFastVLine(rx, kScopeY + 1, 4, theme::fade(theme::kGreenDim, 230));
        else
            c.drawFastVLine(rx, kScopeY + 1, 2, theme::kLine);
    }
    const int bot = kScopeY + kScopeH - 3, hMax = kScopeH - 12;
    for (int k = 0; k < kCombBins; ++k) {
        const float v = gViz.comb.v[k];
        const int rx = kTraceX + k * 4 + 1;
        if (v > 0.f) {
            const bool isF = (k >= fBin - 1 && k <= fBin + 1);
            const uint16_t base = f.bend ? theme::kAmber
                                  : isF  ? theme::blend(theme::kGreen, theme::kIdle, 140)
                                  : harm[k]
                                      ? theme::blend(theme::kGreen, theme::kAmber, 76)
                                      : theme::kGreen;
            const int hgt = (int)(v * hMax);
            for (int y = 0; y < hgt; ++y) {
                const float rel = (float)y / hMax;
                float bb = (v - rel) * 3.2f;  // tops dissolve into dither
                if (bb > 1.f) bb = 1.f;
                if (bb * 255.f > bayer4(rx + (y & 1), y) * 15.2f)
                    c.fillRect(rx, bot - y, 2, 1,
                               theme::fade(base, (uint8_t)(64 + bb * 191.f)));
            }
        }
        if (gViz.comb.pk[k] > 0.05f)  // falling peak cap
            c.fillRect(rx, bot - (int)(gViz.comb.pk[k] * hMax), 2, 1,
                       theme::fade(theme::kIdle, 242));
    }
    // the baseline glows with the note's energy — the floor is alive
    c.drawFastHLine(kTraceX, bot + 2, kTraceW,
                    theme::fade(theme::kGreenDim,
                                (uint8_t)(60 + fminf(f.l.level, 1.f) * 160.f)));
}

// ---- HARMONOGRAPH (mode 6): a four-pendulum drawing machine ----------------
// Victorian apparatus, INTEGRATED rather than plotted: two damped pendulums
// per axis, coupled within each axis by a shared beam, kept alive by an
// escapement while a note is held. The amplitude envelope is the pen's
// friction — every patch in the library draws in its own handwriting. A new
// note kicks a still-swinging machine (legato builds compound figures), the
// restoring force is the true -w^2*sin(theta) so a hard strike relaxes
// through figure families as it decays, and the morph fader rotates the
// paper (lateral Lissajous -> rotary spirograph rosettes).
struct HgOsc {
    float p, v;
};
HgOsc gHgO[4];  // 0,1 = X pair; 2,3 = Y pair
bool gHgInit = false;
int gHgPos = 0, gHgFill = 0;
float gHgAgc = 1.f, gHgMax = 1.f, gHgRot = 0.f, gHgVibPh = 0.f;
float gHgRatio = 1.26f, gHgTarget = 57.f, gHgPrevTarget = 52.f, gHgPrevLev = 0.f;

constexpr int kHgSub = 24;      // integrator substeps per frame (one point each)
constexpr float kHgDt = 0.05f;  // ~1.1 s of visible trail at 800 points

void hgPushPoint() {
    const float ux = gHgO[0].p + gHgO[1].p;
    const float uy = gHgO[2].p + gHgO[3].p;
    // paper rotation: morph walks the machine from lateral to rotary
    gHgRot += morph::pos() * 0.010f / kHgSub;
    const float ca = cosf(gHgRot), sa = sinf(gHgRot);
    const float rx = ux * ca - uy * sa;
    const float ry = ux * sa + uy * ca;
    // pen AGC — same lesson as gAgcTrack: a quiet patch must still draw big
    const float rad = sqrtf(rx * rx + ry * ry);
    gHgMax = fmaxf(fmaxf(rad, gHgMax * 0.995f), 0.25f);
    gHgAgc += (1.75f / gHgMax - gHgAgc) * 0.03f;
    const float sc = gHgAgc * 30.f;
    int px = (int)(kTraceW * 0.5f + rx * sc + 0.5f);
    int py = (int)(kScopeH * 0.5f + ry * sc * 0.62f + 0.5f);  // squash to the rect
    if (px < 0) px = 0;
    if (px > kTraceW - 1) px = kTraceW - 1;
    if (py < 0) py = 0;
    if (py > kScopeH - 1) py = kScopeH - 1;
    gViz.harmo[gHgPos].x = (uint8_t)px;
    gViz.harmo[gHgPos].y = (uint8_t)py;
    gHgPos = (gHgPos + 1) % kHgPts;
    if (gHgFill < kHgPts) ++gHgFill;
}

void hgStep(const VizFeed& f) {
    const auto& s = store::get().synth;
    // pendulum ratio = the interval between the last two LANDED pitches (a
    // glide morphs the figure); mono fallback keeps a held note two-frequency
    if (f.l.active) {
        if (f.l.glide01 > 0.95f && fabsf(f.l.pitchMidi - gHgTarget) > 0.4f) {
            gHgPrevTarget = gHgTarget;
            gHgTarget = f.l.pitchMidi;
        }
        float semis = gHgTarget - gHgPrevTarget;
        if (fabsf(semis) < 0.2f) semis = s.detuneCents / 100.f;
        const float tgt = powf(2.f, clampf(semis, -12.f, 12.f) / 12.f);
        gHgRatio += (tgt - gHgRatio) * 0.06f;
    }
    const float wob = 1.f + f.vib01 * 0.09f * sinf(gHgVibPh);
    gHgVibPh += 0.22f;
    const float wBase = 1.5f;
    const float w[4] = {wBase * wob, wBase * gHgRatio * wob,
                        wBase * 0.996f * wob,             // slight detune: the
                        wBase * gHgRatio * 1.005f * wob}; // figure never closes
    // friction IS the amplitude envelope: pluck = tight spiral, pad = rosette
    const float zeta = clampf(0.004f + 0.030f / fmaxf(0.05f, s.decayS), 0.004f, 0.05f);
    // escapement: a held note is a running clock (limit cycle — required for
    // the Huygens entrainment below; released notes drift apart as they die)
    const float esc = f.l.active ? 0.058f * clampf(f.l.level, 0.f, 1.f) : 0.f;
    // the shared beam: chorus sets coupling — quasi-periodic below ~0.10,
    // anti-phase lock through the middle, beat death near 1
    const float kC = 0.15f + s.chorusDepth * 0.55f;
    const float gx = f.tiltX * 0.055f;  // the drawing table is sloped
    const float gy = f.tiltY * 0.018f;
    for (int n = 0; n < kHgSub; ++n) {
        for (int i = 0; i < 4; ++i) {
            const int j = i ^ 1;  // same-axis partner ONLY (Huygens 1665:
                                  // perpendicular pendulums don't entrain)
            const float restore = -w[i] * w[i] * sinf(gHgO[i].p * 0.92f);
            const float beam = kC * (gHgO[j].p - gHgO[i].p);
            const float drive = esc * (gHgO[i].v > 0.f ? 1.f : -1.f);
            const float grav = (i < 2) ? gx : gy;
            gHgO[i].v += (restore + beam + drive + grav - 2.f * zeta * w[i] * gHgO[i].v) * kHgDt;
        }
        for (int i = 0; i < 4; ++i) gHgO[i].p += gHgO[i].v * kHgDt;
        hgPushPoint();
    }
}

void drawHarmonograph(M5Canvas& c) {
    const VizFeed f = vizFeed();
    if (!gHgInit) {
        gHgO[0].p = 0.9f;
        gHgO[1].p = -0.6f;
        gHgO[2].p = 0.5f;
        gHgO[3].p = -0.8f;
        gHgO[0].v = gHgO[1].v = gHgO[2].v = gHgO[3].v = 0.f;
        gHgPos = gHgFill = 0;
        gHgAgc = gHgMax = 1.f;
        gHgRot = 0.f;
        gHgPrevLev = 0.f;
        gHgInit = true;
    }
    // a strike kicks the machine; drive throws it into the nonlinear regime,
    // so a hot patch draws wild and relaxes, exactly like it sounds
    if (f.l.level - gHgPrevLev > 0.22f) {
        const float kick = 2.0f + store::get().synth.drive * 0.55f;
        for (int i = 0; i < 4; ++i)
            gHgO[i].v += (hash01(i, gVizFrame, 7) - 0.5f) * kick;
    }
    gHgPrevLev = f.l.level;
    hgStep(f);
    uint16_t base =
        theme::blend(theme::kGreen, theme::kSteel, (uint8_t)(clampf(morph::pos(), 0.f, 1.f) * 255.f));
    if (f.bend) base = theme::kAmber;
    // oldest -> newest, so the live end sits on top. The ghost terminates by
    // construction: points fall off the end of the ring (a decaying raster
    // never reaches zero on integer state — the permanent-stain bug).
    for (int n = 0; n < gHgFill; ++n) {
        const int idx = (gHgPos - gHgFill + n + kHgPts * 2) % kHgPts;
        const HgPoint& p = gViz.harmo[idx];
        // time-uniform capture means point spacing ~ 1/pen-speed: cusps get
        // dwell-time brightness for free
        const uint8_t age = (uint8_t)(30 + (uint32_t)n * 225 / gHgFill);
        c.drawPixel(kTraceX + p.x, kScopeY + p.y, theme::fade(base, age));
    }
    if (gHgFill) {  // hot pen tip
        const HgPoint& t = gViz.harmo[(gHgPos - 1 + kHgPts) % kHgPts];
        c.drawPixel(kTraceX + t.x, kScopeY + t.y, theme::blend(base, theme::kIdle, 200));
    }
}

// ---- INTERFERENCE (mode 7): a two-source ripple tank -----------------------
// The field is evaluated analytically per 2x2 block — zero persistent state,
// no grid (a wave-equation grid needs kilobytes we measured we don't have).
// The two sources are the last two landed pitches: the interval sets the
// fringe geometry, unequal frequencies sweep the pattern at EXACTLY the
// audible beat rate, vibrato FM propagates outward as wavefront bunching,
// tilt refracts the wavefronts (WKB midpoint phase), reverb adds image
// sources mirrored off the tank walls, and morph grows source A from a point
// into a bar (circular -> plane waves).
static const int16_t kSinLut[257] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739,
    9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811,
    25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521,
    32609, 32678, 32728, 32757, 32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
    32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571, 30273, 29956, 29621, 29268,
    28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151,
    15446, 14732, 14010, 13279, 12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179,
    6393, 5602, 4808, 4011, 3212, 2410, 1608, 804, 0, -804, -1608, -2410,
    -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159,
    -20787, -21403, -22005, -22594, -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956, -30273, -30571, -30852, -31113,
    -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580,
    -31356, -31113, -30852, -30571, -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731, -23170, -22594, -22005, -21403,
    -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011,
    -3212, -2410, -1608, -804, 0};  // flash-resident: sinf in the inner loop
                                    // would dominate the whole frame
inline float ifSin(float ph) {
    float u = ph * (256.f / 6.2831853f);
    int i = (int)floorf(u);
    const float fr = u - (float)i;
    i &= 255;
    const float a = (float)kSinLut[i];
    return (a + ((float)kSinLut[i + 1] - a) * fr) * (1.f / 32767.f);
}

struct IfSrc {
    float x, y, k, phase, amp;
};
IfSrc gIfSrc[6];
bool gIfInit = false;
float gIfPhA = 0.f, gIfPhB = 0.f, gIfTilt = 0.f, gIfVibPh = 0.f;
float gIfSep = 8.f, gIfRatio = 1.f, gIfAmp = 0.f, gIfKA = 0.2f, gIfKB = 0.2f;
float gIfTargetA = 57.f, gIfTargetB = 52.f;

void drawInterference(M5Canvas& c) {
    const VizFeed f = vizFeed();
    const auto& s = store::get().synth;
    if (!gIfInit) {
        gIfAmp = 0.f;
        gIfSep = 8.f;
        gIfRatio = 1.f;
        gIfTilt = 0.f;
        gIfInit = true;
    }
    if (f.l.active && f.l.glide01 > 0.95f && fabsf(f.l.pitchMidi - gIfTargetA) > 0.4f) {
        gIfTargetB = gIfTargetA;  // the previous landed pitch becomes source B
        gIfTargetA = f.l.pitchMidi;
    }
    // separation = interval (unison collapses to concentric rings); all eased
    // so glides sweep the fringes instead of cutting
    const float semis = clampf(gIfTargetA - gIfTargetB, -24.f, 24.f);
    gIfSep += (clampf(fabsf(semis) * 7.f, 6.f, 96.f) - gIfSep) * 0.08f;
    gIfKA += (clampf(0.10f + (gIfTargetA - 36.f) * 0.0045f, 0.10f, 0.55f) - gIfKA) * 0.1f;
    gIfKB += (clampf(0.10f + (gIfTargetB - 36.f) * 0.0045f, 0.10f, 0.55f) - gIfKB) * 0.1f;
    gIfRatio += (powf(2.f, semis / 12.f) - gIfRatio) * 0.06f;
    // waves rise fast, die slowly (~2.5 s) — the tank settles, never snaps
    const float tgtAmp =
        (f.l.active || f.l.sounding > 0) ? clampf(f.l.level, 0.f, 1.f) : 0.f;
    gIfAmp += (tgtAmp - gIfAmp) * (tgtAmp > gIfAmp ? 0.12f : 0.035f);
    gIfTilt += (f.tiltX * 0.55f - gIfTilt) * 0.1f;
    // vibrato is FM on the emission clock — the bunching propagates outward,
    // so you can watch vibrato you played half a second ago still travelling
    gIfVibPh += 1.1519f;  // ~5.5 Hz at 30 fps
    const float vibMul = 1.f + f.vib01 * 0.22f * sinf(gIfVibPh);
    gIfPhA += 0.33f * vibMul;
    gIfPhB += 0.33f * gIfRatio * vibMul;
    if (gIfAmp < 0.02f) return;  // a glassy, still tank (already faded to it)

    // sources: A (possibly a bar under morph), B, then reverb image sources
    const float cy = kScopeH * 0.5f;
    const float ax = kTraceW * 0.5f - gIfSep * 0.5f;
    const float bx = kTraceW * 0.5f + gIfSep * 0.5f;
    const float mp = clampf(morph::pos(), 0.f, 1.f);
    const int aEm = 1 + (mp > 0.30f ? 1 : 0) + (mp > 0.65f ? 1 : 0);
    const float spread = 10.f + mp * 8.f;
    int n = 0;
    for (int e = 0; e < aEm; ++e) {  // a point grows into a bar: plane waves
        gIfSrc[n].x = ax;
        gIfSrc[n].y = cy + (e == 0 ? 0.f : (e == 1 ? -spread : spread));
        gIfSrc[n].k = gIfKA;
        gIfSrc[n].phase = gIfPhA;
        gIfSrc[n].amp = gIfAmp * (aEm > 1 ? 0.62f : 1.f);
        ++n;
    }
    const int bIdx = n;
    gIfSrc[n].x = bx;
    gIfSrc[n].y = cy;
    gIfSrc[n].k = gIfKB;
    gIfSrc[n].phase = gIfPhB;
    gIfSrc[n].amp = gIfAmp;
    ++n;
    const float mix = clampf(s.reverbMix, 0.f, 1.f);
    if (mix > 0.08f) {  // image-source method: a wall is a mirror
        const float wallL = -kTraceW * 0.5f * s.reverbSize;
        const float wallR = kTraceW * (0.5f + s.reverbSize);
        const int bases[2] = {0, bIdx};
        for (int b2 = 0; b2 < 2 && n < 6; ++b2) {
            gIfSrc[n] = gIfSrc[bases[b2]];
            gIfSrc[n].x = 2.f * wallL - gIfSrc[bases[b2]].x;
            gIfSrc[n].amp *= mix * 0.7f;  // absorption on reflection
            ++n;
            if (n >= 6) break;
            gIfSrc[n] = gIfSrc[bases[b2]];
            gIfSrc[n].x = 2.f * wallR - gIfSrc[bases[b2]].x;
            gIfSrc[n].amp *= mix * 0.7f;
            ++n;
        }
    }

    uint16_t base = theme::blend(theme::kGreen, theme::kSteel, (uint8_t)(mp * 255.f));
    if (f.bend) base = theme::kAmber;
    const uint16_t hot = theme::blend(base, theme::kIdle, 180);
    float* dy2 = gScopeBuf;  // within-frame scratch only (mode never copyScopes)
    for (int j = 0; j < kScopeH; j += 2) {
        for (int m = 0; m < n; ++m) {
            const float d = (float)j - gIfSrc[m].y;
            dy2[m] = d * d;
        }
        for (int i = 0; i < kTraceW; i += 2) {
            float a = 0.f;
            for (int m = 0; m < n; ++m) {
                const float dx = (float)i - gIfSrc[m].x;
                const float r = sqrtf(dx * dx + dy2[m]);
                // tilt refraction, first order: depth ramp scales the phase
                // accumulated along the path, evaluated at its midpoint (WKB)
                const float xm = (i + gIfSrc[m].x) * 0.5f;
                const float kEff = gIfSrc[m].k * (1.f - gIfTilt * (xm / kTraceW - 0.5f));
                a += gIfSrc[m].amp * ifSin(kEff * r - gIfSrc[m].phase) *
                     (1.f - fminf(r * 0.0035f, 0.75f));
            }
            // signed field -> directional light: one crest edge bright, the
            // other dark — a lit liquid surface, not a plot. The whole light
            // scales with the eased amplitude: the constant 0.42 baseline
            // rendered a gray wash at ZERO ripple height, which then vanished
            // in one frame at the early-return — the "flash to black". Now
            // the tank dims continuously as the waves flatten.
            float v = (0.42f + a * 0.34f) * fminf(1.f, gIfAmp * 2.2f);
            if (v < 0.f) v = 0.f;
            if (v > 1.f) v = 1.f;
            const uint16_t col = (v > 0.86f) ? hot : base;
            const uint8_t lvl = (uint8_t)(v * 255.f);
            // compose first, dither ONCE on the final value (two independent
            // thresholds in one renderer beat against each other as speckle)
            for (int dy = 0; dy < 2; ++dy) {
                const int y = j + dy;
                if (y >= kScopeH) break;
                for (int dx3 = 0; dx3 < 2; ++dx3) {
                    const int x = i + dx3;
                    if (x >= kTraceW) break;
                    if (lvl <= bayer4(x, y) * 12) continue;
                    c.drawPixel(kTraceX + x, kScopeY + y, theme::fade(col, lvl));
                }
            }
        }
    }
}

// CRT depth: bevelled glass corners so the scope seats in its bezel like a
// little tube screen. (The drifting scanline sheen was removed — too busy.)
// Overlaid on top of the scope, skipped during the quick-edit panel.
void drawCrt(M5Canvas& c) {
    const int x0 = kTraceX, x1 = kTraceX + kTraceW - 1;
    const int y0 = kScopeY, y1 = kScopeY + kScopeH - 1;

    // bevelled glass corners — a 45° chamfer masked back to black
    const int r = 7;
    for (int i = 0; i < r; ++i) {
        const int w = r - i;
        c.drawFastHLine(x0, y0 + i, w, theme::kBg);              // top-left
        c.drawFastHLine(x1 - w + 1, y0 + i, w, theme::kBg);      // top-right
        c.drawFastHLine(x0, y1 - i, w, theme::kBg);              // bottom-left
        c.drawFastHLine(x1 - w + 1, y1 - i, w, theme::kBg);      // bottom-right
    }
}

void drawScope(M5Canvas& c, uint32_t now) {
    if (keys::quickEditActive()) {
        drawEditPanel(c);
        return;
    }
    ++gVizFrame;
    const uint8_t mode = store::get().scopeMode;
    static uint8_t prevMode = 255;
    if (mode != prevMode) {  // the union holds ONE mode's history — restart
        prevMode = mode;     // the incoming mode clean
        gPrevValid = false;
        gTrailInit = false;
        gTapeInit = false;
        gCombInit = false;
        gStrA[0] = gStrA[1] = gStrA[2] = 0.f;  // the string starts at rest
        gStrPrevActive = false;
        gHgInit = false;
        gIfInit = false;
        gShockN = 0;  // shared shock ring starts clean for cymatic/interference
    }
    switch (mode) {
        case 1: drawPitchTrail(c); return;
        case 2: drawTape(c); return;
        case 3: drawCymatic(c); return;
        case 4: drawStringWave(c); return;
        case 5: drawComb(c); return;
        case 6: drawHarmonograph(c); return;
        case 7: drawInterference(c); return;
        default: break;  // 0 = the waveform scope below
    }
    // graticule — seated a touch below geometric center: the readout and
    // annunciators own the top of the scope, so the optical middle is lower
    const int wMid = kScopeMid + 5;
    c.drawFastHLine(kTraceX, wMid, kTraceW, theme::kLine);
    for (int x = kTraceX; x < kTraceX + kTraceW; x += 29)
        c.drawFastVLine(x, wMid - 2, 5, theme::kLine);

    const int n = audio::copyScope(gScopeBuf, 512);
    if (n < kTraceW + 2) return;

    // rising zero-crossing trigger in the first half -> stable trace
    int trig = 0;
    for (int i = 1; i < n - kTraceW; ++i) {
        if (gScopeBuf[i - 1] <= 0.f && gScopeBuf[i] > 0.f) {
            trig = i;
            break;
        }
    }

    float wavePk = 0.f;
    for (int i = 0; i < n; ++i) {
        const float a = fabsf(gScopeBuf[i]);
        if (a > wavePk) wavePk = a;
    }
    agcFeed(wavePk);
    const uint16_t bright = theme::kGreen;
    // AGC gain: the raw mix sits far below full scale, so a fixed gain drew a
    // near-flat line — normalize so the trace fills the tube at any volume
    // (0.85 keeps full swings inside the tube around the lowered midline)
    const float gain = (kScopeH / 2 - 3) * 0.85f / gAgcTrack;

    // afterglow: last frame's trace lingers like phosphor (dithered so the
    // faint ramp dissolves instead of ending at an edge)
    if (gPrevValid) {
        for (int x = 1; x < kTraceW; ++x)
            c.drawLine(kTraceX + x - 1, gViz.wavePrev[x - 1], kTraceX + x, gViz.wavePrev[x],
                       theme::fadeDither(theme::kGreen, 80, kTraceX + x, gViz.wavePrev[x]));
    }
    int prevY = 0;
    for (int x = 0; x < kTraceW; ++x) {
        const float s = gScopeBuf[trig + x];
        int y = wMid - (int)(s * gain);
        if (y < kScopeY) y = kScopeY;
        if (y > kScopeY + kScopeH - 1) y = kScopeY + kScopeH - 1;
        if (x > 0) c.drawLine(kTraceX + x - 1, prevY, kTraceX + x, y, bright);
        gViz.wavePrev[x] = (int16_t)y;
        prevY = y;
    }
    gPrevValid = true;
}

void drawReadout(M5Canvas& c) {
    auto l = audio::lead();
    if (!l.active) return;

    char name[8];
    int cents;
    dsp::midiToNoteCents(l.pitchMidi, name, sizeof name, cents);

    c.setTextDatum(top_right);
    c.setFont(&fonts::FreeMonoBold12pt7b);
    c.setTextColor(theme::kIdle);
    c.drawString(name, cfg::kScreenW - 30, kScopeY + 3);

    char cb[8];
    snprintf(cb, sizeof cb, "%+03dc", cents);
    c.setFont(&fonts::Font0);
    c.setTextColor(cents == 0 ? theme::kDim : theme::kAmber);
    c.drawString(cb, cfg::kScreenW - 5, kScopeY + 8);
    c.setTextDatum(top_left);

    // glide progress bar — fills as the slide arrives
    if (l.glide01 < 0.99f) {
        const int bx = cfg::kScreenW - 72, by = kScopeY + 24, bw = 66, bh = 3;
        c.drawRect(bx, by, bw, bh, theme::kLine);
        c.fillRect(bx + 1, by + 1, (int)((bw - 2) * l.glide01), bh - 2, theme::kGreen);
    }
}

void drawBottom(M5Canvas& c, uint32_t now) {
    auto& cf = store::get();
    auto& s = cf.synth;
    char buf[44];

    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kDim, theme::kBg);
    snprintf(buf, sizeof buf, "GLD %dms  %s  %s", (int)(s.glideS * 1000),
             dsp::waveformName(s.wave),
             s.glideMode == dsp::GlideMode::LegatoOnly ? "legato" : "always");
    c.drawString(buf, 4, kBottomY);
    // compact: worst case "CUT 12.0k VOL 100 BND 12" = 24ch = 144px,
    // safely clear of the mini grid-map starting at x=166
    if (s.cutoffHz >= 1000.f)
        snprintf(buf, sizeof buf, "CUT %.1fk VOL %d BND %d", s.cutoffHz / 1000.f,
                 (int)(s.masterVol * 100), cf.bendRange);
    else
        snprintf(buf, sizeof buf, "CUT %d VOL %d BND %d", (int)s.cutoffHz,
                 (int)(s.masterVol * 100), cf.bendRange);
    c.drawString(buf, 4, kBottomY + 10);

    // mini grid-map: 4x10, top row = string 3. green = held lead, steel =
    // latched drone (the backing's colour; blinking white on the jam-motion
    // beat — you can SEE the arp walk), small amber dot = root degrees (lock on)
    // or in-scale keys (lock off), faint = the rest.
    const int gx = 166, gy = kBottomY, cw = 7, ch = 6;
    const auto& sc = dsp::kScales[cf.layout.scaleIdx];
    const int rowDeg = dsp::rowDegrees(cf.layout);
    // Guide tones: while the progression walks, tick every key belonging to the
    // chord sounding NOW. Scale lock already guarantees no wrong notes; this is
    // the next rung — which of the right notes are the strong ones over this
    // bar. Fetched once per frame; the cell in the loop is the only cost.
    uint8_t guidePcs[3];
    const int nGuide = keys::progChordPcs(guidePcs, 3);
    int gs = -1, gc = -1;
    keys::progCurrentCell(gs, gc);  // the root's own cell, already outlined below
    int droneCount = 0;
    for (int str = 0; str < dsp::kGridStrings; ++str) {
        const int y = gy + (3 - str) * ch;
        for (int col = 0; col < dsp::kGridCols; ++col) {
            const int x = gx + col * cw;
            const int st = keys::noteState(str, col, now);
            if (st > 0) {
                if (st >= 2) ++droneCount;
                const uint16_t fill = st == 1   ? theme::kGreen
                                      : st == 2 ? theme::kSteel
                                                : theme::kIdle;  // beat blink
                c.fillRect(x, y, cw - 1, ch - 1, fill);
                continue;  // held and drone cells win: no tick, no dot
            }
            bool mark;
            if (cf.layout.scaleLock) {
                mark = ((str * rowDeg + col) % sc.len) == 0;  // root degrees
            } else {
                mark = dsp::chromaticInScale(cf.layout, str, col);
            }
            c.fillRect(x + 2, y + 2, 2, 2, mark ? theme::kAmber : theme::kLine);
            // A 1 px corner tick, clear of the centre dot. It must whisper —
            // this map already carries held/drone/beat/in-scale meaning — so it
            // takes the dim accent and the one free pixel. Skipped on the root's
            // own cell, which the steel outline below already claims.
            if (nGuide > 0 && !(str == gs && col == gc)) {
                const int cellPc =
                    (int)(dsp::gridToMidi(cf.layout, str, col, false) + 0.5f);
                const uint8_t pcHere = (uint8_t)(((cellPc % 12) + 12) % 12);
                for (int i = 0; i < nGuide; ++i)
                    if (guidePcs[i] == pcHere) {
                        c.drawPixel(x + cw - 2, y, theme::kAmberDim);
                        break;
                    }
            }
        }
    }
    // progression: outline the chord root sounding now, so the walking
    // backing is visible on the grid map even though no key is held for it
    if (gs >= 0) {
        const int x = gx + gc * cw, y = gy + (3 - gs) * ch;
        c.drawRect(x, y, cw - 1, ch - 1, theme::kSteel);
    }

    // the backing, counted where it lives — drones sit outside the lead cap
    if (droneCount > 0) {
        snprintf(buf, sizeof buf, "+%d", droneCount);
        c.setTextColor(theme::kSteel, theme::kBg);
        c.setTextDatum(top_right);
        c.drawString(buf, gx - 3, gy + 9);
        c.setTextDatum(top_left);
    }
}

// Loop pedal state, top-left of the scope: REC blinks red while the take
// rolls, LOOP green with a cycle-progress bar while it plays, OVR amber
// while layering. Dim LOOP = a stopped take waiting on the pedal.
void drawLoop(M5Canvas& c, uint32_t now) {
    const looper::State st = looper::state();
    if (st == looper::State::Empty || keys::quickEditActive()) return;

    const int x = kTraceX + 2, y = kScopeY + 3;
    char buf[16];
    c.setFont(&fonts::Font0);
    switch (st) {
        case looper::State::Recording:
            if ((now >> 8) & 1) c.fillCircle(x + 3, y + 3, 3, theme::kRed);
            snprintf(buf, sizeof buf, "REC %lu.%lus", (unsigned long)(looper::positionMs(now) / 1000),
                     (unsigned long)(looper::positionMs(now) % 1000 / 100));
            c.setTextColor(theme::kRed, theme::kBg);
            c.drawString(buf, x + 10, y);
            break;
        case looper::State::Playing:
        case looper::State::Overdub: {
            const bool ovr = st == looper::State::Overdub;
            c.setTextColor(ovr ? theme::kAmber : theme::kGreen, theme::kBg);
            c.drawString(ovr ? "OVR" : "LOOP", x, y);
            const uint32_t len = looper::lengthMs();
            if (len > 0) {  // cycle progress — see the downbeat coming
                const int bx = x + 28, bw = 44;
                c.drawRect(bx, y + 1, bw, 5, theme::kLine);
                c.fillRect(bx + 1, y + 2, (int)((bw - 2) * looper::positionMs(now) / len), 3,
                           ovr ? theme::kAmber : theme::kGreenDim);
            }
            // layer count: amber while a peel is in effect (live < total)
            if (looper::overflowed()) {
                c.setTextColor(theme::kRed, theme::kBg);
                c.drawString("FULL", x + 76, y);
            } else if (looper::topLayers() > 0) {
                const int live = looper::liveLayers() + 1, top = looper::topLayers() + 1;
                char lc[12];
                if (live < top) snprintf(lc, sizeof lc, "x%d/%d", live, top);
                else            snprintf(lc, sizeof lc, "x%d", live);
                c.setTextColor(live < top ? theme::kAmber : theme::kDim, theme::kBg);
                c.drawString(lc, x + 76, y);
            }
            break;
        }
        case looper::State::Stopped:
            c.setTextColor(theme::kDim, theme::kBg);
            c.drawString("LOOP --", x, y);
            break;
        default:
            break;
    }
}

// The auto-progression annunciator: the chord sequence as note-name chips with
// the chord sounding now boxed amber. You can read where you are in the loop at
// a glance, and "PROG: tap chords" prompts the setup when it's still empty.
void drawProg(M5Canvas& c, uint32_t now) {
    if (!keys::progActive() || keys::quickEditActive()) return;
    int y = kScopeY + 3;
    if (looper::state() != looper::State::Empty) y += 10;  // yield the top line
    const int x0 = kTraceX + 2;
    c.setFont(&fonts::Font0);
    const int len = keys::progLen();
    if (len == 0) {
        c.setTextColor(theme::kDim, theme::kBg);
        c.drawString("PROG: tap chords", x0, y);
        return;
    }
    c.setTextColor(theme::kAmber, theme::kBg);
    c.drawString("PROG", x0, y);
    const int cur = keys::progIndex();
    int x = x0 + 28;
    char nm[6];
    for (int i = 0; i < len && x < kTraceX + kTraceW - 14; ++i) {
        keys::progStepName(i, nm, sizeof nm);
        const int w = (int)strlen(nm) * 6 + 3;
        if (i == cur) {
            c.fillRect(x - 1, y - 1, w, 9, theme::kAmber);
            c.setTextColor(theme::kBg, theme::kAmber);
        } else {
            c.setTextColor(theme::kDim, theme::kBg);
        }
        c.drawString(nm, x + 1, y);
        x += w + 2;
    }
}

// Low-battery warning: a pocket instrument that dies mid-jam without telling
// you is a broken promise. Amber and steady from kBatWarnPct, red and blinking
// from kBatCritPct.
//
// The thresholds LATCH, and that is the entire point of this function. The
// gauge is one ADC read of a rail the speaker sags (see the config.h note), so
// the old bare `level > 20` test made the badge appear and vanish every poll
// while you played — a warning that cries wolf is worse than none. Once armed,
// a latch holds until the reading has recovered past a wide clear line on
// kBatClearPolls polls in a row, so no single noisy sample can disarm it.
//
// The percentage shown is the LOWEST reading since the warning armed. It only
// ever falls, so the digits never bounce, and it is the honest number: the
// worst this battery has actually measured.
//
// NOT handled here, deliberately: suppressing the badge while charging.
// isCharging() returns charge_unknown on this board — M5 document that the
// Cardputer cannot report charge status at all — so it has to be inferred from
// the voltage trend, and nobody has measured what that trend looks like yet.
// Until then a charging unit clears the latch by itself once the reading
// passes the clear line. See docs/roadmap/22-battery-warning.md.
void drawBattery(M5Canvas& c, uint32_t now) {
    static int level = -1;       // last raw reading
    static int shown = -1;       // lowest reading since the warning armed
    static uint32_t lastPoll = 0;
    static bool warned = false, crit = false;
    static uint8_t clearRun = 0;

    if (level < 0 || now - lastPoll > cfg::kBatPollMs) {
        lastPoll = now;
        level = M5.Power.getBatteryLevel();
        if (level >= 0) {
            if (level <= cfg::kBatWarnPct) warned = true;
            if (level <= cfg::kBatCritPct) crit = true;
            if (warned && (shown < 0 || level < shown)) shown = level;
            // Only the latch currently showing gets to clear; when red drops
            // back to amber the run restarts against amber's own clear line.
            const bool recovering =
                level >= (crit ? cfg::kBatCritClear : cfg::kBatWarnClear);
            if (recovering) {
                if (++clearRun >= cfg::kBatClearPolls) {
                    clearRun = 0;
                    if (crit) crit = false;                    // red -> amber
                    else { warned = false; shown = -1; }       // amber -> gone
                }
            } else {
                clearRun = 0;
            }
        }
    }
    if (!warned || shown < 0 || keys::quickEditActive()) return;
    if (crit && (now >> 9) & 1) return;  // blink when critical
    char buf[12];
    snprintf(buf, sizeof buf, "BAT %d%%", shown);
    c.setFont(&fonts::Font0);
    c.setTextColor(crit ? theme::kRed : theme::kAmber, theme::kBg);
    // drop below whatever owns the top-left corner (loop and/or progression)
    int y = kScopeY + 3;
    if (looper::state() != looper::State::Empty) y += 10;
    if (keys::progActive()) y += 10;
    c.drawString(buf, kTraceX + 2, y);
}

void drawHint(M5Canvas& c) {
    c.setFont(&fonts::Font0);
    if (keys::quickEditActive()) {
        // Name the sound row here. The edit panel already lists the ten slots
        // down its right column — but that column yields to the context viz for
        // six of the ten parameters, and the selection is sticky, so one poke at
        // ATTACK hides the bank on every subsequent fn hold. This line is the
        // one thing always on screen while fn is down, and players reported
        // never finding the sounds at all. It spells the range the same way the
        // edit status bar three rows up does ("q-p sound") — two spellings of
        // one range on one screen just looked like two different things. No
        // "release to play": nobody holds fn by accident, and the row is worth
        // more as two real gestures than as one gesture plus a reminder.
        c.setTextColor(theme::kAmber, theme::kBg);
        c.drawString("q - p sounds   1-0 param", 2, kHintY);
        return;
    }
    if (demo::active()) {
        c.setTextColor(theme::kAmber, theme::kBg);
        c.drawString("playing itself - any key takes over", 2, kHintY);
        return;
    }
    // the hint line turns loop-aware while a take exists — the gestures live
    // on screen exactly when they're relevant (no more hunting for clear)
    const looper::State ls = looper::state();
    c.setTextColor(theme::kDim, theme::kBg);
    if (ls == looper::State::Recording)
        c.drawString("recording...  alt: close the loop", 2, kHintY);
    else if (keys::progActive())
        c.drawString("tap row = chord progression   bksp clear", 2, kHintY);
    else if (ls != looper::State::Empty)
        c.drawString("alt dub   hold clear   fn+alt undo", 2, kHintY);
    else
        c.drawString("fn edit  tab setup  shift chrom  ` exit", 2, kHintY);  // 40ch=240px @x2
}

// The morph strip, top-center of the scope: current sound on the left (green,
// the live side), the previous sound on the right (amber), the bar filling
// toward whichever one the blend is leaning into. Drawn whenever the blend is
// off-center — G0 leans and switch transitions both — so the pair is always
// named on screen and never has to be remembered.
void drawMorphStrip(M5Canvas& c) {
    const float p = morph::pos();
    if (p < 0.02f || keys::quickEditActive() || !store::morphSourceValid()) return;
    c.setFont(&fonts::Font0);
    char cur[14], src[14];
    snprintf(cur, sizeof cur, "%s", store::liveName());
    snprintf(src, sizeof src, "%s", store::morphSourceName());
    const int bw = 40, bx = cfg::kScreenW / 2 - bw / 2, by = kScopeY + 4;
    c.setTextDatum(top_right);
    c.setTextColor(theme::kGreen, theme::kBg);
    c.drawString(cur, bx - 5, by);
    c.setTextDatum(top_left);
    c.setTextColor(theme::kAmber, theme::kBg);
    c.drawString(src, bx + bw + 5, by);
    c.drawRect(bx, by, bw, 7, theme::kLine);
    c.fillRect(bx + 1, by + 1, (int)((bw - 2) * p + 0.5f), 5, theme::kAmber);
}

void drawIntro(M5Canvas& c) {
    const int w = 212, h = 92, x = (cfg::kScreenW - w) / 2, y = 20;
    c.fillRoundRect(x, y, w, h, 5, theme::kPanel);
    c.drawRoundRect(x, y, w, h, 5, theme::kAmber);
    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString("GLIDE - sounds made for you", x + 8, y + 6);
    c.setTextColor(theme::kIdle, theme::kPanel);
    c.drawString("your sounds are unique to this", x + 8, y + 20);
    c.drawString("device. q is always home (GLIDE).", x + 8, y + 31);
    c.drawString("make your own anytime:", x + 8, y + 42);
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString("tab > CREATE > Randomize / Mutate", x + 8, y + 53);
    c.setTextColor(theme::kGreen, theme::kPanel);
    c.drawString("undo anything - press a key to play", x + 8, y + 72);
}

}  // namespace

void run() {
    M5Canvas canvas(&M5Cardputer.Display);
    if (!canvas.createSprite(cfg::kScreenW, cfg::kScreenH)) {
        // No RAM for the frame buffer is a visible failure, not a blank stare.
        M5Cardputer.Display.fillScreen(theme::kBg);
        M5Cardputer.Display.setTextColor(theme::kRed);
        M5Cardputer.Display.drawString("UI ALLOC FAILED", 10, 40);
        for (;;) delay(1000);
    }

    uint32_t introShownAt = millis();

    // ---- idle dimming / screensaver state ----------------------------------
    // Hands-off time (keys::lastActivityMs) walks the screen through three stages:
    // 0 full brightness, 1 dimmed, 2 the phosphor screensaver. The backlight
    // eases between levels — dims slowly, wakes fast, so the first touch feels
    // instant (and, since it went through keys::poll, it also plays the note).
    // Plain locals: run() never returns, so they live for the session.
    float briCur = (float)cfg::kBrightNormal;  // current backlight, ramped
    int   briSet = -1;                         // last value pushed to the panel
    int   idleStage = 0, prevIdleStage = 0;

    for (;;) {
        const uint32_t frameStart = millis();
        auto& cf = store::get();

        keys::Actions act = keys::poll(frameStart);

        // Which idle stage are we in? (Guarded by the "Screen idle" setting.)
        const uint32_t idle = frameStart - keys::lastActivityMs();
        idleStage = 0;
        if (cf.idleMode >= 1 && idle >= cfg::kIdleDimMs) idleStage = 1;
        if (cf.idleMode >= 2 && idle >= cfg::kScreensaverMs) idleStage = 2;
        // Demo mode is an unattended showcase — keep the screen lit and playing
        // for it. (A plain loop/jam still lets the screen rest: the screensaver
        // breathes with it, which is half the point.)
        if (demo::active()) idleStage = 0;
        if (idleStage != prevIdleStage) {
            if (idleStage == 2) screensaver::reset();
            if (prevIdleStage == 2) {  // waking from the saver: restart the short-
                gPrevValid = false;    // history scope modes clean, like leaving settings
                gTrailInit = false;
                gTapeInit = false;
            }
            prevIdleStage = idleStage;
        }
        // ease the backlight toward the stage's level (wake fast, dim slowly)
        const float briTarget = idleStage == 0   ? (float)cfg::kBrightNormal
                                : idleStage == 1 ? (float)cfg::kBrightDim
                                                 : (float)cfg::kBrightSaver;
        if (briCur < briTarget) briCur = fminf(briCur + 24.f, briTarget);
        else if (briCur > briTarget) briCur = fmaxf(briCur - 4.f, briTarget);
        const int bi = (int)(briCur + 0.5f);
        if (bi != briSet) {
            M5Cardputer.Display.setBrightness((uint8_t)bi);
            briSet = bi;
        }
        looper::tick(frameStart);  // schedule due loop-playback events
        if (demo::pending()) demo::start(frameStart);  // armed from settings
        if (demo::active() && act.gridPressed) demo::stop();  // the takeover
        demo::tick(frameStart);
        if (act.gridPressed) soundcard::dismiss();  // playing reclaims the scope

        if (act.exitApp) {
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::AllOff, 0));
            led::off();
            store::persistNow();
            store::flushMorphPartner();  // the blend pair must survive the reboot
            delay(120);  // let the release tails fade
            ESP.restart();
        }
        if (act.openSettings) {
            // a modal owns its own loop — hand it a full-brightness screen, and
            // reset the ramp so returning to perform doesn't flicker back up
            M5Cardputer.Display.setBrightness(cfg::kBrightNormal);
            briCur = (float)cfg::kBrightNormal;
            briSet = cfg::kBrightNormal;
            demo::stop();  // the bed survives; the melody yields to the menus
            settings::run(canvas);
            keys::resync();
            gPrevValid = false;
            gTrailInit = false;  // restart the pitch trail clean
            gTapeInit = false;   // short-history modes restart; the long-memory
                                 // modes (strata/constellation/bloom) keep their
                                 // page — that history is their whole point
            introShownAt = millis();
            continue;
        }
        if (act.listen) {
            M5Cardputer.Display.setBrightness(cfg::kBrightNormal);
            briCur = (float)cfg::kBrightNormal;
            briSet = cfg::kBrightNormal;
            demo::stop();  // same yield as settings
            listen_screen::run(canvas);
            // No keys::resync() here: LISTEN owns that now. Its result card
            // is played over, so it resyncs BEFORE the card starts polling —
            // a second one on the way back would clearLeadNotes() and cut a
            // note still held as the card closed.
            gPrevValid = false;
            gTrailInit = false;
            gTapeInit = false;
            continue;
        }

        if (!cf.seenIntro && (act.gridPressed || millis() - introShownAt > cfg::kIntroMs)) {
            cf.seenIntro = true;
            store::markDirty();
        }

        applyTilt();

        // G0 trigger macro: momentary reads the level; latch toggles on each
        // press (rising edge), so a tap arms it and a second tap releases.
        const bool trigRaw = keys::triggerHeld();
        static bool trigPrev = false, trigLatched = false;
        static uint8_t trigActPrev = cf.triggerAction;
        if (cf.triggerAction != trigActPrev) {  // action changed in settings:
            trigActPrev = cf.triggerAction;     // never come back pre-engaged
            trigLatched = false;
        }
        if (trigRaw && !trigPrev) trigLatched = !trigLatched;
        trigPrev = trigRaw;
        const bool trigEngaged = cf.triggerLatch ? trigLatched : trigRaw;
        const bool trigMorph =
            (store::TriggerAction)cf.triggerAction == store::TriggerAction::Morph;

        // synth morph: G0-Morph leans toward the previous sound by the trigger
        // depth; a sound switch kicked pos to 1 and it glides home from here
        morph::setHold(trigMorph && trigEngaged ? cf.triggerDepth : 0.f);
        morph::tick(frameStart);

        // lead = live sound; backing = its frozen sound when the jam is locked.
        // Both are local copies so the G0 trigger macro never bakes into the
        // saved sound (drive especially is a real param, not a live-mod field).
        dsp::SynthParams leadParams = cf.synth;
        dsp::SynthParams backParams = cf.backingLocked ? cf.backingSynth : cf.synth;
        if (morph::pos() > 0.001f)  // blend the LEAD only; the bed stays steady
            leadParams = dsp::morphParams(leadParams, store::morphSource(), morph::pos());
        if (trigEngaged && !trigMorph)
            applyTrigger(leadParams, backParams, cf.triggerAction, cf.triggerDepth);
        audio::setParams(leadParams, backParams);
        // NVS flushes happen only at a quiet moment: hands off for a few
        // seconds and no backing being scheduled from this loop. On a crowded
        // shared partition the flush's flash-GC stall is SECONDS (measured:
        // persistNow 1.5-2 s after a preset switch, morph blob ~0.4 s) — in
        // the 500 ms debounce it froze this loop mid-gesture, eating the fn
        // release and hanging the quick menu open until the write returned.
        // Never quiet here = the settings-close / exit flush picks it up.
        const bool quiet =
            frameStart - keys::lastActivityMs() > 4000 && !keys::backingActive();
        store::tick(frameStart, quiet);
        if (quiet) store::flushMorphPartner();

        // onboard LED mirrors the lead voice: pitch -> hue, activity ->
        // brightness, fresh attacks and bends throw a white sparkle
        {
            static bool prevLedActive = false;
            auto ld = audio::lead();
            const bool bending = fabsf(keys::bendCentsNow()) > 2.f;
            const bool accent = (ld.active && !prevLedActive) || bending;
            prevLedActive = ld.active;
            led::update(ld.active, ld.pitchMidi, 1.f, accent);
        }

        // ---- draw ----------------------------------------------------------
        // Deep idle: the screensaver takes the whole frame. Everything above
        // still ran (audio, tilt, LED, the loop/jam clocks), so a backing keeps
        // playing under it — only the drawing is replaced.
        if (idleStage >= 2) {
            screensaver::draw(canvas, frameStart);
            canvas.pushSprite(0, 0);
            const uint32_t spent = millis() - frameStart;
            if (spent < cfg::kFrameMs) delay(cfg::kFrameMs - spent);
            continue;
        }

        canvas.fillScreen(theme::kBg);
        drawStatus(canvas);
        drawScope(canvas, frameStart);
        if (!keys::quickEditActive()) drawCrt(canvas);  // CRT bezel over the trace
        drawReadout(canvas);
        drawLoop(canvas, frameStart);
        drawProg(canvas, frameStart);
        drawMorphStrip(canvas);
        drawBattery(canvas, frameStart);
        drawBottom(canvas, frameStart);
        drawHint(canvas);
        soundcard::draw(canvas, frameStart);  // under the HUD: fresh feedback wins
        hud::draw(canvas, frameStart);
        if (!cf.seenIntro) drawIntro(canvas);
        if (demo::active() && morph::pos() < 0.02f && ((frameStart >> 9) & 1) &&
            !keys::quickEditActive()) {  // blinking DEMO badge (yields to the strip)
            canvas.setFont(&fonts::Font0);
            canvas.setTextColor(theme::kAmber, theme::kBg);
            canvas.setTextDatum(top_center);
            canvas.drawString("DEMO", cfg::kScreenW / 2, kScopeY + 4);
            canvas.setTextDatum(top_left);
        }
        if (trigEngaged && !trigMorph) {  // G0 macro engaged — name the action
                                          // (Morph draws its own strip instead)
            char tb[12];
            snprintf(tb, sizeof tb, "%s%s", store::triggerActionTag(cf.triggerAction),
                     cf.triggerLatch ? "*" : "");
            canvas.setFont(&fonts::Font0);
            canvas.setTextColor(theme::kAmber, theme::kBg);
            canvas.setTextDatum(top_center);
            canvas.drawString(tb, cfg::kScreenW / 2, kScopeY + 4);
            canvas.setTextDatum(top_left);
        }
        canvas.pushSprite(0, 0);

        const uint32_t spent = millis() - frameStart;
        if (spent < cfg::kFrameMs) delay(cfg::kFrameMs - spent);
    }
}

}  // namespace perform
