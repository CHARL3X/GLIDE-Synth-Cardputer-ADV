#include "sound_gen.h"

namespace dsp {

namespace {

// A self-contained LCG (Numerical Recipes constants). Seeded per call so the
// generator stays pure and deterministic — no shared global state, nothing
// from millis(). This is the SAME generator the old UI randomizer used, now
// living in dsp/ where it's seedable and testable.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    float f() {  // uniform 0..1
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) * (1.f / 16777216.f);
    }
    int i(int lo, int hi) { return lo + (int)(f() * (float)(hi - lo + 1)); }  // inclusive
    bool chance(float p) { return f() < p; }
};

template <typename T>
T clampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// One field's musical range, shared by generate (paint within it) and mutate
// (nudge by a fraction of its span, then clamp back inside). Keeping the bounds
// in one place is what stops a mutation from ever drifting a value out of the
// playable window the randomizer respects.
struct Range { float lo, hi; };
constexpr Range kCutoff   = {120.f, 11000.f};
constexpr Range kReso     = {0.f, 0.90f};
constexpr Range kAttack   = {0.f, 1.2f};
constexpr Range kDecay    = {0.02f, 1.5f};
constexpr Range kSustain  = {0.f, 1.f};
constexpr Range kRelease  = {0.05f, 2.0f};
constexpr Range kGlide    = {0.f, 0.4f};
constexpr Range kDetune   = {0.f, 30.f};
constexpr Range kFenvOct  = {0.f, 3.5f};
constexpr Range kFenvDec  = {0.05f, 1.2f};
constexpr Range kSub      = {0.f, 0.85f};
constexpr Range kDrive    = {1.f, 6.f};
constexpr Range kReverb   = {0.f, 0.6f};
constexpr Range kDelay    = {0.f, 0.5f};
constexpr Range kChorus   = {0.f, 0.7f};
constexpr Range kLfo1Rate = {0.1f, 9.f};
constexpr Range kLfo2Rate = {0.05f, 6.f};
constexpr Range kModEnvA  = {0.001f, 0.5f};
constexpr Range kModEnvD  = {0.05f, 1.3f};
constexpr Range kModDepth = {-1.f, 1.f};
constexpr Range kTiltDep  = {0.30f, 0.90f};

// Nudge a continuous value: with probability `prob`, add a bounded random delta
// of up to `amount` * the range span (signed), then clamp into range. Used only
// by mutate — generate paints fresh from the ranges directly.
float nudge(Rng& r, float v, const Range& rg, float amount, float prob) {
    if (!r.chance(prob)) return v;
    const float span = rg.hi - rg.lo;
    v += (r.f() * 2.f - 1.f) * amount * span;
    return clampT(v, rg.lo, rg.hi);
}

// A tilt route biased toward the expressive three (vibrato / cutoff / volume) —
// "off" is rare so a generated patch almost always responds to lean.
uint8_t randTiltRoute(Rng& r, bool allowOff) {
    if (allowOff && r.chance(0.25f)) return (uint8_t)TiltRoute::Off;
    const TiltRoute opts[3] = {TiltRoute::Vibrato, TiltRoute::Cutoff, TiltRoute::Volume};
    return (uint8_t)opts[r.i(0, 2)];
}

}  // namespace

GenPatch generateSound(uint32_t seed) {
    Rng r(seed);
    GenPatch g;
    SynthParams& s = g.synth;  // starts at the neutral GLIDE defaults

    s.wave = (Waveform)r.i(0, (int)Waveform::Count - 1);
    // glide is the soul of the instrument — bias toward having some, occasionally
    // make it an always-glide patch (dreamy chord slides).
    s.glideS = r.f() * r.f() * kGlide.hi;
    s.glideMode = r.chance(0.35f) ? GlideMode::Always : GlideMode::LegatoOnly;

    // filter: usually lowpass (the home voice), sometimes a character mode
    s.filterMode = (uint8_t)(r.chance(0.6f) ? (int)FilterMode::LP
                                            : r.i(0, (int)FilterMode::Count - 1));
    s.cutoffHz = 400.f + r.f() * r.f() * 7000.f;  // skew bright-but-not-harsh
    s.resonance = r.f() * 0.6f;

    s.attackS  = r.f() * r.f() * 0.4f;
    s.decayS   = 0.05f + r.f() * 0.6f;
    s.sustain  = 0.3f + r.f() * 0.7f;
    s.releaseS = 0.1f + r.f() * 0.8f;

    s.detuneCents = r.chance(0.5f) ? 0.f : r.f() * 18.f;
    s.fenvOct  = r.chance(0.5f) ? 0.f : r.f() * 2.f;
    s.fenvDecS = 0.1f + r.f() * 0.6f;
    s.subLevel = r.chance(0.6f) ? 0.f : r.f() * 0.7f;
    s.noiseLevel = r.chance(0.8f) ? 0.f : r.f() * 0.08f;  // a little key-knock, rarely
    s.drive    = 1.f + r.f() * r.f() * 4.f;

    if (r.chance(0.5f)) { s.reverbMix = r.f() * 0.5f; s.reverbSize = 0.4f + r.f() * 0.5f; }
    if (r.chance(0.4f)) { s.delayMix = r.f() * 0.4f; s.delaySync = (uint8_t)r.i(1, 5); s.delayFb = 0.2f + r.f() * 0.4f; }
    if (r.chance(0.4f)) s.chorusDepth = r.f() * 0.6f;

    // movement: give BOTH LFOs and the mod-env real settings up front, so
    // whatever a routing slot lands on actually moves. Skew the rates slow with
    // the odd fast one.
    s.lfo1RateHz = 0.15f + r.f() * r.f() * 9.f;
    s.lfo1Shape  = (uint8_t)r.i(0, (int)LfoShape::Count - 1);
    s.lfo2RateHz = 0.1f + r.f() * r.f() * 6.f;
    s.lfo2Shape  = (uint8_t)r.i(0, (int)LfoShape::Count - 1);
    s.modEnvAtkS = r.f() * r.f() * 0.5f;
    s.modEnvDecS = 0.08f + r.f() * 1.2f;
    const int nmod = r.i(0, 3);  // 0..3 routings — more engine in play = richer
    for (int i = 0; i < nmod && i < kModSlots; ++i) {
        const ModSource src = (ModSource)r.i(1, (int)ModSource::Count - 1);
        const ModDest   dst = (ModDest)r.i(1, (int)ModDest::Count - 1);
        s.slots[i] = ModSlot::make(src, dst, (r.f() * 2.f - 1.f) * 0.6f);
    }

    // tilt personality: a generated patch should respond to lean. Axis A almost
    // always on; axis B (roll) opt-in-ish.
    g.tiltRoute  = randTiltRoute(r, /*allowOff=*/false);
    g.tiltDepth  = 0.4f + r.f() * 0.45f;
    g.tiltRouteB = randTiltRoute(r, /*allowOff=*/true);
    g.tiltDepthB = 0.35f + r.f() * 0.4f;
    return g;
}

