#include "audition.h"

#include <Arduino.h>  // millis()

#include "../dsp/params.h"
#include "../io/audio_engine.h"

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

// A short phrase, not a single beep — see audition.h. A fixed lick (same notes
// every roll) makes two sounds easy to A/B.
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
    gStep = 0;
}

bool active() { return gT0 != 0; }

}  // namespace audition
