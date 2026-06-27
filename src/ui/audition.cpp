#include "audition.h"

#include <Arduino.h>  // millis()

#include "../dsp/params.h"
#include "../io/audio_engine.h"

namespace audition {

namespace {
constexpr uint8_t kPreviewId = 251;  // distinct from the boot chime's 250

// A short phrase, not a single beep — see audition.h. A fixed lick (same notes
// every roll) makes two sounds easy to A/B.
struct PrevStep { uint16_t atMs; uint8_t type; float pitch; };
enum { kPrevOn = 0, kPrevReta = 1, kPrevOff = 2 };  // On = re-attack(+glide)
const PrevStep kPhrase[] = {
    {   0, kPrevOn,   52.f},  // low — hear the body
    { 300, kPrevReta, 59.f},  // slide up
    { 640, kPrevReta, 55.f},  // ...and back down: glide reads both ways
    {1000, kPrevOn,   64.f},  // re-attack, mid
    {1380, kPrevReta, 71.f},  // slide up high
    {1860, kPrevOn,   60.f},  // re-attack, settle into a sustain
    {2600, kPrevOff,   0.f},  // release — the patch's own tail rings on
};
constexpr int kPhraseLen = (int)(sizeof kPhrase / sizeof kPhrase[0]);

uint32_t gT0 = 0;  // phrase start time (0 = idle sentinel)
int gStep = 0;     // next step to fire
}  // namespace

void start() {
    gT0 = millis();
    if (gT0 == 0) gT0 = 1;  // 0 means idle; never let now() land there
    gStep = 0;              // a fresh roll re-articulates from the top
}

void tick() {
    if (!gT0) return;
    // fresh clock, not a cached frame `now`: a cached value captured before
    // start() ran would predate gT0 and fire the whole phrase in one frame.
    const uint32_t dt = millis() - gT0;
    while (gStep < kPhraseLen && dt >= kPhrase[gStep].atMs) {
        const PrevStep& s = kPhrase[gStep];
        if (s.type == kPrevOff) {
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kPreviewId));
        } else {
            const auto t = s.type == kPrevReta ? dsp::NoteEvent::Retarget : dsp::NoteEvent::On;
            audio::pushEvent(dsp::NoteEvent::make(t, kPreviewId, 0xFF, false, s.pitch));
        }
        ++gStep;
    }
    if (gStep >= kPhraseLen) gT0 = 0;  // done — the tail is the engine's to finish
}

void stop() {
    audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kPreviewId));
    gT0 = 0;
    gStep = 0;
}

bool active() { return gT0 != 0; }

}  // namespace audition
