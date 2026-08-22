// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// The audition clock: how long this PARTICULAR sound needs to show itself.
//
// The preview phrase's notes are fixed (same lick every roll — that's what
// makes two sounds A/B-able, and its articulation carries hard-won lessons:
// distinct ids per note, glides only on the Retargets). But rolls vary wildly
// in TIME: a pluck is all there in 100 ms, while a slow-attack pad, a
// filter-bloom brass, a chorale-speed rotary or a tempo-synced wobble need a
// second or more before they sound like themselves. Field report, verified:
// under fixed note lengths those rolls previewed faint or flat — loud on the
// keypress but cut off before the character arrived — then surprised the
// player once held by hand.
//
// planAudition() reads the patch and bends the phrase's CLOCK, never its
// notes: snappy sounds keep the familiar tight cadence bit-for-bit
// (stretch 1, hold 740 ms), slow ones stretch the slide phrases a little and
// hold the final note until the character lands — the envelope developed
// (attack + filter-bloom attack), any slow routed movement (LFO -> amp or
// cutoff below ~2 Hz, or S&H) shown for a cycle and a quarter, a
// tempo-synced movement for two of its divisions. Deterministic from the
// params alone: the same sound always previews identically. PURE dsp/ so the
// native tests walk the exact schedule the device plays.
#pragma once
#include "params.h"

namespace dsp {

struct AuditionPlan {
    float stretch;        // multiplier on the slide-phrase step times (1..1.5)
    uint16_t finalHoldMs; // hold of the phrase's last note (740..2200)
};

inline AuditionPlan planAudition(const SynthParams& s) {
    // how long until the voice has BECOME itself: amp attack + filter bloom
    // (+ the mod envelope's rise, when it's routed somewhere audible)
    float devel = s.attackS + s.fenvAtkS;

    // the slowest audibly-routed movement's period — flutter, breath, rotary,
    // wobble. Faster than ~2 Hz already reads inside the normal hold.
    float periodS = 0.f;
    for (int i = 0; i < kModSlots; ++i) {
        const ModSlot& m = s.slots[i];
        const bool audible = (m.dest == (uint8_t)ModDest::Amp ||
                              m.dest == (uint8_t)ModDest::Cutoff) &&
                             (m.depth >= 0.15f || m.depth <= -0.15f);
        if (!audible) continue;
        if (m.src == (uint8_t)ModSource::ModEnv) {
            devel += s.modEnvAtkS;
            continue;
        }
        if (m.src != (uint8_t)ModSource::LFO1 && m.src != (uint8_t)ModSource::LFO2) continue;
        const bool one = m.src == (uint8_t)ModSource::LFO1;
        const uint8_t sync = one ? s.lfo1Sync : s.lfo2Sync;
        const float rate = one ? s.lfo1RateHz : s.lfo2RateHz;
        const uint8_t shape = one ? s.lfo1Shape : s.lfo2Shape;
        float p = 0.f;
        if (sync != 0) {
            // synced movement rides the jam clock: show TWO divisions so the
            // rhythm reads as rhythm, not as a one-off dip
            const float bpm = s.tempoBpm < 40.f ? 40.f : s.tempoBpm;
            p = 2.f * delaySyncBeats(sync) * (60.f / bpm);
        } else if (rate < 2.f && rate > 0.02f) {
            // slow free movement (an S&H holds a full period too): a cycle and
            // a quarter, so the listener hears it turn around
            p = 1.25f / rate;
        } else if (shape == (uint8_t)LfoShape::SH && rate > 0.02f) {
            p = 2.5f / rate;  // a few steps of the stepper
        }
        if (p > 3.f) p = 3.f;  // glacial movement: show what a hold can show
        if (p > periodS) periodS = p;
    }

    AuditionPlan plan;
    plan.stretch = 1.f + devel;
    if (plan.stretch > 1.45f) plan.stretch = 1.45f;
    float hold = 0.74f;
    const float byDevel = devel * 2.5f + 0.4f;
    if (byDevel > hold) hold = byDevel;
    if (periodS > hold) hold = periodS;
    if (hold > 2.f) hold = 2.f;  // whole phrase stays under ~5 s at the corner
    plan.finalHoldMs = (uint16_t)(hold * 1000.f + 0.5f);
    return plan;
}

}  // namespace dsp
