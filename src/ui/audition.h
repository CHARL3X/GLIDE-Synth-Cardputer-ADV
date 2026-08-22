// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// A short canned phrase that auditions the LIVE lead sound — the "hit Randomize
// and keep listening" loop, and the SD-library preview. Plays a low note that
// glides up and back, a couple of re-attacks across the range, then a sustained
// note left to ring on its own tail (~2.5 s + release): long enough to actually
// hear the attack bloom, filter/LFO/mod-env motion, glide both ways, and the
// reverb/delay tail. A fixed lick makes two sounds easy to A/B.
//
// It plays on a dedicated preview voice through the live engine params, so the
// caller sets the sound it wants to hear (applyGenerated / applyStoredPatch)
// first, then calls start(). Scheduling is non-blocking: call tick() every UI
// frame and it fires each step as it comes due — neither the UI nor the backing
// jam ever stalls. Shared by the settings SOUND menu and the SD browser.
#pragma once
#include <cstdint>

namespace audition {

void start();   // (re)articulate the phrase from the top on the live sound —
                // reads the live params and bends the phrase's CLOCK to them
                // (dsp/audition_plan.h): slow swells, blooms, flutter and
                // synced wobbles get the time they need; snappy sounds keep
                // the tight base cadence
void tick();    // call each UI frame; fires scheduled events when due
void stop();    // silence the preview voice now (call on leaving a screen)
bool active();  // a phrase is still being scheduled (the tail rings on after)
uint32_t lengthMs();  // this phrase's scheduled length (for the sound card)

}  // namespace audition
