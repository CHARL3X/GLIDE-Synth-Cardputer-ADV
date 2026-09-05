// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "arp.h"

namespace dsp {

namespace {
constexpr float kGate = 0.55f;  // the note releases this far into its step:
                                // a run, not a smear, on a pad with a tail
inline float clampBpm(float b) { return b < 20.f ? 20.f : (b > 300.f ? 300.f : b); }
}  // namespace

// root-3rd-5th per octave, then the root on top: 1-3-5-8 for one octave,
// 1-3-5-8-10-12-15 for two. The chord arrives root-first from chordPitches;
// sorting keeps the walk musical if a voicing ever hands it out of order.
int Arp::buildSteps(float* steps) const {
    const int n = cfg_.n > 3 ? 3 : cfg_.n;
    if (n <= 0) return 0;
    float p[3];
    for (int i = 0; i < n; ++i) p[i] = cfg_.pitches[i];
    for (int i = 1; i < n; ++i) {  // insertion sort, n <= 3
        const float k = p[i];
        int j = i - 1;
        while (j >= 0 && p[j] > k) { p[j + 1] = p[j]; --j; }
        p[j + 1] = k;
    }
    const int span = cfg_.span < 1 ? 1 : (cfg_.span > kArpMaxSpan ? kArpMaxSpan : cfg_.span);
    int c = 0;
    for (int o = 0; o < span; ++o)
        for (int i = 0; i < n && c < kArpMaxSteps; ++i) steps[c++] = p[i] + 12.f * o;
    if (c < kArpMaxSteps) steps[c++] = p[0] + 12.f * span;
    return c;
}

// Pattern walk: up cycles the list, down cycles it backwards, up/down bounces
// without repeating the ends (0 1 2 3 2 1 | 0 ...).
float Arp::stepPitch(int idx, const float* steps, int count) const {
    if (count <= 1) return steps[0];
    switch (cfg_.pattern) {
        case 1: return steps[count - 1 - (idx % count)];
        case 2: {
            const int period = 2 * (count - 1);
            const int k = idx % period;
            return steps[k < count ? k : period - k];
        }
        default: return steps[idx % count];
    }
}

int Arp::emitOff(NoteEvent* out, int cap, int k) {
    if (soundId_ < 0) return k;
    if (k < cap) out[k++] = NoteEvent::make(NoteEvent::Off, (uint8_t)soundId_);
    soundId_ = -1;
    return k;
}

int Arp::emitOn(NoteEvent* out, int cap, int k, float pitch) {
    const uint8_t id = useB_ ? kArpIdB : kArpIdA;
    useB_ = !useB_;
    if (k < cap) {
        NoteEvent ev = NoteEvent::make(NoteEvent::On, id, 0xFF, false, pitch);
        ev.backing = true;  // protected + the backing bus, normal release
        out[k++] = ev;
    }
    soundId_ = id;
    return k;
}

int Arp::advance(int n, float sampleRate, float bpm, NoteEvent* out, int cap) {
    int k = 0;
    if (!cfg_.on || cfg_.n == 0) {
        if (running_) {  // off edge: let the sounding note go, then rest
            running_ = false;
            k = emitOff(out, cap, k);
        }
        return k;  // idle: zero per-block work
    }
    float steps[kArpMaxSteps];
    const int count = buildSteps(steps);
    if (count == 0) return k;

    if (!running_ || cfg_.gen != gen_) {
        // a new chord (or the arp just switched on): restart the walk and
        // strike NOW — this block is the chord's downbeat
        running_ = true;
        gen_ = cfg_.gen;
        idx_ = 0;
        count_ = 0.f;
        k = emitOff(out, cap, k);
        return emitOn(out, cap, k, stepPitch(idx_, steps, count));
    }

    float beats = delaySyncBeats(cfg_.rate);
    if (beats <= 0.f) beats = 0.5f;  // free/unknown division: eighths
    const float stepLen = sampleRate * 60.f / clampBpm(bpm) * beats;
    count_ += (float)n;
    if (soundId_ >= 0 && count_ >= stepLen * kGate) k = emitOff(out, cap, k);
    if (count_ >= stepLen) {
        count_ -= stepLen;
        if (count_ >= stepLen) count_ = 0.f;  // stalled (tempo jump): resync
        const int period = cfg_.pattern == 2 && count > 1 ? 2 * (count - 1) : count;
        idx_ = (idx_ + 1) % period;
        k = emitOff(out, cap, k);
        k = emitOn(out, cap, k, stepPitch(idx_, steps, count));
    }
    return k;
}

}  // namespace dsp
