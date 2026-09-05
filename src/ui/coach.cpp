// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "coach.h"

#include <cstdio>

#include "../config.h"
#include "theme.h"

#ifndef GLIDE_HOST_BUILD
#include "../io/demo.h"
#include "../io/keys.h"
#include "../storage/glide_config.h"
#include "hud.h"
#endif

namespace coach {

namespace {

// ---- copy tables (pure — shared with the host render) ----------------------
// Every line is Font0 at x=6: keep l1/l2 <= 38 chars (6 px/char, 240 wide).
// The title row shares its line with the right-aligned progress + skip text,
// so titles stay <= 24 chars. Every chord spells fn explicitly — "q..p omits
// holding fn" was a real complaint from hardware (same rule as help.cpp).
struct StepDef {
    const char* title;
    const char* l1;
    const char* l2;
};

const StepDef kSteps[kTutSteps] = {
    {"WELCOME",
     "this is GLIDE. press any letter key.",
     "four rows = a scale. no wrong notes."},
    // Concrete keys, not "the same row": with jam rows on (the default) the
    // bottom row latches chords, where a "same row" attempt can't slide and
    // strands the step. g and j sit mid-keyboard on a lead row, one hand.
    {"THE SLIDE",
     "hold g and tap j - one voice glides.",
     "release j: it slides back home."},
    {"CHANGE KEY",
     "hold fn and tap k - the key steps up.",
     "match any song without stopping."},
    {"CHANGE SCALE",
     "still holding fn, tap s - new scale.",
     "major, minor, blues... the mood."},
    {"TEN SOUNDS",
     "hold fn and tap w - a second sound.",
     "q..p each hold one. o+p: yours alone."},
    {"MAKE A SOUND",
     "press tab, then fire Randomize.",
     "a sound is born. undo always works."},
    {"ONE MORE THING",
     "hold fn+k: the mic hears your song",
     "and retunes you. now go play."},
};

const StepDef kTips[kTipCount] = {
    {"OFF-SCALE A LOT?",
     "fn+k steps the key, fn+s the scale.",
     "retune to the song, mid-jam."},
    {"TEN SOUNDS",
     "hold fn, tap q..p - ten sounds live.",
     "o and p were rolled for you alone."},
    {"MAKE YOUR OWN",
     "tab > Randomize rolls a new sound.",
     "it can't hurt a saved slot. roll away."},
    {"ARPEGGIATE IT",
     "fn+a walks your chords, note by note.",
     "tap again: down, up/down, then off."},
};

// The banner sits right under the scope (13..94) and paints over the perform
// screen's readouts + hint line — the playing surface and the pitch trail stay
// fully visible, which is the whole point: the player performs the gesture and
// WATCHES it land while the banner asks for it.
constexpr int kBannerY = 95;
constexpr int kBannerH = 40;  // 95..134

// One banner frame: rule on top, title row, two instruction lines. Text on the
// panel must pass kPanel as its background — kBg punches a screen-coloured box
// behind every glyph on the two light palettes (see viz_render/README.md).
void bannerChrome(M5Canvas& c, const StepDef& s, uint16_t accent, const char* right) {
    c.fillRect(0, kBannerY, cfg::kScreenW, kBannerH, theme::kPanel);
    c.drawFastHLine(0, kBannerY, cfg::kScreenW, accent);
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(accent, theme::kPanel);
    c.drawString(s.title, 6, kBannerY + 4);
    if (right) {
        c.setTextDatum(top_right);
        c.setTextColor(theme::kDim, theme::kPanel);
        c.drawString(right, cfg::kScreenW - 4, kBannerY + 4);
        c.setTextDatum(top_left);
    }
    c.setTextColor(theme::kIdle, theme::kPanel);
    c.drawString(s.l1, 6, kBannerY + 16);
    c.drawString(s.l2, 6, kBannerY + 26);
}

}  // namespace

void drawBanner(M5Canvas& c, int step, bool celebrate) {
    if (step < 0 || step >= kTutSteps) return;
    if (celebrate) {
        // the payoff frame: the gesture just landed — the step's title flips
        // green for a beat before the next ask, so doing it feels SEEN
        StepDef done = kSteps[step];
        char t[40];
        snprintf(t, sizeof t, "%s - GOT IT", kSteps[step].title);
        done.title = t;
        bannerChrome(c, done, theme::kGreen, nullptr);
        return;
    }
    char right[16];
    snprintf(right, sizeof right, "%d/%d  ` skip", step + 1, kTutSteps);
    bannerChrome(c, kSteps[step], theme::kAmber, right);
}

void drawTip(M5Canvas& c, int tip) {
    if (tip < 0 || tip >= kTipCount) return;
    bannerChrome(c, kTips[tip], theme::kAmber, "tip");
}

void drawOffer(M5Canvas& c) {
    // Same card language as the first-run intro (perform_screen drawIntro):
    // rounded panel, amber frame, green action line. <=33 chars per line at x+8.
    const int w = 216, h = 96, x = (cfg::kScreenW - w) / 2, y = 18;
    c.fillRoundRect(x, y, w, h, 5, theme::kPanel);
    c.drawRoundRect(x, y, w, h, 5, theme::kAmber);
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(theme::kAmber, theme::kPanel);
    c.drawString("GLIDE - the 60-second tour", x + 8, y + 6);
    c.setTextColor(theme::kIdle, theme::kPanel);
    c.drawString("the best of GLIDE hides under fn:", x + 8, y + 20);
    c.drawString("key, scale, sounds, the mic. six", x + 8, y + 31);
    c.drawString("gestures, learned by doing them.", x + 8, y + 42);
    c.setTextColor(theme::kGreen, theme::kPanel);
    c.drawString("enter: take the tour (~1 min)", x + 8, y + 58);
    c.setTextColor(theme::kDim, theme::kPanel);
    c.drawString("any other key: not now", x + 8, y + 70);
    c.drawString("(replay: settings > Tutorial)", x + 8, y + 81);
}

#ifndef GLIDE_HOST_BUILD

namespace {

// RAM note (Hard Rule #7): this is the coach's entire resident footprint,
// ~26 bytes of .bss. It cannot live in the perform screen's VizState union —
// it must survive settings/LISTEN modal trips and scope-mode switches (the
// union restarts per mode). Strings above are const -> flash.
bool gTutActive = false;
bool gOfferUp = false;
bool gTutPending = false;   // settings armed a (re)start; perform launches it
uint8_t gStep = 0;
uint32_t gAdvancedAt = 0;   // celebration window start (0 = none yet)
uint32_t gStepAt = 0;       // when the current step started showing
uint32_t gOfferAt = 0;
int8_t gTip = -1;           // tip currently showing (-1 = none)
uint32_t gTipAt = 0;
bool gTipThisSession = false;  // at most one tip per session — never a nag
uint16_t gShiftNotes = 0;   // off-scale notes this session (the fn+k/s trigger)
uint16_t gGridNotes = 0;    // notes this session (proof they're really playing)
uint16_t gProgSteps = 0;    // chord steps tapped this session (a backing exists)

constexpr uint32_t kCelebrateMs = 700;
constexpr uint32_t kDoneCardMs = 8000;    // the final tell-card auto-closes
constexpr uint32_t kOfferGraceMs = 1200;  // keys land before the card is read:
                                          // consumed, but never counted as an answer
constexpr uint32_t kOfferTimeoutMs = 30000;  // untouched boot: card yields to the
                                             // idle path, re-offers next boot
constexpr uint32_t kTipMs = 5000;
constexpr uint8_t kTipKeyScale = 0, kTipSlots = 1, kTipRandom = 2, kTipArp = 3;
constexpr uint16_t kProgSpelled = 2;  // steps/session = they spelled a progression
constexpr uint16_t kShiftFight = 12;  // shift-notes/session = fighting the scale
constexpr uint16_t kWarmedUp = 40;    // notes/session before an unprompted tip

bool taught(uint8_t bit) { return (store::get().taughtMask >> bit) & 1u; }
void markTaught(uint8_t bit) {
    auto& g = store::get();
    if ((g.taughtMask >> bit) & 1u) return;
    g.taughtMask |= 1u << bit;
    store::markDirty();
}

void finish() {
    gTutActive = false;
    auto& g = store::get();
    g.tutDone = true;
    g.tutStep = 0;
    store::markDirty();
}

void advance(uint32_t nowMs) {
    // Celebrate real gestures only — "WELCOME - GOT IT" for pressing a key
    // would spend the payoff frame on nothing.
    gAdvancedAt = gStep >= 1 ? nowMs : 0;
    if (gStep + 1 >= kTutSteps) {
        finish();
        return;
    }
    ++gStep;
    gStepAt = nowMs;
    auto& g = store::get();
    if (!g.tutDone) {  // first run resumes where it left off; a replay doesn't
        g.tutStep = gStep;
        store::markDirty();
    }
}

void startAt(uint8_t step, uint32_t nowMs) {
    gTutActive = true;
    gOfferUp = false;
    gStep = step < kTutSteps ? step : 0;
    gStepAt = nowMs;
    gAdvancedAt = 0;
}

}  // namespace

void begin() {
    auto& g = store::get();
    if (g.tutDone) return;
    const uint32_t now = millis();
    // A truly fresh unit (or a wiped one) meets the ritual instead of the old
    // intro card; a unit that already accepted the offer resumes where the
    // reboot cut it off. Everyone else gets asked, once per boot, until they
    // answer. (NVS-dead devices read as fresh every boot — same skippable
    // behaviour the intro card always had there.)
    if (store::bootCount() <= 1 || g.tutOffered) {
        startAt(g.tutStep, now);
        if (!g.tutOffered) g.tutOffered = true;
        if (!g.seenIntro) g.seenIntro = true;  // the ritual replaces the card
        store::markDirty();
    } else {
        gOfferUp = true;
        gOfferAt = now;
    }
}

void notify(Ev e) {
    // Session telemetry + permanent suppression: a gesture the player performs
    // on their own retires its tip forever — the coach never explains what
    // someone already does.
    switch (e) {
        case Ev::Grid:
            if (gGridNotes < 60000) ++gGridNotes;
            break;
        case Ev::ShiftNote:
            if (gShiftNotes < 60000) ++gShiftNotes;
            break;
        case Ev::KeyCycle:
        case Ev::ScaleCycle:
            markTaught(kTipKeyScale);
            break;
        case Ev::SlotLoad:
            markTaught(kTipSlots);
            break;
        case Ev::Randomize:
            markTaught(kTipRandom);
            break;
        case Ev::ProgStep:
            if (gProgSteps < 60000) ++gProgSteps;
            break;
        case Ev::ArpCycle:
            markTaught(kTipArp);
            break;
        default:
            break;
    }

    if (!gTutActive) return;
    const uint32_t now = millis();
    switch (gStep) {
        case 0: if (e == Ev::Grid) advance(now); break;
        case 1: if (e == Ev::Slide) advance(now); break;
        case 2: if (e == Ev::KeyCycle) advance(now); break;
        case 3: if (e == Ev::ScaleCycle) advance(now); break;
        case 4: if (e == Ev::SlotLoad) advance(now); break;
        case 5: if (e == Ev::Randomize) advance(now); break;
        case 6:  // the tell-card: any note after a beat of reading closes it
            if (e == Ev::Grid && now - gStepAt > 1500) advance(now);
            break;
        default: break;
    }
}

void tick(uint32_t nowMs) {
    if (gTutActive) {
        if (gStep == kTutSteps - 1 && nowMs - gStepAt > kDoneCardMs) finish();
        return;
    }
    if (gOfferUp) {
        if (nowMs - gOfferAt > kOfferTimeoutMs) gOfferUp = false;  // this session
        return;
    }
    if (gTip >= 0) {
        if (nowMs - gTipAt > kTipMs) gTip = -1;
        return;
    }
    // Contextual tips. Held while the tutorial is unresolved (it does the
    // teaching), during a demo, and while fn is down (the edit layer owns the
    // screen). One per session; each marked taught the moment it shows, so a
    // tip fires at most ONCE in the instrument's whole life.
    if (gTipThisSession || !store::get().tutDone) return;
    if (demo::active() || keys::quickEditActive()) return;
    int t = -1;
    if (!taught(kTipKeyScale) && gShiftNotes >= kShiftFight) {
        t = kTipKeyScale;  // fighting the scale RIGHT NOW — the teachable moment
    } else if (!taught(kTipSlots) && store::bootCount() >= 3 && gGridNotes >= kWarmedUp) {
        t = kTipSlots;
    } else if (!taught(kTipRandom) && store::bootCount() >= 5 && gGridNotes >= kWarmedUp) {
        t = kTipRandom;
    } else if (!taught(kTipArp) && gProgSteps >= kProgSpelled && gGridNotes >= kWarmedUp) {
        t = kTipArp;  // a progression is spelled and they're soloing over it:
                      // the moment the walk would have been the next move
    }
    if (t < 0) return;
    gTip = (int8_t)t;
    gTipAt = nowMs;
    gTipThisSession = true;
    markTaught((uint8_t)t);
}

void draw(M5Canvas& c, uint32_t nowMs) {
    if (gOfferUp) {
        drawOffer(c);
        return;
    }
    if (gTutActive) {
        const bool cel = gStep > 0 && gAdvancedAt != 0 && nowMs - gAdvancedAt < kCelebrateMs;
        drawBanner(c, cel ? gStep - 1 : gStep, cel);
        return;
    }
    if (gTip >= 0) drawTip(c, gTip);
}

bool tutorialActive() { return gTutActive; }
bool offerActive() { return gOfferUp; }
bool active() { return gTutActive || gOfferUp; }

void offerAccept() {
    if (!gOfferUp || millis() - gOfferAt < kOfferGraceMs) return;
    auto& g = store::get();
    g.tutOffered = true;
    store::markDirty();
    startAt(g.tutStep, millis());
}

void offerDecline() {
    if (!gOfferUp || millis() - gOfferAt < kOfferGraceMs) return;
    gOfferUp = false;
    auto& g = store::get();
    g.tutOffered = true;
    g.tutDone = true;  // "not now" resolves it — the settings row is the way back
    store::markDirty();
}

void skip() {
    if (!gTutActive) return;
    finish();
    hud::show("TOUR", "replay: settings", -1.f);  // name the way back, always
}

void requestTutorial() { gTutPending = true; }
bool tutorialPending() { return gTutPending; }

void startPending() {
    gTutPending = false;
    startAt(0, millis());
}

#endif  // GLIDE_HOST_BUILD

}  // namespace coach
