// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "settings_screen.h"

#include <esp_random.h>

#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../dsp/params.h"
#include "../dsp/patches.h"
#include "../dsp/scales.h"
#include "../dsp/sound_gen.h"
#include "../io/audio_engine.h"
#include "../io/demo.h"
#include "../io/keys.h"
#include "../io/looper.h"
#include "../io/sd_store.h"
#include "../io/tilt.h"
#include "../storage/glide_config.h"
#include "audition.h"
#include "coach.h"
#include "help.h"
#include "sd_browser.h"
#include "sound_card.h"
#include "text_entry.h"
#include "sound_viz.h"
#include "theme.h"

namespace settings {

namespace {

bool gOpenHelp = false;   // set by the Help item; run() opens the modal (it owns the canvas)
bool gOpenSdLoad = false; // set by "Load from SD"; run() opens the browser modal
bool gOpenSdSave = false; // set by "Save to SD"; run() opens the name prompt
int gFlashRow = -1;       // a one-shot row blink confirming an action fired
uint32_t gFlashUntil = 0;
float gMutateAmt = 0.30f; // how far each Mutate roams (session pref; 0..1)
char gLastSaved[24] = ""; // name of the most recent Save to SD (shown in its row)
bool gReRollArmed = false;     // Re-roll bank is irreversible -> two-tap confirm
uint32_t gReRollArmedAt = 0;

// positional key codes (y*14+x) — same convention as keys.cpp
constexpr int kUp = 39;     // ;
constexpr int kDown = 53;   // .
constexpr int kLeft = 52;   // ,
constexpr int kRight = 54;  // /
constexpr int kDecAlt = 25; // [
constexpr int kIncAlt = 26; // ]
constexpr int kEnter = 41;
constexpr int kFn = 28;     // fn — held, jumps section to section
constexpr int kExit1 = 0;   // `
constexpr int kExit2 = 14;  // tab

template <typename T>
T clampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

struct Item {
    const char* name;
    void (*format)(char* out, int cap);
    void (*adjust)(int dir);
    bool repeatable;   // true = hold ,/ to ramp (continuous numerics); enums/
                       // toggles/actions stay tap-only (left {} = false by default)
    float (*fill)();   // 0..1 for a value gauge drawn mid-row (nullptr = none;
                       // return <0 to skip this frame, e.g. a synced delay time)
    bool bipolar;      // fill() is -1..1, drawn from a centre-zero tick
};

// A value string ending in this (non-printing) tag tells the row renderer to draw
// the , and / keys' LEFT/RIGHT arrow icons after the text — the "press left/right
// here" affordance on action rows. Those keys are silk-screened with arrows, so
// we show the arrows, not the literal punctuation. Drawn (Font2 has no arrow
// glyphs); Font0 contexts use the CP437 glyphs \x11 \x10 \x1e \x1f directly.
constexpr char kLRtag = '\x01';

void fRoot(char* o, int c) { snprintf(o, c, "%s", dsp::kNoteNames[store::get().layout.rootSemis]); }
void aRoot(int d) {
    auto& l = store::get().layout;
    l.rootSemis = (uint8_t)(((int)l.rootSemis + d + 12) % 12);
}

void fScale(char* o, int c) { snprintf(o, c, "%s", dsp::kScales[store::get().layout.scaleIdx].name); }
void aScale(int d) {
    auto& l = store::get().layout;
    l.scaleIdx = (uint8_t)(((int)l.scaleIdx + d + dsp::kScaleCount) % dsp::kScaleCount);
}

void fRowInt(char* o, int c) { snprintf(o, c, "%d st", store::get().layout.rowIntervalSemis); }
void aRowInt(int d) {
    auto& l = store::get().layout;
    l.rowIntervalSemis = (uint8_t)clampT((int)l.rowIntervalSemis + d, 1, 12);
}

void fGlideMode(char* o, int c) {
    snprintf(o, c, "%s",
             store::get().synth.glideMode == dsp::GlideMode::LegatoOnly ? "legato only" : "always");
}
void aGlideMode(int) {
    auto& s = store::get().synth;
    s.glideMode = s.glideMode == dsp::GlideMode::LegatoOnly ? dsp::GlideMode::Always
                                                            : dsp::GlideMode::LegatoOnly;
}

void fStringMode(char* o, int c) {
    snprintf(o, c, "%s", store::get().stringMode ? "strings (mono rows)" : "free poly");
}
void aStringMode(int) { store::get().stringMode = !store::get().stringMode; }

void fJamRows(char* o, int c) {
    const uint8_t j = store::get().jamRows;
    snprintf(o, c, "%s", j == 0 ? "off" : (j == 1 ? "bottom row" : "bottom 2 rows"));
}
void aJamRows(int d) {
    auto& g = store::get();
    g.jamRows = (uint8_t)clampT((int)g.jamRows + d, 0, 2);
}

void fOctGlide(char* o, int c) {
    snprintf(o, c, "%s", store::get().octaveGlide ? "sweep (glide)" : "re-strike");
}
void aOctGlide(int) { store::get().octaveGlide = !store::get().octaveGlide; }

void fDroneVoice(char* o, int c) {
    const uint8_t v = store::get().droneVoicing;
    snprintf(o, c, "%s", v == 0 ? "single" : (v == 1 ? "+ octave" : "+ fifth"));
}
void aDroneVoice(int d) {
    auto& g = store::get();
    g.droneVoicing = (uint8_t)clampT((int)g.droneVoicing + d, 0, 2);
}

// Jam octave: where the backing (drones + progression chords) sits relative to
// the grid. Honest about the ceiling fold: when the grid is so high that the
// register folds down (dsp::backingShift), the row says so.
void fJamOct(char* o, int c) {
    static const char* kNames[5] = {"-2 oct (way under)", "-1 oct (under grid)",
                                    "0 (with the grid)", "+1 oct (over grid)",
                                    "+2 oct (way over)"};
    const auto& l = store::get().layout;
    const int v = clampT((int)l.jamOctave, -2, 2);
    const int got = (int)(dsp::backingShift(l) / 12.f);
    if (got != v) snprintf(o, c, "%+d oct (folds to %+d)", v, got);
    else snprintf(o, c, "%s", kNames[v + 2]);
}
void aJamOct(int d) {
    auto& l = store::get().layout;
    l.jamOctave = (int8_t)clampT((int)l.jamOctave + d, -2, 2);
    keys::backingRegisterChanged();  // hear it now, not on the next bar
}

void fJamMotion(char* o, int c) {
    // "arp (1 drone/beat)": the older per-beat drone walk. The arpeggiator
    // proper is fn+a (a texture on the progression); this label keeps the
    // settings row from being the second thing on the device called "arp".
    static const char* kNames[4] = {"sustained", "pulse", "arp (1 drone/beat)", "progression"};
    const uint8_t m = store::get().jamMotion;
    snprintf(o, c, "%s", kNames[m < 4 ? m : 0]);
}
void aJamMotion(int d) {
    auto& g = store::get();
    g.jamMotion = (uint8_t)clampT((int)g.jamMotion + d, 0, 3);
}

void fJamBpm(char* o, int c) { snprintf(o, c, "%d bpm", store::get().jamBpm); }
void aJamBpm(int d) {
    auto& g = store::get();
    g.jamBpm = (uint16_t)clampT((int)g.jamBpm + d * 4, 40, 240);
}

// Metronome level (the toggle is the live fn+\ gesture — one source of truth;
// fn+ctrl/opt steps this same value mid-performance).
void fMetroVol(char* o, int c) {
    const auto& g = store::get();
    snprintf(o, c, "%d%%%s", g.metroVol, g.metroOn ? "" : " (off)");
}
void aMetroVol(int d) {
    auto& g = store::get();
    g.metroVol = (uint8_t)clampT((int)g.metroVol + d * 5, 0, 100);
    // Audible under the finger: the perform loop's per-frame publish isn't
    // running inside settings, so push the level ourselves (params only — the
    // settings-close flush persists it).
    g.synth.metroLevel = g.metroVol;
    audio::setParams(g.synth, g.backingLocked ? g.backingSynth : g.synth);
    store::markDirty();
}
float gMetroVolF() { return store::get().metroVol / 100.f; }

void fJamChord(char* o, int c) { snprintf(o, c, "%d beats", store::get().jamChordBeats); }
void aJamChord(int d) {
    auto& g = store::get();
    g.jamChordBeats = (uint8_t)clampT((int)g.jamChordBeats + d, 1, 8);
}

// Loop snap: the pedal's close tap quantizes the take length to the jam clock
// (dsp::quantizeLoopMs), so loop + progression stay phase-locked instead of
// drifting. Off = the raw human length (the old free behaviour).
void fLoopSnap(char* o, int c) {
    const uint8_t m = store::get().loopSnap;
    snprintf(o, c, "%s", m == 0 ? "off" : (m == 1 ? "beat" : "bar"));
}
void aLoopSnap(int d) {
    auto& g = store::get();
    g.loopSnap = (uint8_t)clampT((int)g.loopSnap + d, 0, 2);
}

// The tilt map (both routes/depths + dual) is either a global rig setting that
// follows your hands across every sound, or a per-sound personality reloaded on
// each switch. Global by default (and matches how Morph already behaved).
void fTiltLock(char* o, int c) {
    snprintf(o, c, "%s", store::get().tiltLock ? "global" : "per sound");
}
void aTiltLock(int) { store::setTiltLock(!store::get().tiltLock); }

// The route rows cycle off/cutoff/vibrato/volume/morph as one sequence, but
// MORPH lands in the global rig flag (survives sound switches, like the G0
// action) while the physical routes stay per-patch personality. Cycling off
// morph un-masks the patch's own route.
void fTilt(char* o, int c) {
    if (store::get().tiltMorphA) snprintf(o, c, "morph (global)");
    else snprintf(o, c, "%s", store::tiltRouteName(store::get().tiltRoute));
}
void aTilt(int d) {
    auto& g = store::get();
    const int cur = g.tiltMorphA ? (int)store::TiltRoute::Morph : (int)g.tiltRoute;
    const int next = (cur + d + (int)store::TiltRoute::Count) % (int)store::TiltRoute::Count;
    if (next == (int)store::TiltRoute::Morph) {
        g.tiltMorphA = true;  // rig setting: the patch's own route is masked, kept
    } else {
        g.tiltMorphA = false;
        g.tiltRoute = (store::TiltRoute)next;
    }
}

void fTiltDepth(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().tiltDepth * 100)); }
void aTiltDepth(int d) {
    auto& g = store::get();
    g.tiltDepth = clampT(g.tiltDepth + d * 0.05f, 0.f, 1.f);
}

