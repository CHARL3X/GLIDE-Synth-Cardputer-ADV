#include "morph.h"

#include "../storage/glide_config.h"

namespace morph {

namespace {
float gPos = 0.f;
float gTarget = 0.f;
uint32_t gLastMs = 0;
}  // namespace

void kick() {
    // a switch is a morph that completes: land on the new sound instantly in
    // state (names, saves — all today's semantics), start the AUDIBLE blend at
    // the old sound and let it glide home
    if (store::morphSourceValid() && store::get().morphMs > 0) gPos = 1.f;
}

void setHold(float target01) {
    gTarget = target01 < 0.f ? 0.f : (target01 > 1.f ? 1.f : target01);
}

void tick(uint32_t nowMs) {
    const uint32_t dt = gLastMs ? nowMs - gLastMs : 0;
    gLastMs = nowMs;
    if (!store::morphSourceValid()) { gPos = 0.f; return; }
    const uint16_t ms = store::get().morphMs;
    if (ms == 0) { gPos = gTarget; return; }  // morph time off: snap
    const float step = (float)dt / (float)ms;
    if (gPos < gTarget) gPos = gPos + step > gTarget ? gTarget : gPos + step;
    else if (gPos > gTarget) gPos = gPos - step < gTarget ? gTarget : gPos - step;
}

float pos() { return gPos; }

}  // namespace morph