GenPatch mutateSound(const GenPatch& base, float amount, uint32_t seed) {
    if (amount <= 0.f) return base;
    amount = clampT(amount, 0.f, 1.f);
    Rng r(seed);
    GenPatch g = base;
    SynthParams& s = g.synth;

    // continuous params: nudge a (rising-with-amount) share of them, each by a
    // bounded fraction of its own range — so character survives a gentle mutate
    // and only a bold one rewrites the patch.
    const float p = 0.45f + 0.45f * amount;  // per-field chance to move
    s.cutoffHz    = nudge(r, s.cutoffHz, kCutoff, amount, p);
    s.resonance   = nudge(r, s.resonance, kReso, amount, p);
    s.attackS     = nudge(r, s.attackS, kAttack, amount, p);
    s.decayS      = nudge(r, s.decayS, kDecay, amount, p);
    s.sustain     = nudge(r, s.sustain, kSustain, amount, p);
    s.releaseS    = nudge(r, s.releaseS, kRelease, amount, p);
    s.glideS      = nudge(r, s.glideS, kGlide, amount, p);
    s.detuneCents = nudge(r, s.detuneCents, kDetune, amount, p);
    s.fenvOct     = nudge(r, s.fenvOct, kFenvOct, amount, p);
    s.fenvDecS    = nudge(r, s.fenvDecS, kFenvDec, amount, p);
    s.subLevel    = nudge(r, s.subLevel, kSub, amount, p);
    s.drive       = nudge(r, s.drive, kDrive, amount, p);
    s.reverbMix   = nudge(r, s.reverbMix, kReverb, amount, p);
    s.delayMix    = nudge(r, s.delayMix, kDelay, amount, p);
    s.chorusDepth = nudge(r, s.chorusDepth, kChorus, amount, p);
    s.lfo1RateHz  = nudge(r, s.lfo1RateHz, kLfo1Rate, amount, p);
    s.lfo2RateHz  = nudge(r, s.lfo2RateHz, kLfo2Rate, amount, p);
    s.modEnvAtkS  = nudge(r, s.modEnvAtkS, kModEnvA, amount, p);
    s.modEnvDecS  = nudge(r, s.modEnvDecS, kModEnvD, amount, p);
    g.tiltDepth   = nudge(r, g.tiltDepth, kTiltDep, amount, p);
    g.tiltDepthB  = nudge(r, g.tiltDepthB, kTiltDep, amount, p);

    // categorical params: flip with a probability that climbs with amount. A
    // gentle mutate rarely changes the waveform or a routing; a wild one might.
    const float pc = amount * 0.6f;
    if (r.chance(pc)) s.wave = (Waveform)r.i(0, (int)Waveform::Count - 1);
    if (r.chance(pc * 0.7f)) s.filterMode = (uint8_t)r.i(0, (int)FilterMode::Count - 1);
    if (r.chance(pc * 0.5f)) s.glideMode = r.chance(0.5f) ? GlideMode::Always : GlideMode::LegatoOnly;
    if (r.chance(pc * 0.6f)) s.lfo1Shape = (uint8_t)r.i(0, (int)LfoShape::Count - 1);
    if (r.chance(pc * 0.6f)) s.lfo2Shape = (uint8_t)r.i(0, (int)LfoShape::Count - 1);
    if (r.chance(pc * 0.4f)) s.delaySync = (uint8_t)r.i(0, kDelaySyncCount - 1);

    // mod matrix: occasionally rewire a slot (source+dest), and always allow the
    // depths to drift. This is where a mutate can change how a patch *moves*.
    for (int i = 0; i < kModSlots; ++i) {
        if (r.chance(pc * 0.4f)) {
            const ModSource src = (ModSource)r.i(0, (int)ModSource::Count - 1);
            const ModDest   dst = (ModDest)r.i(1, (int)ModDest::Count - 1);
            s.slots[i] = ModSlot::make(src, dst, (r.f() * 2.f - 1.f) * 0.6f);
        } else if (s.slots[i].src != (uint8_t)ModSource::None) {
            s.slots[i].depth = nudge(r, s.slots[i].depth, kModDepth, amount, p);
        }
    }

    if (r.chance(pc * 0.5f)) g.tiltRoute  = randTiltRoute(r, /*allowOff=*/false);
    if (r.chance(pc * 0.5f)) g.tiltRouteB = randTiltRoute(r, /*allowOff=*/true);
    return g;
}

}  // namespace dsp
