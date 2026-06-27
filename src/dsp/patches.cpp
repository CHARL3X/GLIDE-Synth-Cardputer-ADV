#include "patches.h"

#include <cstring>

namespace dsp {

namespace {

void setName(Patch& p, const char* n) {
    strncpy(p.name, n, sizeof p.name - 1);
    p.name[sizeof p.name - 1] = '\0';
}

// The factory bank — a CURATED set the player hand-picked, not random sounds:
//   q  GLIDE   the signature / boot tone (the engine default)
//   w  ACID    the resonant 303 squelch (kept verbatim — the player's favourite)
//   e  Bass        |
//   r  Solo        |
//   t  Ethereal    | six presets the player designed with the generative engine
//   y  Fat Square  | and saved to the SD card, baked in here verbatim (decoded
//   u  Fatter sq.  | from the .gpat files) so they ship in the box, card or not
//   i  FattySlider |
//   o  (generative)  the two per-device "roll" slots — see kFirstGenSlot. The
//   p  (generative)  BRASS/GLASS definitions below are just a fallback; these
//                    slots are regenerated from the unit's seed at runtime.
// The six preset values came straight off the card via the patch codec, so the
// in-box bank is exactly what the player heard when they saved them.
void buildBank(Patch* P) {
    // q — GLIDE: the signature, AND the literal power-on sound. It is therefore
    // the engine's default tone, deliberately: a clean, dry saw — 4 kHz cutoff,
    // a touch of resonance, no send FX, no filter bloom, no drive. Raw and
    // immediate, the "original GLIDE tone." Selecting fn+q now lands you on
    // exactly what you booted into (and what a factory reset gives), so the
    // first sound and the boot sound can never diverge again. The trick: state
    // NO synth overrides — a default-constructed Patch already holds the
    // default SynthParams, so this slot == the engine default by construction.
    // Only the tilt personality (sing on lean, brighten on roll) is set.
    {
        Patch& p = P[0];
        setName(p, "GLIDE");
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.55f;
        p.tiltRouteB = TiltRoute::Cutoff;
        p.tiltDepthB = 0.6f;
    }
    // The lush "analog poly" remake of GLIDE that used to live in slot 0 — a
    // wide three-saw stack with a slow per-note filter bloom, tube warmth, deep
    // chorus and a real room. Genuinely nice, just not the raw default the
    // player reaches for. Parked here verbatim as a ready-made candidate for
    // the planned expanded preset bank (drop it into a new slot when the bank
    // grows past ten):
    //   s.wave = Waveform::FatSaw; s.detuneCents = 18.f; s.drive = 1.7f;
    //   s.cutoffHz = 3200.f; s.resonance = 0.14f;
    //   s.fenvOct = 1.2f; s.fenvDecS = 0.5f;
    //   s.attackS = 0.012f; s.decayS = 0.35f; s.sustain = 0.70f; s.releaseS = 0.5f;
    //   s.glideS = 0.12f;
    //   s.chorusDepth = 0.55f; s.delayMix = 0.12f; s.delayFb = 0.26f; s.delaySync = 3;
    //   s.reverbMix = 0.28f; s.reverbSize = 0.62f;
    //   tiltRoute = Vibrato (0.55), tiltRouteB = Cutoff (0.6)
    // w — ACID: resonant squelch; tilt IS the wah. Lean into it. A dub delay
    // with heavy regen and a little room give the 303 line space to breathe
    // between squelches. (Kept verbatim — the player's favourite, moved to w.)
    {
        Patch& p = P[1];
        setName(p, "ACID");
        auto& s = p.synth;
        s.wave = Waveform::Saw;
        s.cutoffHz = 480.f;
        s.resonance = 0.8f;
        s.fenvOct = 3.f;
        s.fenvDecS = 0.18f;
        s.drive = 2.8f;
        s.sustain = 0.6f;
        s.decayS = 0.18f;
        s.releaseS = 0.16f;
        s.glideS = 0.10f;
        s.delayMix = 0.30f;
        s.delayTimeS = 0.28f;
        s.delayFb = 0.42f;
        s.reverbMix = 0.12f;
        s.reverbSize = 0.5f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 1.f;
    }
    // e — Bass: a fat pulse bass — a square sub-octave for weight, pushed into
    // the clipper for growl, with a snappy filter-env pluck. Tilt opens the
    // cutoff (the wah/squelch), roll adds a touch of vibrato. (SD preset.)
    {
        Patch& p = P[2];
        setName(p, "Bass");
        auto& s = p.synth;
        s.wave = Waveform::Pulse;
        s.attackS = 0.0061f;
        s.decayS = 0.1802f;
        s.sustain = 0.745f;
        s.releaseS = 0.6701f;
        s.glideS = 0.0774f;
        s.cutoffHz = 516.8f;
        s.resonance = 0.092f;
        s.detuneCents = 10.49f;
        s.fenvDecS = 0.1566f;
        s.fenvOct = 1.542f;
        s.subLevel = 0.644f;
        s.drive = 3.176f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.842f;
        p.tiltRouteB = TiltRoute::Vibrato;
        p.tiltDepthB = 0.393f;
    }
    // r — Solo: a bright square lead, always-gliding, with a 1/8-triplet delay,
    // a touch of chorus and a singing vibrato tilt — sits over a progression.
    // (SD preset.)
    {
        Patch& p = P[3];
        setName(p, "Solo");
        auto& s = p.synth;
        s.wave = Waveform::Square;
        s.glideMode = GlideMode::Always;
        s.attackS = 0.1437f;
        s.decayS = 0.5243f;
        s.sustain = 0.491f;
        s.releaseS = 0.5278f;
        s.glideS = 0.0209f;
        s.cutoffHz = 3943.9f;
        s.resonance = 0.181f;
        s.detuneCents = 17.24f;
        s.fenvDecS = 0.2571f;
        s.fenvOct = 0.626f;
        s.drive = 3.814f;
        s.chorusDepth = 0.194f;
        s.delayMix = 0.355f;
        s.delayFb = 0.361f;
        s.delaySync = 4;               // 1/8 triplet, locked to the jam tempo
        s.slots[0] = ModSlot::make(ModSource::LFO2, ModDest::Drive, 0.037f);
        s.slots[1] = ModSlot::make(ModSource::LFO2, ModDest::Reverb, -0.020f);
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.474f;
        p.tiltDepthB = 0.368f;
    }
    // t — Ethereal: a soft triangle pad, long always-glide, a roomy hall. Tilt
    // is a deep singing vibrato; roll swells the volume. (SD preset.)
    {
        Patch& p = P[4];
        setName(p, "Ethereal");
        auto& s = p.synth;
        s.wave = Waveform::Triangle;
        s.glideMode = GlideMode::Always;
        s.attackS = 0.0714f;
        s.decayS = 0.3955f;
        s.sustain = 0.788f;
        s.releaseS = 0.2563f;
        s.glideS = 0.0073f;
        s.cutoffHz = 1235.0f;
        s.resonance = 0.235f;
        s.detuneCents = 0.f;
        s.fenvDecS = 0.3814f;
        s.fenvOct = 1.191f;
        s.drive = 1.144f;
        s.reverbMix = 0.320f;
        s.reverbSize = 0.698f;
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.831f;
        p.tiltRouteB = TiltRoute::Volume;
        p.tiltDepthB = 0.579f;
    }
    // y — Fat Square: a punchy square with a bright per-note filter bloom and a
    // faint noise knock on the attack; tilt opens the cutoff. (SD preset — the
    // "fat" one of the family.)
    {
        Patch& p = P[5];
        setName(p, "Fat Square");
        auto& s = p.synth;
        s.wave = Waveform::Square;
        s.attackS = 0.0129f;
        s.decayS = 0.5112f;
        s.sustain = 0.885f;
        s.releaseS = 0.1001f;
        s.glideS = 0.0179f;
        s.cutoffHz = 5020.1f;
        s.resonance = 0.065f;
        s.detuneCents = 0.f;
        s.fenvDecS = 0.4776f;
        s.fenvOct = 1.814f;
        s.subLevel = 0.007f;
        s.noiseLevel = 0.055f;
        s.drive = 1.025f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.839f;
        p.tiltDepthB = 0.739f;
    }
    // u — Hollow: a bright, driven square run through a NOTCH filter — phasey
    // and hollow rather than dark — with a short room. Tilt swells the volume,
    // roll opens the cutoff. (SD preset, formerly "Fatter square".)
    {
        Patch& p = P[6];
        setName(p, "Hollow");
        auto& s = p.synth;
        s.wave = Waveform::Square;
        s.attackS = 0.0358f;
        s.decayS = 0.4702f;
        s.sustain = 0.871f;
        s.releaseS = 0.3639f;
        s.glideS = 0.0086f;
        s.cutoffHz = 6507.5f;
        s.resonance = 0.027f;
        s.filterMode = 3;          // notch: the hollow/phasey character
        s.detuneCents = 0.f;
        s.fenvDecS = 0.1386f;
        s.drive = 2.700f;
        s.reverbMix = 0.245f;
        s.reverbSize = 0.526f;
        p.tiltRoute = TiltRoute::Volume;
        p.tiltDepth = 0.579f;
        p.tiltRouteB = TiltRoute::Cutoff;
        p.tiltDepthB = 0.712f;
    }
    // i — Drift: a lush, always-gliding square — slow swell, deep chorus and a
    // 1/8 delay over a sub-fattened body, with a slow filter bloom. The most
    // "sliding" of the set; tilt opens the cutoff. (SD preset, formerly
    // "FattySlider".)
    {
        Patch& p = P[7];
        setName(p, "Drift");
        auto& s = p.synth;
        s.wave = Waveform::Square;
        s.glideMode = GlideMode::Always;
        s.attackS = 0.2052f;
        s.decayS = 0.3953f;
        s.sustain = 0.743f;
        s.releaseS = 0.3814f;
        s.glideS = 0.0386f;
        s.cutoffHz = 3431.0f;
        s.resonance = 0.403f;
        s.detuneCents = 0.f;
        s.fenvDecS = 0.4549f;
        s.fenvOct = 1.526f;
        s.subLevel = 0.213f;
        s.noiseLevel = 0.034f;
        s.drive = 2.084f;
        s.chorusDepth = 0.596f;
        s.delayMix = 0.335f;
        s.delayFb = 0.556f;
        s.delaySync = 3;               // 1/8, locked to the jam tempo
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.836f;
        p.tiltDepthB = 0.719f;
    }
    // o — generative slot (see kFirstGenSlot): regenerated from the unit's seed
    // at runtime, so this definition is only a fallback if generation is ever
    // disabled. Kept as BRASS — a bold synth-brass section (Jupiter/OB lineage):
    // detuned saws with a slower FILTER attack so each note swells into the
    // "blat," pushed with drive for body; chorus + hall widen the section.
    {
        Patch& p = P[8];
        setName(p, "BRASS");
        auto& s = p.synth;
        s.wave = Waveform::FatSaw;
        s.detuneCents = 10.f;      // a section, still in tune (16 was too wide)
        s.drive = 2.0f;
        s.cutoffHz = 1500.f;
        s.resonance = 0.18f;
        s.fenvAtkS = 0.035f;       // the brass swell: filter rises into the note
        s.fenvOct = 2.3f;
        s.fenvDecS = 0.35f;
        s.attackS = 0.04f;
        s.decayS = 0.3f;
        s.sustain = 0.85f;
        s.releaseS = 0.25f;
        s.autoVibCents = 1.2f;     // a hint of section shimmer
        s.glideS = 0.08f;
        s.chorusDepth = 0.30f;     // widen the section, tastefully
        s.delayMix = 0.10f;
        s.delayTimeS = 0.30f;
        s.delayFb = 0.28f;
        s.reverbMix = 0.24f;
        s.reverbSize = 0.6f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 0.5f;
        p.tiltRouteB = TiltRoute::Vibrato;  // roll = section shake on the brass
        p.tiltDepthB = 0.4f;
    }
    // p — generative slot (see kFirstGenSlot): regenerated from the unit's seed
    // at runtime; this definition is only a fallback. Kept as GLASS — a
    // crystalline struck bell/mallet: triangle struck hard (sustain 0) with a
    // fast bright ping, then a long shimmering delay + hall tail it rings out
    // into; the slow glide bends the bell on slides.
    {
        Patch& p = P[9];
        setName(p, "GLASS");
        auto& s = p.synth;
        s.wave = Waveform::Triangle;   // pure, bell-like
        s.cutoffHz = 5000.f;
        s.resonance = 0.12f;
        s.fenvAtkS = 0.001f;
        s.fenvOct = 2.2f;              // a bright metallic strike...
        s.fenvDecS = 0.12f;            // ...that pings and is gone
        s.attackS = 0.001f;
        s.decayS = 0.9f;
        s.sustain = 0.f;               // struck, not held
        s.releaseS = 0.7f;             // rings out
        s.glideS = 0.05f;
        s.chorusDepth = 0.3f;          // a touch of shimmer (was a bit much)
        s.delayMix = 0.30f;
        s.delayFb = 0.40f;
        s.delaySync = 2;               // dotted-eighth: the bell shimmer in tempo
        s.reverbMix = 0.42f;
        s.reverbSize = 0.85f;          // long ethereal hall
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.4f;
        p.tiltRouteB = TiltRoute::Cutoff;  // roll = ring the bell brighter
        p.tiltDepthB = 0.5f;
    }
}

}  // namespace

const Patch* factoryPatches() {
    static Patch bank[kPatchCount];
    static bool built = false;
    if (!built) {
        buildBank(bank);
        built = true;
    }
    return bank;
}

}  // namespace dsp