void fTiltB(char* o, int c) {
    if (store::get().tiltMorphB) snprintf(o, c, "morph (global)");
    else snprintf(o, c, "%s", store::tiltRouteName(store::get().tiltRouteB));
}
void aTiltB(int d) {
    auto& g = store::get();
    const int cur = g.tiltMorphB ? (int)store::TiltRoute::Morph : (int)g.tiltRouteB;
    const int next = (cur + d + (int)store::TiltRoute::Count) % (int)store::TiltRoute::Count;
    if (next == (int)store::TiltRoute::Morph) {
        g.tiltMorphB = true;
    } else {
        g.tiltMorphB = false;
        g.tiltRouteB = (store::TiltRoute)next;
    }
    // The roll axis only reads when the global 2D flag is armed (perform: axis B
    // is applied `if (tiltDual)`), and that flag defaults off + reverts off after
    // an NVS-pressure boot. So assigning the l/r axis a job here was silently
    // inert — "Tilt l/r route = vibrato" did nothing until you also found the
    // hidden enter-tap 2D gesture. Mirror cycleTilt's self-heal so the menu is
    // honest: a real route (or morph) arms the roll axis and floors its depth;
    // cycling back to plain "off" disarms it. Tilt itself must be on to read
    // either axis, so a live route turns it on too.
    const bool active = g.tiltMorphB || g.tiltRouteB != store::TiltRoute::Off;
    g.tiltDual = active;
    if (active) {
        g.tiltOn = true;
        if (g.tiltDepthB < 0.05f) g.tiltDepthB = 0.6f;
    }
}

void fTiltDepthB(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().tiltDepthB * 100)); }
void aTiltDepthB(int d) {
    auto& g = store::get();
    g.tiltDepthB = clampT(g.tiltDepthB + d * 0.05f, 0.f, 1.f);
}

void fTiltCenter(char* o, int c) {
    snprintf(o, c, "%+d (set: , /)", (int)(store::get().tiltCenter * 100));
}
void aTiltCenter(int) {
    // capture how you're holding it RIGHT NOW as "flat" — BOTH axes at once,
    // one gesture. The reading is one-pole smoothed (15%/step), so converge
    // before trusting it; a single poll would capture stale history.
    for (int i = 0; i < 30; ++i) {
        tilt::poll();
        delay(2);
    }
    auto& g = store::get();
    g.tiltCenter = tilt::raw();
    g.tiltCenterB = tilt::rawB();
}

void fPatchReset(char* o, int c) {
    const int slot = store::get().currentPatch;
    snprintf(o, c, "%s%s", store::patchName(slot),
             store::patchHasOverride(slot) ? "* -> factory" : " (factory)");
}
void aPatchReset(int) {
    const int slot = store::get().currentPatch;
    store::clearOverride(slot);
    store::applyPatch(slot);  // reload the factory sound immediately
}

// Blanket escape hatch: every saved slot back to factory in one tap, so you can
// experiment fearlessly and always return to stock. Settings (layout, tilt,
// jam...) are untouched — only the 10 sound overrides are dropped. The full
// nuke (sounds AND settings) is still the boot-time BKSP factory reset.
void fAllSoundsReset(char* o, int c) {
    int n = 0;
    for (int i = 0; i < dsp::kPatchCount; ++i) n += store::patchHasOverride(i) ? 1 : 0;
    if (n > 0) snprintf(o, c, "%d saved -> stock", n);
    else       snprintf(o, c, "all stock");
}
void aAllSoundsReset(int) {
    for (int i = 0; i < dsp::kPatchCount; ++i) store::clearOverride(i);
    store::applyPatch(store::get().currentPatch);  // reload the now-factory sound live
}

void fBendMs(char* o, int c) { snprintf(o, c, "%d ms", store::get().bendMs); }
void aBendMs(int d) { store::get().bendMs = (uint16_t)clampT((int)store::get().bendMs + d * 50, 50, 1000); }

// Synth morph: one time constant for all timbre glides — how long a sound
// switch takes to arrive, and how fast a G0 morph (Trigger action) sweeps.
void fMorphMs(char* o, int c) {
    const uint16_t m = store::get().morphMs;
    if (m == 0) snprintf(o, c, "off (snap)");
    else        snprintf(o, c, "%d ms", m);
}
void aMorphMs(int d) {
    auto& g = store::get();
    g.morphMs = (uint16_t)clampT((int)g.morphMs + d * 50, 0, 2000);
}
float gMorphMsF() { return store::get().morphMs / 2000.f; }

void fDetune(char* o, int c) { snprintf(o, c, "%d cents", (int)store::get().synth.detuneCents); }
void aDetune(int d) {
    auto& s = store::get().synth;
    s.detuneCents = (float)clampT((int)s.detuneCents + d * 2, 0, 50);
}

// Per-voice pitch wander. Defaults non-zero, so the row's job is mostly to let
// someone turn it DOWN — hence "off" rather than "0 cents" at the bottom.
void fDrift(char* o, int c) {
    const int v = (int)store::get().synth.driftCents;
    if (v == 0) snprintf(o, c, "off");
    else snprintf(o, c, "%d cents", v);
}
void aDrift(int d) {
    auto& s = store::get().synth;
    s.driftCents = (float)clampT((int)s.driftCents + d, 0, 12);
}

void fFilterMode(char* o, int c) {
    snprintf(o, c, "%s", dsp::filterModeName((dsp::FilterMode)store::get().synth.filterMode));
}
void aFilterMode(int d) {
    auto& s = store::get().synth;
    s.filterMode = (uint8_t)(((int)s.filterMode + d + (int)dsp::FilterMode::Count) % (int)dsp::FilterMode::Count);
}

void fRes(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.resonance * 100)); }
void aRes(int d) {
    auto& s = store::get().synth;
    s.resonance = clampT(s.resonance + d * 0.05f, 0.f, 0.95f);
}

// ---- the send-FX rack, now live-editable (and saved per slot like every
// other sound param). Dial the space, fn+shift+letter to keep it.
void fChorus(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.chorusDepth * 100)); }
void aChorus(int d) {
    auto& s = store::get().synth;
    s.chorusDepth = clampT(s.chorusDepth + d * 0.05f, 0.f, 1.f);
}

void fDelaySend(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.delayMix * 100)); }
void aDelaySend(int d) {
    auto& s = store::get().synth;
    s.delayMix = clampT(s.delayMix + d * 0.05f, 0.f, 1.f);
}

void fDelayTime(char* o, int c) {
    auto& s = store::get().synth;
    if (s.delaySync) snprintf(o, c, "%s (synced)", dsp::delaySyncName(s.delaySync));
    else snprintf(o, c, "%d ms", (int)(s.delayTimeS * 1000));
}
void aDelayTime(int d) {
    auto& s = store::get().synth;
    s.delayTimeS = (float)clampT((int)(s.delayTimeS * 1000) + d * 10, 10, 600) / 1000.f;
}

void fDelaySync(char* o, int c) { snprintf(o, c, "%s", dsp::delaySyncName(store::get().synth.delaySync)); }
void aDelaySync(int d) {
    auto& s = store::get().synth;
    s.delaySync = (uint8_t)(((int)s.delaySync + d + dsp::kDelaySyncCount) % dsp::kDelaySyncCount);
}

void fDelayFb(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.delayFb * 100)); }
void aDelayFb(int d) {
    auto& s = store::get().synth;
    s.delayFb = clampT(s.delayFb + d * 0.05f, 0.f, 0.9f);
}

void fReverbSend(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.reverbMix * 100)); }
void aReverbSend(int d) {
    auto& s = store::get().synth;
    s.reverbMix = clampT(s.reverbMix + d * 0.05f, 0.f, 1.f);
}

void fReverbSize(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().synth.reverbSize * 100)); }
void aReverbSize(int d) {
    auto& s = store::get().synth;
    s.reverbSize = clampT(s.reverbSize + d * 0.05f, 0.f, 1.f);
}

// Tap tempo: each press here is a beat — two or more in time set the jam BPM,
// which drives the progression AND the tempo-synced delay. The state machine
// lives in keys.cpp because `\` taps the same tempo out on the perform screen;
// sharing it means a series can start on one and finish on the other.
void fTapTempo(char* o, int c) { snprintf(o, c, "%d bpm  (or tap \\)", store::get().jamBpm); }
void aTapTempo(int) { keys::tapTempo(millis()); }

// The scope modes, in scopeMode order (append-only — the index persists).
// 0/1 are the originals; 2..5 are the generative modes from the viz lab.
constexpr const char* kScopeModeNames[] = {"waveform",     "pitch trail", "tape",
                                           "cymatic",      "string",      "comb",
                                           "harmonograph", "interference"};
constexpr int kScopeModeCount = (int)(sizeof(kScopeModeNames) / sizeof(kScopeModeNames[0]));
void fScopeMode(char* o, int c) {
    snprintf(o, c, "%s", kScopeModeNames[store::get().scopeMode < kScopeModeCount
                                             ? store::get().scopeMode
                                             : 0]);
}
void aScopeMode(int d) {
    auto& g = store::get();
    g.scopeMode = (uint8_t)(((int)g.scopeMode + d + kScopeModeCount) % kScopeModeCount);
}

void fTheme(char* o, int c) { snprintf(o, c, "%s", theme::name(theme::current())); }
void aTheme(int d) {
    auto& g = store::get();
    const uint8_t was = g.themeId;
    g.themeId = (uint8_t)(((int)g.themeId + d + theme::count()) % theme::count());
    // Arriving at "custom" from a preset opens it as a tweakable COPY of that
    // preset. Landing on a fixed default instead would yank the palette out from
    // under the hand that was cycling through them looking for one it liked.
    if (g.themeId == theme::customIndex() && was != theme::customIndex()) {
        theme::setLook(theme::recipeForPreset(was));
        g.themeLook = theme::packLook(theme::look());
    }
    theme::setTheme(g.themeId);  // live — the menu restyles under your finger
}

