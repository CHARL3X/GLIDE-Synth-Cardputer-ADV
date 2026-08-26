// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// All user-adjustable state, with NVS persistence. "Nothing hardcoded":
// every parameter the synth or layout consumes lives here, is editable
// on-device, and survives reboot. Writes are debounced so a knob sweep
// doesn't hammer flash.
#pragma once
#include <cstdint>
#include "../dsp/params.h"
#include "../dsp/patches.h"
#include "../dsp/pitch.h"
#include "../dsp/sound_gen.h"
#include "patch_codec.h"  // store::PatchData (the SD/NVS-shared patch unit)

namespace store {

using TiltRoute = dsp::TiltRoute;  // moved into dsp so patches carry it
using dsp::tiltRouteName;

// The G0 top button is an assignable momentary performance macro (the "trigger
// throw"). Like the tilt route, it picks one destination and a depth — but it's
// a global gesture, not a per-patch personality, so it lives in GlideConfig.
// Every action writes only into the per-frame live-mod fields (cutoffModOct,
// bendCents) or a local param copy (drive), never into the saved sound.
enum class TriggerAction : uint8_t { Muffle, Brighten, PitchDive, Drive, Morph, Count };

inline const char* triggerActionName(uint8_t a) {
    switch ((TriggerAction)a) {
        case TriggerAction::Muffle:    return "muffle (filter dn)";
        case TriggerAction::Brighten:  return "brighten (filter up)";
        case TriggerAction::PitchDive: return "pitch dive";
        case TriggerAction::Drive:     return "drive grit";
        case TriggerAction::Morph:     return "synth morph";
        default:                       return "?";
    }
}

// Short tag for the on-scope badge (≤6 chars to sit beside the loop status).
inline const char* triggerActionTag(uint8_t a) {
    switch ((TriggerAction)a) {
        case TriggerAction::Muffle:    return "MUFFLE";
        case TriggerAction::Brighten:  return "BRIGHT";
        case TriggerAction::PitchDive: return "DIVE";
        case TriggerAction::Drive:     return "GRIT";
        case TriggerAction::Morph:     return "MORPH";
        default:                       return "TRIG";
    }
}

struct GlideConfig {
    dsp::SynthParams synth;   // engine params (ADSR, glide, wave, filter...)
    dsp::Layout layout;       // key, scale, octave, row interval, lock

