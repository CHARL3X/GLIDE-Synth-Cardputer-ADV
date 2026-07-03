#include "listen_screen.h"

#include "../config.h"
#include "../dsp/key_detect.h"
#include "../dsp/scales.h"
#include "../io/listen.h"
#include "../storage/glide_config.h"
#include "hud.h"
#include "theme.h"

namespace listen_screen {

namespace {

struct Ctx {
    M5Canvas* c;
    dsp::KeyGuess guess;
    bool btPrev;  // backtick held on the previous progress tick (edge detect)
};

// Direct positional read, splash-style: the modal owns the loop, so
// keys::poll isn't draining the FIFO for us.
bool backtickHeld() {
    M5Cardputer.update();
    for (int i = 0; i < 4; ++i) M5Cardputer.Keyboard.updateKeyList();
    for (const auto& p : M5Cardputer.Keyboard.keyList())
        if (p.y == 0 && p.x == 0) return true;  // ` (key code 0)
    return false;
}

void drawListening(M5Canvas& c, float frac) {
    c.fillScreen(theme::kBg);
    c.setTextDatum(middle_center);
    c.setFont(&fonts::Font4);
    c.setTextColor(theme::kAmber, theme::kBg);
    c.drawString("LISTENING", cfg::kScreenW / 2, 40);
    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString("play the song at me", cfg::kScreenW / 2, 62);

    const int bw = 168, bx = (cfg::kScreenW - bw) / 2, by = 78;
    c.drawRect(bx, by, bw, 8, theme::kLine);
    const int fw = (int)((bw - 2) * (frac > 1.f ? 1.f : frac));
    if (fw > 0) c.fillRect(bx + 1, by + 1, fw, 6, theme::kAmber);

    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString("` cancel", cfg::kScreenW / 2, 100);
    c.setTextDatum(top_left);
    c.pushSprite(0, 0);
}

bool onProgress(void* user, float frac) {
    Ctx& ctx = *(Ctx*)user;
    drawListening(*ctx.c, frac);
    const bool bt = backtickHeld();
    const bool cancel = bt && !ctx.btPrev;  // newly pressed only
    ctx.btPrev = bt;
    return !cancel;
}

void onSamples(void* user, const int16_t* mono, int n) {
    Ctx& ctx = *(Ctx*)user;
    // Audio is already resumed; this runs on the UI thread and the render
    // task doesn't care how long we take.
    ctx.guess = dsp::detectKey(mono, n, (float)listen::kRateHz);
}

void drawResult(M5Canvas& c, const dsp::KeyGuess& g, int applied, int prevRoot) {
    c.fillScreen(theme::kBg);

    char head[24];
    snprintf(head, sizeof head, "%s %s", dsp::kNoteNames[g.rootPc],
             g.minor ? "MIN" : "MAJ");
    c.setTextDatum(top_left);
    c.setFont(&fonts::Font4);
    c.setTextColor(theme::kGreen, theme::kBg);
    c.drawString(head, 12, 8);

    char sub[28];
    if (applied != g.rootPc)
        snprintf(sub, sizeof sub, "-> root %s (your scale)", dsp::kNoteNames[applied]);
    else
        snprintf(sub, sizeof sub, "root %s", dsp::kNoteNames[applied]);
    c.setFont(&fonts::Font2);
    c.setTextColor(theme::kIdle, theme::kBg);
    c.drawString(sub, 12, 34);
    if (g.confidence < 0.3f) {
        c.setFont(&fonts::Font0);
        c.setTextColor(theme::kDim, theme::kBg);
        c.drawString("weak signal - fn+k to nudge", 12, 52);
    }
    if (applied != prevRoot) {
        c.setFont(&fonts::Font0);
        c.setTextColor(theme::kAmber, theme::kBg);
        c.setTextDatum(top_right);
        c.drawString("RETUNED", cfg::kScreenW - 12, 12);
        c.setTextDatum(top_left);
    }

    // The twelve chroma bars: what the instrument actually heard.
    const int bx = 12, bw = 15, bmax = 46, by0 = 118;
    for (int pc = 0; pc < 12; ++pc) {
        const int x = bx + pc * (bw + 4);
        const int h = 2 + (int)(g.chroma[pc] * (bmax - 2));
        const uint16_t col = pc == g.rootPc     ? theme::kGreen
                             : pc == applied    ? theme::kAmber
                                                : theme::kDim;
        c.fillRect(x, by0 - h, bw, h, col);
        c.setFont(&fonts::Font0);
        c.setTextColor(pc == g.rootPc ? theme::kGreen : theme::kDim, theme::kBg);
        c.drawString(dsp::kNoteNames[pc], x + 2, by0 + 4);
    }
    c.pushSprite(0, 0);
}

// Speaker.begin() failed after the mic released the codec: the instrument
// is dead and must say so at full volume (fatalAudio's mirror).
[[noreturn]] void fatalResume() {
    auto& d = M5Cardputer.Display;
    d.fillScreen(theme::kBg);
    d.drawRect(2, 2, cfg::kScreenW - 4, cfg::kScreenH - 4, theme::kRed);
    d.setTextDatum(top_left);
    d.setFont(&fonts::Font2);
    d.setTextColor(theme::kRed, theme::kBg);
    d.drawString("AUDIO RESTART FAILED", 12, 14);
    d.setFont(&fonts::Font0);
    d.setTextColor(theme::kIdle, theme::kBg);
    d.drawString("The speaker did not come back after", 12, 40);
    d.drawString("the mic released the codec (LISTEN).", 12, 52);
    d.setTextColor(theme::kDim, theme::kBg);
    d.drawString("Power-cycle the device.", 12, 72);
    Serial.println("[glide] AUDIO RESTART FAILED after LISTEN");
    bool on = true;
    for (;;) {
        d.fillCircle(cfg::kScreenW - 14, 14, 4, on ? theme::kRed : theme::kBg);
        on = !on;
        delay(500);
    }
}

}  // namespace

void run(M5Canvas& canvas) {
    Ctx ctx;
    ctx.c = &canvas;
    ctx.guess = dsp::KeyGuess::make();
    ctx.btPrev = backtickHeld();  // swallow a backtick already down at entry

    drawListening(canvas, 0.f);
    const listen::Result r = listen::capture(onProgress, &ctx, onSamples);

    switch (r) {
        case listen::Result::ResumeFailed:
            fatalResume();
        case listen::Result::AllocFailed:
            hud::showError("LISTEN", "no memory");
            return;
        case listen::Result::NoMic:
            hud::showError("LISTEN", "mic unavailable");
            return;
        case listen::Result::Cancelled:
            hud::show("LISTEN", "cancelled", -1.f);
            return;
        case listen::Result::Ok:
            break;
    }

    if (!ctx.guess.valid) {
        hud::showError("LISTEN", "no signal");
        return;
    }

    auto& g = store::get();
    const int prevRoot = g.layout.rootSemis;
    const int applied = dsp::applyRootForScale(ctx.guess.rootPc, ctx.guess.minor,
                                               g.layout.scaleIdx);
    g.layout.rootSemis = (uint8_t)applied;
    store::markDirty();
    Serial.printf("[listen] heard %s %s conf %.2f -> root %s\n",
                  dsp::kNoteNames[ctx.guess.rootPc], ctx.guess.minor ? "min" : "maj",
                  ctx.guess.confidence, dsp::kNoteNames[applied]);

    // Result card: ~1.6 s, backtick skips.
    drawResult(canvas, ctx.guess, applied, prevRoot);
    const uint32_t until = millis() + 1600;
    bool btPrev = backtickHeld();
    while ((int32_t)(until - millis()) > 0) {
        const bool bt = backtickHeld();
        if (bt && !btPrev) break;
        btPrev = bt;
        delay(16);
    }
    hud::show("KEY", dsp::kNoteNames[applied], -1.f);
}

}  // namespace listen_screen
