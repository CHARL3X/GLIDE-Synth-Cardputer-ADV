// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// GLIDE — a continuous-pitch polyphonic slide instrument for the
// M5Stack Cardputer ADV.
//
// Boot: M5 init -> config load -> audio engine (or a LOUD failure screen,
// never a silent dead instrument) -> splash with gliding chime -> play.
#include <M5Cardputer.h>

#include "config.h"
#include "dsp/patches.h"
#include "io/audio_engine.h"
#include "io/keys.h"
#include "io/led.h"
#include "io/tilt.h"
#include "storage/glide_config.h"
#include "ui/coach.h"
#include "ui/perform_screen.h"
#include "ui/splash.h"
#include "ui/theme.h"

namespace {

// Hard requirement from the build brief: any audio init failure must surface
// a visible error. The web prototype once shipped a silent dead power button
// and it cost a debugging round trip. Not again.
[[noreturn]] void fatalAudio(const char* reason) {
    auto& d = M5Cardputer.Display;
    d.fillScreen(theme::kBg);
    d.drawRect(2, 2, cfg::kScreenW - 4, cfg::kScreenH - 4, theme::kRed);
    d.setTextDatum(top_left);
    d.setFont(&fonts::Font2);
    d.setTextColor(theme::kRed, theme::kBg);
    d.drawString("AUDIO INIT FAILED", 12, 14);
    d.setFont(&fonts::Font0);
    d.setTextColor(theme::kIdle, theme::kBg);
    d.drawString(reason, 12, 40);
    d.setTextColor(theme::kDim, theme::kBg);
    d.drawString("Is this a Cardputer ADV (ES8311)?", 12, 60);
    d.drawString("Check M5Cardputer/M5Unified versions", 12, 72);
    d.drawString("in platformio.ini, then rebuild.", 12, 84);
    Serial.printf("[glide] AUDIO INIT FAILED: %s\n", reason);
    bool on = true;
    for (;;) {
        d.fillCircle(cfg::kScreenW - 14, 14, 4, on ? theme::kRed : theme::kBg);
        on = !on;
        delay(500);
    }
}

}  // namespace