    bool stringMode = true;   // rows are mono "strings" with legato hand-off
                              // (guitar feel); off = free poly allocation
    bool octaveGlide = true;  // octave keys sweep held notes instead of jumping
    // Tilt is NEVER pitch bend. Two simultaneous axes: A = forward/back,
    // B = left/right roll. The stock rig is Morph on f/b at 90% + vibrato on
    // l/r at 60% — and tiltLock (below) makes that map global, so it follows
    // your hands across every sound instead of reloading per patch. The morph
    // axis is deep on purpose: at 60% a lean only ever half-arrived at the
    // other sound, which reads as a wobble rather than a blend.
    TiltRoute tiltRoute = TiltRoute::Vibrato;   // axis A physical route (masked by
                                                // tiltMorphA below in the stock rig)
    float tiltDepth = 0.9f;   // axis A depth, 0..1 (fresh devices only — an
                              // existing unit has "tiltdep" in NVS and keeps it)
    float tiltCenter = 0.23f;  // axis A calibrated "flat" default (~20° of pitch,
                               // in angle units where 1.0 = 90°) — wherever YOU hold it
    TiltRoute tiltRouteB = TiltRoute::Vibrato;  // axis B (roll) route — vibrato l/r
    float tiltDepthB = 0.6f;  // axis B depth, 0..1
    float tiltCenterB = 0.f;  // axis B calibrated "flat"
    bool tiltOn = true;       // tilt expression on by default
    bool tiltDual = true;     // roll axis (B) live by default — the 2D body
    // Tilt->MORPH is a RIG setting, global like the G0 trigger action — not a
    // patch personality. The morph partner is "the sound you were just on"
    // (session state), so a patch can't meaningfully own the mapping; and the
    // player who set it expects it to survive sound switches. While a flag is
    // on it masks that axis's per-patch physical route; patches themselves can
    // never carry the morph route (coerced to Off on load). Morph on f/b by
    // default — the player's most-used gesture.
    bool tiltMorphA = true;
    bool tiltMorphB = false;
    // The whole tilt map (both routes, both depths, dual) is a global rig
    // setting, not a per-sound personality. On (default): a sound switch never
    // reloads tilt, so your Morph-f/b + vibrato-l/r rig follows every patch with
    // zero saving — tilt is a physical control surface tied to your hands, and
    // Morph was already global, so this makes both halves of the gesture behave
    // alike. Off: applyPatchData restores per-patch tilt personality.
    bool tiltLock = true;
    uint8_t currentPatch = 0; // active sound slot (fn+q..p)
    uint8_t jamRows = 1;      // 0=off, 1..2 bottom rows become tap-to-latch
                              // drones (-1 oct): the layering jam — backing
                              // rings underneath while you solo above. On by
                              // default (bottom row) so the backing is ready.
    uint8_t droneVoicing = 2; // 0=single note, 1=+octave, 2=+fifth (power
                              // chord) — one drone key voices a fuller backing
    uint8_t jamMotion = 3;    // 0=sustained, 1=pulse (re-strike together),
                              // 2=arp (re-strike one per beat), 3=progression
                              // (default) — turn jam rows on and you're ready
                              // to tap a chord loop
                              // (tap chords on the jam row — no timing — and
                              // they loop one diatonic chord per bar in tempo,
                              // gliding between changes; you solo on top)
    uint16_t jamBpm = 100;    // jam-motion / progression tempo
    uint8_t jamChordBeats = 4;// progression: beats each chord holds (1 bar)
    uint8_t loopSnap = 2;     // loop-close length snap to the jam clock:
                              // 0=off (raw human length), 1=beat, 2=bar
                              // (default — loop and progression lock out of
                              // the box; see dsp/quantize.h)
    uint16_t bendMs = 250;    // time to reach full bend range
    uint8_t bendRange = 2;    // semitones
    uint8_t scopeMode = 1;    // 0=waveform scope, 1=pitch trail (default — the
                              // glide drawn over time, the instrument's whole
                              // point; watch a slide curve between the notes),
                              // 2=tape, 3=cymatic, 4=string, 5=comb,
                              // 6=harmonograph, 7=interference (the generative
                              // scope modes — append-only, persists in NVS)
    uint8_t themeId = 0;      // ui/theme.cpp palette index (0 = phosphor).
                              // Append-only for the same reason as scopeMode.
    uint8_t idleMode = 2;     // hands-off screen behaviour: 0=off (always full
                              // brightness), 1=dim only, 2=dim then a phosphor
                              // screensaver (default). Timings + levels in config.h.
    bool bootSound = true;
    bool seenIntro = false;

    // ---- G0 trigger macro ---------------------------------------------------
    // Default is SYNTH MORPH, latched: tap G0 to become the previous sound,
    // tap to come back. (The partner survives a reboot, and the boot seeds the
    // GLIDE<->ACID pair when there is none, so this always works out of the
    // box.) Muffle — the original throw — is one menu step away.
    uint8_t triggerAction = (uint8_t)TriggerAction::Morph;
    float   triggerDepth  = 0.70f;  // 0..1 — how hard the action drives
    bool    triggerLatch  = true;   // false = momentary (hold), true = tap-latch

    // ---- synth morph -------------------------------------------------------
    // One time constant for all timbre glides: how long a sound switch takes to
    // arrive, and how fast a G0 morph sweeps. 0 = instant (the old snap).
    uint16_t morphMs = 300;

