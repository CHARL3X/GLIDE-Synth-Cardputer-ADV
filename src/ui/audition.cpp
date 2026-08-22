// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "audition.h"

#include <Arduino.h>  // millis()

#include "../dsp/audition_plan.h"
#include "../dsp/params.h"
#include "../io/audio_engine.h"
#include "../storage/glide_config.h"

namespace audition {

namespace {
// One id PER NOTE (distinct from the boot chime's 250). This is load-bearing:
// re-pressing a still-sounding id takes the synth's legato path and GLIDES to
// the new pitch — so with a single shared id, every note of the preview slid
// in on the patch's glide time, even for LegatoOnly sounds whose real playing
// attacks land on pitch instantly. A long-glide roll then previewed as an
// out-of-tune smear that never hit any note, nothing like it actually plays.
// Distinct ids make each re-attack a fresh voice (exactly a fresh key press);
// the Retarget slides still ride the glide, so the glide character is heard
// where a player would hear it.
constexpr uint8_t kPreviewIds[3] = {251, 252, 253};

// A short phrase, not a single beep — see audition.h. The NOTES are fixed
// (same lick every roll — that's what makes two sounds easy to A/B, and the
// per-note ids below are hard-won), but the CLOCK bends per sound:
// dsp::planAudition() stretches the slide phrases and lengthens the final
// hold for rolls whose character needs time to arrive — slow swells, filter
// blooms, flutter and rotary periods, tempo-synced wobbles. Snappy sounds
// keep this exact base cadence, so the roll-listen-roll loop stays tight.
struct PrevStep { uint16_t atMs; uint8_t type; uint8_t note; float pitch; };
enum { kPrevOn = 0, kPrevReta = 1, kPrevOff = 2 };
const PrevStep kPhrase[] = {
    {   0, kPrevOn,   0, 52.f},  // low — hear the body
    { 300, kPrevReta, 0, 59.f},  // slide up
    { 640, kPrevReta, 0, 55.f},  // ...and back down: glide reads both ways
    {1000, kPrevOff,  0, 0.f},   // finger lifts as the next lands...
    {1000, kPrevOn,   1, 64.f},  // ...fresh attack, mid
    {1380, kPrevReta, 1, 71.f},  // slide up high
    {1860, kPrevOff,  1, 0.f},
    {1860, kPrevOn,   2, 60.f},  // fresh attack, settle into a sustain
    {2600, kPrevOff,  2, 0.f},   // release — the patch's own tail rings on
};
constexpr int kPhraseLen = (int)(sizeof kPhrase / sizeof kPhrase[0]);

uint32_t gT0 = 0;                 // phrase start time (0 = idle sentinel)
bool gArmed = false;              // start() ran; the clock begins on first tick()
int gStep = 0;                    // next step to fire
uint16_t gAt[kPhraseLen] = {0};   // this sound's schedule (stretched kPhrase)
uint32_t gLenMs = 2600;           // gAt's last entry — the phrase length
}  // namespace

void start() {
    // bend the phrase's clock to the LIVE sound (the caller just set it)
    const dsp::AuditionPlan plan = dsp::planAudition(store::get().synth);
    for (int i = 0; i < kPhraseLen; ++i)
        gAt[i] = (uint16_t)(kPhrase[i].atMs * plan.stretch + 0.5f);
    gAt[kPhraseLen - 1] = (uint16_t)(gAt[kPhraseLen - 2] + plan.finalHoldMs);
    gLenMs = gAt[kPhraseLen - 1];
    // ARM only — the clock starts on the first tick(). start() runs inside a
    // button handler, and anything slow between it and the loop's tick (an NVS
    // flush of a freshly rolled patch can take seconds on the full shared
    // partition) would land the first tick mid-phrase: the catch-up loop then
    // fires the early On/Off pairs in one silent batch and only the final note
    // sounds. A stall must DELAY the phrase, never swallow its opening.
    gT0 = 0;
    gArmed = true;
    gStep = 0;              // a fresh roll re-articulates from the top
}

void tick() {
    if (gArmed) {
        gArmed = false;
        gT0 = millis();
        if (gT0 == 0) gT0 = 1;  // 0 means idle; never let now() land there
    }
    if (!gT0) return;
    // fresh clock, not a cached frame `now`: a cached value captured before
    // start() ran would predate gT0 and fire the whole phrase in one frame.
    const uint32_t dt = millis() - gT0;
    while (gStep < kPhraseLen && dt >= gAt[gStep]) {
        const PrevStep& s = kPhrase[gStep];
        const uint8_t id = kPreviewIds[s.note];
        if (s.type == kPrevOff) {
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, id));
        } else {
            const auto t = s.type == kPrevReta ? dsp::NoteEvent::Retarget : dsp::NoteEvent::On;
            audio::pushEvent(dsp::NoteEvent::make(t, id, 0xFF, false, s.pitch));
        }
        ++gStep;
    }
    if (gStep >= kPhraseLen) gT0 = 0;  // done — the tail is the engine's to finish
}

void stop() {
    for (uint8_t id : kPreviewIds)  // any of the phrase's notes may be sounding
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, id));
    gT0 = 0;
    gArmed = false;
    gStep = 0;
}

bool active() { return gArmed || gT0 != 0; }

uint32_t lengthMs() { return gLenMs + 300; }  // phrase + a breath of the tail

}  // namespace audition
