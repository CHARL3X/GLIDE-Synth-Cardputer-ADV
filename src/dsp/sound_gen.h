// GLIDE generative sound engine — the heart of "your instrument is yours."
//
// Rolls a complete, musically-bounded patch from a seed, or mutates an existing
// one to explore its neighbourhood. Seeded and DETERMINISTIC: the same seed
// always yields the same patch. Two payoffs fall out of that:
//   - a per-device seed gives every unit a unique-but-reproducible starting
//     bank (no two players' instruments sound alike out of the box), and
//   - the host tests can pin the behaviour exactly (env:native).
//
// PURE C++: no Arduino, no M5, no millis(), no global RNG state. All randomness
// flows from the `seed` argument through a local LCG. This lives under the
// dsp/ porting boundary and must keep compiling in env:native — so the soul of
// the instrument (its sound *generator*) ports to future hardware unchanged,
// exactly like the synth voice does.
#pragma once
#include <cstdint>

#include "params.h"

namespace dsp {

// A complete generated patch: the synth voice plus its tilt "personality."
// The four tilt fields live OUTSIDE SynthParams (they're config, not synth
// params) — this struct mirrors the shape of store::PatchData so storage can
// map a GenPatch across the dsp/storage boundary without dsp/ ever depending
// on storage/. (dsp/ defines TiltRoute, so it's free to suggest one here.)
struct GenPatch {
    SynthParams synth;
    uint8_t tiltRoute  = (uint8_t)TiltRoute::Vibrato;
    float   tiltDepth  = 0.55f;
    uint8_t tiltRouteB = (uint8_t)TiltRoute::Off;
    float   tiltDepthB = 0.60f;
};

// The character a roll commits to BEFORE painting parameters. This is what
// makes two rolls sound like two different instruments instead of two shades
// of the same one: independent uniform draws regress every patch to the
// statistical middle ("a buzzy wave with some reverb"), so the generator first
// picks a personality, then paints correlated values inside that personality's
// window — plucks that actually stop, pads that actually swell, acid that
// actually squelches. Wild keeps the old anything-goes chaos in the pool.
// Append-only (a future "roll style" picker may persist it): add before Count.
enum class Archetype : uint8_t { Pluck, Bell, Pad, Bass, Acid, Lead, Brass, Chip, Wild, Count };

inline const char* archetypeName(Archetype a) {
    switch (a) {
        case Archetype::Pluck: return "pluck";
        case Archetype::Bell:  return "bell";
        case Archetype::Pad:   return "pad";
        case Archetype::Bass:  return "bass";
        case Archetype::Acid:  return "acid";
        case Archetype::Lead:  return "lead";
        case Archetype::Brass: return "brass";
        case Archetype::Chip:  return "chip";
        case Archetype::Wild:  return "wild";
        default:               return "?";
    }
}

// The archetype a bare seed rolls — a weighted pick (pads and plucks common,
// brass/chip/wild the spice). Deterministic and separate from the parameter
// paint, so generateSound(seed) == generateSound(seed, archetypeForSeed(seed)).
Archetype archetypeForSeed(uint32_t seed);

// Roll a brand-new patch from `seed`. Deterministic: same seed -> same patch.
// Every field lands inside the engine's musical bounds (see the clamps in the
// .cpp), so a roll is always playable — never a dead or blown-out sound. The
// player's master volume is NOT touched here (the caller keeps it).
GenPatch generateSound(uint32_t seed);

// Same, but with the character chosen by the caller — the hook for a future
// "roll me a pad" style picker. Deterministic in (seed, a).
GenPatch generateSound(uint32_t seed, Archetype a);

// FROZEN: the pre-archetype generator, kept verbatim so devices whose two
// generative slots (o,p) were rolled by it keep those exact sounds across this
// update — storage gates on a genver flag and only moves a device to the new
// generator when the player re-rolls the bank (their choice, never an update's).
// Never edit this function; the native tests pin its output with golden values.
GenPatch generateSoundLegacy(uint32_t seed);

// Evolve `base` by `amount` in [0,1]: small = a subtle variation that keeps the
// character (find the neighbour you almost had), large = a bold leap. Continuous
// params nudge by a bounded delta scaled to their range; categorical params
// (waveform, filter mode, LFO shapes, mod routings, tilt) flip with a
// probability that rises with `amount`. `seed` makes the mutation reproducible.
// amount == 0 returns `base` unchanged.
GenPatch mutateSound(const GenPatch& base, float amount, uint32_t seed);

// A stable hash of a patch's audible character. Deterministic and padding-safe
// (hashes named fields, not raw bytes): the same sound always hashes the same.
// Used to name a patch from its own contents. FROZEN: its field coverage must
// never change — every custom slot's displayed name derives from it, so a
// coverage change would rename players' saved sounds after an update. New
// fields go in patchHashFull() below instead.
uint32_t patchHash(const GenPatch& g);

// The COMPREHENSIVE hash: every field a patch persists (the codec table),
// except the player's master volume and the live-mod fields. This is the hash
// for unsaved-edit detection (store::liveDirty) and same-sound checks, where a
// missed field silently loses a player's work — patchHash() deliberately skips
// some fields for name stability, so it must never be used for those jobs.
// When adding a SynthParams field, add it HERE (test_dsp enforces coverage
// field-by-field) and leave patchHash() alone.
uint32_t patchHashFull(const GenPatch& g);

// Build an evocative, deterministic name from `seed`, e.g. "warm-haze-3f2a"
// (adjective-noun-hex). Always null-terminated within `cap`. Pure, so the same
// sound — fed patchHash(g) — always names itself the same way. This is how a
// generated sound becomes "yours" rather than "patch-07". Used for SD filenames
// and the library browser, where there's room for the full name.
void nameForSeed(uint32_t seed, char* out, int cap);

// A COMPACT label from the same seed — one word (the noun, e.g. "haze"), ≤6
// chars — for the cramped status-bar slot spot, where a full name wouldn't fit
// beside the scale/octave readouts. Shares the noun with nameForSeed(), so a
// slot labelled "haze" and its SD file "warm-haze-3f2a" read as the same sound.
void shortNameForSeed(uint32_t seed, char* out, int cap);

// THE canonical auto-name: adjective-noun, e.g. "warm-haze" — no hex tag. This
// is the ONE name a sound shows everywhere (slot, status bar, SD library, the
// Save default), so what you see is always what you save. Uniqueness across the
// library is handled by rename + a save-time collision suffix, not a hex tag.
// Same words as nameForSeed()/shortNameForSeed(); deterministic from the seed.
void soundName(uint32_t seed, char* out, int cap);

}  // namespace dsp