    // ---- solo/backing split (transient performance state, never persisted) --
    // When you switch sound (or shift octave) over a running jam, the backing
    // freezes onto the sound it was playing so the solo gets its own voice and
    // register. The backing keeps its own oscillator/filter/envelope; the two
    // layers share one reverb/delay "room" (the live patch's FX).
    dsp::SynthParams backingSynth;   // the frozen backing sound (when locked)
    bool backingLocked = false;      // true once the backing is held apart
};

GlideConfig& get();

// The route an axis actually plays: the global morph flag masks the patch's
// physical route (morph is a rig setting — see tiltMorphA/B above).
inline TiltRoute effectiveTiltRoute(bool morphFlag, TiltRoute patchRoute) {
    return morphFlag ? TiltRoute::Morph : patchRoute;
}

void begin();                 // load from NVS (or defaults on first boot)
bool nvsHealthy();            // false if NVS failed to open -> nothing persists
uint32_t bootCount();         // DIAGNOSTIC: boots persisted in NVS (climbs => persist works)
bool writeProbeOk();          // DIAGNOSTIC: did this boot's write+readback succeed?
void markDirty();             // schedule a debounced persist

// ---- demo loan ------------------------------------------------------------
// Demo mode BORROWS the instrument: from demoLoanBegin() every flat-key flush
// and morph-partner write is skipped, so nothing the demo touches (live sound,
// currentPatch, jam knobs, the bed's frozen blend pair) reaches flash. The
// loan ends two ways: a power cycle hands the instrument back exactly as it
// was, or the player's own edit AFTER the demo has yielded (a markDirty while
// not driving) adopts the current state and persistence resumes. Playing over
// the takeover bed never adopts — notes aren't config. RAM-only flags.
void demoLoanBegin();         // demo::start — before its first mutation
void demoLoanYield();         // demo::stop — the demo is no longer driving

// ---- odometer -------------------------------------------------------------
// Lifetime play counters, shown as one quiet read-only row in SYSTEM. Notes =
// player-struck note starts only (lead presses, drone latches, chord-step
// taps); never looper playback, jam re-strikes, or octave sweeps. Seconds =
// hands-on time (accumulates while a struck note is <30 s old). It is a
// record of the instrument's life, not a setting: it survives every reset,
// including the boot-BKSP factory wipe (eraseAllStorage carries it across).
void odoNote();               // count one player-struck note (press sites only)
uint32_t odoNotes();
uint32_t odoSeconds();
// Call each frame; performs the deferred write. allowFlush=false keeps the
// odometer clock running but holds the debounced persistNow(): on a crowded
// shared partition the flush's changed-key appends force flash GC (measured
// 1.5-2 s after a preset switch), so the perform loop passes its quiet-moment
// gate here — flush only with idle hands and no backing being scheduled.
void tick(uint32_t nowMs, bool allowFlush = true);
void persistNow();
void flushMorphPartner();     // write the morph-partner blob if stale. NOT part
                              // of persistNow(): on a crowded shared partition
                              // the blob's erase-and-rewrite forces flash GC
                              // (seconds), so it runs only at quiet moments —
                              // idle hands, settings close, app exit — never in
                              // the debounced flush 500 ms after a sound switch
void resetDefaults();         // restore + persist
void eraseAllStorage();       // LAST RESORT, boot factory-reset only: erase the
                              // whole shared NVS partition (every app's data),
                              // re-init, and rewrite this unit's identity
                              // (seed + genver) so the generative o/p slots
                              // stay the sounds the player knows. This is the
                              // cure for STORAGE FULL when GLIDE's own keys
                              // weren't the (only) hog.
void setTiltLock(bool on);    // flip the global/per-sound tilt map AND rebase the
                              // unsaved-* reference, so toggling the mode can't
                              // leave a stale dirty marker

// ---- sound slots (fn+q..p) ----------------------------------------------
// Each of the 10 slots is a factory patch plus an optional user override
// saved over it (fn+shift+q..p). Overrides are versioned NVS blobs: a
// firmware that changes SynthParams silently falls back to factory.
void applyPatch(int slot);             // load slot -> working sound + tilt
bool savePatch(int slot);              // working sound -> slot override
const char* lastSaveError();           // WHY the last save failed, for the HUD —
                                       // "save failed" with no reason is the same
                                       // sin as a silently dead instrument. Names
                                       // the shared-NVS-full case with real numbers.

// ---- solo/backing split -------------------------------------------------
// Freeze the current sound as the backing (called when the player switches
// sound over a running jam); unlock when the backing is gone. The live mods
// are neutralised in the frozen copy so the bed stays steady.
void lockBacking();
void unlockBacking();
bool backingLocked();
void clearOverride(int slot);          // back to factory
bool patchHasOverride(int slot);
const char* patchName(int slot);       // factory name
const char* liveName();                // the live working sound's name — what the
                                       // status bar shows and Save-to-SD uses
bool liveDirty();                      // true iff the live sound has unsaved edits
                                       // (differs from the current slot's stored
                                       // sound); clears on save/load (status-bar *)
void refreshLiveName();                // recompute liveName() from the current synth
                                       // (for paths that bypass applyPatchData)
bool saveToSlot(int slot, const PatchData& pd);  // write a patch (e.g. an SD-library
                                       // sound) onto a slot, carrying its name;
                                       // does not disturb the live sound

// ---- generative sound: "your instrument is yours" -----------------------
// The bank is curated (q=GLIDE, w=ACID, e..i = the baked SD presets), but the
// last two slots (o,p — slot >= dsp::kFirstGenSlot) are GENERATIVE: filled with
// patches rolled from the unit's stable unique seed, so no two players' o,p
// sound alike. The seed is created once at first boot and persisted. reRollBank()
// resets the bank to stock and rolls fresh randoms for o,p.
uint32_t deviceSeed();                 // this unit's stable unique seed
void reRollBank();                     // reset the bank to the curated presets and
                                       // roll fresh randoms for o,p, then reload
                                       // the current slot live
void applyStoredPatch(const PatchData& pd);   // load an SD-library patch -> live
void applyGenerated(const dsp::GenPatch& g);  // load a rolled/mutated sound ->
                                       // live working sound (keeps master vol;
                                       // not a slot until you save it)

// ---- synth morph source --------------------------------------------------
// Every sound change (slot switch, roll, SD load, undo...) snapshots the
// OUTGOING live sound as the morph source — "the sound you were just on".
// G0's Morph action blends toward it; a switch plays the same blend in
// reverse as its transition.
// PERSISTED (NVS key "msrc", the same tagged codec as a slot): the partner is
// half of what you are playing, and losing it on reboot silently re-paired
// every sound with GLIDE. It is stored as a whole sound rather than a slot
// reference, so a partner that came from a re-roll or an SD file restores
// exactly like one that came from a slot. Boot falls back to the GLIDE<->ACID
// pair when there is nothing stored, so this is never invalid in practice.
const dsp::SynthParams& morphSource();
const char* morphSourceName();
bool morphSourceValid();

// ---- non-destructive live-sound history (RAM only; never persisted) -----
// Roll / Mutate / Init checkpoint the live sound first, so a player can always
// step back to a sound they liked instead of losing it to an eager re-roll.
// This is the "test without trashing" guarantee. Session/performance state,
// like the loop pedal — it does not survive a reboot.
void historyCheckpoint();              // snapshot the current live sound+tilt
bool historyUndo();                    // restore the previous snapshot. false if none
bool historyRedo();                    // re-apply a stepped-back snapshot. false if none
bool historyCanUndo();
bool historyCanRedo();
int  historyUndoDepth();               // how many steps back are available (HUD)

}  // namespace store