// ---- the custom palette's five dials --------------------------------------
// These rows are HIDDEN unless Theme reads "custom" (see isHidden), so the
// feature costs a player who never wants it exactly one extra word at the end
// of a cycle they already had. Every dial applies immediately, which makes the
// menu its own preview: it is drawn in the palette being edited.
constexpr const char* kHueNames[12] = {"red",  "orange", "yellow", "lime",
                                       "green", "mint",  "cyan",   "azure",
                                       "blue", "violet", "magenta", "pink"};
// The stock, named the way a player would describe it rather than as a percent.
constexpr const char* kGroundNames[9] = {"black", "near-black", "dusk",  "slate",
                                         "shadow", "ash",       "stock", "paper",
                                         "bright"};

const char* groundWord(int g) {
    static const uint8_t kEdge[9] = {2, 5, 9, 14, 20, 26, 31, 36, 41};
    for (int i = 0; i < 9; ++i)
        if (g < (int)kEdge[i]) return kGroundNames[i];
    return kGroundNames[8];
}

// The accent dial is an ANGLE from the primary, so name the RELATIONSHIP — that
// is what the player is actually choosing, and it survives turning Hue.
const char* accentWord(int deg) {
    if (deg < 20 || deg > 340) return "same";
    if (deg < 50 || deg > 310) return "near";
    if (deg < 100 || deg > 260) return "wide";
    if (deg < 140 || deg > 220) return "triad";
    if (deg < 165 || deg > 195) return "split";
    return "opposite";
}

void applyLook(const theme::Look& l) {
    theme::setLook(l);
    store::get().themeLook = theme::packLook(l);
    store::markDirty();
}

void fLookHue(char* o, int c) {
    const int deg = theme::look().hue * 5;
    snprintf(o, c, "%s %d", kHueNames[((deg + 15) / 30) % 12], deg);
}
void aLookHue(int d) {
    theme::Look l = theme::look();  // hue is a circle: it wraps, never clamps
    l.hue = (uint8_t)((l.hue + d + theme::kLookHueMax + 1) % (theme::kLookHueMax + 1));
    applyLook(l);
}

void fLookAccent(char* o, int c) {
    const int deg = theme::look().accent * 5;
    snprintf(o, c, "%s %d", accentWord(deg), deg);
}
void aLookAccent(int d) {
    theme::Look l = theme::look();
    l.accent =
        (uint8_t)((l.accent + d + theme::kLookAccentMax + 1) % (theme::kLookAccentMax + 1));
    applyLook(l);
}

void fLookVivid(char* o, int c) {
    snprintf(o, c, "%d%%", theme::look().vivid * 100 / theme::kLookVividMax);
}
void aLookVivid(int d) {
    theme::Look l = theme::look();
    l.vivid = (uint8_t)clampT((int)l.vivid + d, 0, (int)theme::kLookVividMax);
    applyLook(l);
}
float gLookVividF() { return (float)theme::look().vivid / (float)theme::kLookVividMax; }

void fLookGround(char* o, int c) {
    snprintf(o, c, "%s", groundWord(theme::look().ground));
}
void aLookGround(int d) {
    theme::Look l = theme::look();
    l.ground = (uint8_t)clampT((int)l.ground + d, 0, (int)theme::kLookGroundMax);
    applyLook(l);
}
float gLookGroundF() { return (float)theme::look().ground / (float)theme::kLookGroundMax; }

void fLookContrast(char* o, int c) {
    snprintf(o, c, "%d%%", theme::look().contrast * 100 / theme::kLookContrastMax);
}
void aLookContrast(int d) {
    theme::Look l = theme::look();
    l.contrast = (uint8_t)clampT((int)l.contrast + d, 0, (int)theme::kLookContrastMax);
    applyLook(l);
}
float gLookContrastF() {
    return (float)theme::look().contrast / (float)theme::kLookContrastMax;
}

// The look roller. Same bargain as the sound randomizer: a player who does not
// want to design anything still gets to own their instrument's face.
void fRollLook(char* o, int c) { snprintf(o, c, "surprise me%c", kLRtag); }
void aRollLook(int) {
    applyLook(theme::rollLook(esp_random()));
}

// Hands-off screen behaviour. off = always full brightness; dim = ease the
// backlight down after a spell; saver = dim, then a phosphor screensaver. Any
// key wakes it and plays. Timings/levels live in config.h.
void fIdle(char* o, int c) {
    static const char* kNames[3] = {"off", "dim", "dim + saver"};
    snprintf(o, c, "%s", kNames[store::get().idleMode < 3 ? store::get().idleMode : 0]);
}
void aIdle(int d) {
    auto& g = store::get();
    g.idleMode = (uint8_t)(((int)g.idleMode + d + 3) % 3);
}

void fBoot(char* o, int c) { snprintf(o, c, "%s", store::get().bootSound ? "on" : "off"); }
void aBoot(int) { store::get().bootSound = !store::get().bootSound; }

void fIntro(char* o, int c) { snprintf(o, c, "%s", store::get().seenIntro ? "hidden" : "will show"); }
void aIntro(int) { store::get().seenIntro = !store::get().seenIntro; }

// The odometer: lifetime notes struck and hands-on hours, read-only. No
// streaks, no goals — just the instrument's life with you, counted quietly.
void fOdo(char* o, int c) {
    const unsigned long n = store::odoNotes();
    const unsigned long s = store::odoSeconds();
    const unsigned long h = s / 3600, m = (s % 3600) / 60;
    if (h > 0)
        snprintf(o, c, "%lu notes  %luh %lum", n, h, m);
    else
        snprintf(o, c, "%lu notes  %lum", n, m);
}
void aOdo(int) {}  // an odometer only counts forward

// Shared-flash health, in player units (one "save" = one fn+shift slot write).
// Storage is self-managing since v2.8: saved sounds live on the SD card, the
// system's own sliver of the shared partition heals itself at boot, and this
// row just says which world the player is in — in words that don't need a
// degree. Read-only, like the odometer.
void fStorage(char* o, int c) {
    if (!store::nvsHealthy()) { snprintf(o, c, "unavailable"); return; }
    if (store::healedAtBoot()) { snprintf(o, c, "self-cleaned - OK"); return; }
    if (sdstore::available()) snprintf(o, c, "OK - sounds on SD");
    else snprintf(o, c, "OK - insert SD to save");
}
void aStorage(int) {}  // nothing to adjust — it manages itself

void fReset(char* o, int c) { snprintf(o, c, "press , or /"); }
void aReset(int) { store::resetDefaults(); }

// G0 top-button macro: pick the action, how hard it drives, and whether it's
// held (momentary) or tap-to-toggle (latch).
void fTrigAct(char* o, int c) { snprintf(o, c, "%s", store::triggerActionName(store::get().triggerAction)); }
void aTrigAct(int d) {
    auto& g = store::get();
    g.triggerAction = (uint8_t)(((int)g.triggerAction + d + (int)store::TriggerAction::Count) %
                                (int)store::TriggerAction::Count);
}

void fTrigDepth(char* o, int c) { snprintf(o, c, "%d %%", (int)(store::get().triggerDepth * 100)); }
void aTrigDepth(int d) {
    auto& g = store::get();
    g.triggerDepth = clampT(g.triggerDepth + d * 0.05f, 0.f, 1.f);
}

void fTrigMode(char* o, int c) { snprintf(o, c, "%s", store::get().triggerLatch ? "latch (tap)" : "momentary"); }
void aTrigMode(int) { store::get().triggerLatch = !store::get().triggerLatch; }

// ---- modulation: LFOs, mod-env, and the routing matrix --------------------
void fmtHz(char* o, int c, float hz) {
    const int h = (int)(hz * 100 + 0.5f);
    snprintf(o, c, "%d.%02d Hz", h / 100, h % 100);
}
void adjRate(float& hz, int d) {
    const float step = hz < 2.f ? 0.1f : (hz < 8.f ? 0.5f : 1.f);  // fine low, coarse high
    hz = clampT(hz + d * step, 0.05f, 30.f);
}
void fLfo1Rate(char* o, int c) { fmtHz(o, c, store::get().synth.lfo1RateHz); }
void aLfo1Rate(int d) { adjRate(store::get().synth.lfo1RateHz, d); }
void fLfo1Shape(char* o, int c) {
    snprintf(o, c, "%s", dsp::lfoShapeName((dsp::LfoShape)store::get().synth.lfo1Shape));
}
void aLfo1Shape(int d) {
    auto& s = store::get().synth;
    s.lfo1Shape = (uint8_t)(((int)s.lfo1Shape + d + (int)dsp::LfoShape::Count) % (int)dsp::LfoShape::Count);
}
void fLfo1Sync(char* o, int c) { snprintf(o, c, "%s", dsp::delaySyncName(store::get().synth.lfo1Sync)); }
void aLfo1Sync(int d) {
    auto& s = store::get().synth;
    s.lfo1Sync = (uint8_t)(((int)s.lfo1Sync + d + dsp::kDelaySyncCount) % dsp::kDelaySyncCount);
}
void fLfo2Rate(char* o, int c) { fmtHz(o, c, store::get().synth.lfo2RateHz); }
void aLfo2Rate(int d) { adjRate(store::get().synth.lfo2RateHz, d); }
void fLfo2Shape(char* o, int c) {
    snprintf(o, c, "%s", dsp::lfoShapeName((dsp::LfoShape)store::get().synth.lfo2Shape));
}
void aLfo2Shape(int d) {
    auto& s = store::get().synth;
    s.lfo2Shape = (uint8_t)(((int)s.lfo2Shape + d + (int)dsp::LfoShape::Count) % (int)dsp::LfoShape::Count);
}
void fLfo2Sync(char* o, int c) { snprintf(o, c, "%s", dsp::delaySyncName(store::get().synth.lfo2Sync)); }
void aLfo2Sync(int d) {
    auto& s = store::get().synth;
    s.lfo2Sync = (uint8_t)(((int)s.lfo2Sync + d + dsp::kDelaySyncCount) % dsp::kDelaySyncCount);
}
void fModEnvAtk(char* o, int c) { snprintf(o, c, "%d ms", (int)(store::get().synth.modEnvAtkS * 1000)); }
void aModEnvAtk(int d) {
    auto& s = store::get().synth;
    s.modEnvAtkS = clampT(s.modEnvAtkS + d * 0.005f, 0.001f, 2.f);
}
void fModEnvDec(char* o, int c) { snprintf(o, c, "%d ms", (int)(store::get().synth.modEnvDecS * 1000)); }
void aModEnvDec(int d) {
    auto& s = store::get().synth;
    s.modEnvDecS = clampT(s.modEnvDecS + d * 0.02f, 0.01f, 4.f);
}

