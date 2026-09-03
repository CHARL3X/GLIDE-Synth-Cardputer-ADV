// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "listen_screen.h"

#include "../config.h"
#include "../dsp/key_detect.h"
#include "../dsp/scales.h"
#include "theme.h"

#ifndef GLIDE_HOST_BUILD
#include "../dsp/beat_detect.h"
#include "../io/audio_engine.h"
#include "../io/keys.h"
#include "../io/listen.h"
#include "../io/looper.h"
#include "../storage/glide_config.h"
#include "hud.h"
#include "morph.h"
#endif

namespace listen_screen {

// ---- shared chart geometry -------------------------------------------------
// Both screens show the same twelve-bar pitch-class chart in the same place, so
// the live view and the verdict read as one instrument rather than two designs.
// The bar block is centred inside the panel: 12 * 17 - 3 = 201 wide in a 228
// panel leaves 13 either side.
namespace {
constexpr int kChartX = 6, kChartW = 228;  // the panel rect, both screens
constexpr int kBarW = 14, kBarStep = 17;
constexpr int kBarX0 = kChartX + 13;
constexpr int kRadius = 7;  // corner rounding, shared by the chart and the card

// The chart's frame: a rounded outline, a half-scale graticule, and the rule
// its bars stand on. Deliberately UNFILLED — the outline alone is what makes an
// empty chart read as a meter at rest instead of a hole in the screen, and a
// filled slab behind the bars only competed with them for attention.
void drawChartFrame(M5Canvas& c, int y, int h, int base, int barTop) {
    c.drawRoundRect(kChartX, y, kChartW, h, kRadius, theme::kLine);
    // Both rules sit on the screen ground, so they fade toward kBg — which is
    // also the only fade that stays correct on the two light palettes.
    c.drawFastHLine(kChartX + 4, (base + barTop) / 2, kChartW - 8,
                    theme::fade(theme::kLine, 170));
    c.drawFastHLine(kChartX + 4, base, kChartW - 8, theme::kLine);
}
}  // namespace

// The live view: the twelve chroma bins fill in real time as rounds land — the
// instrument visibly hearing — with a short pulse on each audible round and,
// once a verdict starts forming, what it currently thinks. Redrawn every
// ~100 ms progress tick. Everything it shows is passed in, so it stays pure.
void drawListening(M5Canvas& c, float frac, const float* chroma,
                   const dsp::KeyGuess& guess, int rounds, bool pulse,
                   uint32_t nowMs) {
    c.fillScreen(theme::kBg);

    c.setTextDatum(top_right);
    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString("` cancel", cfg::kScreenW - 8, 5);

    // The title sits amber and a glint sweeps through it, letter by letter. It
    // replaces a two-state colour flash that was doing the same job badly: the
    // word never changes colour as a whole, so nothing blinks, and a travelling
    // highlight says "still working" where a static title cannot.
    //
    // The ramp runs amber -> kIdle rather than amber -> green, because those
    // two roles are the accent and the HOT text and so are far apart in all ten
    // palettes; amber and green sit almost on top of each other on paper and
    // drafting, where the sweep would have been invisible. A landed round is
    // carried by the glint's reach and intensity instead of by its colour —
    // the surge is legible without anything flashing.
    //
    // Per-letter because Font4 is proportional: textWidth() gives each advance,
    // and the cells tile on a ground already painted kBg, so no seams show.
    static const char kWord[] = "LISTENING";
    const int nch = (int)(sizeof kWord - 1);
    c.setFont(&fonts::Font4);
    c.setTextDatum(middle_left);
    const int reach = pulse ? 4 : 3, peakF = pulse ? 255 : 150;
    int wx = cfg::kScreenW / 2 - c.textWidth(kWord) / 2;
    const int head = (int)((nowMs / 90) % (uint32_t)(nch + 6)) - 3;
    for (int i = 0; i < nch; ++i) {
        const char one[2] = {kWord[i], '\0'};
        int d = i - head;
        if (d < 0) d = -d;
        const int f = d >= reach ? 0 : peakF - d * (peakF / reach);
        c.setTextColor(f > 0 ? theme::blend(theme::kAmber, theme::kIdle,
                                            (uint8_t)f)
                             : theme::kAmber,
                       theme::kBg);
        c.drawString(one, wx, 27);
        wx += c.textWidth(one);
    }

    c.setTextDatum(middle_center);
    c.setFont(&fonts::Font0);
    char sub[28];
    if (rounds == 0) {
        snprintf(sub, sizeof sub, "play the song at me");
        c.setTextColor(theme::kDim, theme::kBg);
    } else if (guess.valid) {
        snprintf(sub, sizeof sub, "hearing %s %s...", dsp::kNoteNames[guess.rootPc],
                 guess.minor ? "min" : "maj");
        c.setTextColor(theme::kIdle, theme::kBg);
    } else {
        snprintf(sub, sizeof sub, "locking in...");
        c.setTextColor(theme::kDim, theme::kBg);
    }
    c.drawString(sub, cfg::kScreenW / 2, 48);

    const int pw = 168, px0 = (cfg::kScreenW - pw) / 2, py0 = 57;
    c.drawRect(px0, py0, pw, 6, theme::kLine);
    const int fw = (int)((pw - 2) * (frac > 1.f ? 1.f : frac));
    if (fw > 0) c.fillRect(px0 + 1, py0 + 1, fw, 4, theme::kAmber);

    // The chroma chart, peak-normalized. Green = the forming tonic; the pulse
    // brightens the rest for the beat of a landed round.
    const int base = 116, maxH = 44;
    drawChartFrame(c, 66, 64, base, base - maxH);
    float peak = 0.f;
    for (int pc = 0; pc < 12; ++pc)
        if (chroma[pc] > peak) peak = chroma[pc];
    c.setTextDatum(top_left);
    c.setFont(&fonts::Font0);
    for (int pc = 0; pc < 12; ++pc) {
        const int x = kBarX0 + pc * kBarStep;
        int h = 2;
        if (peak > 1e-9f) h = 2 + (int)(chroma[pc] / peak * (float)(maxH - 2));
        const bool tonic = guess.valid && pc == guess.rootPc;
        const uint16_t col = tonic   ? theme::kGreen
                             : pulse ? theme::kIdle
                                     : theme::kDim;
        c.fillRect(x, base - h, kBarW, h, col);
        // The frame is unfilled, so these labels sit on the screen ground and
        // take kBg as their background. (The verdict card's copies of the same
        // labels take kPanel, because there they sit on the card.)
        c.setTextColor(tonic ? theme::kGreen : theme::kDim, theme::kBg);
        c.drawString(dsp::kNoteNames[pc], x + 1, 119);
    }
    c.pushSprite(0, 0);
}

// The verdict card. It FLOATS — a panel inset from the screen edges with the
// live scope running in the strip below it — because the card is played over
// and has to look like it. Full-bleed it read as a wall even after the keys
// went live underneath: nothing on screen belonged to the instrument any more.
//
// ap is the SONG's refined truth ("A DOR", not the raw profile winner a modal
// vamp can mislabel); sel is the landing currently applied to the instrument
// (== ap until the player nudges to an alternate with space). The chroma bars
// show what was actually heard, and the amber strip under them shows the
// applied scale's footprint against it — the fit is visible, not asserted.
void drawResult(M5Canvas& c, const dsp::KeyGuess& g, const dsp::ListenApply& ap,
                const dsp::ListenApply& sel, int prevRoot, int prevScale,
                int altIdx, int altCount, int bpm, const float* wave, int waveN) {
    c.fillScreen(theme::kBg);
    const bool scaleChanged = sel.scaleIdx != prevScale;

    // The card body. The soft outer edge is defined against kBg, so the same
    // one line lifts the panel on a dark ground and drops a shadow on a light
    // one — no per-theme special case, and none of the ten can invert it.
    const int cx = kChartX, cy = 3, cw = kChartW, chh = 117;
    c.drawRoundRect(cx - 1, cy - 1, cw + 2, chh + 2, kRadius + 1,
                    theme::blend(theme::kBg, theme::kLine, 110));
    c.fillRoundRect(cx, cy, cw, chh, kRadius, theme::kPanel);
    c.drawRoundRect(cx, cy, cw, chh, kRadius, theme::kLine);

    char head[24];
    snprintf(head, sizeof head, "%s %s", dsp::kNoteNames[ap.tonicPc],
             dsp::listenModeName(ap.mode));
    c.setTextDatum(top_left);
    c.setFont(&fonts::Font4);
    c.setTextColor(theme::kGreen, theme::kPanel);
    c.drawString(head, 13, 7);

    c.setFont(&fonts::Font0);
    if (sel.rootPc != prevRoot || scaleChanged) {
        c.setTextDatum(top_right);
        c.setTextColor(theme::kAmber, theme::kPanel);
        c.drawString("RETUNED", cfg::kScreenW - 13, 7);
    }
    if (bpm > 0) {  // the jam tempo it locked (only shown when applied)
        char tb[12];
        snprintf(tb, sizeof tb, "%d BPM", bpm);
        c.setTextDatum(top_right);
        c.setTextColor(theme::kGreen, theme::kPanel);
        c.drawString(tb, cfg::kScreenW - 13, 19);
    }
    c.setTextDatum(top_left);

    // How sure it is, shown honestly: a thin meter under the verdict it
    // belongs to, green once the lock threshold was truly cleared, amber below.
    const int mx = 13, my = 33, mw = 96;
    c.drawRect(mx, my, mw, 4, theme::kLine);
    float conf = g.confidence;
    if (conf > 1.f) conf = 1.f;
    const int mf = (int)((mw - 2) * conf);
    if (mf > 0)
        c.fillRect(mx + 1, my + 1, mf, 2,
                   conf >= 0.5f ? theme::kGreen : theme::kAmber);

    // The applied landing.
    char sub[30];
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
    c.setTextColor(theme::kIdle, theme::kPanel);
    c.drawString(sub, 13, 40);

    // One status line. With nothing to warn about it becomes the invitation,
    // because the card's whole reason to exist is that you play through it.
    c.setFont(&fonts::Font0);
    char st[32];
    uint16_t stCol = theme::kDim;
    if (altIdx > 0) {
        snprintf(st, sizeof st, "2nd guess %d/%d", altIdx + 1, altCount);
        stCol = theme::kAmber;
    } else if (sel.safe) {
        snprintf(st, sizeof st, "clash heard - safe pent");
        stCol = theme::kAmber;
    } else if (g.confidence < 0.3f) {
        snprintf(st, sizeof st, "weak signal");
    } else {
        snprintf(st, sizeof st, "play it - that's how you know");
    }
    c.setTextColor(stCol, theme::kPanel);
    c.drawString(st, 13, 59);

    // The chroma chart: same bars, same columns, same place as the live view.
    bool inScale[12] = {false};
    if (sel.scaleIdx >= 0 && sel.scaleIdx < dsp::kScaleCount) {
        const dsp::Scale& sc = dsp::kScales[sel.scaleIdx];
        for (int i = 0; i < sc.len; ++i)
            inScale[(sel.rootPc + sc.steps[i]) % 12] = true;
    }
    const int base = 103, maxH = 31;
    c.drawFastHLine(kChartX + 4, base, kChartW - 8, theme::kLine);
    for (int pc = 0; pc < 12; ++pc) {
        const int x = kBarX0 + pc * kBarStep;
        const int h = 2 + (int)(g.chroma[pc] * (maxH - 2));
        const uint16_t col = pc == ap.tonicPc    ? theme::kGreen
                             : pc == sel.rootPc  ? theme::kAmber
                                                 : theme::kDim;
        c.fillRect(x, base - h, kBarW, h, col);
        if (inScale[pc]) c.fillRect(x, base + 2, kBarW, 2, theme::kAmber);
        c.setTextColor(pc == ap.tonicPc ? theme::kGreen : theme::kDim,
                       theme::kPanel);
        c.drawString(dsp::kNoteNames[pc], x + 1, base + 6);
    }

    // Outside the card, on the bare screen: the live scope. The card is played
    // over, and this is the proof — it moves the instant a key goes down, which
    // no amount of hint text says as fast. A silent room draws a flat line,
    // which is a horizon rather than an absence.
    const int wx = 8, ww = 112, wcy = 129, wamp = 5;
    // Silence draws in the dim green and playing in the live one, so a resting
    // trace recedes into the furniture instead of reading as a decorative rule
    // ruled under the card — and the moment a key goes down the strip lights.
    float wpk = 0.f;
    for (int i = 0; i < waveN; ++i) {
        const float a = wave[i] < 0.f ? -wave[i] : wave[i];
        if (a > wpk) wpk = a;
    }
    const uint16_t wcol = wpk > 0.02f ? theme::kGreen : theme::kGreenDim;
    int lx = wx, ly = wcy;
    for (int i = 0; i < ww; ++i) {
        float v = 0.f;
        if (waveN > 0) {
            v = wave[i * waveN / ww];
            if (v > 1.f) v = 1.f;
            else if (v < -1.f) v = -1.f;
        }
        const int x = wx + i, y = wcy - (int)(v * wamp);
        if (i) c.drawLine(lx, ly, x, y, wcol);
        lx = x;
        ly = y;
    }
    // The key hint lives out here with the scope rather than inside the card:
    // it is about what your hands can do, not about the verdict. "space: alt"
    // used to sit inside and was read as the ALT KEY — which is a real key on
    // this keyboard, and the first thing a player reached for.
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_right);
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString(altCount > 1 ? "space: next guess" : "` closes",
                 cfg::kScreenW - 8, 125);
    c.setTextDatum(top_left);
    c.pushSprite(0, 0);
}

