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
#include "io/sd_store.h"
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

    // Boot heap map, serial-only: one line per stage so the next resident-heap
    // regression is a diff of two boot logs, not a day of bisection. This is
    // how the v2.8 "no memory" on fn+k was found (the SD mount's true cost).
    auto heapLine = [](const char* stage) {
        Serial.printf("[heap] %-16s free=%u largest=%u\n", stage,
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    };
    heapLine("post M5.begin");

    // RAM-ceiling choreography, load-bearing order (rule 7 — the other order
    // was MEASURED as an "UI ALLOC FAILED" boot, twice): the UI's 65 KB frame
    // buffer is claimed FIRST, on a virgin heap, where no driver residue or
    // fragmentation can ever starve it. Only then the SD card (its mount
    // keeps ~13.9 KB resident — FATFS sector buffers, measured; see
    // sdstore::begin) — store::begin() wants it up for the slot
    // migration and the boot self-heal. Card-less is fine: everything falls
    // back to factory/generative and a failed mount is remembered (backoff).
    perform::preallocUi();
    heapLine("post preallocUi");
    sdstore::begin();
    heapLine("post sd mount");
    store::begin();
    heapLine("post store");
    theme::setTheme(store::get().themeId);  // saved palette styles everything
                                            // from the splash on

    if (!audio::begin()) fatalAudio(audio::lastError());
    audio::setParams(store::get().synth);
    heapLine("post audio");

    // Storage messaging, v2.8. Saved sounds live on the SD card and the
    // system's own sliver of the shared partition SELF-HEALS in store::begin()
    // (erase + rebuild from RAM/mirrors when real writes fail). So there are
    // only two things a player can ever see here, and neither asks them to
    // understand NVS:
    //  - the friendly note that a heal/restore happened (nothing lost, no
    //    action) — informational, 2 s;
    //  - the demoted warning for the pathological remainder (the namespace
    //    won't open at all, or even the heal couldn't bring writes back).
    if (store::healedAtBoot() && store::nvsHealthy() && store::writeProbeOk()) {
        auto& d = M5Cardputer.Display;
        d.fillScreen(theme::kBg);
        d.setTextDatum(top_left);
        d.setFont(&fonts::Font2);
        d.setTextColor(theme::kGreen, theme::kBg);
        d.drawString("STORAGE FIXED", 12, 16);
        d.setFont(&fonts::Font0);
        d.setTextColor(theme::kIdle, theme::kBg);
        d.drawString("The shared storage filled up.", 12, 44);
        d.drawString("GLIDE cleaned it out for you.", 12, 56);
        d.drawString("Your sounds and settings are safe.", 12, 68);
        d.setTextColor(theme::kDim, theme::kBg);
        d.drawString("Nothing to do - play on.", 12, 88);
        delay(2000);
    } else if (!store::nvsHealthy() || !store::writeProbeOk()) {
        auto& d = M5Cardputer.Display;
        d.fillScreen(theme::kBg);
        d.setTextDatum(top_left);
        d.setFont(&fonts::Font2);
        d.setTextColor(theme::kRed, theme::kBg);
        d.drawString("STORAGE UNAVAILABLE", 12, 16);
        d.setFont(&fonts::Font0);
        d.setTextColor(theme::kIdle, theme::kBg);
        d.drawString("Settings can't be remembered right", 12, 44);
        d.drawString("now. Playing works normally, and", 12, 56);
        d.drawString("sounds on the SD card are safe.", 12, 68);
        d.setTextColor(theme::kDim, theme::kBg);
        d.drawString("If this keeps happening: hold BKSP", 12, 88);
        d.drawString("during the boot logo (reset).", 12, 100);
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
        // Clear the ten slots (card files + any legacy NVS copies) and the SD
        // mirrors — a reset the player asked for must never be resurrected
        // from the card at the next boot. The /glide library is NEVER touched.
        for (int i = 0; i < dsp::kPatchCount; ++i) store::clearOverride(i);
        store::clearSdMirrors();
        // If even the lvpat-size probe still fails after GLIDE's own cleanup,
        // the space is other namespaces' — escalate to the partition erase.
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
        d.drawString("settings + the 10 slots cleared", cfg::kScreenW / 2, 78);
        d.drawString("(your SD library /glide is kept)", cfg::kScreenW / 2, 90);
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
