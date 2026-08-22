// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "listen_screen.h"

#include "../config.h"
#include "../dsp/beat_detect.h"
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
    dsp::BeatState* beat;  // onset envelope, summed across the same rounds
    float chroma[12];   // evidence, summed across listening rounds (each
                        // audible round normalized: one round = one vote)
    int rounds;         // segments analyzed so far
    int audibleRounds;  // rounds that actually carried music
    int heardSamples;   // audible samples accumulated (silent rounds don't count)
    int prevApplied;    // packed (root, scale) the PREVIOUS audible round
                        // would have locked (-1 = none yet) — the stability
                        // check tracks the FULL verdict, not just the root:
                        // a lock while the scale is still flip-flopping
                        // (Aeolian<->Dorian) takes whichever the last round
                        // happened to say
    uint32_t pulseUntil;  // live-view pulse: an audible round just landed
    bool heard;         // any segment rose above the silence floor
    bool btPrev;  // backtick held on the previous progress tick (edge detect)
};

// Stop listening early only when the verdict is this sure AND at least this
// much music has been heard — rounds can be as short as 0.5 s on a tight
// heap, and one loud chord must not get to confidently name ITS key.
// Confidence is scale-AWARE (classifyChromaForScale): the relative twin maps
// to the same applied root, so its closeness no longer blocks the lock.
constexpr float kEnoughConfidence = 0.5f;
constexpr int kMinHeardForStop = (int)(listen::kRateHz * 3);
// EVERY stop requires stability — two consecutive audible rounds agreeing on
// the applied root. There used to be a near-certain (0.85) single-round
// bypass; a field capture killed it: LISTEN hit at a vamp's A7 bar, one
// chord scored enormously on its own key, and the listen locked A major
// before the song's Em half had sounded once. Single-round certainty is
// exactly the certainty a two-chord song fakes best. Costs one extra round
// (~2-3 s) on easy songs; the ceiling is still 9 s.
// Tempo applies only above this confidence: the jam tempo moving on a weak
// beat guess would be worse than it staying put (fn+\ fixes it in two taps,
// but an unasked-for wrong tempo is a betrayal; an unchanged one is honest).
constexpr float kTempoApplyConfidence = 0.35f;

// Direct positional read, splash-style: the modal owns the loop, so
// keys::poll isn't draining the FIFO for us.
bool backtickHeld() {
    M5Cardputer.update();
    for (int i = 0; i < 4; ++i) M5Cardputer.Keyboard.updateKeyList();
    for (const auto& p : M5Cardputer.Keyboard.keyList())
        if (p.y == 0 && p.x == 0) return true;  // ` (key code 0)
    return false;
}

// The verdict card's keys, same direct positional read: bit 0 = dismiss
// (` or enter), bit 1 = space (cycle the second guesses — the sd_browser
// convention: space is the "try it" key).
uint32_t cardKeysHeld() {
    M5Cardputer.update();
    for (int i = 0; i < 4; ++i) M5Cardputer.Keyboard.updateKeyList();
    uint32_t m = 0;
    for (const auto& p : M5Cardputer.Keyboard.keyList()) {
        const int code = p.y * 14 + p.x;
        if (code == 0 || code == 41) m |= 1u;
        if (code == 55) m |= 2u;
    }
    return m;
}