void setup() {
    auto mcfg = M5.config();
    mcfg.internal_spk = true;
    // Configuration only (verified in M5Unified source): this sets the mic
    // pins and registers the ES8311 record-mode callback but starts NOTHING —
    // and the callback setter is protected, so this flag is the only way in.
    // The codec is half-duplex; the Speaker.end() -> Mic.begin() -> Mic.end()
    // -> Speaker.begin() handoff lives solely in io/listen.cpp (LISTEN).
    mcfg.internal_mic = true;
    mcfg.internal_imu = true;   // optional tilt modulation
    M5Cardputer.begin(mcfg, true);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(cfg::kBrightNormal);
    M5Cardputer.Display.fillScreen(theme::kBg);

    Serial.begin(115200);
    Serial.println("[glide] boot");

    store::begin();
    theme::setTheme(store::get().themeId);  // saved palette styles everything
                                            // from the splash on

    if (!audio::begin()) fatalAudio(audio::lastError());
    audio::setParams(store::get().synth);

    // Storage trouble — say so out loud rather than letting saves silently
    // fail (Hard Rule #3). Three distinct states, worst first: the namespace
    // wouldn't open at all; it opened but even a 4-byte write+readback probe
    // failed (the shared 16K NVS partition — Launcher + every app — is hard
    // full); or the probes pass but a patch-size write doesn't (storagePinched:
    // NVS keeps a whole page back for GC, so the ~400 B slot saves are the
    // FIRST thing to die, long before small settings writes — measured at
    // "used 365/504", which looks like 139 free and is really ~13). That third
    // state used to be invisible here, so fn+shift just failed with no boot
    // warning ever naming the fix. All non-fatal.
    if (!store::nvsHealthy() || !store::writeProbeOk() || store::storagePinched()) {
        const bool open = store::nvsHealthy();
        const bool pinched = open && store::writeProbeOk();  // saves fail; settings still land
        auto& d = M5Cardputer.Display;
        d.fillScreen(theme::kBg);
        d.setTextDatum(top_left);
        d.setFont(&fonts::Font2);
        d.setTextColor(theme::kRed, theme::kBg);
        d.drawString(open ? "STORAGE FULL" : "STORAGE UNAVAILABLE", 12, 16);
        d.setFont(&fonts::Font0);
        d.setTextColor(theme::kIdle, theme::kBg);
        if (pinched) {
            d.drawString("Saving sounds to slots will fail.", 12, 44);
            d.drawString("(Settings still persist, for now.)", 12, 56);
        } else {
            d.drawString("Settings and saved sounds will NOT", 12, 44);
            d.drawString("persist across reboots.", 12, 56);
        }
        d.setTextColor(theme::kDim, theme::kBg);
        if (open) {
            // Name the way out ON the screen — "clear device storage" left
            // people stranded (the Launcher has no user-visible NVS tool).
            // The gesture it points at is the boot factory reset below, which
            // escalates to a full partition erase when saves can't land.
            // Font0 is 6 px/char from x=12: keep each line <= 37 chars.
            d.drawString("Fix: hold BKSP during the boot logo", 12, 76);
            d.drawString("= factory reset. Sounds not saved", 12, 88);
            d.drawString("to the SD card will be lost.", 12, 100);
        } else {
            d.drawString("NVS would not open. (Check NVS partition.)", 12, 76);
        }
        delay(2500);
    }

    // Escape hatch: press BACKSPACE during the boot splash -> full factory
    // reset (settings AND saved sound slots). Works even if stored state
    // ever wedges the UI. NOTE: it must be a press DURING the splash — the
    // ADV's TCA8418 keyboard is event-driven, so a key held from power-on
    // never produces an event (audited; a held-key gesture is dead on this
    // hardware).
    if (splash::run()) {
        // When this boot's write probe failed, the shared partition is full
        // and clearing GLIDE's own keys may not be enough (other apps and the
        // Launcher share it) — escalate to a full partition erase. The player
        // just confirmed a factory reset, so nothing consented-to is lost,
        // and the unit's seed identity is rewritten inside.
        if (!store::writeProbeOk()) store::eraseAllStorage();
        store::resetDefaults();
        for (int i = 0; i < dsp::kPatchCount; ++i) store::clearOverride(i);
        // The 4-byte probe passes long after slot saves start failing (NVS
        // reserves a GC page the stats count as free). If GLIDE's own cleanup
        // above didn't make room for patch-size writes again, the space is
        // other namespaces' — escalate to the same partition erase. Re-probed
        // with a real patch-size write, so a reset on a merely-crowded (but
        // working) partition never nukes the neighbours by mistake.
        store::storageReprobe();
        if (store::storagePinched()) {
            store::eraseAllStorage();
            store::resetDefaults();
        }
        audio::setParams(store::get().synth);
        auto& d = M5Cardputer.Display;
        d.fillScreen(theme::kBg);
        d.setFont(&fonts::Font2);
        d.setTextDatum(middle_center);
        d.setTextColor(theme::kAmber, theme::kBg);
        d.drawString("FACTORY RESET", cfg::kScreenW / 2, 58);
        d.setFont(&fonts::Font0);
        d.setTextColor(theme::kDim, theme::kBg);
        d.drawString("settings + saved sounds cleared", cfg::kScreenW / 2, 78);
        d.setTextDatum(top_left);
        delay(1600);
    }

    keys::begin();
    tilt::begin();
    led::begin();
    coach::begin();  // after the splash/factory-reset block: the tour auto-runs
                     // on a fresh unit, offers itself once on an existing one

    Serial.printf("[glide] ready  heap=%u  starved=%u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)audio::starvedBlocks());
}

void loop() {
    perform::run();  // never returns
}