#ifndef GLIDE_HOST_BUILD
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

// How long the verdict card holds. It used to be 2 s and the keyboard was
// dead behind it, which made the card a wall: too short to read the landing
// or notice the "space: alt" hint, yet long enough to block the one thing a
// player wants the instant a key is named — to play in it. The card is now
// played over (keys::poll runs inside its loop), so length costs nothing:
// ` / enter still dismisses instantly, and the notes keep sounding either
// way. Every space press re-arms the FULL window, not a shorter one — each
// alternate is a fresh thing to judge, and a partial re-arm would SHORTEN
// the card when pressed early.
constexpr uint32_t kCardMs = 6000;

// Direct positional read, splash-style: the modal owns the loop, so
// keys::poll isn't draining the FIFO for us.
bool backtickHeld() {
    M5Cardputer.update();
    for (int i = 0; i < 4; ++i) M5Cardputer.Keyboard.updateKeyList();
    for (const auto& p : M5Cardputer.Keyboard.keyList())
        if (p.y == 0 && p.x == 0) return true;  // ` (key code 0)
    return false;
}

// The verdict card's own keys: bit 0 = dismiss (` or enter), bit 1 = space
// (cycle the second guesses — the sd_browser convention: space is the "try
// it" key). Unlike backtickHeld() this deliberately does NOT call update() /
// updateKeyList(): the card is played over, so keys::poll() owns the loop
// there and must be the ONLY thing draining the TCA8418 FIFO. A second
// drain here would swallow the very press events that make the notes. The
// list poll() just refreshed is read as-is.
uint32_t cardKeysDown() {
    uint32_t m = 0;
    for (const auto& p : M5Cardputer.Keyboard.keyList()) {
        const int code = p.y * 14 + p.x;
        if (code == 0 || code == 41) m |= 1u;
        if (code == 55) m |= 2u;
    }
    return m;
}