// The live view: the twelve chroma bins fill in real time as rounds land —
// the instrument visibly hearing — with a short pulse on each audible round
// and, once a verdict starts forming, what it currently thinks. Redrawn
// every ~100 ms progress tick; everything it shows lives in Ctx (stack of
// this modal — the RAM ceiling forbids anything resident).
void drawListening(M5Canvas& c, float frac, const Ctx& ctx) {
    c.fillScreen(theme::kBg);
    const bool pulse = (int32_t)(ctx.pulseUntil - millis()) > 0;

    c.setTextDatum(top_right);
    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString("` cancel", cfg::kScreenW - 8, 6);

    c.setTextDatum(middle_center);
    c.setFont(&fonts::Font4);
    c.setTextColor(pulse ? theme::kGreen : theme::kAmber, theme::kBg);
    c.drawString("LISTENING", cfg::kScreenW / 2, 30);

    c.setFont(&fonts::Font0);
    char sub[28];
    if (ctx.rounds == 0) {
        snprintf(sub, sizeof sub, "play the song at me");
        c.setTextColor(theme::kDim, theme::kBg);
    } else if (ctx.guess.valid) {
        snprintf(sub, sizeof sub, "hearing %s %s...",
                 dsp::kNoteNames[ctx.guess.rootPc],
                 ctx.guess.minor ? "min" : "maj");
        c.setTextColor(theme::kIdle, theme::kBg);
    } else {
        snprintf(sub, sizeof sub, "locking in...");
        c.setTextColor(theme::kDim, theme::kBg);
    }
    c.drawString(sub, cfg::kScreenW / 2, 50);

    const int bw = 168, bx = (cfg::kScreenW - bw) / 2, by = 62;
    c.drawRect(bx, by, bw, 6, theme::kLine);
    const int fw = (int)((bw - 2) * (frac > 1.f ? 1.f : frac));
    if (fw > 0) c.fillRect(bx + 1, by + 1, fw, 4, theme::kAmber);

    // The chroma bins so far, peak-normalized. Green = the forming tonic;
    // the pulse brightens the rest for the beat of a landed round.
    float peak = 0.f;
    for (int pc = 0; pc < 12; ++pc)
        if (ctx.chroma[pc] > peak) peak = ctx.chroma[pc];
    const int cbx = 12, cbw = 15, cmax = 44, cy0 = 126;
    for (int pc = 0; pc < 12; ++pc) {
        const int x = cbx + pc * (cbw + 4);
        int h = 2;
        if (peak > 1e-9f)
            h = 2 + (int)(ctx.chroma[pc] / peak * (float)(cmax - 2));
        const bool tonic = ctx.guess.valid && pc == ctx.guess.rootPc;
        const uint16_t col = tonic   ? theme::kGreen
                             : pulse ? theme::kIdle
                                     : theme::kDim;
        c.fillRect(x, cy0 - h, cbw, h, col);
    }
    c.setTextDatum(top_left);
    c.pushSprite(0, 0);
}

bool onProgress(void* user, float frac) {
    Ctx& ctx = *(Ctx*)user;
    drawListening(*ctx.c, frac, ctx);
    const bool bt = backtickHeld();
    const bool cancel = bt && !ctx.btPrev;  // newly pressed only
    ctx.btPrev = bt;
    return !cancel;
}

// One round of evidence. Returns true to keep listening: a single round can
// catch one chord and name ITS key, so every stop requires a confident
// verdict that has ALSO held steady across two audible rounds. Rounds
// accumulate NORMALIZED — one round, one vote — so a loud chorus can't
// out-vote quiet honest verses.
bool onSegment(void* user, const int16_t* mono, int n) {
    Ctx& ctx = *(Ctx*)user;
    ++ctx.rounds;
    if (!dsp::segmentAudible(mono, n)) return true;  // silent round: wait for the song
    ctx.heard = true;
    ctx.heardSamples += n;
    ++ctx.audibleRounds;
    ctx.pulseUntil = millis() + 400;  // the live view flashes: round landed
    if (ctx.beat) dsp::accumulateOnsets(*ctx.beat, mono, n, (float)listen::kRateHz);
    dsp::accumulateChromaNormalized(mono, n, (float)listen::kRateHz, ctx.chroma);
    const int scaleIdx = store::get().layout.scaleIdx;
    ctx.guess = dsp::classifyChromaForScale(ctx.chroma, scaleIdx);
    if (!ctx.guess.valid) return true;

    // Stability tracks the FULL verdict — root AND scale, packed — so both a
    // tonic re-seat arriving with round two's fresh b7 evidence and a scale
    // verdict still flip-flopping (Aeolian<->Dorian on borderline 6ths)
    // break the stop and earn the song more listening, instead of locking
    // whichever reading the last round happened to say.
    const dsp::ListenApply lap = dsp::applyListen(scaleIdx, ctx.guess);
    const int applied = lap.rootPc * 64 + lap.scaleIdx;
    const bool stable = applied == ctx.prevApplied && ctx.audibleRounds >= 2;
    ctx.prevApplied = applied;
    if (ctx.heardSamples < kMinHeardForStop) return true;
    return !(stable && ctx.guess.confidence >= kEnoughConfidence);
}