// Per-slot src/dest/amount thunks (6 slots). A macro keeps the 18 functions
// honest — each binds to one slot by index. (C++11: no template thunk table.)
#define MOD_SLOT_THUNKS(N)                                                                 \
    void fSlot##N##Src(char* o, int c) {                                                   \
        snprintf(o, c, "%s", dsp::modSourceName((dsp::ModSource)store::get().synth.slots[N].src)); \
    }                                                                                      \
    void aSlot##N##Src(int d) {                                                            \
        auto& s = store::get().synth.slots[N];                                             \
        s.src = (uint8_t)(((int)s.src + d + (int)dsp::ModSource::Count) % (int)dsp::ModSource::Count); \
    }                                                                                      \
    void fSlot##N##Dst(char* o, int c) {                                                   \
        snprintf(o, c, "%s", dsp::modDestName((dsp::ModDest)store::get().synth.slots[N].dest)); \
    }                                                                                      \
    void aSlot##N##Dst(int d) {                                                            \
        auto& s = store::get().synth.slots[N];                                             \
        s.dest = (uint8_t)(((int)s.dest + d + (int)dsp::ModDest::Count) % (int)dsp::ModDest::Count); \
    }                                                                                      \
    void fSlot##N##Amt(char* o, int c) {                                                   \
        snprintf(o, c, "%+d %%", (int)(store::get().synth.slots[N].depth * 100));          \
    }                                                                                      \
    void aSlot##N##Amt(int d) {                                                            \
        auto& s = store::get().synth.slots[N];                                             \
        s.depth = clampT(s.depth + d * 0.05f, -1.f, 1.f);                                  \
    }                                                                                      \
    float gSlot##N##Amt() { return store::get().synth.slots[N].depth; }
MOD_SLOT_THUNKS(0)
MOD_SLOT_THUNKS(1)
MOD_SLOT_THUNKS(2)
MOD_SLOT_THUNKS(3)
MOD_SLOT_THUNKS(4)
MOD_SLOT_THUNKS(5)
#undef MOD_SLOT_THUNKS

void fHelp(char* o, int c) { snprintf(o, c, "open ->"); }
void aHelp(int) { gOpenHelp = true; }  // run() does the actual modal open

// The playable tour (ui/coach.cpp). Settings just arms it; run() exits and the
// perform loop launches the banner — the tour happens ON the instrument, not
// in a menu. Same hand-off as Demo mode.
void fTut(char* o, int c) {
    snprintf(o, c, "%s%c", store::get().tutDone ? "replay the tour" : "take the tour",
             kLRtag);
}
void aTut(int) { coach::requestTutorial(); }

// Demo mode: settings just arms it; run() exits and the perform loop starts it
// (the demo plays on the perform screen, where the scope and trail can dance).
void fDemo(char* o, int c) { snprintf(o, c, "play itself%c", kLRtag); }
void aDemo(int) { demo::requestStart(); }

// ---- value gauges: 0..1 fills for the continuous sound rows ---------------
// (same mixer-strip idea as the quick-edit layer, so a page of numbers reads
// at a glance; ranges mirror each row's adjust clamp)
float gRes()       { return store::get().synth.resonance / 0.95f; }
float gDetune()    { return store::get().synth.detuneCents / 50.f; }
float gDrift()     { return store::get().synth.driftCents / 12.f; }
float gChorus()    { return store::get().synth.chorusDepth; }
float gDelaySend() { return store::get().synth.delayMix; }
float gDelayTime() {  // synced: the ms value is dormant — no gauge
    const auto& s = store::get().synth;
    return s.delaySync ? -1.f : (s.delayTimeS * 1000.f - 10.f) / 590.f;
}
float gDelayFb()   { return store::get().synth.delayFb / 0.9f; }
float gRevSend()   { return store::get().synth.reverbMix; }
float gRevSize()   { return store::get().synth.reverbSize; }
float gLfoRate(float hz) { return logf(hz / 0.05f) / logf(30.f / 0.05f); }  // log axis
float gLfo1Rate()  { return gLfoRate(store::get().synth.lfo1RateHz); }
float gLfo2Rate()  { return gLfoRate(store::get().synth.lfo2RateHz); }
float gModAtk()    { return store::get().synth.modEnvAtkS / 2.f; }
float gModDec()    { return store::get().synth.modEnvDecS / 4.f; }
float gMutAmtF()   { return gMutateAmt; }
float gTiltDep()   { return store::get().tiltDepth; }
float gTiltDepB()  { return store::get().tiltDepthB; }
float gTrigDep()   { return store::get().triggerDepth; }
float gBendMsF()   { return (store::get().bendMs - 50.f) / 950.f; }
float gJamBpmF()   { return (store::get().jamBpm - 40.f) / 200.f; }

// ---- sound-design starting points -----------------------------------------
void pushLiveSound() {  // apply the working sound to the engine + persist
    auto& g = store::get();
    g.synth.tempoBpm = (float)g.jamBpm;
    audio::setParams(g.synth, g.backingLocked ? g.backingSynth : g.synth);
    store::markDirty();  // a player action — adopts a demo loan before the flush
    store::persistNow();
}

// The randomize/load audition phrase now lives in ui/audition.{h,cpp} (shared
// with the SD browser's preview). Call audition::start() after changing the live
// sound; audition::tick() each frame from run().

// Build a GenPatch snapshot of the live sound (for mutate / naming / saving).
dsp::GenPatch liveAsGen() {
    const auto& g = store::get();
    dsp::GenPatch gp;
    gp.synth = g.synth;
    gp.tiltRoute = (uint8_t)g.tiltRoute;
    gp.tiltDepth = g.tiltDepth;
    gp.tiltRouteB = (uint8_t)g.tiltRouteB;
    gp.tiltDepthB = g.tiltDepthB;
    return gp;
}

void fInitSound(char* o, int c) { snprintf(o, c, "blank slate%c", kLRtag); }
void aInitSound(int) {
    auto& g = store::get();
    store::historyCheckpoint();                // undoable — never trash a sound
    keys::soundSwitchBegin();                  // over a jam: blank the SOLO, bed holds
    const float vol = g.synth.masterVol;       // keep the player's level
    g.synth = dsp::SynthParams();              // neutral: plain saw, no FX, no mod
    g.synth.masterVol = vol;
    store::refreshLiveName();                  // the blank sound gets its own name
    pushLiveSound();
    soundcard::show();                         // the blank face, seen
}

// THE button. Roll a whole new patch from a fresh hardware-random seed, land it
// live, and audition it on the spot — hit it until it sings. Nothing is saved
// until you shift-save onto a slot, so a roll can never wreck a sound you kept.
void fRandomize(char* o, int c) { snprintf(o, c, "surprise me%c", kLRtag); }
void aRandomize(int) {
    store::historyCheckpoint();
    keys::soundSwitchBegin();  // over a jam: freeze the backing so the roll is solo-only
    // The expanded archetype pool — Randomize always rolls with the newest
    // engine (a fresh hardware seed each press: no stored-seed continuity to
    // preserve). The character is drawn explicitly so the card can NAME it.
    const uint32_t sd = esp_random();
    const dsp::Archetype arch = dsp::archetypeForSeedV3(sd);
    store::applyGenerated(dsp::generateSoundV4(sd, arch));
    audition::start();
    soundcard::showRolled((uint8_t)arch, audition::lengthMs());  // see the roll — and its character, in colour
    coach::notify(coach::Ev::Randomize);
}

// Evolve the CURRENT sound instead of rolling fresh — sculpt toward a vibe. The
// amount knob below sets how far it roams.
void fMutate(char* o, int c) { snprintf(o, c, "evolve this%c", kLRtag); }
void aMutate(int) {
    store::historyCheckpoint();
    keys::soundSwitchBegin();  // over a jam: evolve the SOLO, the backing holds
    store::applyGenerated(dsp::mutateSound(liveAsGen(), gMutateAmt, esp_random()));
    audition::start();
    soundcard::show(audition::lengthMs());
}

void fMutAmt(char* o, int c) { snprintf(o, c, "%d %%", (int)(gMutateAmt * 100 + 0.5f)); }
void aMutAmt(int d) { gMutateAmt = clampT(gMutateAmt + d * 0.05f, 0.05f, 1.f); }

// Non-destructive history: step back to a sound you had, or forward again.
void fUndo(char* o, int c) {
    const int d = store::historyUndoDepth();
    if (d > 0) snprintf(o, c, "step back (%d)%c", d, kLRtag);
    else       snprintf(o, c, "(nothing back)");
}
void aUndo(int) {
    if (!store::historyCanUndo()) return;
    keys::soundSwitchBegin();  // undo/redo move the solo only; the backing holds
    store::historyUndo();
    audition::start();
    soundcard::show(audition::lengthMs());
}
void fRedo(char* o, int c) {
    if (store::historyCanRedo()) snprintf(o, c, "step forward%c", kLRtag);
    else                         snprintf(o, c, "(nothing fwd)");
}
void aRedo(int) {
    if (!store::historyCanRedo()) return;
    keys::soundSwitchBegin();
    store::historyRedo();
    audition::start();
    soundcard::show(audition::lengthMs());
}

