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
    int prevApplied;    // applied root the PREVIOUS audible round would have
                        // locked (-1 = none yet) — the stability check
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
// A merely-confident verdict must also be STABLE — two consecutive audible
// rounds agreeing on the applied root — before it may stop the listen: one
// harmonically lopsided section (a long IV vamp) can be sure and wrong.
// Only a near-certain verdict may lock on a single round.
constexpr float kSureConfidence = 0.85f;
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

void drawListening(M5Canvas& c, float frac, int rounds) {
    c.fillScreen(theme::kBg);
    c.setTextDatum(middle_center);
    c.setFont(&fonts::Font4);
    c.setTextColor(theme::kAmber, theme::kBg);
    c.drawString("LISTENING", cfg::kScreenW / 2, 40);
    c.setFont(&fonts::Font0);
    c.setTextColor(theme::kDim, theme::kBg);
    c.drawString(rounds == 0 ? "play the song at me" : "locking in...",
                 cfg::kScreenW / 2, 62);

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
    drawListening(*ctx.c, frac, ctx.rounds);
    const bool bt = backtickHeld();
    const bool cancel = bt && !ctx.btPrev;  // newly pressed only
    ctx.btPrev = bt;
    return !cancel;
}

// One round of evidence. Returns true to keep listening: a single round can
// catch one chord and name ITS key, so a merely-confident verdict must also
// hold steady across two audible rounds before it stops the listen (a
// near-certain one may stop alone). Rounds accumulate NORMALIZED — one round,
// one vote — so a loud chorus can't out-vote quiet honest verses.
bool onSegment(void* user, const int16_t* mono, int n) {
    Ctx& ctx = *(Ctx*)user;
    ++ctx.rounds;
    if (!dsp::segmentAudible(mono, n)) return true;  // silent round: wait for the song
    ctx.heard = true;
    ctx.heardSamples += n;
    ++ctx.audibleRounds;
    if (ctx.beat) dsp::accumulateOnsets(*ctx.beat, mono, n, (float)listen::kRateHz);
    dsp::accumulateChromaNormalized(mono, n, (float)listen::kRateHz, ctx.chroma);
    const int scaleIdx = store::get().layout.scaleIdx;
    ctx.guess = dsp::classifyChromaForScale(ctx.chroma, scaleIdx);
    if (!ctx.guess.valid) return true;

    const int applied =
        dsp::applyRootForScale(ctx.guess.rootPc, ctx.guess.minor, scaleIdx);
    const bool stable = applied == ctx.prevApplied && ctx.audibleRounds >= 2;
    ctx.prevApplied = applied;
    if (ctx.heardSamples < kMinHeardForStop) return true;
    const bool stop = ctx.guess.confidence >= kSureConfidence ||
                      (ctx.guess.confidence >= kEnoughConfidence && stable);
    return !stop;
}

