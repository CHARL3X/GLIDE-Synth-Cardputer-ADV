// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
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
constexpr Range kFenvAtk  = {0.001f, 0.5f};
constexpr Range kSub      = {0.f, 0.85f};
constexpr Range kNoise    = {0.f, 0.30f};
constexpr Range kDrive    = {1.f, 6.f};
constexpr Range kAutoVib  = {0.f, 15.f};
constexpr Range kDrift    = {0.f, 12.f};
constexpr Range kReverb   = {0.f, 0.6f};
constexpr Range kRvbSize  = {0.30f, 0.95f};
constexpr Range kDelay    = {0.f, 0.5f};
constexpr Range kDelayFb  = {0.f, 0.70f};
constexpr Range kDelayTm  = {0.08f, 0.55f};
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

// ---- archetype paint helpers ------------------------------------------------
float uni(Rng& r, float lo, float hi) { return lo + r.f() * (hi - lo); }  // flat in window

Waveform pickWave(Rng& r, const Waveform* opts, int n) { return opts[r.i(0, n - 1)]; }

// Echo send: mostly tempo-synced (the jam pocket), sometimes free-time.
void rollEcho(Rng& r, SynthParams& s, float mixLo, float mixHi, float fbLo, float fbHi) {
    s.delayMix = uni(r, mixLo, mixHi);
    if (r.chance(0.7f)) {
        s.delaySync = (uint8_t)r.i(1, kDelaySyncCount - 1);
    } else {
        s.delaySync = 0;
        s.delayTimeS = uni(r, 0.12f, 0.45f);
    }
    s.delayFb = uni(r, fbLo, fbHi);
}
void rollRoom(Rng& r, SynthParams& s, float mixLo, float mixHi, float szLo, float szHi) {
    s.reverbMix = uni(r, mixLo, mixHi);
    s.reverbSize = uni(r, szLo, szHi);
}

// First free routing slot, or -1 (all six taken — can't happen from a fresh roll).
int freeSlot(const SynthParams& s) {
    for (int i = 0; i < kModSlots; ++i)
        if (s.slots[i].src == (uint8_t)ModSource::None) return i;
    return -1;
}
void addMod(SynthParams& s, ModSource src, ModDest dst, float depth) {
    const int i = freeSlot(s);
    if (i >= 0) s.slots[i] = ModSlot::make(src, dst, depth);
}

// The deterministic musical guardrail pass every roll goes through — the fix
// for "sometimes a roll just sounds like trash." Pure coupling rules (no RNG),
// so generate stays reproducible. Each rule guards a way an otherwise-in-range
// combination stops being playable:
//   - a highpass parked high leaves nothing below it: the note whispers away;
//   - a bandpass off the melodic band is all hiss or all rumble;
//   - screaming resonance INTO heavy drive is a self-oscillating shriek;
//   - echo + hall cranked together wash any note into mush;
//   - a "struck" sound (sustain ~0) with no decay body is a click, not a note;
//   - a slow swell that then ducks to a low sustain reads as a broken note;
//   - a mod slot aimed at Pitch scales ±1 octave at depth 1 — anything past
//     ~1 semitone of wobble is atonal chaos on a scale-locked instrument
//     (this was the single biggest source of unusable rolls);
//   - full-depth Amp tremolo gates the note to silence in its troughs.
void sanitizePatch(GenPatch& g) {
    SynthParams& s = g.synth;
    auto clampR = [](float v, const Range& rg) { return clampT(v, rg.lo, rg.hi); };
    s.cutoffHz    = clampR(s.cutoffHz, kCutoff);
    s.resonance   = clampR(s.resonance, kReso);
    s.attackS     = clampR(s.attackS, kAttack);
    s.decayS      = clampR(s.decayS, kDecay);
    s.sustain     = clampR(s.sustain, kSustain);
    s.releaseS    = clampR(s.releaseS, kRelease);
    s.glideS      = clampR(s.glideS, kGlide);
    s.detuneCents = clampR(s.detuneCents, kDetune);
    s.fenvOct     = clampR(s.fenvOct, kFenvOct);
    s.fenvDecS    = clampR(s.fenvDecS, kFenvDec);
    s.fenvAtkS    = clampR(s.fenvAtkS, kFenvAtk);
    s.subLevel    = clampR(s.subLevel, kSub);
    s.noiseLevel  = clampR(s.noiseLevel, kNoise);
    s.drive       = clampR(s.drive, kDrive);
    s.autoVibCents = clampR(s.autoVibCents, kAutoVib);
    s.chorusDepth = clampR(s.chorusDepth, kChorus);
    s.delayMix    = clampR(s.delayMix, kDelay);
    s.delayFb     = clampR(s.delayFb, kDelayFb);
    s.delayTimeS  = clampR(s.delayTimeS, kDelayTm);
    s.reverbMix   = clampR(s.reverbMix, kReverb);
    s.reverbSize  = clampR(s.reverbSize, kRvbSize);
    s.lfo1RateHz  = clampR(s.lfo1RateHz, kLfo1Rate);
    s.lfo2RateHz  = clampR(s.lfo2RateHz, kLfo2Rate);
    s.modEnvAtkS  = clampR(s.modEnvAtkS, kModEnvA);
    s.modEnvDecS  = clampR(s.modEnvDecS, kModEnvD);

    if (s.filterMode == (uint8_t)FilterMode::HP && s.cutoffHz > 1800.f) s.cutoffHz = 1800.f;
    if (s.filterMode == (uint8_t)FilterMode::BP) s.cutoffHz = clampT(s.cutoffHz, 300.f, 4500.f);
    if (s.resonance > 0.7f && s.drive > 3.5f) s.drive = 3.5f;
    const float wash = s.delayMix + s.reverbMix;
    if (wash > 0.8f) {
        const float k = 0.8f / wash;
        s.delayMix *= k;
        s.reverbMix *= k;
    }
    if (s.sustain < 0.1f && s.decayS < 0.25f) s.decayS = 0.25f;
    if (s.attackS > 0.5f) {
        if (s.sustain < 0.5f) s.sustain = 0.5f;
        if (s.releaseS < 0.4f) s.releaseS = 0.4f;
    }
    // Always-glide means EVERY note slides in, so the glide must be quick
    // enough to LAND: the one-pole slew needs ~4.6×glideS to settle, and past
    // ~0.16 s a phrase at playing speed never reaches its pitches — every note
    // sounds flat, parked between where it was and where it was aimed. (Longer
    // glides stay available to LegatoOnly rolls, where fresh attacks land
    // instantly and only deliberate hammer-on slides ride the glide.)
    if (s.glideMode == GlideMode::Always && s.glideS > 0.16f) s.glideS = 0.16f;
    for (int i = 0; i < kModSlots; ++i) {
        ModSlot& m = s.slots[i];
        if (m.src == (uint8_t)ModSource::None) continue;
        if (m.dest == (uint8_t)ModDest::Pitch) m.depth = clampT(m.depth, -0.08f, 0.08f);
        if (m.dest == (uint8_t)ModDest::Amp)   m.depth = clampT(m.depth, -0.60f, 0.60f);
    }
    s.driftCents = clampR(s.driftCents, kDrift);  // RNG-free: cannot move a golden
    g.tiltDepth  = clampT(g.tiltDepth, 0.f, 1.f);
    g.tiltDepthB = clampT(g.tiltDepthB, 0.f, 1.f);
}

// The roll polish — pure coupling rules, NO RNG, idempotent. A PURE wave
// (sine/triangle: no partials above the fundamental) behind a highpass or
// bandpass whose passband sits above the note is a dead roll (~-40 dB at
// pitch, the cardinal sin) — the spice twist and a couple of native windows
// could mint one. Harmonic waves survive those filters and keep them; pure
// ones revert to lowpass. Wobble refuses HP outright: its sub carry IS the
// character. Applied inside the paint for the second-wave archetypes and by
// the V3 layer for every archetype — never by the frozen v2/legacy paths.
void rollPolish(GenPatch& g, Archetype a) {
    SynthParams& s = g.synth;
    const bool pure = s.wave == Waveform::Sine || s.wave == Waveform::Triangle;
    const bool thin = s.filterMode == (uint8_t)FilterMode::HP ||
                      s.filterMode == (uint8_t)FilterMode::BP;
    if (pure && thin) s.filterMode = (uint8_t)FilterMode::LP;
    if (a == Archetype::Wobble && s.filterMode == (uint8_t)FilterMode::HP)
        s.filterMode = (uint8_t)FilterMode::LP;
}

}  // namespace

