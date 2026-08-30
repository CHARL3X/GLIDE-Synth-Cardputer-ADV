// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// The coach — everything that teaches the fn layer with the player's own hands.
//
// Why it exists: ten out of ten real players never found fn+k / fn+s /
// hold-fn+k / fn+q..p — the most-used gestures on the instrument — after days
// to weeks. The intro card proved WHY: read-and-dismiss teaching at minute
// zero has no retention (any key dismisses it, and pressing keys is the first
// thing every player does). So the coach never teaches by prose alone:
//
// - The TUTORIAL is a playable ritual: a bottom banner asks for one gesture
//   and advances only when the player's fingers actually perform it. The
//   instrument keeps playing the whole time — the banner watches the key
//   event stream, it never takes the keyboard away. ` skips; a settings row
//   replays. Auto-runs on a fresh unit (replacing the intro card); existing
//   units get a one-time offer card instead.
// - The TIPS are one-shot contextual toasts, fired at the moment a player
//   demonstrably has the problem the gesture solves (lots of off-scale shift
//   notes -> fn+k/fn+s exists), and permanently suppressed the moment they
//   use the gesture themselves. At most one per session, at most once ever.
#pragma once
#include <M5Cardputer.h>  // host build: support/viz_render/shim provides M5Canvas

namespace coach {

constexpr int kTutSteps = 7;
constexpr int kTipCount = 3;

// Pure draws, host-rendered by support/viz_render/render_coach.cpp so every
// banner/tip/offer state is reviewed as pixels, never designed blind.
void drawBanner(M5Canvas& c, int step, bool celebrate);
void drawTip(M5Canvas& c, int tip);
void drawOffer(M5Canvas& c);

#ifndef GLIDE_HOST_BUILD
// The gesture stream. keys.cpp and settings_screen.cpp report what the player
// actually DID; the coach decides what (if anything) that advances or retires.
enum class Ev : uint8_t {
    Grid,        // any note key went down
    Slide,       // a legato hand-off (the slide itself) happened
    KeyCycle,    // fn+k fired (the tap-cycle, or the LISTEN hold)
    ScaleCycle,  // fn+s fired
    SlotLoad,    // fn+q..p loaded a sound
    Randomize,   // settings > Randomize rolled a sound
    ShiftNote,   // an off-scale (shift) note struck under scale lock
};

void begin();               // after store::begin(): auto-start / offer / resume
void notify(Ev e);
void tick(uint32_t nowMs);  // timers: final-card auto-close, offer timeout, tips
void draw(M5Canvas& c, uint32_t nowMs);  // banner / offer / tip, whichever is up

bool tutorialActive();  // the ritual banner is up (perform keeps the screen awake)
bool offerActive();     // the one-time tour offer card is up (keys treats it modally)
bool active();          // either of the above (never true for a mere tip)

void offerAccept();      // enter on the offer card
void offerDecline();     // any other key on the offer card
void skip();             // ` tap while the ritual is up
void requestTutorial();  // settings row arms a start; perform launches it
bool tutorialPending();
void startPending();     // perform loop: consume the armed start
#endif  // GLIDE_HOST_BUILD

}  // namespace coach