void drawResult(M5Canvas& c, const dsp::KeyGuess& g, const dsp::ListenApply& ap,
                int applied, int prevRoot, int scaleIdx, bool scaleChanged,
                int bpm) {
    c.fillScreen(theme::kBg);

    // Headline: the REFINED verdict (tonic + mode) — "A DOR", not the raw
    // profile winner a modal vamp can mislabel ("D MAJ" for an A Dorian jam).
    char head[24];
    snprintf(head, sizeof head, "%s %s", dsp::kNoteNames[ap.tonicPc],
             dsp::listenModeName(ap.mode));
    c.setTextDatum(top_left);
    c.setFont(&fonts::Font4);
    c.setTextColor(theme::kGreen, theme::kBg);
    c.drawString(head, 12, 8);

    char sub[28];
    if (scaleChanged)
        snprintf(sub, sizeof sub, "-> root %s (%s)", dsp::kNoteNames[applied],
                 dsp::kScales[scaleIdx].shortName);
    else if (applied != ap.tonicPc)
        snprintf(sub, sizeof sub, "-> root %s (your scale)", dsp::kNoteNames[applied]);
    else
        snprintf(sub, sizeof sub, "root %s (%s)", dsp::kNoteNames[applied],
                 dsp::kScales[scaleIdx].shortName);
    c.setFont(&fonts::Font2);
    c.setTextColor(theme::kIdle, theme::kBg);
    c.drawString(sub, 12, 34);
    if (bpm > 0) {  // the jam tempo it locked (only shown when applied)
        char tb[12];
        snprintf(tb, sizeof tb, "%d BPM", bpm);
        c.setTextDatum(top_right);
        c.setTextColor(theme::kGreen, theme::kBg);
        c.drawString(tb, cfg::kScreenW - 12, 34);
        c.setTextDatum(top_left);
    }
    if (g.confidence < 0.3f) {
        c.setFont(&fonts::Font0);
        c.setTextColor(theme::kDim, theme::kBg);
        c.drawString("weak signal - fn+k to nudge", 12, 52);
    }
    if (applied != prevRoot || scaleChanged) {
        c.setFont(&fonts::Font0);
        c.setTextColor(theme::kAmber, theme::kBg);
        c.setTextDatum(top_right);
        c.drawString("RETUNED", cfg::kScreenW - 12, 12);
        c.setTextDatum(top_left);
    }

    // The twelve chroma bars: what the instrument actually heard. Green =
    // the refined tonic, amber = the applied root.
    const int bx = 12, bw = 15, bmax = 46, by0 = 118;
    for (int pc = 0; pc < 12; ++pc) {
        const int x = bx + pc * (bw + 4);
        const int h = 2 + (int)(g.chroma[pc] * (bmax - 2));
        const uint16_t col = pc == ap.tonicPc   ? theme::kGreen
                             : pc == applied    ? theme::kAmber
                                                : theme::kDim;
        c.fillRect(x, by0 - h, bw, h, col);
        c.setFont(&fonts::Font0);
        c.setTextColor(pc == ap.tonicPc ? theme::kGreen : theme::kDim, theme::kBg);
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
    ctx.heard = false;
    ctx.btPrev = backtickHeld();  // swallow a backtick already down at entry

    drawListening(canvas, 0.f, 0);
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
    const dsp::ListenApply ap = dsp::applyListen(prevScale, ctx.guess);
    const int newScale = ap.scaleIdx;
    const int applied = ap.rootPc;
    const bool scaleChanged = newScale != prevScale;
    g.layout.scaleIdx = (uint8_t)newScale;
    g.layout.rootSemis = (uint8_t)applied;
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
        "[listen] raw %s %s conf %.2f (%d rounds) -> %s %s%s%s | root %s (%s) | "
        "tempo %s %.1f conf %.2f%s\n",
        dsp::kNoteNames[ctx.guess.rootPc], ctx.guess.minor ? "min" : "maj",
        ctx.guess.confidence, ctx.rounds, dsp::kNoteNames[ap.tonicPc],
        dsp::listenModeName(ap.mode), ap.modal ? " (modal)" : "",
        ap.tiebreak ? " (tiebreak)" : "", dsp::kNoteNames[applied],
        dsp::kScales[newScale].shortName, tempo.valid ? "ok" : "none", tempo.bpm,
        tempo.confidence, appliedBpm ? " (applied)" : "");

    // Result card: ~1.6 s, backtick skips.
    drawResult(canvas, ctx.guess, ap, applied, prevRoot, newScale, scaleChanged,
               appliedBpm);
    const uint32_t until = millis() + 1600;
    bool btPrev = backtickHeld();
    while ((int32_t)(until - millis()) > 0) {
        const bool bt = backtickHeld();
        if (bt && !btPrev) break;
        btPrev = bt;
        delay(16);
    }
    if (scaleChanged) {
        char v[16];
        snprintf(v, sizeof v, "%s %s", dsp::kNoteNames[applied],
                 dsp::kScales[newScale].shortName);
        hud::show("KEY", v, -1.f);
    } else {
        hud::show("KEY", dsp::kNoteNames[applied], -1.f);
    }
}

}  // namespace listen_screen