// The verdict card. ap is the SONG's refined truth (tonic + mode — "A DOR",
// not the raw profile winner a modal vamp can mislabel); sel is the landing
// currently applied to the instrument (== ap until the player nudges to an
// alternate with space). The chroma bars show what was actually heard, and
// the amber strip under them shows the applied scale's footprint against it
// — the fit is visible, not asserted.
void drawResult(M5Canvas& c, const dsp::KeyGuess& g, const dsp::ListenApply& ap,
                const dsp::ListenApply& sel, int prevRoot, int prevScale,
                int altIdx, int altCount, int bpm) {
    c.fillScreen(theme::kBg);
    const bool scaleChanged = sel.scaleIdx != prevScale;

    char head[24];
    snprintf(head, sizeof head, "%s %s", dsp::kNoteNames[ap.tonicPc],
             dsp::listenModeName(ap.mode));
    c.setTextDatum(top_left);
    c.setFont(&fonts::Font4);
    c.setTextColor(theme::kGreen, theme::kBg);
    c.drawString(head, 12, 6);

    // How sure it is, shown honestly: a thin meter, green once the lock
    // threshold was truly cleared, amber below it.
    const int mx = 12, my = 33, mw = 96;
    c.drawRect(mx, my, mw, 4, theme::kLine);
    float conf = g.confidence;
    if (conf > 1.f) conf = 1.f;
    const int mf = (int)((mw - 2) * conf);
    if (mf > 0)
        c.fillRect(mx + 1, my + 1, mf, 2,
                   conf >= 0.5f ? theme::kGreen : theme::kAmber);

    if (sel.rootPc != prevRoot || scaleChanged) {
        c.setFont(&fonts::Font0);
        c.setTextColor(theme::kAmber, theme::kBg);
        c.setTextDatum(top_right);
        c.drawString("RETUNED", cfg::kScreenW - 12, 8);
        c.setTextDatum(top_left);
    }
    if (bpm > 0) {  // the jam tempo it locked (only shown when applied)
        char tb[12];
        snprintf(tb, sizeof tb, "%d BPM", bpm);
        c.setTextDatum(top_right);
        c.setTextColor(theme::kGreen, theme::kBg);
        c.drawString(tb, cfg::kScreenW - 12, 20);
        c.setTextDatum(top_left);
    }

    // The applied landing.
    char sub[28];
    if (scaleChanged)
        snprintf(sub, sizeof sub, "-> root %s (%s)", dsp::kNoteNames[sel.rootPc],
                 dsp::kScales[sel.scaleIdx].shortName);
    else if (sel.rootPc != ap.tonicPc)
        snprintf(sub, sizeof sub, "-> root %s (your scale)",
                 dsp::kNoteNames[sel.rootPc]);
    else
        snprintf(sub, sizeof sub, "root %s (%s)", dsp::kNoteNames[sel.rootPc],
                 dsp::kScales[sel.scaleIdx].shortName);
    c.setFont(&fonts::Font2);
    c.setTextColor(theme::kIdle, theme::kBg);
    c.drawString(sub, 12, 42);

    // Status left / nudge hint right, one Font0 line.
    c.setFont(&fonts::Font0);
    if (altIdx > 0) {
        char at[20];
        snprintf(at, sizeof at, "2nd guess %d/%d", altIdx + 1, altCount);
        c.setTextColor(theme::kAmber, theme::kBg);
        c.drawString(at, 12, 62);
    } else if (sel.safe) {
        c.setTextColor(theme::kAmber, theme::kBg);
        c.drawString("clash heard - safe pent", 12, 62);
    } else if (g.confidence < 0.3f) {
        c.setTextColor(theme::kDim, theme::kBg);
        c.drawString("weak signal", 12, 62);
    }
    if (altCount > 1) {
        c.setTextDatum(top_right);
        c.setTextColor(theme::kDim, theme::kBg);
        c.drawString("space: alt", cfg::kScreenW - 12, 62);
        c.setTextDatum(top_left);
    }

    // The twelve chroma bars: what the instrument actually heard. Green =
    // the refined tonic, amber = the applied root; the amber strip under a
    // bar marks a note the applied scale contains.
    bool inScale[12] = {false};
    if (sel.scaleIdx >= 0 && sel.scaleIdx < dsp::kScaleCount) {
        const dsp::Scale& sc = dsp::kScales[sel.scaleIdx];
        for (int i = 0; i < sc.len; ++i)
            inScale[(sel.rootPc + sc.steps[i]) % 12] = true;
    }
    const int bx = 12, bw = 15, bmax = 42, by0 = 116;
    for (int pc = 0; pc < 12; ++pc) {
        const int x = bx + pc * (bw + 4);
        const int h = 2 + (int)(g.chroma[pc] * (bmax - 2));
        const uint16_t col = pc == ap.tonicPc    ? theme::kGreen
                             : pc == sel.rootPc  ? theme::kAmber
                                                 : theme::kDim;
        c.fillRect(x, by0 - h, bw, h, col);
        if (inScale[pc]) c.fillRect(x, by0 + 2, bw, 2, theme::kAmber);
        c.setFont(&fonts::Font0);
        c.setTextColor(pc == ap.tonicPc ? theme::kGreen : theme::kDim, theme::kBg);
        c.drawString(dsp::kNoteNames[pc], x + 2, by0 + 6);
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
    // The onset envelope (~4 KB) is heap-allocated for the modal's life ONLY.
    // The frame-buffer sprite sits within ~1 KB of the RAM ceiling, so a
    // static here breaks boot ("UI ALLOC FAILED" — measured the hard way on
    // this very feature), and a RESIDENT heap block would shrink LISTEN's
    // record rounds forever (perform_screen's .bss note; a 19.5 KB field
    // once cost fn+k entirely). Allocated BEFORE capture() so the round
    // sizing sees the true largest block; freed the moment the tempo verdict
    // is taken. If even 4 KB can't alloc, tempo sits out and the key listen
    // proceeds untouched.
    dsp::BeatState* beat = (dsp::BeatState*)malloc(sizeof(dsp::BeatState));
    if (beat) {
        beat->len = 0;      // in-place init: BeatState::make() returns the
        beat->prevE = -1.f; // whole 4 KB by value, which this task's stack
    }                       // must never host

    Ctx ctx;
    ctx.c = &canvas;
    ctx.beat = beat;
    ctx.guess = dsp::KeyGuess::make();
    for (int i = 0; i < 12; ++i) ctx.chroma[i] = 0.f;
    ctx.rounds = 0;
    ctx.audibleRounds = 0;
    ctx.heardSamples = 0;
    ctx.prevApplied = -1;
    ctx.pulseUntil = 0;
    ctx.heard = false;
    ctx.btPrev = backtickHeld();  // swallow a backtick already down at entry

    drawListening(canvas, 0.f, ctx);
    const listen::Result r = listen::capture(onProgress, &ctx, onSegment);

    // Take the tempo verdict and free the envelope HERE, before the result
    // switch: one free covers every return path below, and the 4 KB is gone
    // before the perform screen is back.
    dsp::TempoGuess tempo = dsp::TempoGuess::make();
    if (beat) {
        tempo = dsp::estimateTempo(*beat);
        free(beat);
        beat = nullptr;
        ctx.beat = nullptr;
    }

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
    const int prevScale = g.layout.scaleIdx;
    // The full listen verdict: refined mode (Dorian/Mixolydian by degree
    // evidence), the tonic tiebreak (a mixo-flavoured "D major" re-seats as
    // the A Dorian vamp it is), and a landing mapped to the player's scale
    // FAMILY — plain canvases play the mode, pentatonics swap tonic-home,
    // Blues stays Blues and re-centres. Weak evidence = frozen behavior.
    dsp::ListenApply alts[4];
    const int nAlts = dsp::listenAlternates(prevScale, ctx.guess, alts, 4);
    const dsp::ListenApply ap = alts[0];  // primary == applyListen
    int altIdx = 0;
    dsp::ListenApply sel = ap;
    g.layout.scaleIdx = (uint8_t)sel.scaleIdx;
    g.layout.rootSemis = (uint8_t)sel.rootPc;
    // Tempo, from the same capture (verdict taken above, before the result
    // switch): the jam clock (and with it the synced delay and LFOs) locks
    // to the song's groove — but only on a confident beat. An invalid or
    // weak guess leaves the tempo exactly where it was.
    int appliedBpm = 0;
    if (tempo.valid && tempo.confidence >= kTempoApplyConfidence) {
        int b = (int)(tempo.bpm + 0.5f);
        if (b < 40) b = 40;
        if (b > 240) b = 240;
        g.jamBpm = (uint16_t)b;
        appliedBpm = b;
    }
    store::markDirty();
    // Test-mode instrumentation: everything the verdict was built from, so a
    // wrong landing can be tuned from numbers instead of vibes.
    Serial.printf("[listen] chroma ");
    for (int pc = 0; pc < 12; ++pc)
        Serial.printf("%s=%.2f ", dsp::kNoteNames[pc], ctx.guess.chroma[pc]);
    Serial.println();
    Serial.printf(
        "[listen] raw %s %s conf %.2f (%d rounds) -> %s %s%s%s%s | root %s (%s) | "
        "tempo %s %.1f conf %.2f%s\n",
        dsp::kNoteNames[ctx.guess.rootPc], ctx.guess.minor ? "min" : "maj",
        ctx.guess.confidence, ctx.rounds, dsp::kNoteNames[ap.tonicPc],
        dsp::listenModeName(ap.mode), ap.modal ? " (modal)" : "",
        ap.tiebreak ? " (tiebreak)" : "", ap.safe ? " (safe)" : "",
        dsp::kNoteNames[sel.rootPc], dsp::kScales[sel.scaleIdx].shortName,
        tempo.valid ? "ok" : "none", tempo.bpm, tempo.confidence,
        appliedBpm ? " (applied)" : "");

    // Result card: ~2 s, backtick/enter dismisses, space cycles the second
    // guesses (each press applies that landing live and re-arms the timer,
    // so a near-miss verdict is fixed in one tap instead of a re-listen).
    drawResult(canvas, ctx.guess, ap, sel, prevRoot, prevScale, altIdx, nAlts,
               appliedBpm);
    uint32_t until = millis() + 2000;
    uint32_t keysPrev = cardKeysHeld();
    while ((int32_t)(until - millis()) > 0) {
        const uint32_t held = cardKeysHeld();
        const uint32_t pressed = held & ~keysPrev;
        keysPrev = held;
        if (pressed & 1u) break;  // ` / enter: keep what's applied
        if ((pressed & 2u) && nAlts > 1) {
            altIdx = (altIdx + 1) % nAlts;
            sel = alts[altIdx];
            g.layout.scaleIdx = (uint8_t)sel.scaleIdx;
            g.layout.rootSemis = (uint8_t)sel.rootPc;
            store::markDirty();
            Serial.printf("[listen] nudge %d/%d -> %s (%s)\n", altIdx + 1,
                          nAlts, dsp::kNoteNames[sel.rootPc],
                          dsp::kScales[sel.scaleIdx].shortName);
            drawResult(canvas, ctx.guess, ap, sel, prevRoot, prevScale, altIdx,
                       nAlts, appliedBpm);
            until = millis() + 2600;
        }
        delay(16);
    }
    if (sel.scaleIdx != prevScale) {
        char v[16];
        snprintf(v, sizeof v, "%s %s", dsp::kNoteNames[sel.rootPc],
                 dsp::kScales[sel.scaleIdx].shortName);
        hud::show("KEY", v, -1.f);
    } else {
        hud::show("KEY", dsp::kNoteNames[sel.rootPc], -1.f);
    }
}

}  // namespace listen_screen