bool onProgress(void* user, float frac) {
    Ctx& ctx = *(Ctx*)user;
    const uint32_t now = millis();
    drawListening(*ctx.c, frac, ctx.chroma, ctx.guess, ctx.rounds,
                  (int32_t)(ctx.pulseUntil - now) > 0, now);
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

// The modal proper. Returns true if it has ALREADY rebuilt the keyboard's
// edge state — only the result-card path has, because that path hands the
// keyboard to keys::poll() and must resync BEFORE doing so. Every other path
// returns false and lets run() resync on the way out.
bool runModal(M5Canvas& canvas) {
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

    drawListening(canvas, 0.f, ctx.chroma, ctx.guess, ctx.rounds, false, millis());
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
            return false;
        case listen::Result::NoMic:
            hud::showError("LISTEN", "mic unavailable");
            return false;
        case listen::Result::Cancelled:
            hud::show("LISTEN", "cancelled", -1.f);
            return false;
        case listen::Result::Ok:
            break;
    }

    if (!ctx.guess.valid) {
        hud::showError("LISTEN", "no signal");
        return false;
    }

    auto& g = store::get();
    const int prevRoot = g.layout.rootSemis;
    const int prevScale = g.layout.scaleIdx;
    // The full listen verdict: refined mode (Dorian/Mixolydian by degree
    // evidence), the tonic tiebreak (a mixo-flavoured "D major" re-seats as
    // the A Dorian vamp it is), and a landing mapped to the player's scale
    // FAMILY — plain canvases play the mode, pentatonics swap tonic-home,
    // Blues stays Blues and re-centres. Weak evidence = frozen behavior.
    // Room past the sibling readings for the detector's two runner-up KEYS:
    // when the tonic itself lands wrong, the siblings are all flavors of the
    // same mistake, and the runner-ups are the only way space can fix it.
    dsp::ListenApply alts[6];
    const int nAlts = dsp::listenAlternates(prevScale, ctx.guess, alts, 6);
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

    // The keyboard belongs to keys::poll() from here on, so the modal's stale
    // edge state is rebuilt NOW rather than on the way out (perform_screen no
    // longer resyncs after this screen — see run()). Order is load-bearing
    // twice over: poll() needs a truthful gPrevMask or the keys still held
    // from the fn+k gesture read as fresh presses, and a resync AFTER the
    // card would clearLeadNotes() and chop a note still under the player's
    // fingers as the card times out.
    keys::resync();

    // Result card: kCardMs, backtick/enter dismisses, space cycles the second
    // guesses (each press applies that landing live and re-arms the timer,
    // so a near-miss verdict is fixed in one tap instead of a re-listen) —
    // and the instrument PLAYS underneath it, which is the whole point of the
    // card: hearing the key it just named is how you know it got it right.
    //
    // The card redraws every frame now, because the scope strip under it is
    // LIVE — that strip is what tells a player the keys still work, and a
    // still picture cannot say it. The buffer is a modal-lifetime local: the
    // perform screen's equivalent is a 2 KB static, which rule 7 forbids
    // adding a second of, and 112 floats of stack costs nothing here (the
    // 4 KB onset envelope this frame was hosting is already freed).
    float wave[112] = {0.f};
    int waveN = 0;
    drawResult(canvas, ctx.guess, ap, sel, prevRoot, prevScale, altIdx, nAlts,
               appliedBpm, wave, waveN);
    uint32_t until = millis() + kCardMs;
    uint32_t nextFrame = millis() + cfg::kFrameMs;
    uint32_t keysPrev = cardKeysDown();
    while ((int32_t)(until - millis()) > 0) {
        const uint32_t now = millis();
        // poll() plays the grid, advances the living backing, and drains the
        // keyboard FIFO; looper::tick keeps a recorded take playing, the same
        // courtesy settings / sd_browser / help extend to a running loop.
        // Notes deliberately neither dismiss the card nor extend it: it is a
        // fixed window you play straight through.
        const keys::Actions act = keys::poll(now);
        looper::tick(now);
        // The card is a surface for NOTES, not a whole perform frame — tilt,
        // the G0 trigger macro and the param push stay perform's business.
        // morph is the exception, because it is STATE that goes stale: a
        // sound grabbed under the card kicks the blend to 1 (fully the old
        // sound) and only tick() walks it home. Left unticked for kCardMs,
        // the card would close and audibly jump back to the previous sound
        // before gliding in. Ticking it costs one line and no glitch.
        morph::tick(now);
        // Tab can't reach settings from here (poll consumed the flag and the
        // perform loop will never see it), so rather than eat the key, let it
        // close the card — a second tab then opens settings as it always has.
        if (act.openSettings) break;
        const uint32_t held = cardKeysDown();
        const uint32_t pressed = held & ~keysPrev;
        keysPrev = held;
        if (pressed & 1u) {  // ` / enter: keep what's applied
            // This press was spent on the card. Neuter its hold gesture so
            // the same finger can't also exit the app (`) or cycle the tilt
            // route (enter) the instant the perform screen is back — resync()
            // used to cover that, and can't here (it would cut a held note).
            keys::consumeDismissHold();
            break;
        }
        if ((pressed & 2u) && nAlts > 1) {
            altIdx = (altIdx + 1) % nAlts;
            sel = alts[altIdx];
            g.layout.scaleIdx = (uint8_t)sel.scaleIdx;
            g.layout.rootSemis = (uint8_t)sel.rootPc;
            store::markDirty();
            Serial.printf("[listen] nudge %d/%d -> %s (%s)\n", altIdx + 1,
                          nAlts, dsp::kNoteNames[sel.rootPc],
                          dsp::kScales[sel.scaleIdx].shortName);
            until = millis() + kCardMs;  // a full window to judge this one
            nextFrame = now;             // show the new landing immediately
        }
        // ~30 fps, the perform screen's own cadence: fast enough that the
        // scope reads as live, slow enough to leave poll() the loop.
        if ((int32_t)(now - nextFrame) >= 0) {
            nextFrame = now + cfg::kFrameMs;
            waveN = audio::copyScope(wave, (int)(sizeof wave / sizeof wave[0]));
            drawResult(canvas, ctx.guess, ap, sel, prevRoot, prevScale, altIdx,
                       nAlts, appliedBpm, wave, waveN);
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
    return true;  // the card path already resynced, before it started polling
}

}  // namespace

void run(M5Canvas& canvas) {
    // Every path out of this modal needs the keyboard's edge state rebuilt,
    // but WHERE differs: the result card plays notes, so it resyncs before it
    // starts polling and says so. Resyncing again here would clearLeadNotes()
    // and cut a note the player is still holding as the card closes. Paths
    // that never reached the card (cancelled, no mic, no signal) report false
    // and are resynced on the way out, exactly as before.
    // NEVER park/remount the SD mount around this modal, however tempting the
    // ~14 KB looks: measured (v2.8.x bench), every end()+begin() cycle
    // re-seats the mount's allocations inside the largest free region and
    // splits it — three LISTEN cycles walked the largest block from 25.6 KB
    // to 15.9 KB and killed fn+k for the session, while freeing the mount
    // never grew the largest block even once (its pieces are never adjacent).
    // A mount claimed once at boot and left alone keeps the heap layout — and
    // LISTEN's record rounds — identical for the life of the session.
    if (!runModal(canvas)) keys::resync();
}
#endif  // GLIDE_HOST_BUILD

}  // namespace listen_screen