// Save the live sound to the SD library under an auto-generated, evocative name
// (e.g. "warm-haze-3f") derived from the sound itself — so it's reproducibly
// yours. Shows the name in the row on success.
// Pick the filename for a save: the sound's own name (= what the status bar
// shows), but if a DIFFERENT sound already holds that name, suffix -2/-3.. so a
// save never silently clobbers another sound. Re-saving the SAME sound (same
// hash) overwrites its own file (idempotent). `out` cap >= 24.
void chooseSaveName(const char* base, const dsp::GenPatch& gp, char* out, int cap) {
    if (!sdstore::exists(base)) { snprintf(out, cap, "%s", base); return; }
    store::PatchData ex;  // seed from GLIDE so a short file still decodes cleanly
    const dsp::Patch& fp = dsp::factoryPatches()[0];
    ex.synth = fp.synth;
    ex.tiltRoute = (uint8_t)fp.tiltRoute; ex.tiltDepth = fp.tiltDepth;
    ex.tiltRouteB = (uint8_t)fp.tiltRouteB; ex.tiltDepthB = fp.tiltDepthB;
    if (sdstore::load(base, ex)) {
        dsp::GenPatch eg;
        eg.synth = ex.synth; eg.tiltRoute = ex.tiltRoute; eg.tiltDepth = ex.tiltDepth;
        eg.tiltRouteB = ex.tiltRouteB; eg.tiltDepthB = ex.tiltDepthB;
        // FULL hash: two sounds differing only in a name-hash-exempt field
        // (delay fb, LFO shape...) are different sounds — suffix, don't clobber
        if (dsp::patchHashFull(eg) == dsp::patchHashFull(gp)) { snprintf(out, cap, "%s", base); return; }
    }
    for (int i = 2; i <= 9; ++i) {
        snprintf(out, cap, "%s-%d", base, i);
        if (!sdstore::exists(out)) return;
    }
    snprintf(out, cap, "%s", base);  // library full of this name (≥8): overwrite base
}

void fSaveSd(char* o, int c) {
    if (gLastSaved[0]) snprintf(o, c, "%s", gLastSaved);
    else               snprintf(o, c, "name & save%c", kLRtag);
}
// Write the live sound to the card under `wantName`. The typed name is
// sanitised FIRST so any collision suffix lands on the real stem, and the file
// is then written under exactly the stem stored inside it — a long or
// punctuated name must never save as one thing and read back as another.
void saveLiveToSd(const char* wantName) {
    const dsp::GenPatch gp = liveAsGen();
    store::PatchData pd;
    pd.synth = gp.synth;
    pd.synth.bendCents = 0.f; pd.synth.vibratoCents = 0.f;  // never bake live-mods
    pd.synth.cutoffModOct = 0.f; pd.synth.volMod = 1.f; pd.synth.tempoBpm = 120.f;
    pd.tiltRoute = gp.tiltRoute; pd.tiltDepth = gp.tiltDepth;
    pd.tiltRouteB = gp.tiltRouteB; pd.tiltDepthB = gp.tiltDepthB;
    char base[sdstore::kMaxNameLen + 1];
    sdstore::sanitize(wantName, base, sizeof base);
    char finalName[24];
    chooseSaveName(base, gp, finalName, sizeof finalName);
    char stem[sdstore::kMaxNameLen + 1];   // the suffix may push past the length
    sdstore::sanitize(finalName, stem, sizeof stem);  // cap — re-clamp, once
    strncpy(pd.name, stem, sizeof pd.name - 1);
    pd.name[sizeof pd.name - 1] = '\0';
    if (sdstore::save(stem, pd)) {
        strncpy(gLastSaved, pd.name, sizeof gLastSaved - 1);
        gLastSaved[sizeof gLastSaved - 1] = '\0';
    } else {
        // WHY it failed, not a guess — "card full" and "no card" are different
        // problems and the player can only act on the true one (Hard Rule #3).
        snprintf(gLastSaved, sizeof gLastSaved, "%s", sdstore::lastError());
    }
}

// Naming happens in run(), which owns the canvas the prompt needs.
void aSaveSd(int) { gOpenSdSave = true; }

void fLoadSd(char* o, int c) { snprintf(o, c, "browse card%c", kLRtag); }
void aLoadSd(int) { gOpenSdLoad = true; }  // run() opens the browser modal

// Reset the bank to stock + roll fresh randoms for the two generative slots
// (o,p). This drops every saved override — q..i return to their curated factory
// presets and o,p get brand-new random sounds — and can't be undone, so unlike
// Randomize/Mutate (which only touch the undoable live sound) it asks for a
// confirming second tap.
constexpr uint32_t kReRollArmMs = 3000;
bool reRollArmed() { return gReRollArmed && (millis() - gReRollArmedAt < kReRollArmMs); }
void fReRoll(char* o, int c) {
    if (reRollArmed()) snprintf(o, c, "SURE? tap again");
    else               snprintf(o, c, "reset bank, roll o/p%c", kLRtag);
}
void aReRoll(int) {
    if (reRollArmed()) {
        gReRollArmed = false;
        store::historyCheckpoint();  // the live sound stays recoverable via Undo
        store::reRollBank();         // ...the slots do not — hence the confirm
        audition::start();
        soundcard::show(audition::lengthMs());
    } else {
        gReRollArmed = true;         // first tap arms; second within 3s confirms
        gReRollArmedAt = millis();
    }
}

const Item kItems[] = {
    // Sections are COLLAPSIBLE (enter on a header expands/collapses; ◄/► too).
    // Settings opens with CREATE expanded and everything else collapsed, so the
    // whole map (~10 headers) is visible at a glance instead of one endless scroll.
    // fn+up/down still jumps header-to-header. Order = make -> keep -> shape ->
    // play -> system.
    //
    // "How to play" rides ABOVE the fold, first row on the screen, exempt from
    // collapsing (isHidden): it spent a year as the last row of the last
    // section and ten out of ten surveyed players never found the manual —
    // or the gestures it documents. The one navigational row outranks a knob.
    {"How to play", fHelp, aHelp},
    // CREATE leads on purpose: opening settings lands the cursor on RANDOMIZE, so
    // the generative loop (randomize -> hear -> mutate -> keep) is the first thing
    // every player meets — never buried. A rough sound is never a dead end:
    // Randomize and Mutate are right here, drawn as boxed buttons (see the row
    // renderer) so they read as primary actions, not value rows.
    {"CREATE (make your own)", nullptr, nullptr},
    {"Randomize", fRandomize, aRandomize},   // boxed button (select, then ,///enter)
    {"Mutate", fMutate, aMutate},            // boxed button
    {"Mutate amt", fMutAmt, aMutAmt, true, gMutAmtF},
    {"Undo", fUndo, aUndo},
    {"Redo", fRedo, aRedo},
    {"Init sound", fInitSound, aInitSound},
    {"LIBRARY", nullptr, nullptr},
    {"Save to SD", fSaveSd, aSaveSd},
    {"Load from SD", fLoadSd, aLoadSd},
    {"Re-roll bank", fReRoll, aReRoll},
    {"Sound reset", fPatchReset, aPatchReset},
    {"Reset all sounds", fAllSoundsReset, aAllSoundsReset},
    {"TONE", nullptr, nullptr},
    {"Filter mode", fFilterMode, aFilterMode},
    {"Resonance", fRes, aRes, true, gRes},
    {"Fat detune", fDetune, aDetune, true, gDetune},
    {"Drift", fDrift, aDrift, true, gDrift},
    {"EFFECTS", nullptr, nullptr},
    {"Chorus", fChorus, aChorus, true, gChorus},
    {"Delay send", fDelaySend, aDelaySend, true, gDelaySend},
    {"Delay time", fDelayTime, aDelayTime, true, gDelayTime},
    {"Delay sync", fDelaySync, aDelaySync},
    {"Delay fb", fDelayFb, aDelayFb, true, gDelayFb},
    {"Reverb send", fReverbSend, aReverbSend, true, gRevSend},
    {"Reverb size", fReverbSize, aReverbSize, true, gRevSize},
    {"MODULATION", nullptr, nullptr},
    {"LFO1 rate", fLfo1Rate, aLfo1Rate, true, gLfo1Rate},
    {"LFO1 shape", fLfo1Shape, aLfo1Shape},
    {"LFO1 sync", fLfo1Sync, aLfo1Sync},
    {"LFO2 rate", fLfo2Rate, aLfo2Rate, true, gLfo2Rate},
    {"LFO2 shape", fLfo2Shape, aLfo2Shape},
    {"LFO2 sync", fLfo2Sync, aLfo2Sync},
    {"Mod env atk", fModEnvAtk, aModEnvAtk, true, gModAtk},
    {"Mod env dec", fModEnvDec, aModEnvDec, true, gModDec},
    // The 6 routing slots. Each reads as "Mod N = <source>" then indented "to
    // <dest>" and "amount"; an unused slot (source off) collapses to its one line.
    {"Mod 1", fSlot0Src, aSlot0Src},
    {"   to", fSlot0Dst, aSlot0Dst},
    {"   amount", fSlot0Amt, aSlot0Amt, true, gSlot0Amt, true},
    {"Mod 2", fSlot1Src, aSlot1Src},
    {"   to", fSlot1Dst, aSlot1Dst},
    {"   amount", fSlot1Amt, aSlot1Amt, true, gSlot1Amt, true},
    {"Mod 3", fSlot2Src, aSlot2Src},
    {"   to", fSlot2Dst, aSlot2Dst},
    {"   amount", fSlot2Amt, aSlot2Amt, true, gSlot2Amt, true},
    {"Mod 4", fSlot3Src, aSlot3Src},
    {"   to", fSlot3Dst, aSlot3Dst},
    {"   amount", fSlot3Amt, aSlot3Amt, true, gSlot3Amt, true},
    {"Mod 5", fSlot4Src, aSlot4Src},
    {"   to", fSlot4Dst, aSlot4Dst},
    {"   amount", fSlot4Amt, aSlot4Amt, true, gSlot4Amt, true},
    {"Mod 6", fSlot5Src, aSlot5Src},
    {"   to", fSlot5Dst, aSlot5Dst},
    {"   amount", fSlot5Amt, aSlot5Amt, true, gSlot5Amt, true},
    {"LAYOUT", nullptr, nullptr},
    {"Root key", fRoot, aRoot},
    {"Scale", fScale, aScale},
    {"Row interval", fRowInt, aRowInt},
    {"Glide mode", fGlideMode, aGlideMode},
    {"Allocation", fStringMode, aStringMode},
    {"Octave keys", fOctGlide, aOctGlide},
    {"Bend time", fBendMs, aBendMs, true, gBendMsF},
    {"Morph time", fMorphMs, aMorphMs, true, gMorphMsF},
    {"JAM / BACKING", nullptr, nullptr},
    {"Jam rows (drones)", fJamRows, aJamRows},
    {"Drone voicing", fDroneVoice, aDroneVoice},
    {"Jam octave", fJamOct, aJamOct},
    {"Jam motion", fJamMotion, aJamMotion},
    {"Jam tempo", fJamBpm, aJamBpm, true, gJamBpmF},
    {"Tap tempo", fTapTempo, aTapTempo},
    {"Metronome vol", fMetroVol, aMetroVol, true, gMetroVolF},
    {"Chord length", fJamChord, aJamChord},
    {"Loop snap", fLoopSnap, aLoopSnap},
    {"TILT", nullptr, nullptr},
    {"Tilt map", fTiltLock, aTiltLock},
    {"Tilt f/b route", fTilt, aTilt},
    {"Tilt f/b depth", fTiltDepth, aTiltDepth, true, gTiltDep},
    {"Tilt l/r route", fTiltB, aTiltB},
    {"Tilt l/r depth", fTiltDepthB, aTiltDepthB, true, gTiltDepB},
    {"Tilt center", fTiltCenter, aTiltCenter},
    {"TRIGGER (G0 button)", nullptr, nullptr},
    {"Trigger action", fTrigAct, aTrigAct},
    {"Trigger depth", fTrigDepth, aTrigDepth, true, gTrigDep},
    {"Trigger mode", fTrigMode, aTrigMode},
    {"SYSTEM", nullptr, nullptr},
    {"Demo mode", fDemo, aDemo},
    {"Display", fScopeMode, aScopeMode},
    {"Theme", fTheme, aTheme},
    {"  Hue", fLookHue, aLookHue, true},
    {"  Accent", fLookAccent, aLookAccent, true},
    {"  Vividness", fLookVivid, aLookVivid, true, gLookVividF},
    {"  Ground", fLookGround, aLookGround, true, gLookGroundF},
    {"  Contrast", fLookContrast, aLookContrast, true, gLookContrastF},
    {"  Roll look", fRollLook, aRollLook},
    {"Screen idle", fIdle, aIdle},
    {"Boot sound", fBoot, aBoot},
    {"Intro card", fIntro, aIntro},
    {"Tutorial", fTut, aTut},
    {"Odometer", fOdo, aOdo},
    {"Storage", fStorage, aStorage},
    {"Reset defaults", fReset, aReset},
};
constexpr int kItemCount = (int)(sizeof(kItems) / sizeof(kItems[0]));
constexpr int kVisible = 8;

