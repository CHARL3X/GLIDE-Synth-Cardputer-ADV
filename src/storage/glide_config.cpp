#include "glide_config.h"

#include <Preferences.h>

#include "../config.h"
#include "../dsp/scales.h"

namespace store {

namespace {
GlideConfig gCfg;
Preferences gPrefs;
bool gDirty = false;
uint32_t gDirtySince = 0;

template <typename T>
T clampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

GlideConfig& get() {
    return gCfg;
}

void begin() {
    gPrefs.begin(cfg::kNvsNamespace, false);
    GlideConfig d;  // defaults

    auto& s = gCfg.synth;
    s.wave = (dsp::Waveform)clampT<int>(gPrefs.getUChar("wave", (uint8_t)d.synth.wave), 0,
                                        (int)dsp::Waveform::Count - 1);
    s.glideMode = (dsp::GlideMode)clampT<int>(
        gPrefs.getUChar("gmode", (uint8_t)d.synth.glideMode), 0, (int)dsp::GlideMode::Count - 1);
    s.attackS  = clampT<int>(gPrefs.getInt("atk", (int)(d.synth.attackS * 1000)), 0, 2000) / 1000.f;
    s.decayS   = clampT<int>(gPrefs.getInt("dec", (int)(d.synth.decayS * 1000)), 1, 2000) / 1000.f;
    s.sustain  = clampT<int>(gPrefs.getInt("sus", (int)(d.synth.sustain * 100)), 0, 100) / 100.f;
    s.releaseS = clampT<int>(gPrefs.getInt("rel", (int)(d.synth.releaseS * 1000)), 1, 3000) / 1000.f;
    s.glideS   = clampT<int>(gPrefs.getInt("glide", (int)(d.synth.glideS * 1000)), 0, 2000) / 1000.f;
    s.cutoffHz = (float)clampT<int>(gPrefs.getInt("cut", (int)d.synth.cutoffHz), 80, 12000);
    s.resonance = clampT<int>(gPrefs.getInt("res", (int)(d.synth.resonance * 100)), 0, 95) / 100.f;
    s.masterVol = clampT<int>(gPrefs.getInt("vol", (int)(d.synth.masterVol * 100)), 0, 100) / 100.f;
    s.detuneCents = (float)clampT<int>(gPrefs.getInt("det", (int)d.synth.detuneCents), 0, 50);
    s.voiceCount = clampT<int>(gPrefs.getUChar("voices", d.synth.voiceCount), 1, dsp::kMaxVoices);

    auto& l = gCfg.layout;
    l.rootSemis = clampT<int>(gPrefs.getUChar("root", d.layout.rootSemis), 0, 11);
    l.scaleIdx = clampT<int>(gPrefs.getUChar("scale", d.layout.scaleIdx), 0, dsp::kScaleCount - 1);
    l.octave = clampT<int>(gPrefs.getChar("oct", d.layout.octave), 1, 7);
    l.rowIntervalSemis = clampT<int>(gPrefs.getUChar("rowint", d.layout.rowIntervalSemis), 1, 12);
    l.scaleLock = gPrefs.getBool("lock", d.layout.scaleLock);

    gCfg.stringMode = gPrefs.getBool("strmode", d.stringMode);
    gCfg.octaveGlide = gPrefs.getBool("octgl", d.octaveGlide);
    gCfg.tiltRoute = (TiltRoute)clampT<int>(gPrefs.getUChar("tiltrt", (uint8_t)d.tiltRoute), 0,
                                            (int)TiltRoute::Count - 1);
    gCfg.tiltOn = gPrefs.getBool("tilton", d.tiltOn);
    gCfg.bendMs = clampT<int>(gPrefs.getUShort("bendms", d.bendMs), 50, 1000);
    gCfg.bendRange = clampT<int>(gPrefs.getUChar("bendrg", d.bendRange), 1, 12);
    gCfg.bootSound = gPrefs.getBool("boot", d.bootSound);
    gCfg.seenIntro = gPrefs.getBool("intro", d.seenIntro);
}

void persistNow() {
    const auto& s = gCfg.synth;
    gPrefs.putUChar("wave", (uint8_t)s.wave);
    gPrefs.putUChar("gmode", (uint8_t)s.glideMode);
    gPrefs.putInt("atk", (int)(s.attackS * 1000));
    gPrefs.putInt("dec", (int)(s.decayS * 1000));
    gPrefs.putInt("sus", (int)(s.sustain * 100));
    gPrefs.putInt("rel", (int)(s.releaseS * 1000));
    gPrefs.putInt("glide", (int)(s.glideS * 1000));
    gPrefs.putInt("cut", (int)s.cutoffHz);
    gPrefs.putInt("res", (int)(s.resonance * 100));
    gPrefs.putInt("vol", (int)(s.masterVol * 100));
    gPrefs.putInt("det", (int)s.detuneCents);
    gPrefs.putUChar("voices", s.voiceCount);

    const auto& l = gCfg.layout;
    gPrefs.putUChar("root", l.rootSemis);
    gPrefs.putUChar("scale", l.scaleIdx);
    gPrefs.putChar("oct", l.octave);
    gPrefs.putUChar("rowint", l.rowIntervalSemis);
    gPrefs.putBool("lock", l.scaleLock);

    gPrefs.putBool("strmode", gCfg.stringMode);
    gPrefs.putBool("octgl", gCfg.octaveGlide);
    gPrefs.putUChar("tiltrt", (uint8_t)gCfg.tiltRoute);
    gPrefs.putBool("tilton", gCfg.tiltOn);
    gPrefs.putUShort("bendms", gCfg.bendMs);
    gPrefs.putUChar("bendrg", gCfg.bendRange);
    gPrefs.putBool("boot", gCfg.bootSound);
    gPrefs.putBool("intro", gCfg.seenIntro);
    gDirty = false;
}

void markDirty() {
    gDirty = true;
    gDirtySince = millis();
}

void tick(uint32_t nowMs) {
    if (gDirty && nowMs - gDirtySince >= cfg::kPersistDebounceMs) persistNow();
}

void resetDefaults() {
    const bool seen = gCfg.seenIntro;  // don't re-show the intro on reset
    gCfg = GlideConfig();
    gCfg.seenIntro = seen;
    persistNow();
}

}  // namespace store