// FROZEN — the pre-archetype generator, verbatim. Devices whose generative
// slots (o,p) were rolled by this keep their exact sounds until the player
// re-rolls the bank (storage/glide_config gates on "genver"). The native tests
// pin this function's output with golden values: NEVER edit it. New ideas go
// in generateSound() below.
GenPatch generateSoundLegacy(uint32_t seed) {
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

Archetype archetypeForSeed(uint32_t seed) {
    // A 16-entry weighted table: pads the most common (this is a glide
    // instrument — sounds that love to slide), plucks/bells/bass/acid/leads
    // the working middle, brass/chip/wild the spice. The seed is scrambled so
    // the archetype draw doesn't correlate with the paint stream's first draw.
    static const Archetype kTable[16] = {
        Archetype::Pluck, Archetype::Pluck, Archetype::Bell, Archetype::Bell,
        Archetype::Pad,   Archetype::Pad,   Archetype::Pad,  Archetype::Bass,
        Archetype::Bass,  Archetype::Acid,  Archetype::Acid, Archetype::Lead,
        Archetype::Lead,  Archetype::Brass, Archetype::Chip, Archetype::Wild,
    };
    Rng r(seed ^ 0x243F6A88u);
    return kTable[r.i(0, 15)];
}

Archetype archetypeForSeedV3(uint32_t seed) {
    // The expanded (genver-3) pool: a 32-entry weighted table. The core nine
    // keep ~the proportions the v2 table gave them (pads still the most common
    // — this is a glide instrument), and the second wave takes ~a quarter of
    // the rolls between them: whistle and keys the working newcomers, organ /
    // wobble / strings the spice. A different scramble constant than the v2
    // table (the next word of pi's fraction) so the two pools' picks
    // decorrelate; v2 stays frozen for genver-2 slot regeneration.
    static const Archetype kTable[32] = {
        Archetype::Pluck,   Archetype::Pluck,   Archetype::Pluck,
        Archetype::Bell,    Archetype::Bell,    Archetype::Bell,
        Archetype::Pad,     Archetype::Pad,     Archetype::Pad,   Archetype::Pad,
        Archetype::Bass,    Archetype::Bass,    Archetype::Bass,
        Archetype::Acid,    Archetype::Acid,    Archetype::Acid,
        Archetype::Lead,    Archetype::Lead,    Archetype::Lead,
        Archetype::Brass,   Archetype::Brass,
        Archetype::Chip,    Archetype::Chip,
        Archetype::Wild,    Archetype::Wild,
        Archetype::Whistle, Archetype::Whistle,
        Archetype::Organ,
        Archetype::Keys,    Archetype::Keys,
        Archetype::Wobble,
        Archetype::Strings,
    };
    Rng r(seed ^ 0x85A308D3u);
    return kTable[r.i(0, 31)];
}

Archetype archetypeForSeedV5(uint32_t seed) {
    // The widest (genver-5) pool: a 40-entry weighted table. The core nine
    // keep their V3 proportions, the second wave firms up to a real quarter,
    // and the third wave (drone, gate) lands about one roll in ten between
    // them. Scramble constant: the next word of pi's fraction, so this table's
    // picks decorrelate from both frozen pools.
    static const Archetype kTable[40] = {
        Archetype::Pluck,   Archetype::Pluck,   Archetype::Pluck,
        Archetype::Bell,    Archetype::Bell,    Archetype::Bell,
        Archetype::Pad,     Archetype::Pad,     Archetype::Pad,   Archetype::Pad,
        Archetype::Bass,    Archetype::Bass,    Archetype::Bass,
        Archetype::Acid,    Archetype::Acid,    Archetype::Acid,
        Archetype::Lead,    Archetype::Lead,    Archetype::Lead,
        Archetype::Brass,   Archetype::Brass,
        Archetype::Chip,    Archetype::Chip,
        Archetype::Wild,    Archetype::Wild,
        Archetype::Whistle, Archetype::Whistle,
        Archetype::Organ,   Archetype::Organ,
        Archetype::Keys,    Archetype::Keys,    Archetype::Keys,
        Archetype::Wobble,  Archetype::Wobble,
        Archetype::Strings, Archetype::Strings,
        Archetype::Drone,   Archetype::Drone,
        Archetype::Gate,    Archetype::Gate,
    };
    Rng r(seed ^ 0x13198A2Eu);
    return kTable[r.i(0, 39)];
}

Archetype archetypeForSeedV6(uint32_t seed) {
    // The genver-6 pool: V5's sixteen families, reweighted from 202 rated
    // rolls (2026-09-22). Lead rated 17/19 good and held two of the three
    // "great" rolls, keys 7/9 — each gains a row. Chip rated 1/13 good and
    // wild 0/5 — each drops to one row; both stay (variety), and their
    // windows are what rollPolishV6 fixes. Everything else keeps its V5
    // share. Scramble constant: the next word of pi's fraction after the
    // style stream's, so this table's picks decorrelate from every earlier
    // pool and from the style draw.
    static const Archetype kTable[40] = {
        Archetype::Pluck,   Archetype::Pluck,   Archetype::Pluck,
        Archetype::Bell,    Archetype::Bell,    Archetype::Bell,
        Archetype::Pad,     Archetype::Pad,     Archetype::Pad,   Archetype::Pad,
        Archetype::Bass,    Archetype::Bass,    Archetype::Bass,
        Archetype::Acid,    Archetype::Acid,    Archetype::Acid,
        Archetype::Lead,    Archetype::Lead,    Archetype::Lead,  Archetype::Lead,
        Archetype::Brass,   Archetype::Brass,
        Archetype::Chip,
        Archetype::Wild,
        Archetype::Whistle, Archetype::Whistle,
        Archetype::Organ,   Archetype::Organ,
        Archetype::Keys,    Archetype::Keys,    Archetype::Keys,  Archetype::Keys,
        Archetype::Wobble,  Archetype::Wobble,
        Archetype::Strings, Archetype::Strings,
        Archetype::Drone,   Archetype::Drone,
        Archetype::Gate,    Archetype::Gate,
    };
    Rng r(seed ^ 0xA4093822u);
    return kTable[r.i(0, 39)];
}

GenPatch generateSound(uint32_t seed) {
    return generateSound(seed, archetypeForSeed(seed));
}

GenPatch generateSoundV3(uint32_t seed) {
    return generateSoundV3(seed, archetypeForSeedV3(seed));  // through the polish
}

// The archetype engine. Commit to a character first, then paint every field
// from that character's own (correlated) window — so a roll can be a pluck
// that stops, a pad that swells, a bell that rings, instead of yet another
// mid-everything held tone. Independent uniform draws only ever produce the
// statistical middle; this is what makes two rolls sound like two instruments.
GenPatch generateSound(uint32_t seed, Archetype a) {
    Rng r(seed);
    GenPatch g;
    SynthParams& s = g.synth;  // starts at the neutral GLIDE defaults

    // movement defaults every archetype inherits (specialised below where the
    // movement IS the character): live LFOs and a snappy-ish mod env, so any
    // routing that lands actually moves.
    s.lfo1RateHz = 0.15f + r.f() * r.f() * 8.f;
    s.lfo1Shape  = (uint8_t)r.i(0, (int)LfoShape::Count - 1);
    s.lfo2RateHz = 0.1f + r.f() * r.f() * 5.f;
    s.lfo2Shape  = (uint8_t)r.i(0, (int)LfoShape::Count - 1);
    s.modEnvAtkS = uni(r, 0.002f, 0.08f);
    s.modEnvDecS = uni(r, 0.15f, 0.8f);

    // tilt defaults — most archetypes refine axis A to fit their character.
    g.tiltRoute  = randTiltRoute(r, /*allowOff=*/false);
    g.tiltDepth  = uni(r, 0.4f, 0.85f);
    g.tiltRouteB = randTiltRoute(r, /*allowOff=*/true);
    g.tiltDepthB = uni(r, 0.35f, 0.75f);

    switch (a) {
        case Archetype::Pluck: {  // struck string / kalimba / stab — it STOPS
            static const Waveform w[4] = {Waveform::Saw, Waveform::Square, Waveform::Pulse, Waveform::Triangle};
            s.wave = pickWave(r, w, 4);
            s.attackS = uni(r, 0.001f, 0.02f);
            s.decayS = uni(r, 0.3f, 0.9f);
            s.sustain = uni(r, 0.02f, 0.22f);
            s.releaseS = uni(r, 0.15f, 0.5f);
            s.glideS = uni(r, 0.02f, 0.10f);
            s.glideMode = r.chance(0.15f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)(r.chance(0.15f) ? FilterMode::BP : FilterMode::LP);
            s.cutoffHz = uni(r, 600.f, 3000.f);
            s.resonance = uni(r, 0.05f, 0.45f);
            s.fenvAtkS = uni(r, 0.001f, 0.004f);
            s.fenvOct = uni(r, 0.8f, 2.2f);   // the pick "ping"
            s.fenvDecS = uni(r, 0.08f, 0.3f);
            if (r.chance(0.3f)) s.subLevel = uni(r, 0.15f, 0.5f);
            if (r.chance(0.4f)) s.noiseLevel = uni(r, 0.01f, 0.08f);  // pick knock
            s.drive = uni(r, 1.f, 2.6f);
            if (r.chance(0.25f)) s.chorusDepth = uni(r, 0.1f, 0.4f);
            if (r.chance(0.55f)) rollEcho(r, s, 0.15f, 0.35f, 0.15f, 0.4f);
            if (r.chance(0.45f)) rollRoom(r, s, 0.1f, 0.3f, 0.4f, 0.7f);
            if (r.chance(0.5f)) addMod(s, ModSource::Random, ModDest::Cutoff, uni(r, 0.15f, 0.4f) * (r.chance(0.5f) ? 1.f : -1.f));
            if (r.chance(0.3f)) addMod(s, ModSource::KeyTrack, ModDest::Cutoff, uni(r, 0.2f, 0.5f));
            g.tiltRoute = (uint8_t)(r.chance(0.6f) ? TiltRoute::Cutoff : TiltRoute::Vibrato);
            break;
        }
        case Archetype::Bell: {  // struck, pure, rings out into shimmer
            static const Waveform w[2] = {Waveform::Sine, Waveform::Triangle};
            s.wave = pickWave(r, w, 2);
            s.attackS = uni(r, 0.001f, 0.005f);
            s.decayS = uni(r, 0.5f, 1.4f);
            s.sustain = uni(r, 0.f, 0.06f);
            s.releaseS = uni(r, 0.4f, 1.2f);
            s.glideS = uni(r, 0.02f, 0.12f);  // the bent-bell slide
            s.glideMode = r.chance(0.25f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)(r.chance(0.15f) ? FilterMode::Notch : FilterMode::LP);
            s.cutoffHz = uni(r, 2000.f, 6000.f);
            s.resonance = uni(r, 0.05f, 0.35f);
            s.fenvAtkS = 0.001f;
            s.fenvOct = uni(r, 1.5f, 3.f);    // the metallic strike...
            s.fenvDecS = uni(r, 0.06f, 0.18f);  // ...that pings and is gone
            if (r.chance(0.25f)) s.noiseLevel = uni(r, 0.01f, 0.04f);  // hammer
            s.drive = uni(r, 1.f, 1.8f);
            if (r.chance(0.4f)) s.chorusDepth = uni(r, 0.15f, 0.45f);
            if (r.chance(0.7f)) rollEcho(r, s, 0.2f, 0.45f, 0.3f, 0.55f);
            if (r.chance(0.8f)) rollRoom(r, s, 0.25f, 0.5f, 0.6f, 0.9f);
            if (r.chance(0.4f)) addMod(s, ModSource::Random, ModDest::Cutoff, uni(r, 0.1f, 0.3f) * (r.chance(0.5f) ? 1.f : -1.f));
            g.tiltRoute = (uint8_t)TiltRoute::Vibrato;
            break;
        }
        case Archetype::Pad: {  // the swell — attack finally allowed to be slow
            static const Waveform w[4] = {Waveform::FatSaw, Waveform::Triangle, Waveform::Saw, Waveform::Sine};
            s.wave = pickWave(r, w, 4);
            s.attackS = uni(r, 0.25f, 0.6f);  // capped so the audition lick still speaks
            s.decayS = uni(r, 0.3f, 0.8f);
            s.sustain = uni(r, 0.6f, 1.f);
            s.releaseS = uni(r, 0.8f, 2.f);
            s.glideS = 0.05f + r.f() * r.f() * 0.13f;  // dreamy but LANDS (see sanitize)
            s.glideMode = r.chance(0.6f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)(r.chance(0.8f) ? FilterMode::LP
                                                    : (r.chance(0.5f) ? FilterMode::Notch : FilterMode::BP));
            s.cutoffHz = uni(r, 800.f, 4000.f);
            s.resonance = uni(r, 0.f, 0.4f);
            if (r.chance(0.4f)) {  // slow per-note filter bloom
                s.fenvAtkS = uni(r, 0.1f, 0.4f);
                s.fenvOct = uni(r, 0.4f, 1.5f);
                s.fenvDecS = uni(r, 0.5f, 1.2f);
            }
            if (s.wave == Waveform::FatSaw) s.detuneCents = uni(r, 10.f, 25.f);
            if (r.chance(0.3f)) s.subLevel = uni(r, 0.1f, 0.35f);
            if (r.chance(0.15f)) s.noiseLevel = uni(r, 0.01f, 0.04f);  // air
            s.drive = uni(r, 1.f, 2.2f);
            if (r.chance(0.85f)) s.chorusDepth = uni(r, 0.3f, 0.7f);
            if (r.chance(0.35f)) rollEcho(r, s, 0.1f, 0.3f, 0.2f, 0.45f);
            if (r.chance(0.8f)) rollRoom(r, s, 0.25f, 0.55f, 0.5f, 0.85f);
            if (r.chance(0.4f)) s.autoVibCents = uni(r, 1.f, 6.f);
            s.lfo1RateHz = uni(r, 0.1f, 0.6f);  // the slow breath
            s.modEnvAtkS = uni(r, 0.1f, 0.4f);
            s.modEnvDecS = uni(r, 0.5f, 1.3f);
            addMod(s, ModSource::LFO1, ModDest::Cutoff, uni(r, 0.2f, 0.45f) * (r.chance(0.5f) ? 1.f : -1.f));
            if (r.chance(0.5f)) addMod(s, ModSource::LFO2, ModDest::Amp, uni(r, 0.15f, 0.35f));
            g.tiltRoute = (uint8_t)(r.chance(0.5f) ? TiltRoute::Vibrato : TiltRoute::Volume);
            g.tiltRouteB = (uint8_t)TiltRoute::Cutoff;
            break;
        }
        case Archetype::Bass: {  // weight: sub + drive + a snappy filter pluck
            static const Waveform w[3] = {Waveform::Square, Waveform::Pulse, Waveform::Saw};
            s.wave = pickWave(r, w, 3);
            s.attackS = uni(r, 0.001f, 0.01f);
            s.decayS = uni(r, 0.12f, 0.3f);
            s.sustain = uni(r, 0.5f, 0.9f);
            s.releaseS = uni(r, 0.08f, 0.25f);
            s.glideS = uni(r, 0.03f, 0.10f);
            s.glideMode = r.chance(0.1f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)FilterMode::LP;
            s.cutoffHz = uni(r, 350.f, 1400.f);
            s.resonance = uni(r, 0.05f, 0.5f);
            s.fenvAtkS = uni(r, 0.001f, 0.005f);
            s.fenvOct = uni(r, 0.8f, 2.2f);
            s.fenvDecS = uni(r, 0.1f, 0.3f);
            s.subLevel = uni(r, 0.4f, 0.85f);
            s.drive = uni(r, 2.f, 4.5f);
            if (r.chance(0.15f)) s.chorusDepth = uni(r, 0.1f, 0.3f);  // mostly dry:
            if (r.chance(0.2f)) rollEcho(r, s, 0.1f, 0.2f, 0.15f, 0.3f);  // a bass
            if (r.chance(0.2f)) rollRoom(r, s, 0.05f, 0.15f, 0.4f, 0.6f);  // holds down
            if (r.chance(0.35f)) addMod(s, ModSource::Random, ModDest::Cutoff, uni(r, 0.1f, 0.3f) * (r.chance(0.5f) ? 1.f : -1.f));
            g.tiltRoute = (uint8_t)TiltRoute::Cutoff;  // the growl is the lean
            g.tiltDepth = uni(r, 0.6f, 0.9f);
            break;
        }
        case Archetype::Acid: {  // the squelch — fenv depths ACID itself uses
            static const Waveform w[2] = {Waveform::Saw, Waveform::Square};
            s.wave = pickWave(r, w, 2);
            s.attackS = uni(r, 0.001f, 0.01f);
            s.decayS = uni(r, 0.12f, 0.3f);
            s.sustain = uni(r, 0.35f, 0.7f);
            s.releaseS = uni(r, 0.1f, 0.3f);
            s.glideS = uni(r, 0.05f, 0.15f);  // the 303 slide
            s.glideMode = r.chance(0.2f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)FilterMode::LP;
            s.cutoffHz = uni(r, 250.f, 700.f);   // low base...
            s.resonance = uni(r, 0.55f, 0.9f);   // ...screaming Q...
            s.fenvAtkS = 0.001f;
            s.fenvOct = uni(r, 2.f, 3.5f);       // ...and the env rips it open
            s.fenvDecS = uni(r, 0.1f, 0.35f);
            if (r.chance(0.25f)) s.subLevel = uni(r, 0.1f, 0.3f);
            s.drive = uni(r, 2.2f, 4.f);
            if (r.chance(0.75f)) rollEcho(r, s, 0.2f, 0.4f, 0.35f, 0.6f);  // dub tails
            if (r.chance(0.4f)) rollRoom(r, s, 0.08f, 0.2f, 0.4f, 0.6f);
            s.modEnvAtkS = uni(r, 0.001f, 0.01f);
            s.modEnvDecS = uni(r, 0.1f, 0.4f);
            if (r.chance(0.45f)) {
                if (r.chance(0.5f)) s.lfo1Sync = (uint8_t)r.i(1, kDelaySyncCount - 1);
                else s.lfo1RateHz = uni(r, 0.5f, 3.f);
                addMod(s, ModSource::LFO1, ModDest::Resonance, uni(r, 0.15f, 0.4f) * (r.chance(0.5f) ? 1.f : -1.f));
            }
            if (r.chance(0.3f)) addMod(s, ModSource::Random, ModDest::Cutoff, uni(r, 0.1f, 0.25f) * (r.chance(0.5f) ? 1.f : -1.f));
            g.tiltRoute = (uint8_t)TiltRoute::Cutoff;  // tilt IS the wah
            g.tiltDepth = uni(r, 0.7f, 1.f);
            break;
        }
        case Archetype::Lead: {  // the singing solo voice
            static const Waveform w[4] = {Waveform::Saw, Waveform::Square, Waveform::FatSaw, Waveform::Pulse};
            s.wave = pickWave(r, w, 4);
            s.attackS = uni(r, 0.003f, 0.06f);
            s.decayS = uni(r, 0.15f, 0.5f);
            s.sustain = uni(r, 0.5f, 0.9f);
            s.releaseS = uni(r, 0.15f, 0.6f);
            s.glideS = uni(r, 0.04f, 0.16f);
            s.glideMode = r.chance(0.4f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)(r.chance(0.15f) ? FilterMode::Notch : FilterMode::LP);
            s.cutoffHz = uni(r, 1500.f, 6500.f);
            s.resonance = uni(r, 0.05f, 0.5f);
            if (r.chance(0.4f)) {
                s.fenvOct = uni(r, 0.3f, 1.2f);
                s.fenvDecS = uni(r, 0.15f, 0.5f);
            }
            if (s.wave == Waveform::FatSaw) s.detuneCents = uni(r, 8.f, 20.f);
            if (r.chance(0.2f)) s.subLevel = uni(r, 0.1f, 0.4f);
            s.drive = uni(r, 1.4f, 3.5f);
            s.autoVibCents = uni(r, 2.f, 10.f);  // it sings
            if (r.chance(0.4f)) s.chorusDepth = uni(r, 0.1f, 0.4f);
            if (r.chance(0.65f)) rollEcho(r, s, 0.15f, 0.4f, 0.2f, 0.45f);
            if (r.chance(0.5f)) rollRoom(r, s, 0.1f, 0.3f, 0.4f, 0.7f);
            if (r.chance(0.35f)) addMod(s, ModSource::ModEnv, ModDest::Cutoff, uni(r, 0.15f, 0.4f));
            if (r.chance(0.25f)) addMod(s, ModSource::Random, ModDest::Cutoff, uni(r, 0.08f, 0.2f) * (r.chance(0.5f) ? 1.f : -1.f));
            g.tiltRoute = (uint8_t)TiltRoute::Vibrato;
            g.tiltDepth = uni(r, 0.5f, 0.9f);
            break;
        }
        case Archetype::Brass: {  // the section — filter ATTACK is the swell
            static const Waveform w[2] = {Waveform::FatSaw, Waveform::Saw};
            s.wave = pickWave(r, w, 2);
            s.attackS = uni(r, 0.02f, 0.08f);
            s.decayS = uni(r, 0.25f, 0.5f);
            s.sustain = uni(r, 0.7f, 0.95f);
            s.releaseS = uni(r, 0.2f, 0.5f);
            s.glideS = uni(r, 0.03f, 0.12f);
            s.glideMode = r.chance(0.2f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)FilterMode::LP;
            s.cutoffHz = uni(r, 1000.f, 2500.f);
            s.resonance = uni(r, 0.05f, 0.3f);
            s.fenvAtkS = uni(r, 0.02f, 0.09f);  // rises into the "blat"
            s.fenvOct = uni(r, 1.5f, 2.8f);
            s.fenvDecS = uni(r, 0.25f, 0.5f);
            if (s.wave == Waveform::FatSaw) s.detuneCents = uni(r, 6.f, 14.f);
            s.drive = uni(r, 1.5f, 3.f);
            s.autoVibCents = uni(r, 1.f, 5.f);  // section shimmer
            if (r.chance(0.7f)) s.chorusDepth = uni(r, 0.2f, 0.45f);
            if (r.chance(0.3f)) rollEcho(r, s, 0.08f, 0.2f, 0.2f, 0.35f);
            if (r.chance(0.6f)) rollRoom(r, s, 0.15f, 0.35f, 0.5f, 0.7f);
            if (r.chance(0.3f)) addMod(s, ModSource::ModEnv, ModDest::Drive, uni(r, 0.2f, 0.5f));
            g.tiltRoute = (uint8_t)(r.chance(0.5f) ? TiltRoute::Cutoff : TiltRoute::Vibrato);
            break;
        }
        case Archetype::Chip: {  // 8-bit: bright, dry-ish, trills and tremolo
            static const Waveform w[2] = {Waveform::Square, Waveform::Pulse};
            s.wave = pickWave(r, w, 2);
            s.attackS = uni(r, 0.001f, 0.005f);
            s.decayS = uni(r, 0.08f, 0.3f);
            s.sustain = uni(r, 0.3f, 0.8f);
            s.releaseS = uni(r, 0.05f, 0.2f);
            s.glideS = uni(r, 0.01f, 0.06f);
            s.glideMode = r.chance(0.15f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)FilterMode::LP;
            s.cutoffHz = uni(r, 3000.f, 9000.f);  // barely a filter: the raw chip
            s.resonance = uni(r, 0.f, 0.3f);
            if (r.chance(0.3f)) {
                s.fenvOct = uni(r, 0.2f, 0.8f);
                s.fenvDecS = uni(r, 0.05f, 0.15f);
            }
            if (r.chance(0.5f)) s.noiseLevel = uni(r, 0.02f, 0.12f);  // the blip
            if (r.chance(0.25f)) s.subLevel = uni(r, 0.2f, 0.5f);
            s.drive = uni(r, 1.f, 2.f);
            if (r.chance(0.45f)) rollEcho(r, s, 0.1f, 0.3f, 0.15f, 0.35f);
            if (r.chance(0.25f)) rollRoom(r, s, 0.05f, 0.2f, 0.4f, 0.6f);
            s.lfo1RateHz = uni(r, 4.f, 9.f);
            s.lfo1Shape = (uint8_t)(r.chance(0.5f) ? LfoShape::Square : LfoShape::SH);
            if (r.chance(0.5f)) addMod(s, ModSource::LFO1, ModDest::Pitch, uni(r, 0.02f, 0.06f) * (r.chance(0.5f) ? 1.f : -1.f));  // the trill
            else addMod(s, ModSource::LFO1, ModDest::Amp, uni(r, 0.3f, 0.5f));  // the tremolo
            g.tiltRoute = (uint8_t)(r.chance(0.5f) ? TiltRoute::Volume : TiltRoute::Vibrato);
            break;
        }
        // ---- the second wave (genver 3) — reachable only via the V3 pool ----
        // Each window is designed to land in a family the FROZEN classifier
        // already names well (whistle sings -> lead words, wobble carries ->
        // bass words, organ holds -> the choir/cavern bank...), so the naming
        // contract never moves: nobody's sound is ever relabelled by an update.
        case Archetype::Whistle: {  // the slide whistle / theremin — a held PURE
                                    // wave that breathes and sings. The lineage
                                    // voice: the instrument began as one.
            static const Waveform w[3] = {Waveform::Sine, Waveform::Sine, Waveform::Triangle};
            s.wave = pickWave(r, w, 3);
            s.attackS = uni(r, 0.015f, 0.08f);   // the breath onset
            s.decayS = uni(r, 0.2f, 0.5f);
            s.sustain = uni(r, 0.75f, 1.f);
            s.releaseS = uni(r, 0.15f, 0.5f);
            s.glideS = uni(r, 0.06f, 0.16f);     // gliding IS the instrument here
            s.glideMode = r.chance(0.65f) ? GlideMode::Always : GlideMode::LegatoOnly;
            if (s.glideMode == GlideMode::LegatoOnly && r.chance(0.4f))
                s.glideS = uni(r, 0.12f, 0.22f); // dreamier hammer-on slides only
            s.filterMode = (uint8_t)FilterMode::LP;
            s.cutoffHz = uni(r, 2500.f, 7000.f); // open: a pure wave needs no reining in
            s.resonance = uni(r, 0.f, 0.25f);
            s.noiseLevel = uni(r, 0.02f, 0.09f); // the breath in the tone
            if (r.chance(0.3f)) {                // a soft over-blow bloom
                s.fenvAtkS = uni(r, 0.004f, 0.015f);  // (< the brass-classify gate)
                s.fenvOct = uni(r, 0.3f, 0.8f);
                s.fenvDecS = uni(r, 0.2f, 0.5f);
            }
            s.drive = uni(r, 1.f, 1.6f);
            s.autoVibCents = uni(r, 4.f, 12.f);  // it SINGS (and names as a lead)
            if (r.chance(0.25f)) s.chorusDepth = uni(r, 0.1f, 0.3f);
            if (r.chance(0.5f)) rollEcho(r, s, 0.15f, 0.35f, 0.25f, 0.5f);
            if (r.chance(0.7f)) rollRoom(r, s, 0.2f, 0.45f, 0.55f, 0.85f);
            if (r.chance(0.35f)) {               // a slow swell of air under the tone
                s.lfo2RateHz = uni(r, 0.3f, 1.2f);
                s.lfo2Shape = (uint8_t)LfoShape::Sine;
                addMod(s, ModSource::LFO2, ModDest::Amp, uni(r, 0.1f, 0.25f));
            }
            g.tiltRoute = (uint8_t)TiltRoute::Vibrato;  // the theremin hand
            g.tiltDepth = uni(r, 0.55f, 0.95f);
            break;
        }
        case Archetype::Organ: {  // drawbars under a rotary: instant-on, held,
                                  // and NO per-note filter bloom — the one
                                  // envelope shape no other archetype rolls
            static const Waveform w[5] = {Waveform::Square, Waveform::Square,
                                          Waveform::Sine, Waveform::Sine, Waveform::Pulse};
            s.wave = pickWave(r, w, 5);
            s.attackS = uni(r, 0.001f, 0.006f);
            s.decayS = uni(r, 0.05f, 0.15f);     // moot under a full sustain
            s.sustain = uni(r, 0.9f, 1.f);
            s.releaseS = uni(r, 0.05f, 0.18f);   // organ keys stop when you let go
            s.glideS = uni(r, 0.02f, 0.08f);
            s.glideMode = r.chance(0.1f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)(r.chance(0.2f) ? FilterMode::Notch : FilterMode::LP);
            s.cutoffHz = uni(r, 900.f, 2800.f);  // (< the chip-classify band)
            s.resonance = uni(r, 0.f, 0.3f);
            // fenvOct stays 0 — that flat held face IS the organ
            s.subLevel = uni(r, 0.4f, 0.8f);     // the 16' drawbar
            s.drive = uni(r, 1.f, 2.4f);
            // the rotary: fast spin or slow chorale, on the amp (and sometimes
            // a doppler brightness flutter on the cutoff)
            s.lfo1Shape = (uint8_t)LfoShape::Sine;
            s.lfo1RateHz = r.chance(0.6f) ? uni(r, 5.f, 7.f) : uni(r, 0.6f, 1.3f);
            addMod(s, ModSource::LFO1, ModDest::Amp, uni(r, 0.12f, 0.3f));
            if (r.chance(0.5f)) addMod(s, ModSource::LFO1, ModDest::Cutoff, uni(r, 0.06f, 0.15f));
            if (r.chance(0.55f)) s.chorusDepth = uni(r, 0.2f, 0.5f);
            if (r.chance(0.15f)) rollEcho(r, s, 0.08f, 0.2f, 0.15f, 0.3f);
            if (r.chance(0.65f)) rollRoom(r, s, 0.15f, 0.4f, 0.6f, 0.9f);  // the church
            g.tiltRoute = (uint8_t)TiltRoute::Volume;  // the swell pedal
            g.tiltDepth = uni(r, 0.5f, 0.9f);
            g.tiltRouteB = (uint8_t)TiltRoute::Cutoff;
            break;
        }
        case Archetype::Keys: {  // the tine piano — a soft ping over a held
                                 // middle sustain no pluck reaches, tremolo'd
            static const Waveform w[5] = {Waveform::Triangle, Waveform::Triangle,
                                          Waveform::Triangle, Waveform::Sine, Waveform::Sine};
            s.wave = pickWave(r, w, 5);
            s.attackS = uni(r, 0.001f, 0.006f);
            s.decayS = uni(r, 0.5f, 1.1f);       // the tine blooms, then settles...
            s.sustain = uni(r, 0.28f, 0.48f);    // ...onto a body a pluck never keeps
            s.releaseS = uni(r, 0.2f, 0.5f);
            s.glideS = uni(r, 0.02f, 0.08f);
            s.glideMode = r.chance(0.1f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)FilterMode::LP;
            s.cutoffHz = uni(r, 1100.f, 3200.f);
            s.resonance = uni(r, 0.05f, 0.3f);
            s.fenvAtkS = 0.001f;                 // the bark of the tine
            s.fenvOct = uni(r, 0.5f, 1.5f);
            s.fenvDecS = uni(r, 0.15f, 0.4f);
            if (r.chance(0.35f)) s.subLevel = uni(r, 0.1f, 0.3f);  // (< the bass gate)
            if (r.chance(0.25f)) s.noiseLevel = uni(r, 0.01f, 0.05f);  // hammer thump
            s.drive = r.chance(0.2f) ? uni(r, 2.8f, 3.6f)  // the driven, dirty EP
                                     : uni(r, 1.f, 2.f);
            if (r.chance(0.65f)) {               // the wobbling-speaker tremolo
                s.lfo1Shape = (uint8_t)LfoShape::Sine;
                s.lfo1RateHz = uni(r, 2.5f, 5.5f);
                addMod(s, ModSource::LFO1, ModDest::Amp, uni(r, 0.15f, 0.35f));
            }
            if (r.chance(0.5f)) s.chorusDepth = uni(r, 0.2f, 0.5f);
            if (r.chance(0.35f)) rollEcho(r, s, 0.1f, 0.3f, 0.2f, 0.4f);
            if (r.chance(0.6f)) rollRoom(r, s, 0.1f, 0.3f, 0.45f, 0.7f);
            g.tiltRoute = (uint8_t)(r.chance(0.5f) ? TiltRoute::Vibrato : TiltRoute::Cutoff);
            break;
        }
        case Archetype::Wobble: {  // dub bass: the filter breathes IN TIME —
                                   // a tempo-synced LFO rides the jam clock
                                   // the way the delay already does
            static const Waveform w[2] = {Waveform::Saw, Waveform::Square};
            s.wave = pickWave(r, w, 2);
            s.attackS = uni(r, 0.001f, 0.01f);
            s.decayS = uni(r, 0.2f, 0.4f);
            s.sustain = uni(r, 0.75f, 1.f);      // it has to HOLD for the wobble to show
            s.releaseS = uni(r, 0.1f, 0.3f);
            s.glideS = uni(r, 0.03f, 0.12f);
            s.glideMode = r.chance(0.15f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)FilterMode::LP;
            s.cutoffHz = uni(r, 500.f, 1100.f);  // the LFO opens 1-2 octaves from here
            s.resonance = uni(r, 0.25f, 0.6f);
            if (r.chance(0.25f)) {               // a small strike ping, sometimes
                s.fenvOct = uni(r, 0.3f, 0.8f);
                s.fenvDecS = uni(r, 0.1f, 0.25f);
            }
            s.subLevel = uni(r, 0.5f, 0.85f);    // the carry (and the bass name)
            s.drive = uni(r, 2.f, 4.f);
            // THE wobble: synced LFO1 into the cutoff. Square = the gate chop.
            s.lfo1Sync = (uint8_t)r.i(1, kDelaySyncCount - 1);
            s.lfo1Shape = (uint8_t)(r.chance(0.4f) ? LfoShape::Sine
                                                   : (r.chance(0.5f) ? LfoShape::Tri : LfoShape::Square));
            addMod(s, ModSource::LFO1, ModDest::Cutoff, uni(r, 0.25f, 0.5f));
            if (r.chance(0.3f)) {                // a slow tide under the wobble
                s.lfo2RateHz = uni(r, 0.1f, 0.4f);
                addMod(s, ModSource::LFO2, ModDest::Resonance,
                       uni(r, 0.15f, 0.3f) * (r.chance(0.5f) ? 1.f : -1.f));
            }
            if (r.chance(0.35f)) rollEcho(r, s, 0.1f, 0.25f, 0.3f, 0.55f);  // dub tails
            if (r.chance(0.25f)) rollRoom(r, s, 0.05f, 0.15f, 0.4f, 0.6f);
            if (r.chance(0.1f)) s.chorusDepth = uni(r, 0.1f, 0.25f);
            g.tiltRoute = (uint8_t)TiltRoute::Cutoff;  // the manual wub over the auto one
            g.tiltDepth = uni(r, 0.6f, 1.f);
            break;
        }
        case Archetype::Strings: {  // the bowed ensemble — quicker and brighter
                                    // than a pad: rosin, section vibrato, chorus
            static const Waveform w[3] = {Waveform::FatSaw, Waveform::FatSaw, Waveform::Saw};
            s.wave = pickWave(r, w, 3);
            s.attackS = uni(r, 0.10f, 0.26f);    // bowed, not swelled
            s.decayS = uni(r, 0.3f, 0.7f);
            s.sustain = uni(r, 0.75f, 1.f);
            s.releaseS = uni(r, 0.4f, 1.2f);
            s.glideS = uni(r, 0.04f, 0.14f);
            s.glideMode = r.chance(0.45f) ? GlideMode::Always : GlideMode::LegatoOnly;
            if (r.chance(0.15f)) {               // the thin chamber voicing
                s.filterMode = (uint8_t)FilterMode::HP;
                s.cutoffHz = uni(r, 200.f, 600.f);
            } else {
                s.filterMode = (uint8_t)FilterMode::LP;
                s.cutoffHz = uni(r, 1800.f, 5000.f);  // the rosin brightness
            }
            s.resonance = uni(r, 0.f, 0.3f);
            if (r.chance(0.35f)) {               // a gentle bow-pressure bloom
                s.fenvAtkS = uni(r, 0.004f, 0.015f);  // (< the brass-classify gate)
                s.fenvOct = uni(r, 0.3f, 0.9f);
                s.fenvDecS = uni(r, 0.4f, 0.9f);
            }
            if (s.wave == Waveform::FatSaw) s.detuneCents = uni(r, 12.f, 28.f);
            if (r.chance(0.2f)) s.noiseLevel = uni(r, 0.01f, 0.04f);  // bow air
            s.drive = uni(r, 1.f, 1.8f);
            s.autoVibCents = uni(r, 2.5f, 7.f);  // the section's vibrato
            if (r.chance(0.9f)) s.chorusDepth = uni(r, 0.45f, 0.7f);  // THE ensemble
            if (r.chance(0.25f)) rollEcho(r, s, 0.08f, 0.2f, 0.2f, 0.4f);
            if (r.chance(0.85f)) rollRoom(r, s, 0.2f, 0.45f, 0.6f, 0.9f);
            g.tiltRoute = (uint8_t)(r.chance(0.5f) ? TiltRoute::Volume : TiltRoute::Vibrato);
            g.tiltDepth = uni(r, 0.45f, 0.85f);
            break;
        }
        // ---- the third wave (genver 5) — reachable only via the V5 pool ----
        // Same relabel-safety design as the second wave: each window lands in
        // a family the FROZEN classifier names acceptably (drone swells ->
        // pad words, gate holds bright -> chip/wild words); their OWN noun
        // rows are reached only through the versioned classifySoundV2, which
        // never names anything that re-derives on an older device.
        case Archetype::Drone: {  // the meditative held-forever voice — slow
                                  // bloom, sustain pinned, sub weight, a big
                                  // room, glacial motion. Nothing else rolls
                                  // "holds indefinitely and barely moves."
            static const Waveform w[4] = {Waveform::FatSaw, Waveform::Saw,
                                          Waveform::Sine, Waveform::Triangle};
            s.wave = pickWave(r, w, 4);
            s.attackS = uni(r, 0.30f, 0.55f);    // the bloom (audition holds for it)
            s.decayS = uni(r, 0.5f, 1.0f);       // moot under the pinned sustain
            s.sustain = uni(r, 0.90f, 1.f);      // it HOLDS — that is the character
            s.releaseS = uni(r, 1.2f, 2.0f);     // and it takes its time leaving
            s.glideS = 0.06f + r.f() * r.f() * 0.10f;
            s.glideMode = r.chance(0.7f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)FilterMode::LP;
            s.cutoffHz = uni(r, 500.f, 1800.f);  // dark by default — incense, not glare
            s.resonance = uni(r, 0.05f, 0.4f);
            if (r.chance(0.4f)) {                // a very slow inner filter tide
                s.fenvAtkS = uni(r, 0.2f, 0.45f);
                s.fenvOct = uni(r, 0.3f, 0.8f);
                s.fenvDecS = uni(r, 0.8f, 1.2f);
            }
            s.subLevel = uni(r, 0.4f, 0.8f);     // the ground note under the cloud
            if (s.wave == Waveform::FatSaw) s.detuneCents = uni(r, 12.f, 26.f);
            if (r.chance(0.25f)) s.noiseLevel = uni(r, 0.01f, 0.04f);  // air
            s.drive = uni(r, 1.f, 2.f);
            if (r.chance(0.7f)) s.chorusDepth = uni(r, 0.3f, 0.6f);
            if (r.chance(0.2f)) rollEcho(r, s, 0.08f, 0.2f, 0.25f, 0.5f);
            rollRoom(r, s, 0.30f, 0.55f, 0.70f, 0.95f);  // the temple, always
            if (r.chance(0.3f)) s.autoVibCents = uni(r, 0.5f, 3.f);
            s.lfo1RateHz = uni(r, 0.1f, 0.3f);   // glacial breath
            s.lfo1Shape = (uint8_t)(r.chance(0.6f) ? LfoShape::Sine : LfoShape::Tri);
            addMod(s, ModSource::LFO1, ModDest::Cutoff,
                   uni(r, 0.15f, 0.35f) * (r.chance(0.5f) ? 1.f : -1.f));
            if (r.chance(0.4f)) {                // an even slower swell under it
                s.lfo2RateHz = uni(r, 0.1f, 0.25f);
                s.lfo2Shape = (uint8_t)LfoShape::Sine;
                addMod(s, ModSource::LFO2, ModDest::Amp, uni(r, 0.10f, 0.22f));
            }
            g.tiltRoute = (uint8_t)(r.chance(0.55f) ? TiltRoute::Cutoff : TiltRoute::Volume);
            g.tiltDepth = uni(r, 0.5f, 0.85f);
            break;
        }
        case Archetype::Gate: {  // the trance gate — a held tone whose VOLUME
                                 // chops on a tempo-synced square LFO, the way
                                 // wobble's filter already breathes in time
            static const Waveform w[3] = {Waveform::Saw, Waveform::Square, Waveform::FatSaw};
            s.wave = pickWave(r, w, 3);
            s.attackS = uni(r, 0.001f, 0.01f);
            s.decayS = uni(r, 0.2f, 0.4f);
            s.sustain = uni(r, 0.85f, 1.f);      // it must HOLD for the chop to show
            s.releaseS = uni(r, 0.08f, 0.25f);
            s.glideS = uni(r, 0.02f, 0.08f);
            s.glideMode = r.chance(0.15f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)FilterMode::LP;
            s.cutoffHz = uni(r, 1200.f, 4000.f);
            s.resonance = uni(r, 0.05f, 0.45f);  // (< the acid-classify gate)
            if (r.chance(0.3f)) {                // a small strike ping, sometimes
                s.fenvOct = uni(r, 0.3f, 0.9f);
                s.fenvDecS = uni(r, 0.1f, 0.25f);
            }
            if (r.chance(0.5f)) s.subLevel = uni(r, 0.15f, 0.38f);  // (< the wobble gate)
            if (s.wave == Waveform::FatSaw) s.detuneCents = uni(r, 8.f, 18.f);
            s.drive = uni(r, 1.5f, 3.f);
            // THE gate: synced square LFO1 chopping the amp. sanitize's ±0.60
            // clamp keeps the troughs audible rather than gating to silence.
            s.lfo1Sync = (uint8_t)r.i(1, kDelaySyncCount - 1);
            s.lfo1Shape = (uint8_t)LfoShape::Square;
            addMod(s, ModSource::LFO1, ModDest::Amp, uni(r, 0.40f, 0.60f));
            if (r.chance(0.35f)) {               // slow free tide under the chop
                s.lfo2RateHz = uni(r, 0.2f, 0.8f);
                s.lfo2Shape = (uint8_t)LfoShape::Sine;
                addMod(s, ModSource::LFO2, ModDest::Cutoff,
                       uni(r, 0.10f, 0.25f) * (r.chance(0.5f) ? 1.f : -1.f));
            }
            if (r.chance(0.5f)) rollEcho(r, s, 0.15f, 0.30f, 0.2f, 0.45f);
            if (r.chance(0.35f)) rollRoom(r, s, 0.08f, 0.25f, 0.4f, 0.7f);
            if (r.chance(0.4f)) s.chorusDepth = uni(r, 0.2f, 0.4f);
            g.tiltRoute = (uint8_t)TiltRoute::Cutoff;  // ride the filter over the chop
            g.tiltDepth = uni(r, 0.6f, 0.95f);
            break;
        }
        case Archetype::Wild:
        default: {  // anything-goes chaos across the FULL Range table — the
                    // old spirit, kept in the pool; sanitize reels in the trash
            s.wave = (Waveform)r.i(0, (int)Waveform::Count - 1);
            s.glideS = r.f() * r.f() * kGlide.hi;  // legacy skew: mostly quick
            s.glideMode = r.chance(0.35f) ? GlideMode::Always : GlideMode::LegatoOnly;
            s.filterMode = (uint8_t)r.i(0, (int)FilterMode::Count - 1);
            s.cutoffHz = uni(r, kCutoff.lo, kCutoff.hi);
            s.resonance = uni(r, kReso.lo, kReso.hi);
            s.attackS = uni(r, 0.f, 1.f);
            s.decayS = uni(r, kDecay.lo, kDecay.hi);
            s.sustain = uni(r, 0.f, 1.f);
            s.releaseS = uni(r, kRelease.lo, kRelease.hi);
            if (r.chance(0.5f)) s.detuneCents = uni(r, kDetune.lo, kDetune.hi);
            if (r.chance(0.5f)) s.fenvOct = uni(r, kFenvOct.lo, kFenvOct.hi);
            s.fenvDecS = uni(r, kFenvDec.lo, kFenvDec.hi);
            s.fenvAtkS = uni(r, 0.001f, 0.3f);
            if (r.chance(0.5f)) s.subLevel = uni(r, kSub.lo, kSub.hi);
            if (r.chance(0.3f)) s.noiseLevel = uni(r, 0.f, 0.2f);
            s.drive = uni(r, kDrive.lo, kDrive.hi);
            if (r.chance(0.3f)) s.autoVibCents = uni(r, 0.f, 12.f);
            if (r.chance(0.5f)) s.chorusDepth = uni(r, kChorus.lo, kChorus.hi);
            if (r.chance(0.5f)) rollEcho(r, s, 0.1f, kDelay.hi, 0.15f, kDelayFb.hi);
            if (r.chance(0.5f)) rollRoom(r, s, 0.1f, kReverb.hi, kRvbSize.lo, kRvbSize.hi);
            if (r.chance(0.25f)) s.lfo1Sync = (uint8_t)r.i(1, kDelaySyncCount - 1);
            const int nmod = r.i(1, 3);
            for (int i = 0; i < nmod; ++i) {
                const ModSource src = (ModSource)r.i(1, (int)ModSource::Count - 1);
                const ModDest dst = (ModDest)r.i(1, (int)ModDest::Count - 1);
                addMod(s, src, dst, (r.f() * 2.f - 1.f) * 0.7f);
            }
            break;
        }
    }

    // spice: a small chance of ONE off-archetype twist, so the families stay
    // connected instead of collapsing into nine fixed presets with jitter.
    if (r.chance(0.12f)) {
        if (r.chance(0.5f) && s.filterMode == (uint8_t)FilterMode::LP) {
            s.filterMode = (uint8_t)r.i(1, (int)FilterMode::Count - 1);
        } else {
            const ModSource src = (ModSource)r.i(1, (int)ModSource::Count - 1);
            const ModDest dst = (ModDest)r.i(1, (int)ModDest::Count - 1);
            addMod(s, src, dst, (r.f() * 2.f - 1.f) * 0.5f);
        }
    }

    // Second-wave polish (gated on the new archetypes so the v2 pool stays
    // bit-exact — rollPolish runs no RNG). For rolls that reach the engine
    // through the V3 layer, generateSoundV3 applies the same polish to EVERY
    // archetype (idempotent, so this pre-sanitize pass stays a no-op there).
    if ((int)a >= kArchetypeCountV2) rollPolish(g, a);

    sanitizePatch(g);
    return g;
}

GenPatch generateSoundV3(uint32_t seed, Archetype a) {
    // The V3 layer: the frozen paint engine plus the roll polish for ALL
    // archetypes. This is where a v2-family roll (bell, pad, wild… — still
    // three quarters of the expanded pool) sheds the frozen pool's one known
    // dead-roll quirk: the spice twist stranding a pure wave behind an HP/BP.
    // generateSound(seed[, a]) itself stays bit-exact for genver-2 devices —
    // only rolls minted through V3 (Randomize, genver>=3 slots) are polished.
    // Measured before/after on 3000 rolls: the preview-silent/plays-audible
    // tail this quirk caused drops to zero, with no other field disturbed.
    GenPatch g = generateSound(seed, a);
    rollPolish(g, a);
    return g;
}

GenPatch generateSoundV4(uint32_t seed) { return generateSoundV4(seed, archetypeForSeedV3(seed)); }

GenPatch generateSoundV4(uint32_t seed, Archetype a) {
    // V4 = V3 plus a rolled analog drift. It exists as its own version for one
    // reason: V3 is frozen the moment a device is born under it, because
    // genver-3 units re-derive their o/p slots through it on every boot. Adding
    // the draw inside V3 would have quietly retuned sounds people already own.
    //
    // The draw takes its OWN Rng rather than the paint's, so it cannot advance
    // the shared stream and shift every subsequent value — the whole reason a
    // frozen generator is fragile. Consequence: V4's non-drift output is
    // bit-identical to V3's, which the test suite asserts directly.
    //
    // Most rolls get a little; a quarter get none at all (dead-still digital is
    // a legitimate character, and a pluck rarely wants wander); a few get a lot.
    GenPatch g = generateSoundV3(seed, a);
    Rng r(seed ^ 0x51ED2703u);
    const float roll = r.f();
    g.synth.driftCents = roll < 0.25f  ? 0.f
                         : roll < 0.85f ? uni(r, 1.5f, 5.f)     // the usual: alive
                                        : uni(r, 5.f, 11.f);    // seasick vintage
    sanitizePatch(g);
    return g;
}

namespace {

// The V5 style recolors — pure, RNG-free field transforms applied AFTER the
// frozen paint, so the same family stops meaning the same personality: two
// pluck rolls can now be a kalimba and a muted funk stab instead of two
// shades of one preset. Style 0 is always "classic" (bit-identical to the V4
// roll — asserted in the suite); every transform is loose on purpose because
// sanitizePatch re-runs after it and re-imposes every coupling rule. Wild
// keeps no styles (its identity IS the chaos), and the third-wave archetypes
// keep none yet — their windows are brand new and Phase-3 range tuning from
// the field data will shape them before this version freezes.
void applyStyleV5(GenPatch& g, Archetype a, int style) {
    if (style <= 0) return;
    SynthParams& s = g.synth;
    const bool s1 = style == 1;
    auto hi = [](float& v, float f) { if (v < f) v = f; };
    auto lo = [](float& v, float f) { if (v > f) v = f; };
    switch (a) {
        case Archetype::Pluck:
            if (s1) {  // kalimba: soft, woody, quick and close
                s.wave = Waveform::Triangle;
                s.cutoffHz *= 0.55f;  hi(s.cutoffHz, 500.f);
                s.decayS *= 0.65f;    s.fenvOct *= 0.5f;
                s.chorusDepth = 0.f;  hi(s.reverbMix, 0.18f);
            } else {   // muted funk: choked, driven, midrange
                lo(s.cutoffHz, 800.f); hi(s.cutoffHz, 450.f);
                s.sustain *= 0.4f;   s.decayS *= 0.5f;
                s.drive += 1.6f;     s.delayMix *= 0.4f;
            }
            break;
        case Archetype::Bell:
            if (s1) {  // music box: small, bright, close — decay floored at
                       // 0.5 so it stays a BELL to the frozen classifier
                       // (< 0.45 s of pure-wave decay reads as a pluck)
                s.cutoffHz *= 1.5f;  s.decayS *= 0.55f;  hi(s.decayS, 0.5f);
                s.releaseS *= 0.6f;
                hi(s.reverbMix, 0.22f);
            } else {   // gong: long, dark, a little dirty
                hi(s.decayS, 1.35f);  hi(s.releaseS, 1.2f);
                s.cutoffHz *= 0.65f; s.drive += 0.8f;  hi(s.chorusDepth, 0.25f);
            }
            break;
        case Archetype::Pad:
            if (s1) {  // glass: open, still, precise
                s.cutoffHz *= 1.6f;  s.resonance += 0.15f;
                s.chorusDepth *= 0.4f; s.detuneCents *= 0.35f;
            } else {   // dark cinema: low, wide, cavernous
                s.cutoffHz *= 0.5f;  s.subLevel += 0.3f;
                hi(s.reverbSize, 0.85f); s.reverbMix += 0.15f;
            }
            break;
        case Archetype::Bass:
            if (s1) {  // round sub: clean weight
                s.drive *= 0.5f;  s.cutoffHz *= 0.6f;  hi(s.subLevel, 0.75f);
            } else {   // growler
                s.drive += 1.8f;  s.resonance += 0.2f;  s.fenvDecS *= 1.6f;
            }
            break;
        case Archetype::Acid:
            if (s1) {  // deep dub: low squelch swimming in echo
                hi(s.delayMix, 0.35f);  hi(s.delayFb, 0.55f);  s.cutoffHz *= 0.7f;
            } else {   // screamer (sanitize re-caps drive under the high Q)
                hi(s.resonance, 0.8f);  hi(s.fenvOct, 3.2f);  s.fenvDecS *= 0.8f;
            }
            break;
        case Archetype::Lead:
            if (s1) {  // breath lead: softer, singier
                s.drive *= 0.5f;  s.cutoffHz *= 0.7f;
                s.autoVibCents += 4.f;  s.attackS += 0.05f;
            } else {   // biting lead
                s.drive += 1.5f;  s.cutoffHz *= 1.4f;  s.glideS *= 1.4f;
            }
            break;
        case Archetype::Brass:
            if (s1) {  // mellow horn
                s.cutoffHz *= 0.6f;  s.drive *= 0.6f;  s.fenvOct *= 0.6f;
            } else {   // stab section
                s.attackS *= 0.4f;  s.decayS *= 0.6f;
                s.sustain *= 0.8f;  s.drive += 1.2f;
            }
            break;
        case Archetype::Chip:
            if (s1) {  // lofi lull: rounded, echoing
                s.cutoffHz *= 0.5f;  s.releaseS += 0.2f;  hi(s.delayMix, 0.25f);
            } else {   // arcade shrill
                hi(s.cutoffHz, 7000.f);  s.noiseLevel += 0.08f;  hi(s.lfo1RateHz, 6.5f);
            }
            break;
        case Archetype::Whistle:
            if (s1) {  // airy: more breath, more open, singier
                s.noiseLevel += 0.04f;  s.cutoffHz *= 1.3f;  s.autoVibCents += 2.f;
            } else {   // flutter flute: the breath shakes (was "dark flute" —
                       // darkening a family the field already calls too quiet
                       // was the wrong direction; see rollPolishV5)
                s.lfo1Shape = (uint8_t)LfoShape::Sine;
                hi(s.lfo1RateHz, 4.5f);
                addMod(s, ModSource::LFO1, ModDest::Amp, 0.28f);
                s.autoVibCents *= 1.4f;
            }
            break;
        case Archetype::Organ:  // fenvOct stays 0 — the flat face IS the organ
            if (s1) {  // cathedral: chorale spin in a huge nave
                hi(s.reverbMix, 0.4f);  hi(s.reverbSize, 0.9f);  lo(s.lfo1RateHz, 1.0f);
            } else {   // driven spin: the pushed rotary
                s.drive += 1.4f;  hi(s.lfo1RateHz, 6.0f);  hi(s.chorusDepth, 0.25f);
            }
            break;
        case Archetype::Keys:
            if (s1) {  // dusty tape EP
                s.cutoffHz *= 0.65f;  s.driftCents += 4.f;  hi(s.chorusDepth, 0.3f);
            } else {   // glassy EP
                s.cutoffHz *= 1.5f;  s.fenvOct *= 1.5f;  s.drive *= 0.7f;
            }
            break;
        case Archetype::Wobble:
            if (s1) {  // half-time swamp: the slow deep chop
                s.lfo1Sync = 1;  hi(s.subLevel, 0.75f);  // 1 = the 1/4 division
                s.cutoffHz *= 0.85f;
            } else {   // reso screech wob
                hi(s.resonance, 0.6f);  s.drive += 0.8f;
            }
            break;
        case Archetype::Strings:
            if (s1) {  // chamber: smaller, drier, closer
                s.chorusDepth *= 0.45f;  s.reverbMix *= 0.55f;
                s.attackS *= 0.7f;  s.cutoffHz *= 1.15f;
            } else {   // cinematic swell
                hi(s.attackS, 0.24f);  hi(s.reverbSize, 0.85f);
                s.detuneCents += 6.f;  s.cutoffHz *= 0.8f;
            }
            break;
        default: break;  // Wild / Drone / Gate: classic only (see above)
    }
}

// The V5 polish — a SUPERSET of the frozen rollPolish (that one is shared by
// V3 and V4 and can never change). Pure rules, no RNG, idempotent. New rules
// guard the third wave's identities plus anything a style recolor could bend.
void rollPolishV5(GenPatch& g, Archetype a) {
    rollPolish(g, a);
    SynthParams& s = g.synth;
    // Whistle/bell audibility (field report, 2026-09-15): both families are
    // PURE waves, and at playing pitch a lone sine partial sits mostly below
    // the 1 W speaker's rolloff — measured with the speaker-weighted probe,
    // bell's audition-lick presence was 8-10x under the saw families. Drive
    // is the lever (harmonics land IN the speaker's band); whistle's breath
    // noise gets capped (broadband hiss was masking an already-quiet tone);
    // and both lose their glide excess so lick notes actually LAND (whistle
    // rolled Always-glide 65% of the time at up to 160 ms — heard as smear).
    // V5-only: the frozen V3/V4 paths still roll these families as they did.
    if (a == Archetype::Whistle) {
        // 3.2 came out of the v3.2-era vocal-tract experiment: the drive
        // stage is what manufactures a pure tone's audible partials, and
        // ~3.2 was the measured floor for a sine that CARRIES the speaker
        if (s.drive < 3.2f) s.drive = 3.2f;
        if (s.noiseLevel > 0.05f) s.noiseLevel = 0.05f;
        if (s.glideMode == GlideMode::Always && s.glideS > 0.09f) s.glideS = 0.09f;
        if (s.glideS > 0.13f) s.glideS = 0.13f;
    }
    if (a == Archetype::Bell) {
        if (s.drive < 2.8f) s.drive = 2.8f;
        if (s.glideS > 0.08f) s.glideS = 0.08f;
        // the strike ping is the bell's one in-band signature on this
        // speaker: keep it bright, let it ring a beat longer, and give the
        // hammer a real mallet clack (noise is broadband — always audible —
        // and it gates with the envelope, so no sustained hiss; 0.035 stays
        // under the 0.06 gritty-adjective gate so names keep their shimmer)
        if (s.cutoffHz < 3000.f) s.cutoffHz = 3000.f;
        if (s.fenvOct < 2.0f) s.fenvOct = 2.0f;
        if (s.fenvDecS < 0.12f) s.fenvDecS = 0.12f;
        if (s.noiseLevel < 0.035f) s.noiseLevel = 0.035f;
    }
    if (a == Archetype::Drone) {  // a drone that lets go isn't a drone
        if (s.sustain < 0.85f) s.sustain = 0.85f;
        if (s.releaseS < 0.9f) s.releaseS = 0.9f;
        if (s.filterMode == (uint8_t)FilterMode::HP)
            s.filterMode = (uint8_t)FilterMode::LP;  // the sub carry, like wobble
    }
    if (a == Archetype::Gate) {  // the chop must exist, in time, and hold
        if (s.lfo1Sync == 0) s.lfo1Sync = 3;  // 1/8 — the workhorse division
        if (s.sustain < 0.7f) s.sustain = 0.7f;
        bool chop = false;
        for (int i = 0; i < kModSlots; ++i)
            if (s.slots[i].src == (uint8_t)ModSource::LFO1 &&
                s.slots[i].dest == (uint8_t)ModDest::Amp &&
                s.slots[i].depth >= 0.35f)
                chop = true;
        if (!chop) addMod(s, ModSource::LFO1, ModDest::Amp, 0.5f);
    }
}

// ---- V6: what 202 rated rolls said -------------------------------------
// The first field rating session (2026-09-22, docs/roadmap/28) rated every
// Randomize press 1bad..4great and saved the patch. Every rule below is one
// measured signal from that data — a style whose mean sat under its family's
// classic, or a parameter whose good and bad medians sat far apart within
// one family — turned into one correction or one clamp. Both layers run on
// the FINISHED V5 roll (V5 shipped in v3.3 and is frozen), so a family with
// no rule here passes through bit-identical; the suite asserts that for the
// families the ear called healthy (lead, wobble, keys, gate, strings,
// organ, brass). Pure, RNG-free, idempotent; sanitizePatch re-runs after.

// Style corrections — applied after V5's recolor, so each undoes part of a
// V5 recolor the ratings said went too far.
void applyStyleV6(GenPatch& g, Archetype a, int style) {
    if (style <= 0) return;
    SynthParams& s = g.synth;
    const bool s1 = style == 1;
    switch (a) {
        case Archetype::Pad:
            if (!s1) {  // "cinema" rated 1.90 (n=10): V5 halved the cutoff,
                        // added 0.3 of sub and forced a 0.85 hall — the bad
                        // pads had reverbSize 0.85 and sub 0.30 against the
                        // good pads' 0.62 and 0.10. Net: cutoff x0.7, sub
                        // +0.15, hall capped at 0.70, mix +0.08.
                s.cutoffHz *= 1.4f;
                s.subLevel -= 0.15f;
                if (s.subLevel < 0.f) s.subLevel = 0.f;
                if (s.reverbSize > 0.70f) s.reverbSize = 0.70f;
                s.reverbMix -= 0.07f;
                if (s.reverbMix < 0.f) s.reverbMix = 0.f;
            }
            break;
        case Archetype::Pluck:
            if (s1) {  // "kalimba" rated 2.33 against classic 3.00 (n=12):
                       // good plucks had cutoff 1440 Hz and decay 0.50 s,
                       // bad ones 616 Hz and 0.31 s — V5's x0.55 / x0.65 cut
                       // straight into the bad window. Net: x0.8 (floor
                       // 900) and x0.85.
                s.cutoffHz *= 1.45f;
                if (s.cutoffHz < 900.f) s.cutoffHz = 900.f;
                s.decayS *= 1.3f;
                // ...but under the frozen classifier's bell line: a pure-wave
                // decay of 0.45 s or more names as a bell, and a kalimba that
                // rolls as "pluck" must keep naming like one.
                if (s.decayS > 0.44f) s.decayS = 0.44f;
            }
            break;
        case Archetype::Bass:
            if (s1) {  // "round" rated 2.64 against classic 3.00 (n=11):
                       // bad basses had cutoff 339 Hz and drive 1.4, good
                       // ones 729 Hz and 2.14. Net: cutoff x0.8, drive x0.75.
                s.cutoffHz *= 1.33f;
                s.drive *= 1.5f;
            }
            break;
        case Archetype::Chip:
            if (!s1) {  // "arcade" rated 1.50 (n=6): V5 forced the cutoff to
                        // 7 kHz and the LFO to 6.5 Hz; every bad chip sat at
                        // 7 kHz with a 5-9 Hz wobble, the one good chip at
                        // 1.9 kHz.
                if (s.cutoffHz > 4000.f) s.cutoffHz = 4000.f;
                if (s.lfo1RateHz > 5.f) s.lfo1RateHz = 5.f;
            }
            break;
        case Archetype::Whistle:
            if (s1) {  // "airy" rated 2.00 (n=6): bad whistles were the
                       // brighter ones (6.1 kHz vs 4.0 kHz). Net: x1.1.
                s.cutoffHz *= 0.85f;
            }
            break;
        case Archetype::Bell:
            if (s1) {  // "music box" rated 1.75 (n=4 — light touch only):
                       // let it ring a little longer than the 0.5 s floor.
                if (s.decayS < 0.7f) s.decayS = 0.7f;
            }
            break;
        case Archetype::Organ:
            if (s1) {  // "cathedral" rated 2.00 against classic 2.40 and
                       // "driven" 2.80 (n=2 — light touch): V5 forces a 0.4
                       // mix into a 0.9 hall, and pool-wide the bad rolls
                       // carried twice the reverb of the good ones on this
                       // one-watt speaker. Half the wash, keep the nave.
                if (s.reverbMix > 0.30f) s.reverbMix = 0.30f;
                if (s.reverbSize > 0.80f) s.reverbSize = 0.80f;
            }
            break;
        default: break;  // no rating signal: the V5 style stands
    }
}

// Family-wide clamps toward each family's GOOD median — never a new window,
// so the worst case is a family sounding a little more like its rolls the
// ear liked. Data rows in docs/roadmap/28-field-data-round-1.md, Part B.
void rollPolishV6(GenPatch& g, Archetype a) {
    SynthParams& s = g.synth;
    switch (a) {
        case Archetype::Chip:  // 1/13 good: open 7 kHz, ~0.15 s release, fast LFO
            if (s.cutoffHz > 5000.f) s.cutoffHz = 5000.f;
            if (s.releaseS < 0.22f) s.releaseS = 0.22f;
            if (s.lfo1RateHz > 6.f) s.lfo1RateHz = 6.f;
            break;
        case Archetype::Wild:  // 0/5 good: both bad ones were slow-attack,
                               // long-release, hard-driven — mushy pads by
                               // the classifier's own verdict
            if (s.attackS > 0.15f) s.attackS = 0.15f;
            if (s.releaseS > 0.8f) s.releaseS = 0.8f;
            if (s.drive > 3.5f) s.drive = 3.5f;
            break;
        case Archetype::Pad:  // good 1.5 drive / 0.10 sub, bad 1.84 / 0.30
            if (s.drive > 1.6f) s.drive = 1.6f;
            if (s.subLevel > 0.25f) s.subLevel = 0.25f;
            break;
        case Archetype::Pluck:  // good 1440 Hz / 0.50 s, bad 616 Hz / 0.31 s
            if (s.cutoffHz < 800.f) s.cutoffHz = 800.f;
            if (s.decayS < 0.35f) s.decayS = 0.35f;
            break;
        case Archetype::Bass:  // good 729 Hz, bad 339 Hz
            if (s.cutoffHz < 450.f) s.cutoffHz = 450.f;
            break;
        case Archetype::Whistle:  // good ones had vibrato and sat at 4 kHz
            if (s.cutoffHz > 5000.f) s.cutoffHz = 5000.f;
            if (s.autoVibCents < 5.f) s.autoVibCents = 5.f;
            break;
        case Archetype::Drone:  // bad drones: sine, 0.30 Hz LFO, no vibrato
            if (s.lfo1RateHz < 0.6f) s.lfo1RateHz = 0.6f;
            if (s.autoVibCents < 2.f) s.autoVibCents = 2.f;
            if (s.wave == Waveform::Sine) s.wave = Waveform::Triangle;
            break;
        case Archetype::Acid:  // good 3.24 oct of filter env, bad 2.43
            if (s.fenvOct < 2.6f) s.fenvOct = 2.6f;
            break;
        default: break;  // lead, brass, organ, keys, wobble, strings, gate,
                         // bell: the data called them healthy — untouched
                         // (organ's cathedral style is the one exception,
                         // handled in applyStyleV6)
    }
}

}  // namespace

int styleForSeedV5(uint32_t seed) {
    // Its own stream (the next pi word), so the style draw can never advance
    // the paint's — the same isolation V4's drift roll established. Classic
    // takes a fifth of the rolls (field report: at a third, the styles read
    // as too subtle to notice), the two recolors split the rest evenly.
    Rng r(seed ^ 0x03707344u);
    const int d = r.i(0, 9);
    return d < 2 ? 0 : d < 6 ? 1 : 2;
}

GenPatch generateSoundV5(uint32_t seed) { return generateSoundV5(seed, archetypeForSeedV5(seed)); }

GenPatch generateSoundV5(uint32_t seed, Archetype a) {
    // V5 = V4 over the widest pool, then the style recolor and the V5 polish.
    // V4's paint/polish/drift are called, never copied — so the frozen goldens
    // hold by construction, and a style-0 ("classic") V5 roll is bit-identical
    // to the V4 roll of the same (seed, archetype), which the suite asserts.
    GenPatch g = generateSoundV4(seed, a);
    applyStyleV5(g, a, styleForSeedV5(seed));
    rollPolishV5(g, a);
    sanitizePatch(g);  // RNG-free: re-imposes every coupling rule on the recolor
    return g;
}

int styleForSeedV6(uint32_t seed) { return styleForSeedV5(seed); }  // inherited draw

GenPatch generateSoundV6(uint32_t seed) { return generateSoundV6(seed, archetypeForSeedV6(seed)); }

GenPatch generateSoundV6(uint32_t seed, Archetype a) {
    // V6 = the finished V5 roll, then the rating-driven style correction and
    // polish. V5 is called, never copied — it froze when v3.3 shipped — so
    // its goldens hold by construction and a family with no V6 rule is
    // bit-identical to its V5 roll (asserted in the suite).
    GenPatch g = generateSoundV5(seed, a);
    applyStyleV6(g, a, styleForSeedV6(seed));
    rollPolishV6(g, a);
    sanitizePatch(g);  // RNG-free: re-imposes every coupling rule on the corrections
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
    // the once-neglected character fields mutate too, or evolution could never
    // find a brass swell / breath / shimmer the roll didn't already have
    s.fenvAtkS     = nudge(r, s.fenvAtkS, kFenvAtk, amount, p);
    s.noiseLevel   = nudge(r, s.noiseLevel, kNoise, amount, p);
    s.autoVibCents = nudge(r, s.autoVibCents, kAutoVib, amount, p);
    s.driftCents = nudge(r, s.driftCents, kDrift, amount, p);
    s.delayFb      = nudge(r, s.delayFb, kDelayFb, amount, p);
    s.delayTimeS   = nudge(r, s.delayTimeS, kDelayTm, amount, p);
    s.reverbSize   = nudge(r, s.reverbSize, kRvbSize, amount, p);
    g.tiltDepth   = nudge(r, g.tiltDepth, kTiltDep, amount, p);
    g.tiltDepthB  = nudge(r, g.tiltDepthB, kTiltDep, amount, p);

    // categorical params: flip with a probability that climbs with amount. A
    // gentle mutate rarely changes the waveform or a routing; a wild one might.
    const float pc = amount * 0.6f;
    if (r.chance(pc)) s.wave = (Waveform)r.i(0, (int)Waveform::Count - 1);
    if (r.chance(pc * 0.7f)) {
        s.filterMode = (uint8_t)r.i(0, (int)FilterMode::Count - 1);
        // OUR flip, OUR coupling: a highpass parked high whispers the note away.
        // (Only here — a player's own HP+high-cutoff edit is never "corrected".)
        if (s.filterMode == (uint8_t)FilterMode::HP && s.cutoffHz > 1800.f) s.cutoffHz = 1800.f;
    }
    if (r.chance(pc * 0.5f)) s.glideMode = r.chance(0.5f) ? GlideMode::Always : GlideMode::LegatoOnly;
    // sanitizePatch's Always-glide landing rule, neighbourhood-respecting: a
    // player's own longer-than-cap Always glide is kept as their ceiling — the
    // mutate just can't CREATE the never-lands smear.
    if (s.glideMode == GlideMode::Always) {
        float lim = 0.16f;
        if (base.synth.glideMode == GlideMode::Always && base.synth.glideS > lim)
            lim = base.synth.glideS;
        if (s.glideS > lim) s.glideS = lim;
    }
    if (r.chance(pc * 0.6f)) s.lfo1Shape = (uint8_t)r.i(0, (int)LfoShape::Count - 1);
    if (r.chance(pc * 0.6f)) s.lfo2Shape = (uint8_t)r.i(0, (int)LfoShape::Count - 1);
    if (r.chance(pc * 0.4f)) s.delaySync = (uint8_t)r.i(0, kDelaySyncCount - 1);

    // mod matrix: occasionally rewire a slot (source+dest), and always allow the
    // depths to drift. This is where a mutate can change how a patch *moves*.
    // A Pitch destination scales ±1 octave at depth 1, so a rewire that lands
    // there is clamped to ~1 semitone of wobble (atonal warble is the classic
    // trash roll) — but a depth the PLAYER already set big is respected: drift
    // stays within their own magnitude, never flattened to ours.
    for (int i = 0; i < kModSlots; ++i) {
        if (r.chance(pc * 0.4f)) {
            const ModSource src = (ModSource)r.i(0, (int)ModSource::Count - 1);
            const ModDest   dst = (ModDest)r.i(1, (int)ModDest::Count - 1);
            float d = (r.f() * 2.f - 1.f) * 0.6f;
            if (dst == ModDest::Pitch) d = clampT(d, -0.08f, 0.08f);
            s.slots[i] = ModSlot::make(src, dst, d);
        } else if (s.slots[i].src != (uint8_t)ModSource::None) {
            const float before = s.slots[i].depth;
            float d = nudge(r, before, kModDepth, amount, p);
            if (s.slots[i].dest == (uint8_t)ModDest::Pitch) {
                const float mag = before < 0.f ? -before : before;
                const float lim = mag > 0.15f ? mag : 0.15f;
                d = clampT(d, -lim, lim);
            }
            s.slots[i].depth = d;
        }
    }

    if (r.chance(pc * 0.5f)) g.tiltRoute  = randTiltRoute(r, /*allowOff=*/false);
    if (r.chance(pc * 0.5f)) g.tiltRouteB = randTiltRoute(r, /*allowOff=*/true);
    return g;
}

// ---- patch naming -----------------------------------------------------------
namespace {
// FNV-1a step.
inline uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
// Fold a float into the hash via its quantised value (so tiny float noise
// doesn't change the name, and there are no raw-byte/padding hazards).
inline uint32_t fhash(uint32_t h, float v, float q) { return fnv(h, (uint32_t)(int32_t)(v * q)); }

// LEGACY word tables (frozen): nameForSeed/shortNameForSeed/soundName draw from
// these by raw hash bits. genver-1 devices still derive their o/p slot labels
// this way, so the tables and the bit-picking must never change.
const char* const kAdjs[] = {
    "warm", "bright", "dark", "glassy", "fuzzy", "lush", "hollow", "sharp",
    "deep", "soft", "neon", "dusty", "velvet", "frost", "ember", "tidal",
};
const char* const kNouns[] = {
    "haze", "comet", "drift", "pulse", "bloom", "grain", "choir", "river",
    "spark", "cavern", "prism", "vapor", "signal", "tide", "husk", "moss",
};

// Character-aware word banks (soundNameForPatch). Nouns are per-family (indexed
// by Archetype) and ≤6 chars so they'd even fit the compact status-bar label;
// adjectives are per-timbre. 8 per bank — picked by 3 bits of patchHash each.
// Append/extend freely BUT never reorder or replace existing words: a sound's
// derived name must stay stable across updates (saved names are baked as data,
// but the o/p slots and nameless-save fallbacks re-derive every boot).
const char* const kAdjGritty[8] = {"fuzzy", "gritty", "dusty", "rusty", "ember", "feral", "raw", "burnt"};
const char* const kAdjDark[8]   = {"dark", "deep", "dusky", "velvet", "shadow", "murky", "umber", "sable"};
const char* const kAdjBright[8] = {"bright", "glassy", "neon", "silver", "sharp", "gleam", "crisp", "lucid"};
const char* const kAdjWarm[8]   = {"warm", "soft", "lush", "mellow", "golden", "honey", "misty", "tender"};
const char* const kFamNouns[(int)Archetype::Count][8] = {
    {"harp", "koto", "lute", "quill", "thorn", "dart", "drop", "twang"},      // pluck
    {"bell", "chime", "glass", "halo", "frost", "prism", "hymn", "star"},     // bell
    {"haze", "bloom", "cloud", "veil", "dream", "dawn", "nebula", "tide"},    // pad
    {"root", "rumble", "depth", "boom", "core", "anchor", "fathom", "loam"},  // bass
    {"acid", "wasp", "fizz", "venom", "spiral", "worm", "zap", "sting"},      // acid
    {"voice", "spark", "comet", "flare", "siren", "arrow", "blade", "ray"},   // lead
    {"horn", "brass", "blast", "swell", "crown", "herald", "bugle", "march"}, // brass
    {"chip", "pixel", "sprite", "coin", "laser", "retro", "glitch", "bit"},   // chip
    {"pulse", "grain", "choir", "river", "cavern", "vapor", "signal", "moss"}, // wild/other
    // Second-wave rows: RESERVED, not yet reachable. classifySound() is frozen
    // (its verdict names sounds that re-derive every boot, so changing it would
    // relabel players' o/p slots) and never returns these values — second-wave
    // rolls deliberately land in the frozen families above (whistle sings ->
    // lead words, wobble carries -> bass words, organ holds -> choir/cavern...).
    // The rows exist so the Count-sized array never holds a null row, and so
    // the words are already pinned (append-only) if a future versioned
    // classifier ever reaches them.
    {"reed", "lark", "gale", "kite", "wisp", "breeze", "aria", "flute"},     // whistle
    {"organ", "abbey", "nave", "pipe", "psalm", "rotor", "chapel", "vesper"},// organ
    {"tine", "keys", "felt", "lounge", "amber", "ivory", "mallet", "suede"}, // keys
    {"wub", "dub", "swamp", "bog", "tremor", "surge", "riddim", "quake"},    // wobble
    {"bow", "rosin", "cello", "sonata", "velour", "arco", "viola", "octet"}, // strings
    // Third-wave rows: reachable ONLY through classifySoundV2 (the versioned
    // classifier), exactly like the second-wave rows above — the frozen
    // classifySound never returns these values, so nothing that re-derives on
    // an older device can ever land here. Append-only, like every bank.
    {"om", "hum", "monk", "aura", "altar", "abyss", "eon", "mantra"},        // drone
    {"gate", "chop", "strobe", "slicer", "tick", "blinds", "morse", "relay"} // gate
};
}  // namespace

uint32_t patchHash(const GenPatch& g) {
    const SynthParams& s = g.synth;
    uint32_t h = 2166136261u;
    h = fnv(h, (uint32_t)s.wave);
    h = fnv(h, (uint32_t)s.glideMode);
    h = fnv(h, (uint32_t)s.filterMode);
    h = fhash(h, s.cutoffHz, 0.05f);
    h = fhash(h, s.resonance, 100.f);
    h = fhash(h, s.attackS, 1000.f);
    h = fhash(h, s.decayS, 1000.f);
    h = fhash(h, s.sustain, 100.f);
    h = fhash(h, s.releaseS, 1000.f);
    h = fhash(h, s.glideS, 1000.f);
    h = fhash(h, s.detuneCents, 10.f);
    h = fhash(h, s.fenvOct, 100.f);
    h = fhash(h, s.subLevel, 100.f);
    h = fhash(h, s.drive, 100.f);
    h = fhash(h, s.chorusDepth, 100.f);
    h = fhash(h, s.delayMix, 100.f);
    h = fhash(h, s.reverbMix, 100.f);
    h = fhash(h, s.lfo1RateHz, 100.f);
    h = fhash(h, s.lfo2RateHz, 100.f);
    for (int i = 0; i < kModSlots; ++i) {
        h = fnv(h, (uint32_t)s.slots[i].src);
        h = fnv(h, (uint32_t)s.slots[i].dest);
        h = fhash(h, s.slots[i].depth, 100.f);
    }
    h = fnv(h, (uint32_t)g.tiltRoute);
    h = fnv(h, (uint32_t)g.tiltRouteB);
    return h ? h : 1u;
}

uint32_t patchHashFull(const GenPatch& g) {
    const SynthParams& s = g.synth;
    // Seed from the frozen name hash (everything it covers stays covered), then
    // fold in every persisted field it deliberately omits. masterVol and the
    // live-mod fields (bend/vibrato/cutoffMod/volMod/tempo/tilt values) stay
    // out: they're the player's moment, not the sound.
    uint32_t h = patchHash(g);
    h = fnv(h, (uint32_t)s.voiceCount);
    h = fhash(h, s.fenvAtkS, 1000.f);
    h = fhash(h, s.fenvDecS, 1000.f);
    h = fhash(h, s.noiseLevel, 100.f);
    h = fhash(h, s.autoVibCents, 10.f);
    h = fhash(h, s.driftCents, 10.f);
    h = fhash(h, s.delayTimeS, 1000.f);
    h = fhash(h, s.delayFb, 100.f);
    h = fnv(h, (uint32_t)s.delaySync);
    h = fhash(h, s.reverbSize, 100.f);
    h = fnv(h, (uint32_t)s.lfo1Shape);
    h = fnv(h, (uint32_t)s.lfo1Sync);
    h = fnv(h, (uint32_t)s.lfo2Shape);
    h = fnv(h, (uint32_t)s.lfo2Sync);
    h = fhash(h, s.modEnvAtkS, 1000.f);
    h = fhash(h, s.modEnvDecS, 1000.f);
    h = fhash(h, g.tiltDepth, 100.f);
    h = fhash(h, g.tiltDepthB, 100.f);
    return h ? h : 1u;
}

void nameForSeed(uint32_t seed, char* out, int cap) {
    if (cap <= 0) return;
    // Pull the word choices and the hex tag from DIFFERENT bit ranges, and use a
    // full 16-bit tag (four hex digits). That keeps the name near-collision-free
    // — different sounds get different names, so a Save-to-SD never silently
    // clobbers a different sound — while staying idempotent (same sound -> same
    // hash -> same name, so re-saving just overwrites itself).
    const char* adj = kAdjs[(seed >> 16) & 15];
    const char* noun = kNouns[(seed >> 20) & 15];
    const char hexd[] = "0123456789abcdef";
    const char tag[5] = {hexd[(seed >> 12) & 15], hexd[(seed >> 8) & 15],
                         hexd[(seed >> 4) & 15], hexd[seed & 15], '\0'};
    // assemble "adj-noun-xxxx" by hand (no snprintf — keep dsp/ free of <cstdio>)
    int n = 0;
    auto put = [&](const char* s) { for (; *s && n < cap - 1; ++s) out[n++] = *s; };
    put(adj); put("-"); put(noun); put("-"); put(tag);
    out[n] = '\0';
}

void shortNameForSeed(uint32_t seed, char* out, int cap) {
    if (cap <= 0) return;
    const char* noun = kNouns[(seed >> 20) & 15];  // same noun nameForSeed uses
    int n = 0;
    for (; noun[n] && n < cap - 1; ++n) out[n] = noun[n];
    out[n] = '\0';
}

void soundName(uint32_t seed, char* out, int cap) {
    if (cap <= 0) return;
    // adjective-noun, no hex tag — the same words nameForSeed() picks, just
    // without the four-digit suffix. The hex was collision-insurance for SD
    // filenames; that job now belongs to rename + a save-time suffix, leaving
    // the user-facing name clean and identical wherever a sound appears.
    const char* adj = kAdjs[(seed >> 16) & 15];
    const char* noun = kNouns[(seed >> 20) & 15];
    int n = 0;
    auto put = [&](const char* s) { for (; *s && n < cap - 1; ++s) out[n++] = *s; };
    put(adj); put("-"); put(noun);
    out[n] = '\0';
}

Archetype classifySound(const SynthParams& s) {
    // Order is the heuristic: the strongest audible identities claim a patch
    // first. Percussive (sustain gone) beats everything; a pure-wave long ring
    // is a bell, any other stab a pluck. Then the squelch (screaming reso +
    // deep filter env), the swell (slow attack), the brass bloom (filter
    // attack under a held tone), the weight (sub + low cutoff), the chip
    // (bright raw square, snappy), the singer (built-in vibrato). Anything
    // else is character-neutral and gets the generic word bank.
    const bool pure = (s.wave == Waveform::Sine || s.wave == Waveform::Triangle);
    if (s.sustain < 0.25f)
        return (pure && s.decayS >= 0.45f) ? Archetype::Bell : Archetype::Pluck;
    if (s.resonance >= 0.55f && s.fenvOct >= 1.8f) return Archetype::Acid;
    if (s.attackS >= 0.18f) return Archetype::Pad;
    if (s.fenvAtkS >= 0.018f && s.sustain >= 0.6f) return Archetype::Brass;
    if (s.subLevel >= 0.35f && s.cutoffHz <= 1600.f) return Archetype::Bass;
    if ((s.wave == Waveform::Square || s.wave == Waveform::Pulse) &&
        s.cutoffHz >= 3000.f && s.attackS <= 0.01f && s.releaseS <= 0.25f)
        return Archetype::Chip;
    if (s.autoVibCents >= 2.f) return Archetype::Lead;
    return Archetype::Wild;
}

void soundNameForPatch(const GenPatch& g, char* out, int cap) {
    if (cap <= 0) return;
    const SynthParams& s = g.synth;
    const uint32_t h = patchHash(g);
    // adjective: texture trumps (grit is what you notice first), then
    // brightness picks between dark / bright / warm banks
    const char* const* adjs;
    if (s.drive >= 3.f || s.noiseLevel >= 0.06f) adjs = kAdjGritty;
    else if (s.cutoffHz < 900.f)                 adjs = kAdjDark;
    else if (s.cutoffHz > 4000.f)                adjs = kAdjBright;
    else                                         adjs = kAdjWarm;
    const char* const* nouns = kFamNouns[(int)classifySound(s)];
    const char* adj = adjs[(h >> 16) & 7];
    const char* noun = nouns[(h >> 20) & 7];
    int n = 0;
    auto put = [&](const char* p) { for (; *p && n < cap - 1; ++p) out[n++] = *p; };
    put(adj); put("-"); put(noun);
    out[n] = '\0';
}

Archetype classifySoundV2(const SynthParams& s) {
    // The families the frozen classifier cannot see, strongest identity first.
    // Order is load-bearing: wobble (synced CUTOFF chop over sub) is checked
    // before gate so a spiced wobble that also gained an amp routing keeps its
    // wobble words, and everything third/second-wave is checked before the
    // frozen fallback so a drone doesn't dissolve into "pad".
    const bool pure = (s.wave == Waveform::Sine || s.wave == Waveform::Triangle);
    bool syncedCutoff = false, syncedAmpChop = false, ampMove = false;
    for (int i = 0; i < kModSlots; ++i) {
        const ModSlot& m = s.slots[i];
        if (m.src != (uint8_t)ModSource::LFO1 && m.src != (uint8_t)ModSource::LFO2)
            continue;
        const bool synced = (m.src == (uint8_t)ModSource::LFO1 ? s.lfo1Sync : s.lfo2Sync) != 0;
        const float mag = m.depth < 0.f ? -m.depth : m.depth;
        if (m.dest == (uint8_t)ModDest::Cutoff && synced && mag >= 0.22f) syncedCutoff = true;
        if (m.dest == (uint8_t)ModDest::Amp) {
            ampMove = true;
            if (synced && mag >= 0.35f) syncedAmpChop = true;
        }
    }
    if (syncedCutoff && s.subLevel >= 0.4f) return Archetype::Wobble;
    if (syncedAmpChop && s.sustain >= 0.6f) return Archetype::Gate;
    if (s.attackS <= 0.01f && s.sustain >= 0.85f && s.fenvOct < 0.05f &&
        s.subLevel >= 0.35f && ampMove)
        return Archetype::Organ;
    if (pure && s.sustain >= 0.7f && s.noiseLevel >= 0.02f && s.autoVibCents >= 3.5f)
        return Archetype::Whistle;
    if (s.attackS >= 0.28f && s.sustain >= 0.85f && s.releaseS >= 0.9f &&
        s.subLevel >= 0.38f)
        return Archetype::Drone;
    if (s.attackS >= 0.09f && s.attackS <= 0.27f && s.autoVibCents >= 2.f &&
        s.chorusDepth >= 0.4f)
        return Archetype::Strings;
    if (s.attackS <= 0.01f && s.sustain >= 0.25f && s.sustain <= 0.5f &&
        s.decayS >= 0.45f && s.fenvOct >= 0.4f)
        return Archetype::Keys;
    return classifySound(s);
}

void soundNameForPatchV2(const GenPatch& g, char* out, int cap) {
    if (cap <= 0) return;
    const SynthParams& s = g.synth;
    const uint32_t h = patchHash(g);
    // same adjective logic as the frozen namer — only the noun's FAMILY moves
    const char* const* adjs;
    if (s.drive >= 3.f || s.noiseLevel >= 0.06f) adjs = kAdjGritty;
    else if (s.cutoffHz < 900.f)                 adjs = kAdjDark;
    else if (s.cutoffHz > 4000.f)                adjs = kAdjBright;
    else                                         adjs = kAdjWarm;
    const char* const* nouns = kFamNouns[(int)classifySoundV2(s)];
    const char* adj = adjs[(h >> 16) & 7];
    const char* noun = nouns[(h >> 20) & 7];
    int n = 0;
    auto put = [&](const char* p) { for (; *p && n < cap - 1; ++p) out[n++] = *p; };
    put(adj); put("-"); put(noun);
    out[n] = '\0';
}

}  // namespace dsp