inline bool isHeader(int i) { return kItems[i].format == nullptr; }

// ---- collapsible sections (accordion) -------------------------------------
// Each section (a header + its rows) can be folded. gExpanded[sec] is its state;
// a collapsed section hides all its non-header rows. sectionOf maps a row to its
// section index (count of headers at or before it). Built once — the layout is
// static. Default: only CREATE (section 0) open (set in run()).
constexpr int kMaxSections = 16;
bool gExpanded[kMaxSections];

const int* sectionOfTbl() {
    static int tbl[kItemCount];
    static bool built = false;
    if (!built) {
        int s = -1;
        for (int i = 0; i < kItemCount; ++i) { if (isHeader(i)) ++s; tbl[i] = s < 0 ? 0 : s; }
        built = true;
    }
    return tbl;
}
inline int sectionOf(int i) { return sectionOfTbl()[i]; }
int sectionCount() {
    int n = 0;
    for (int i = 0; i < kItemCount; ++i) if (isHeader(i)) ++n;
    return n;
}

// An unused mod slot collapses to one line: its `to`/`amount` rows are hidden
// until a source is chosen. Map a row's adjust fn back to its slot to decide.
void (*const kSlotDstFn[dsp::kModSlots])(int) = {aSlot0Dst, aSlot1Dst, aSlot2Dst,
                                                 aSlot3Dst, aSlot4Dst, aSlot5Dst};
void (*const kSlotAmtFn[dsp::kModSlots])(int) = {aSlot0Amt, aSlot1Amt, aSlot2Amt,
                                                 aSlot3Amt, aSlot4Amt, aSlot5Amt};
int slotOfSub(void (*adj)(int)) {  // slot index if adj is a dest/amount thunk, else -1
    for (int s = 0; s < dsp::kModSlots; ++s)
        if (adj == kSlotDstFn[s] || adj == kSlotAmtFn[s]) return s;
    return -1;
}
// The five custom-palette dials plus the roller, identified the same way the
// modulation sub-rows are: by which adjust thunk the row points at.
bool isLookSub(void (*adj)(int)) {
    return adj == aLookHue || adj == aLookAccent || adj == aLookVivid ||
           adj == aLookGround || adj == aLookContrast || adj == aRollLook;
}
bool isHidden(int i) {
    if (isHeader(i)) return false;                  // headers always show (they're the map)
    if (i == 0) return false;                       // "How to play" rides above the fold —
                                                    // always visible, in no one's section
    if (!gExpanded[sectionOf(i)]) return true;      // section folded -> all its rows hidden
    // the custom palette's dials hang off the Theme row and exist only while it
    // reads "custom" — that is what keeps this feature at ZERO visible new rows
    if (isLookSub(kItems[i].adjust)) return store::get().themeId != theme::customIndex();
    const int s = slotOfSub(kItems[i].adjust);      // within MODULATION: unused slot sub-rows
    return s >= 0 && store::get().synth.slots[s].src == (uint8_t)dsp::ModSource::None;
}
int buildVisible(int* vis) {  // indices of currently-shown rows (headers + non-collapsed)
    int nv = 0;
    for (int i = 0; i < kItemCount; ++i)
        if (!isHidden(i)) vis[nv++] = i;
    return nv;
}

// "Do something" rows that don't change a visible value — they get a one-shot
// row blink so a tap reads as confirmed (the sound changes but the row text doesn't).
bool isActionRow(int i) {
    if (isHeader(i)) return false;
    const auto a = kItems[i].adjust;
    return a == aInitSound || a == aRandomize || a == aMutate || a == aUndo ||
           a == aRedo || a == aSaveSd || a == aReRoll || a == aPatchReset ||
           a == aAllSoundsReset || a == aReset || a == aRollLook;
}

// Next selectable row in `dir`, skipping only HIDDEN (collapsed) rows, wrapping.
// Headers ARE selectable now — you land on one to expand/collapse its section.
int step(int from, int dir) {
    int i = from;
    for (int n = 0; n < kItemCount; ++n) {
        i = (i + dir + kItemCount) % kItemCount;
        if (!isHidden(i)) return i;  // headers + visible rows both selectable
    }
    return from;
}

// The header of the prev/next section (fn+up/down jumps section-to-section). With
// headers selectable, landing ON the header is the right target — it's the
// section's handle (and where you expand it).
int jumpSection(int sel, int dir) {
    int h = sel;  // find this row's header
    while (h > 0 && !isHeader(h)) --h;
    for (int j = h + dir; j >= 0 && j < kItemCount; j += dir)
        if (isHeader(j)) return j;
    return h;  // already at the first/last section: stay put
}

// Draw the , and / keys' LEFT/RIGHT arrow icons (a ◄ ► pair) as filled
// triangles, left edge at x, vertically centred on cy. Hand-drawn because Font2
// (the value-cell font) has no arrow glyphs. ~kArrowsLRW px wide.
constexpr int kArrowsLRW = 11;
void drawArrowsLR(M5Canvas& c, int x, int cy, uint16_t col) {
    c.fillTriangle(x,     cy,     x + 4, cy - 3, x + 4, cy + 3, col);  // ◄
    const int r = x + 7;                                              // gap, then ►
    c.fillTriangle(r + 4, cy,     r,     cy - 3, r,     cy + 3, col);
}

// A one-line preview of a collapsed section's contents, shown dim on its header
// so the folded map still tells you what's inside. nullptr = no hint.
const char* headerHint(const char* name) {
    if (!strcmp(name, "LIBRARY"))    return "save/load/reset";
    if (!strcmp(name, "TONE"))       return "filter/reso/detune/drift";
    if (!strcmp(name, "EFFECTS"))    return "chorus/delay/reverb";
    if (!strcmp(name, "MODULATION")) return "2 LFOs/matrix";
    if (!strcmp(name, "LAYOUT"))     return "key/scale/glide";
    if (!strcmp(name, "JAM / BACKING")) return "drones/chords";
    if (!strcmp(name, "TILT"))       return "gyro routes";
    return nullptr;
}

// The CREATE actions (Randomize, Mutate) render as full-width BOXED buttons,
// stacked one per row, so they read as primary "do something" actions rather
// than value rows — but they're ordinary selectable rows (;/. to pick, ,///enter
// to fire), no special L/R handling. Detected by their adjust fn.
bool isCreateButton(int i) {
    return kItems[i].adjust == aRandomize || kItems[i].adjust == aMutate;
}
// Draw one stacked button filling the row at y. Selected = amber outline + text;
// flash = filled amber (the one-shot fire confirm); idle = dim outline + text.
// The label is MIDDLE-centred in the box — Font2 is taller than the row, so a
// top baseline drops the letters' bottoms past the box edge (they got clipped by
// the next row). The labels have no descenders, so centring fits cleanly.
void drawActionButton(M5Canvas& c, int y, const char* label, bool sel, bool flash) {
    const int x = 6, w = cfg::kScreenW - 12, top = y - 1, h = 12;  // fits the 13px row
    const uint16_t border = (flash || sel) ? theme::kAmber : theme::kLine;
    const uint16_t fill   = flash ? theme::kAmber : (sel ? theme::kPanel : theme::kBg);
    const uint16_t txt    = flash ? theme::kBg : (sel ? theme::kAmber : theme::kDim);
    c.fillRoundRect(x, top, w, h, 3, fill);
    c.drawRoundRect(x, top, w, h, 3, border);
    c.setFont(&fonts::Font2);
    c.setTextDatum(middle_center);  // vertical-centre so the (descenderless) label
    c.setTextColor(txt, fill);      // sits inside the box instead of dropping out
    c.drawString(label, cfg::kScreenW / 2, top + h / 2);
    c.setTextDatum(top_left);
}

