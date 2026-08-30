// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "splash.h"

#include <M5Cardputer.h>

#include "../config.h"
#include "../dsp/params.h"
#include "../io/audio_engine.h"
#include "../storage/glide_config.h"
#include "glide_logo.h"
#include "perform_screen.h"  // the splash borrows the claimed frame buffer
#include "theme.h"

namespace splash {

bool run() {
    auto& d = M5Cardputer.Display;
    d.fillScreen(theme::kBg);

    // Builder credit, dim, in the clear band below the centred logo. Drawn once
    // here: the logo sprite blits only over its own rect each frame, so this
    // never gets painted over and stays for the whole splash.
    d.setTextDatum(bottom_center);
    d.setFont(&fonts::Font0);
    d.setTextColor(theme::kDim, theme::kBg);
    d.drawString("by CHARL3X  -  github.com/CHARL3X", cfg::kScreenW / 2, cfg::kScreenH - 1);

    // Version stamp, top-right, same dim ink as the credit. The logo blit
    // rect starts at y=(135-115)/2=10, so the top band (y<10) is never
    // repainted and this — drawn once, like the credit — survives every
    // frame. Font0 is 8 px tall: y=1 keeps it fully inside the band.
    d.setTextDatum(top_right);
    d.drawString(cfg::kVersion, cfg::kScreenW - 2, 1);
    d.setTextDatum(top_left);

    // The synthwave wordmark, centered, animated. Each frame is decoded into
    // the perform screen's ALREADY-CLAIMED 65 KB frame buffer (preallocUi in
    // main.cpp owns the boot's first big allocation — there is no headroom
    // for a second full-screen sprite, and exactly one screen draws at a
    // time, so borrowing it is free). White line-art on black composites
    // straight onto the black splash — no color-keying needed.
    const int lw = ui::kGlideLogoW, lh = ui::kGlideLogoH;
    const int lx = (cfg::kScreenW - lw) / 2;
    const int ly = (cfg::kScreenH - lh) / 2;
    M5Canvas* logo = perform::uiCanvas();
    if (logo) {
        // The full canvas blits every frame, so the credit/version drawn on
        // the bare display above would be painted over — draw them into the
        // canvas instead (once; the PNG frames only cover their own rect).
        logo->fillSprite(theme::kBg);
        logo->setTextDatum(bottom_center);
        logo->setFont(&fonts::Font0);
        logo->setTextColor(theme::kDim, theme::kBg);
        logo->drawString("by CHARL3X  -  github.com/CHARL3X", cfg::kScreenW / 2,
                         cfg::kScreenH - 1);
        logo->setTextDatum(top_right);
        logo->drawString(cfg::kVersion, cfg::kScreenW - 2, 1);
        logo->setTextDatum(top_left);
    }

    // boot chime: one note sliding up an octave — the soul, in one second
    const bool chime = store::get().bootSound;
    constexpr uint8_t kChimeId = 250;
    if (chime) {
        dsp::SynthParams p = store::get().synth;  // user's own sound
        p.glideS = 0.22f;
        p.masterVol = p.masterVol * 0.6f;  // gentle: the chime rides your saved level
        audio::setParams(p);
        audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::On, kChimeId, 0xFF, false, 57.f));
    }

    // ping-pong the frames: 0-1-2-3-2-1-0-1-2-3-2-1...  (the "1-2-3-4-3-2-1-..."
    // bounce), so the grid/horizon sweeps in and back out without a hard jump.
    static const uint8_t kSeq[] = {0, 1, 2, 3, 2, 1};
    constexpr int kSeqLen = (int)(sizeof kSeq);
    constexpr int kSteps = 24;          // ~3 bounce cycles
    constexpr uint32_t kFrameMs = 80;
    for (int step = 0; step < kSteps; ++step) {
        int f = kSeq[step % kSeqLen];
        if (f >= ui::kGlideFrameCount) f = ui::kGlideFrameCount - 1;  // fewer frames? clamp
        const auto& fr = ui::kGlideFrames[f];
        if (logo) {
            logo->drawPng(fr.data, fr.len, lx, ly);  // frames are opaque -> no clear needed
            logo->pushSprite(0, 0);
        } else {
            d.drawPng(fr.data, fr.len, lx, ly);   // no frame buffer: decode to screen
        }
        // The chime is a short ~0.8s gesture — it does NOT run the length of
        // the animation (which is much longer now). Glide up early, release
        // well before the visual finishes so the boot tone stays a quick hello.
        if (chime && step == 3)
            audio::pushEvent(
                dsp::NoteEvent::make(dsp::NoteEvent::Retarget, kChimeId, 0xFF, false, 69.f));
        if (chime && step == 10)
            audio::pushEvent(dsp::NoteEvent::make(dsp::NoteEvent::Off, kChimeId));
        M5Cardputer.update();
        delay(kFrameMs);
    }
    delay(300);
    audio::setParams(store::get().synth);  // restore live params

    // Factory reset wipes settings AND saved sounds — it must be deliberate. A
    // stray backspace during boot was nuking people's sessions, so now it takes
    // BKSP held *now* and held through a confirm bar; releasing cancels. (The
    // TCA8418 is event-driven, so a key held from power-on never registers —
    // this is a press made during the splash, then sustained.)
    auto bkspHeld = [&]() {
        M5Cardputer.update();
        for (int i = 0; i < 4; ++i) M5Cardputer.Keyboard.updateKeyList();  // drain FIFO
        for (const auto& p : M5Cardputer.Keyboard.keyList())
            if (p.y == 0 && p.x == 13) return true;  // backspace
        return false;
    };
    if (!bkspHeld()) return false;
    for (int step = 0; step <= 36; ++step) {  // ~1.4 s sustained hold to confirm
        if (!bkspHeld()) return false;        // released -> cancel, boot normally
        d.fillScreen(theme::kBg);
        d.setTextDatum(middle_center);
        d.setFont(&fonts::Font2);
        d.setTextColor(theme::kRed, theme::kBg);
        d.drawString("FACTORY RESET", cfg::kScreenW / 2, 54);
        d.setFont(&fonts::Font0);
        d.setTextColor(theme::kDim, theme::kBg);
        d.drawString("keep holding BKSP  (release = cancel)", cfg::kScreenW / 2, 74);
        const int bw = 168, bx = (cfg::kScreenW - bw) / 2, by = 86;
        d.drawRect(bx, by, bw, 6, theme::kDim);
        d.fillRect(bx + 1, by + 1, (bw - 2) * step / 36, 4, theme::kRed);
        // The cost, named before the point of no return — not after (the
        // FACTORY RESET card in main.cpp only reports what already happened).
        d.setTextColor(theme::kIdle, theme::kBg);
        d.drawString("sounds not saved to SD will be lost", cfg::kScreenW / 2, 104);
        d.setTextDatum(top_left);
        delay(40);
    }
    return true;  // held all the way -> confirmed
}

}  // namespace splash
