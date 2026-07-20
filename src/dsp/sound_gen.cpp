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
    for (int i = 0; i < kModSlots; ++i) {
        ModSlot& m = s.slots[i];
        if (m.src == (uint8_t)ModSource::None) continue;
        if (m.dest == (uint8_t)ModDest::Pitch) m.depth = clampT(m.depth, -0.08f, 0.08f);
        if (m.dest == (uint8_t)ModDest::Amp)   m.depth = clampT(m.depth, -0.60f, 0.60f);
    }
    g.tiltDepth  = clampT(g.tiltDepth, 0.f, 1.f);
    g.tiltDepthB = clampT(g.tiltDepthB, 0.f, 1.f);
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

GenPatch generateSound(uint32_t seed) {
    return generateSound(seed, archetypeForSeed(seed));
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
            s.glideS = uni(r, 0.02f, 0.12f);
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
            s.glideS = uni(r, 0.03f, 0.15f);  // the bent-bell slide
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
            s.glideS = uni(r, 0.1f, 0.35f);
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
            s.glideS = uni(r, 0.03f, 0.12f);
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
            s.glideS = uni(r, 0.06f, 0.18f);  // the 303 slide
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
            s.glideS = uni(r, 0.05f, 0.25f);
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
            s.glideS = uni(r, 0.04f, 0.15f);
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
            s.glideS = uni(r, 0.02f, 0.08f);
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
        case Archetype::Wild:
        default: {  // anything-goes chaos across the FULL Range table — the
                    // old spirit, kept in the pool; sanitize reels in the trash
            s.wave = (Waveform)r.i(0, (int)Waveform::Count - 1);
            s.glideS = uni(r, kGlide.lo, kGlide.hi);
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

    sanitizePatch(g);
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

const char* const kAdjs[] = {
    "warm", "bright", "dark", "glassy", "fuzzy", "lush", "hollow", "sharp",
    "deep", "soft", "neon", "dusty", "velvet", "frost", "ember", "tidal",
};
const char* const kNouns[] = {
    "haze", "comet", "drift", "pulse", "bloom", "grain", "choir", "river",
    "spark", "cavern", "prism", "vapor", "signal", "tide", "husk", "moss",
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

}  // namespace dsp