// While a look dial is selected the hint line gives way to a SPECIMEN. The menu
// already restyles live, but it only ever paints the grounds, the accent and the
// text roles — it never shows the primary (the live trace) or the backing, which
// are the two roles a palette actually lives on. So show all eleven, then a
// scrap of trace in the colour the scope will really draw with.
void drawSpecimen(M5Canvas& c) {
    const uint16_t roles[11] = {theme::kBg,    theme::kPanel,    theme::kLine,
                                theme::kAmber, theme::kAmberDim, theme::kGreen,
                                theme::kGreenDim, theme::kIdle,  theme::kDim,
                                theme::kRed,   theme::kSteel};
    // Occupies exactly the hint line's band (y 125..133) so it can never clip
    // the descenders of the last visible row.
    const int y = 125, h = 9;
    c.fillRect(0, y, cfg::kScreenW, h, theme::kBg);
    for (int i = 0; i < 11; ++i) c.fillRect(5 + i * 10, y, 9, h, roles[i]);
    c.drawFastVLine(4, y, h, theme::kLine);
    c.drawFastVLine(115, y, h, theme::kLine);
    // Two cycles of the trace, blended primary -> accent exactly as the scope
    // blends it with timbre. Quarter-wave table; a float sinf here would be the
    // only one on this path.
    static const int8_t kQ[9] = {0, 17, 34, 49, 63, 74, 82, 87, 89};
    const int x0 = 122, w = cfg::kScreenW - x0 - 5, mid = y + h / 2;
    for (int x = 0; x < w; ++x) {
        const int phase = (x * 64 / w) & 31;           // 32 steps per cycle, 2 cycles
        const int p = phase & 15;                      // 0..15 within a half-wave
        const int amp = kQ[p <= 8 ? p : 16 - p];       // mirror to a quarter table
        const int yy = mid - (phase < 16 ? 1 : -1) * amp * (h / 2) / 89;
        c.drawPixel(x0 + x, yy,
                    theme::blend(theme::kGreen, theme::kAmber, (uint8_t)(x * 255 / w)));
    }
}

void draw(M5Canvas& c, int sel, int top) {
    c.fillScreen(theme::kBg);
    c.fillRect(0, 0, cfg::kScreenW, 14, theme::kPanel);
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString("GLIDE", 4, 3);
    c.drawString("GLIDE", 5, 3);
    c.setTextColor(theme::kIdle, theme::kPanel);
    c.drawString("SETTINGS", 44, 3);

    // battery, polled lazily — it's the natural place to check before a set
    static int bat = -1;
    static uint32_t batAt = 0;
    if (bat < 0 || millis() - batAt > 2000) {
        batAt = millis();
        bat = M5.Power.getBatteryLevel();
    }
    if (bat >= 0) {
        char bb[12];
        snprintf(bb, sizeof bb, "BAT %d%%", bat);
        c.setTextDatum(top_right);
        c.setTextColor(bat <= 20 ? (bat <= 10 ? theme::kRed : theme::kAmber) : theme::kDim,
                       theme::kPanel);
        c.drawString(bb, cfg::kScreenW - 4, 3);
        c.setTextDatum(top_left);
    }

    int vis[kItemCount];
    const int nv = buildVisible(vis);  // collapsed mod slots drop out of the list

    // Row bands first, all text after (transparent): Font2 glyphs are 16 px
    // tall on the 13 px row pitch, so a band or bg-painted text cell drawn for
    // row N+1 used to erase row N's descenders — "cymatic" read as "cumatic".
    for (int row = 0; row < kVisible; ++row) {
        const int vidx = top + row;
        if (vidx >= nv) break;
        const int i = vis[vidx];
        const int y = 18 + row * 13;
        if (isCreateButton(i)) continue;  // buttons paint their own box
        const bool fl = (i == gFlashRow) && (millis() < gFlashUntil);
        if (isHeader(i)) {
            if (i == sel) c.fillRect(0, y - 1, cfg::kScreenW, 12, theme::kPanel);
        } else if (fl || i == sel) {
            c.fillRect(0, y - 1, cfg::kScreenW, 13, fl ? theme::kAmber : theme::kPanel);
        }
    }

    char val[28];
    for (int row = 0; row < kVisible; ++row) {
        const int vidx = top + row;
        if (vidx >= nv) break;
        const int i = vis[vidx];
        const int y = 18 + row * 13;

        if (isHeader(i)) {  // collapsible section handle: ▾ open / ▸ closed
            const bool exp = gExpanded[sectionOf(i)];
            c.setFont(&fonts::Font0);
            c.setTextColor(theme::kAmber);
            char hdr[40];
            // \x1f = ▼ (expanded), \x10 = ► (collapsed) — Font0 CP437 glyphs
            snprintf(hdr, sizeof hdr, "%c %s", exp ? '\x1f' : '\x10', kItems[i].name);
            c.drawString(hdr, 4, y + 3);
            if (!exp) {  // folded: show a dim hint of what's inside
                const char* h = headerHint(kItems[i].name);
                if (h) {
                    c.setTextDatum(top_right);
                    c.setTextColor(theme::kDim);
                    c.drawString(h, cfg::kScreenW - 6, y + 3);
                    c.setTextDatum(top_left);
                }
            }
            c.drawFastHLine(4, y + 12, cfg::kScreenW - 12, theme::kLine);
            continue;
        }

        // CREATE actions: stacked boxed buttons (Randomize / Mutate).
        if (isCreateButton(i)) {
            const bool selRow = (i == sel);
            const bool fl = (i == gFlashRow) && (millis() < gFlashUntil);
            drawActionButton(c, y, kItems[i].name, selRow, fl);
            continue;
        }

        c.setFont(&fonts::Font2);
        const bool isSel = (i == sel);
        const bool flash = (i == gFlashRow) && (millis() < gFlashUntil);  // one-shot confirm
        const uint16_t nameCol = flash ? theme::kBg : (isSel ? theme::kAmber : theme::kDim);
        const uint16_t valCol = flash ? theme::kBg : (isSel ? theme::kIdle : theme::kDim);
        c.setTextColor(nameCol);  // transparent: the band pre-pass owns the bg
        c.drawString(kItems[i].name, 8, y);
        kItems[i].format(val, sizeof val);
        // a trailing kLRtag => this is an action row: draw the , / keys' L/R arrow
        // icons at the right edge and right-align the label to their left.
        int vn = (int)strlen(val);
        const bool lr = vn > 0 && val[vn - 1] == kLRtag;
        if (lr) val[--vn] = '\0';
        int rightX = cfg::kScreenW - 8;
        if (lr) {
            drawArrowsLR(c, rightX - kArrowsLRW, y + 6, valCol);
            rightX -= kArrowsLRW + 4;  // leave a small gap before the text
        }
        // mid-row value gauge (the quick-edit mixer-strip idea, brought here):
        // continuous rows read at a glance; mod amounts get a centre-zero bar.
        // Skipped while the row flashes solid amber.
        if (kItems[i].fill && !flash) {
            const float f = kItems[i].fill();
            const uint16_t gc = isSel ? theme::scale(theme::kAmber, 150)
                                      : theme::scale(theme::kAmber, 60);
            if (kItems[i].bipolar)  viz::drawBipolar(c, 120, y + 2, 44, 8, f, gc);
            else if (f >= 0.f)      viz::drawGauge(c, 120, y + 2, 44, 8, f, gc);
        }
        c.setTextDatum(top_right);
        c.setTextColor(valCol);
        c.drawString(val, rightX, y);
        c.setTextDatum(top_left);
        // shape/mode rows draw the thing, not just its name
        {
            const auto adj = kItems[i].adjust;
            const int ix = rightX - c.textWidth(val) - 22;
            const auto& s = store::get().synth;
            if (adj == aLfo1Shape)
                viz::drawLfoIcon(c, ix, y + 2, 16, 9, (dsp::LfoShape)s.lfo1Shape, valCol);
            else if (adj == aLfo2Shape)
                viz::drawLfoIcon(c, ix, y + 2, 16, 9, (dsp::LfoShape)s.lfo2Shape, valCol);
            else if (adj == aFilterMode)
                viz::drawFilterIcon(c, ix, y + 2, 16, 9, (dsp::FilterMode)s.filterMode, valCol);
        }
    }

    // scroll position map (over the currently-visible rows)
    if (nv > kVisible) {
        int vsel = 0;
        for (int k = 0; k < nv; ++k)
            if (vis[k] == sel) { vsel = k; break; }
        const int trackY = 18, trackH = kVisible * 13 - 2;
        c.drawFastVLine(cfg::kScreenW - 2, trackY, trackH, theme::kLine);
        int thumbH = trackH * kVisible / nv;
        if (thumbH < 4) thumbH = 4;
        const int thumbY = trackY + (trackH - thumbH) * vsel / (nv - 1);
        c.fillRect(cfg::kScreenW - 3, thumbY, 2, thumbH, theme::kDim);
    }

    if (!isHeader(sel) && isLookSub(kItems[sel].adjust)) {
        drawSpecimen(c);
    } else {
        c.setFont(&fonts::Font0);
        c.setTextColor(theme::kDim, theme::kBg);
        // \x1e\x1f = up/down (; .), \x11\x10 = left/right (, /) — the keys' silk-screen
        // arrows. Font0 (GLCD) carries the CP437 glyphs, so draw them directly here.
        // Context-aware: a header folds/unfolds, a row changes its value.
        c.drawString(isHeader(sel) ? "\x1e\x1f move  enter folds  fn jump  ` back"
                                   : "\x1e\x1f move  \x11\x10 change  fn jump  ` back",
                     4, 125);
    }
    soundcard::draw(c, millis());  // a fresh roll's face rides over the list
    c.pushSprite(0, 0);
}

}  // namespace

