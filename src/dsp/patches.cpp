// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "patches.h"

#include <cstring>

namespace dsp {

namespace {

void setName(Patch& p, const char* n) {
    strncpy(p.name, n, sizeof p.name - 1);
    p.name[sizeof p.name - 1] = '\0';
}

// The factory bank — a CURATED set the player hand-picked, not random sounds:
//   q  GLIDE      the signature / boot tone (synth brass on the plain saw)
//   w  ACID       the resonant 303 squelch (kept verbatim — the player's favourite)
//   e  Organ      the bed: drawbar organ, leslie, sits UNDER a solo (demo bed)
//   r  Taser      the lead: open saw + sub, key-track darkening, tilt-swelled echo
//   t  Crisp Horn a bright reed horn that sings its own vibrato
//   y  Fat Square punchy square with a bright per-note filter bloom
//   u  Hollow     driven square through a notch filter, phasey
//   i  Big        highpass square ringing at the corner — hollow and enormous
//   o  (generative)  the two per-device "roll" slots — see kFirstGenSlot. The
//   p  (generative)  BRASS/GLASS definitions below are just a fallback; these
//                    slots are regenerated from the unit's seed at runtime.
// Every preset value here came straight off the player's card via the patch
// codec, so the in-box bank is exactly what they heard when they saved them.
void buildBank(Patch* P) {
    // q — GLIDE: the signature, AND the literal power-on sound. Synth brass in
    // the Jupiter/OB lineage, built on the plain saw the instrument is named
    // for: the filter ATTACKS into each note (78 ms over two octaves) so the
    // tone swells into its own blat instead of arriving flat, the clipper gives
    // it a chest, and a hair of auto-vibrato keeps it from sitting still.
    // Chorus + hall widen it into a section; the mod envelope pushes drive on
    // top, so the swell gets dirtier as it opens. The old raw-saw GLIDE with a
    // body — same wave, same immediacy, more instrument.
    //
    // This slot is what the device boots into. On a FRESH unit storage::begin()
    // seeds the live sound FROM this slot (the freshDevice path in
    // glide_config.cpp) instead of from the bare SynthParams defaults, so the
    // first sound, the factory-reset sound and fn+q stay one sound.
    // NOTE: the engine defaults themselves must NOT be moved to match. The
    // frozen generators start every roll at a default-constructed SynthParams
    // and paint only the fields their archetype owns, so shifting the defaults
    // would silently re-voice o/p on every unit already in the field.
    {
        Patch& p = P[0];
        setName(p, "GLIDE");
        auto& s = p.synth;
        s.attackS = 0.0457f;
        s.decayS = 0.3985f;
        s.sustain = 0.949f;
        s.releaseS = 0.3576f;
        s.glideS = 0.0384f;
        s.cutoffHz = 2224.0f;
        s.resonance = 0.256f;
        s.fenvAtkS = 0.0784f;          // the brass swell: filter rises INTO the note
        s.fenvDecS = 0.4117f;
        s.fenvOct = 2.097f;
        s.drive = 2.885f;
        s.autoVibCents = 3.246f;
        s.chorusDepth = 0.201f;
        s.reverbMix = 0.284f;
        s.reverbSize = 0.639f;
        s.lfo1RateHz = 2.896f;
        s.lfo1Shape = 2;               // saw
        s.lfo2RateHz = 2.114f;
        s.lfo2Shape = 4;               // s&h
        s.modEnvAtkS = 0.0028f;
        s.modEnvDecS = 0.2461f;
        s.slots[0] = ModSlot::make(ModSource::ModEnv, ModDest::Drive, 0.2106f);
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 1.f;
        p.tiltRouteB = TiltRoute::Vibrato;
        p.tiltDepthB = 0.6f;
    }
    // The lush "analog poly" remake of GLIDE that used to live in slot 0 — a
    // wide three-saw stack with a slow per-note filter bloom, tube warmth, deep
    // chorus and a real room. Genuinely nice, just never the sound the player
    // reached for. Parked here verbatim as a ready-made candidate for
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
    // e — Organ: a warm drawbar organ, and the bank's BED. A pulse wave with a
    // square sub under it for the 16' rank, filtered down to 1.1 kHz so it sits
    // BENEATH a solo instead of fighting it, and near-full sustain (0.99) so a
    // held chord simply does not decay. LFO1 is the leslie — routed to amp for
    // the tremolo plus a touch to cutoff for the shimmer that comes with it.
    // The demo plays its progression on this slot (demo.cpp kBedSlot). (SD preset.)
    {
        Patch& p = P[2];
        setName(p, "Organ");
        auto& s = p.synth;
        s.wave = Waveform::Pulse;
        s.attackS = 0.0027f;
        s.decayS = 0.1272f;
        s.sustain = 0.993f;            // drawbar: held is held
        s.releaseS = 0.0566f;
        s.glideS = 0.0224f;
        s.cutoffHz = 1140.4f;
        s.resonance = 0.157f;
        s.subLevel = 0.447f;           // the 16' rank
        s.drive = 1.764f;
        s.reverbMix = 0.400f;
        s.reverbSize = 0.683f;
        s.lfo1RateHz = 5.126f;
        s.lfo2RateHz = 3.051f;
        s.lfo2Shape = 2;               // saw
        s.modEnvAtkS = 0.0723f;
        s.modEnvDecS = 0.2086f;
        s.slots[0] = ModSlot::make(ModSource::LFO1, ModDest::Amp, 0.1959f);  // leslie
        s.slots[1] = ModSlot::make(ModSource::LFO1, ModDest::Cutoff, 0.0641f);
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 1.f;
        p.tiltRouteB = TiltRoute::Vibrato;
        p.tiltDepthB = 0.6f;
    }
    // r — Taser: the bank's lead. A wide-open saw (5.8 kHz, almost no
    // resonance) with a sub under it for weight, driven but nearly dry — its
    // space comes from a long 0.88 tail and a 1/8-triplet echo rather than a
    // wash. Two routings make it: key-track NEGATIVE on cutoff, so it gets
    // DARKER as you play up the neck instead of turning shrill, and tilt-roll
    // on the delay, so leaning swells the repeats. Tilt is vibrato. (SD preset.)
    // (detuneCents rides along from the save but is inert here — detune only
    // applies to FatSaw; see voice.cpp.)
    {
        Patch& p = P[3];
        setName(p, "Taser");
        auto& s = p.synth;
        s.attackS = 0.0808f;
        s.decayS = 0.2855f;
        s.sustain = 0.431f;
        s.releaseS = 0.1471f;
        s.glideS = 0.0130f;
        s.cutoffHz = 5780.7f;
        s.resonance = 0.050f;
        s.detuneCents = 12.87f;
        s.fenvDecS = 0.4462f;
        s.subLevel = 0.374f;
        s.drive = 2.276f;
        s.chorusDepth = 0.079f;
        s.delayMix = 0.061f;
        s.delayFb = 0.533f;
        s.delaySync = 4;               // 1/8 triplet, locked to the jam tempo
        s.reverbMix = 0.100f;
        s.reverbSize = 0.880f;
        s.lfo1RateHz = 0.395f;
        s.lfo1Shape = 1;               // tri
        s.lfo2RateHz = 0.311f;
        s.lfo2Shape = 4;               // s&h
        s.modEnvAtkS = 0.1160f;
        s.modEnvDecS = 0.6910f;
        s.slots[0] = ModSlot::make(ModSource::KeyTrack, ModDest::Cutoff, -0.3593f);
        s.slots[1] = ModSlot::make(ModSource::TiltB, ModDest::Delay, 0.4445f);
        s.slots[2] = ModSlot::make(ModSource::LFO1, ModDest::Amp, -0.0362f);
        p.tiltRoute = TiltRoute::Vibrato;
        p.tiltDepth = 0.616f;
        p.tiltRouteB = TiltRoute::Cutoff;
        p.tiltDepthB = 0.697f;
    }
    // t — Crisp Horn: a bright reed horn. A pulse wave held wide open at 6 kHz
    // with the resonance up at 0.5 for the reedy peak, a short filter-env bite
    // on the attack, and a deep 7-cent auto-vibrato that IS the character — it
    // sings on its own without you leaning. A free-time echo at 0.29 s sits
    // behind it; the mod envelope opens the cutoff on each note. (SD preset.)
    {
        Patch& p = P[4];
        setName(p, "Crisp Horn");
        auto& s = p.synth;
        s.wave = Waveform::Pulse;
        s.attackS = 0.0460f;
        s.decayS = 0.2241f;
        s.sustain = 0.711f;
        s.releaseS = 0.5338f;
        s.glideS = 0.0916f;
        s.cutoffHz = 6066.3f;
        s.resonance = 0.500f;          // the reedy peak
        s.fenvDecS = 0.1505f;
        s.fenvOct = 0.681f;
        s.drive = 2.622f;
        s.autoVibCents = 7.260f;       // it sings without being leaned
        s.delayMix = 0.392f;
        s.delayTimeS = 0.2926f;
        s.delayFb = 0.242f;
        s.lfo1RateHz = 3.350f;
        s.lfo1Shape = 4;               // s&h
        s.lfo2RateHz = 0.172f;
        s.lfo2Shape = 3;               // sqr
        s.modEnvAtkS = 0.0411f;
        s.modEnvDecS = 0.7247f;
        s.slots[0] = ModSlot::make(ModSource::ModEnv, ModDest::Cutoff, 0.2297f);
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 1.f;
        p.tiltRouteB = TiltRoute::Vibrato;
        p.tiltDepthB = 0.6f;
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
    // i — Big: a HIGHPASS square, the only slot in the bank that isn't lowpass.
    // The corner sits at 261 Hz with the resonance screaming at 0.80, so the
    // filter rings AT the corner while everything below it is gone — hollow and
    // enormous at once, which is where the name comes from. A fast 2.6-octave
    // filter env snaps that ring open on every note, the clipper adds grit, and
    // a quarter-note echo makes it a riff machine. (SD preset, replaced "Drift".)
    {
        Patch& p = P[7];
        setName(p, "Big");
        auto& s = p.synth;
        s.wave = Waveform::Square;
        s.attackS = 0.0035f;
        s.decayS = 0.2972f;
        s.sustain = 0.658f;
        s.releaseS = 0.2279f;
        s.glideS = 0.1017f;
        s.cutoffHz = 261.2f;
        s.resonance = 0.795f;
        s.filterMode = 1;              // highpass: everything under the ring is gone
        s.fenvAtkS = 0.0010f;
        s.fenvDecS = 0.2556f;
        s.fenvOct = 2.569f;
        s.drive = 3.004f;
        s.delayMix = 0.292f;
        s.delayFb = 0.543f;
        s.delaySync = 1;               // 1/4, locked to the jam tempo
        s.reverbMix = 0.197f;
        s.reverbSize = 0.472f;
        s.lfo1RateHz = 0.176f;
        s.lfo1Shape = 4;               // s&h
        s.lfo2RateHz = 0.614f;
        s.modEnvAtkS = 0.0036f;
        s.modEnvDecS = 0.3999f;
        p.tiltRoute = TiltRoute::Cutoff;
        p.tiltDepth = 1.f;
        p.tiltRouteB = TiltRoute::Vibrato;
        p.tiltDepthB = 0.6f;
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