void run(M5Canvas& canvas) {
    // quiet the solo layer; latched drones keep ringing so every edit is
    // heard live against the backing — sound design with your ears on
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::LeadsOff, 0));
    gReRollArmed = false;  // never enter settings with a stale re-roll confirm armed

    // Open with only CREATE unfolded, so the whole section map is visible and the
    // cursor lands on the RANDOMIZE button — the generative loop, front and centre.
    for (int s = 0; s < kMaxSections; ++s) gExpanded[s] = false;
    gExpanded[0] = true;

    int sel = step(kItemCount - 1, +1);  // -> "How to play" (row 0, above the fold)
    sel = step(sel, +1);                 // -> the CREATE header
    sel = step(sel, +1);                 // -> the RANDOMIZE button (the landing spot)
    int top = 0;
    uint64_t prev = ~0ULL;  // force first frame to treat keys as already-held

    // hold-to-repeat (DAS/ARR, same feel as the perform-screen keys): nav always
    // repeats (fast scroll of a long list); adjust repeats only on `repeatable`
    // rows (continuous numerics — amounts, rates, %), so enums/toggles don't spin.
    int navRep = 0, adjRep = 0;
    uint32_t navStart = 0, navLast = 0, adjStart = 0, adjLast = 0;

    // wait for the tab press that opened us to clear
    for (;;) {
        const uint32_t now = millis();
        M5Cardputer.update();
        uint64_t cur = 0;
        for (const auto& p : M5Cardputer.Keyboard.keyList()) cur |= 1ULL << (p.y * 14 + p.x);
        const uint64_t pressed = cur & ~prev;
        prev = cur;

        auto hit = [&](int cd) { return (pressed >> cd) & 1ULL; };

        if (hit(kExit1) || hit(kExit2)) break;

        auto held = [&](int cd) { return (cur >> cd) & 1ULL; };
        const bool fnHeld = held(kFn);  // fn = jump section to section

        // --- navigation: ;/. move, fn+;/. jump section, hold to auto-scroll ---
        if (fnHeld && hit(kUp)) sel = jumpSection(sel, -1);
        else if (fnHeld && hit(kDown)) sel = jumpSection(sel, +1);
        else if (hit(kUp)) { sel = step(sel, -1); navRep = -1; navStart = navLast = now; }
        else if (hit(kDown)) { sel = step(sel, +1); navRep = +1; navStart = navLast = now; }
        else if (!fnHeld && navRep == -1 && held(kUp) &&
                 now - navStart >= cfg::kRepeatDelayMs && now - navLast >= cfg::kRepeatRateMs) {
            sel = step(sel, -1); navLast = now;
        } else if (!fnHeld && navRep == +1 && held(kDown) &&
                   now - navStart >= cfg::kRepeatDelayMs && now - navLast >= cfg::kRepeatRateMs) {
            sel = step(sel, +1); navLast = now;
        }
        if (!(held(kUp) || held(kDown)) || fnHeld) navRep = 0;  // released -> disarm
        if (hit(kUp) || hit(kDown)) soundcard::dismiss();  // moving on reclaims the list

        // --- adjust: ,// change; hold repeats only on `repeatable` numeric rows.
        // enter is handled separately (activate an item / fold a header / roll the
        // CREATE bar) so it isn't part of the repeating dir. ---
        const bool enterHit = !fnHeld && hit(kEnter);
        int dir = 0;
        bool dirFromRepeat = false;  // an auto-repeat step, not a fresh tap
        if (!fnHeld) {
            if (hit(kLeft) || hit(kDecAlt)) { dir = -1; adjRep = -1; adjStart = adjLast = now; }
            else if (hit(kRight) || hit(kIncAlt)) { dir = +1; adjRep = +1; adjStart = adjLast = now; }
            else if (!isHeader(sel) && kItems[sel].repeatable && adjRep != 0 &&
                     now - adjStart >= cfg::kRepeatDelayMs && now - adjLast >= cfg::kRepeatRateMs) {
                const bool stillDown = adjRep < 0 ? (held(kLeft) || held(kDecAlt))
                                                  : (held(kRight) || held(kIncAlt));
                if (stillDown) { dir = adjRep; adjLast = now; dirFromRepeat = true; }
            }
        }
        const bool anyAdj = held(kLeft) || held(kDecAlt) || held(kRight) || held(kIncAlt);
        if (!anyAdj || fnHeld) adjRep = 0;  // released -> disarm

        if (gOpenHelp) {  // the Help item asked to open the cheat-sheet modal
            gOpenHelp = false;
            help::run(canvas);
            prev = ~0ULL;  // treat keys held across the modal as already-down
            draw(canvas, sel, top);
            continue;
        }

        if (gOpenSdSave) {  // "Save to SD" asked to name the sound first
            gOpenSdSave = false;
            // Check the card BEFORE the prompt. Making someone type a name and
            // THEN telling them there's no card is the one flow worse than not
            // asking at all.
            if (!sdstore::available() && !sdstore::begin()) {
                snprintf(gLastSaved, sizeof gLastSaved, "%s", sdstore::lastError());
            } else {
                // Pre-filled with the sound's own name, so enter-on-arrival keeps
                // the auto-name exactly as before — naming it is opt-in, one
                // gesture deep, and never a step you're forced through.
                char nm[sdstore::kMaxNameLen + 1] = {0};
                strncpy(nm, store::liveName(), sizeof nm - 1);
                if (textentry::run(canvas, "SAVE TO SD", nm, sizeof nm)) {
                    // Cleared the field and hit enter? Take that as "whatever you
                    // were calling it" rather than writing a file named "patch".
                    saveLiveToSd(nm[0] ? nm : store::liveName());
                } else {
                    snprintf(gLastSaved, sizeof gLastSaved, "cancelled");
                }
            }
            prev = ~0ULL;  // text-entry ate keys — rebuild edge state
            draw(canvas, sel, top);
            continue;
        }

        if (gOpenSdLoad) {  // "Load from SD" asked to open the library browser
            gOpenSdLoad = false;
            char nm[24] = {0};
            const bool loaded = sdbrowser::run(canvas, nm, sizeof nm);
            prev = ~0ULL;
            if (loaded) {  // the browser already applied it live + checkpointed
                auto& g = store::get();
                g.synth.tempoBpm = (float)g.jamBpm;
                audio::setParams(g.synth, g.backingLocked ? g.backingSynth : g.synth);
                store::markDirty();  // a load is the player's — adopts a demo loan
                store::persistNow();
                audition::start();  // audition the loaded sound on return
                soundcard::show(audition::lengthMs());
            }
            draw(canvas, sel, top);
            continue;
        }

        // Persist discrete taps immediately: a pocket device gets its power
        // flicked mid-edit; a debounced write would be lost. But an auto-repeat
        // RAMP is ~16 steps/s — persisting each step is a flash append per step
        // on the tiny shared NVS partition (wear + GC churn on the partition
        // that's known to run full). Ramps mark dirty instead: store::tick()
        // below writes once ~0.5 s after the ramp stops, and settings exit
        // persistNow()s regardless — worst case a power flick mid-ramp loses
        // only the last half-second of the ramp itself.
        auto applyEdit = [&](bool immediate) {
            auto& g = store::get();
            g.synth.tempoBpm = (float)g.jamBpm;  // synced-delay preview
            store::markDirty();  // always: a player edit must adopt a demo loan
                                 // before the flush, or persistNow() skips it
            if (immediate) store::persistNow();
            audio::setParams(g.synth, g.backingLocked ? g.backingSynth : g.synth);
        };

        if (demo::pending()) break;  // Demo mode fired: hand off to the perform loop
        if (coach::tutorialPending()) break;  // Tutorial fired: same hand-off — the
                                              // tour runs on the instrument itself

        bool justExpanded = false;  // -> scroll the opened section to the top
        if (isHeader(sel)) {
            // fold / unfold the section: enter toggles, ► opens, ◄ closes.
            const int sec = sectionOf(sel);
            const bool was = gExpanded[sec];
            if (enterHit) gExpanded[sec] = !gExpanded[sec];
            else if (dir > 0) gExpanded[sec] = true;
            else if (dir < 0) gExpanded[sec] = false;
            justExpanded = !was && gExpanded[sec];
        } else {
            // ordinary row (incl. the Randomize/Mutate buttons): ◄/► adjust,
            // enter activates. The button rows ignore the sign — either fires.
            int d2 = dir;
            if (enterHit && d2 == 0) d2 = +1;
            if (d2 != 0) {
                if (isActionRow(sel)) { gFlashRow = sel; gFlashUntil = now + 160; }  // confirm
                kItems[sel].adjust(d2);
                // Action rows (Randomize/Mutate/Undo...) defer the flush: a
                // whole-patch persistNow() rewrites ~70 NVS keys and can stall
                // for SECONDS on the full shared partition — synchronously,
                // right between audition::start() and the first tick(), which
                // shipped as "Randomize is silent until the last note and the
                // card appears with it". Their handlers markDirty(); the write
                // lands after the phrase (see the audition gate on store::tick
                // below) or on settings exit. Discrete VALUE taps still persist
                // immediately — a pocket device gets its power flicked mid-edit.
                applyEdit(!dirFromRepeat && !isActionRow(sel));
            }
        }

        // scroll in visible-row space (collapsed mod slots aren't counted)
        int vis[kItemCount];
        const int nv = buildVisible(vis);
        int vsel = 0;
        for (int k = 0; k < nv; ++k)
            if (vis[k] == sel) { vsel = k; break; }
        if (justExpanded) top = vsel;  // opened section: its header snaps to the
                                       // top so the revealed rows show at once
        if (vsel < top) top = vsel;
        if (vsel >= top + kVisible) top = vsel - kVisible + 1;
        if (vsel > 0 && isHeader(vis[vsel - 1]) && top > vsel - 1)
            top = vsel - 1;  // keep the section header in view atop its first item
        if (top > nv - kVisible) top = nv - kVisible;  // collapsing shrank the list:
        if (top < 0) top = 0;                          // don't leave blank rows below

        draw(canvas, sel, top);
        // Hold the debounced flush while a preview phrase is running: its NVS
        // write can stall the loop long enough to batch-fire the remaining
        // steps (On+Off in one frame = swallowed notes). The roll must reach
        // the ear first; the write lands the frame after the phrase ends.
        if (!audition::active()) store::tick(now);
        looper::tick(now);   // the loop plays through settings, like the drones
        keys::tickBacking(now);  // ...and so does the jam/chord progression
        audition::tick();    // fire a Randomize/preview audition's events when due
        delay(16);
    }
    audition::stop();  // never leave an audition note ringing if exiting mid-preview
    store::persistNow();
    store::flushLiveSound();     // a rolled/tweaked sound lands here too
    store::flushMorphPartner();  // a Randomize/Mutate/SD-load partner lands here,
                                 // at the menu-close boundary — never mid-phrase
}

}  // namespace settings
