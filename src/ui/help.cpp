// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "help.h"

#include <cstring>

#include "../config.h"
#include "theme.h"

#ifndef GLIDE_HOST_BUILD
#include "../io/keys.h"
#include "../io/looper.h"
#endif

namespace help {

namespace {

// positional key codes (y*14+x) — same convention as keys.cpp / settings
constexpr int kUp = 39;     // ;
constexpr int kDown = 53;   // .
constexpr int kExit1 = 0;   // `
constexpr int kExit2 = 14;  // tab

// The manual is a TABLE, not an essay: most rows are a key token (green chip,
// left column) plus what it does (bright, right column), so the page scans.
// Prose only where a concept needs it; asides/continuations sit dim in the
// right column. Every chord spells fn explicitly — "q..p omits holding fn"
// was a real complaint from hardware.
enum Kind : uint8_t {
    kHd,   // amber section title + rule
    kKey,  // key token on a chip + what it does
    kSub,  // dim continuation/aside, aligned under the desc column
    kTxt,  // prose line
    kGap,  // breathing room before a header
};

struct Line {
    Kind kind;
    const char* key;   // kKey only
    const char* text;  // desc / prose / header
};

// Column layout: key tokens ≤13 chars end by x=85 (chip to 87); descs get
// 24 chars at x=92; prose gets 38 at x=7. Font0: 6 px/char, 240 wide.
constexpr int kProseX = 7;
constexpr int kDescX = 92;

const Line kLines[] = {
    {kHd, nullptr, "THE SLIDE"},
    {kTxt, nullptr, "4 rows = a scale. Just press."},
    {kTxt, nullptr, "Hold a note, tap another in the"},
    {kTxt, nullptr, "same row: legato slide up."},
    {kTxt, nullptr, "Release back onto a held key:"},
    {kTxt, nullptr, "slide down. That glide is GLIDE."},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "PLAY KEYS"},
    {kKey, "shift", "hold: off-scale notes"},
    {kKey, "- / =", "octave down / up"},
    {kKey, "[ / ]", "pitch bend down / up"},
    {kKey, "\\", "tap tempo (tap 4x)"},
    {kKey, "fn+\\", "metronome on / off"},
    {kKey, "space", "sustain pedal"},
    {kKey, "ctrl / opt", "volume (hold: ramp)"},
    {kKey, "fn+ctrl/opt", "metronome volume"},
    {kKey, "enter", "tilt: tap cycles,"},
    {kSub, nullptr, "hold locks"},
    {kKey, "bksp", "panic: all notes off"},
    {kKey, "tab", "settings (all the rest)"},
    {kKey, "` hold", "save + restart"},
    {kSub, nullptr, "(Launcher: BTN RST)"},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "KEY & SCALE"},
    {kKey, "fn+k", "key up (match a song)"},
    {kKey, "fn+s", "next scale (the mood)"},
    {kKey, "fn+k hold", "LISTEN: the mic hears"},
    {kSub, nullptr, "the song, retunes you"},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "SOUNDS ON q..p"},
    {kTxt, nullptr, "One LIVE sound + ten slots q..p:"},
    {kTxt, nullptr, "q GLIDE  w ACID  e..i presets,"},
    {kTxt, nullptr, "o p rolled unique to YOUR unit."},
    {kKey, "fn+q..p", "load that slot"},
    {kSub, nullptr, "(unsaved edits drop)"},
    {kKey, "fn+shift+q..p", "save the live sound"},
    {kSub, nullptr, "onto that key"},
    {kTxt, nullptr, "* by the name (top bar) means"},
    {kTxt, nullptr, "unsaved edits - shift-save"},
    {kTxt, nullptr, "onto its key to keep them."},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "QUICK KNOBS"},
    {kKey, "fn+1..0", "grab a live knob"},
    {kSub, nullptr, "keep fn held, then:"},
    {kKey, "- / =", "turn coarse"},
    {kKey, "[ / ]", "turn fine"},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "LOOP"},
    {kKey, "alt tap", "rec > play > overdub"},
    {kKey, "alt hold", "clear the loop"},
    {kKey, "fn+alt", "peel a layer (undo)"},
    {kTxt, nullptr, "It records your PLAYING and"},
    {kTxt, nullptr, "replays it through whatever"},
    {kTxt, nullptr, "sound is live - swap freely."},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "JAM / BACKING"},
    {kTxt, nullptr, "Settings > Jam rows: the bottom"},
    {kTxt, nullptr, "row becomes chords. Tap one to"},
    {kTxt, nullptr, "start a progression, then solo"},
    {kTxt, nullptr, "on the rows above it."},
    {kKey, "fn+a", "arpeggiate the chords:"},
    {kSub, nullptr, "up, down, up/down, off"},
    {kKey, "fn+z / fn+x", "arp rate / octave span"},
    {kSub, nullptr, "(off again on restart)"},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "TILT  (lean the unit)"},
    {kKey, "fwd/back", "morph: blend toward"},
    {kSub, nullptr, "the last sound"},
    {kKey, "left/right", "vibrato"},
    {kTxt, nullptr, "Never pitch bend. Re-route or"},
    {kTxt, nullptr, "deepen it: Settings > Tilt."},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "MAKE A SOUND  (settings)"},
    {kTxt, nullptr, "Randomize: a new sound each tap,"},
    {kTxt, nullptr, "auditioned instantly. Roll away -"},
    {kTxt, nullptr, "it can't hurt a saved slot."},
    {kTxt, nullptr, "Mutate: nudge the sound you have."},
    {kTxt, nullptr, "Undo / Redo: walk your history."},
    {kTxt, nullptr, "Init: a blank canvas."},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "KEEP A SOUND"},
    {kTxt, nullptr, "shift-save it onto q..p (above),"},
    {kTxt, nullptr, "or settings > Save to SD: comes"},
    {kTxt, nullptr, "pre-named, enter accepts. Load"},
    {kTxt, nullptr, "from SD browses your library."},
    {kTxt, nullptr, "Saves survive reboots + updates."},

    {kGap, nullptr, nullptr},
    {kHd, nullptr, "GOOD TO KNOW"},
    {kTxt, nullptr, "Headphones mute the speaker."},
    {kTxt, nullptr, "Sounds live IN the unit - the SD"},
    {kTxt, nullptr, "card is your library + backup."},
};
constexpr int kLineCount = (int)(sizeof(kLines) / sizeof(kLines[0]));
constexpr int kRowH = 11;
constexpr int kVisible = 10;  // rows below the 14 px title bar

}  // namespace

int lineCount() { return kLineCount; }
int visibleLines() { return kVisible; }

// One full frame at a scroll position. Pure draw — host-rendered by
// support/viz_render (render_help.cpp) so the page is reviewed as pixels,
// never designed blind.
void drawPage(M5Canvas& canvas, int top) {
    canvas.fillScreen(theme::kBg);
    canvas.fillRect(0, 0, cfg::kScreenW, 14, theme::kPanel);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(theme::kAmber, theme::kPanel);
    canvas.drawString("HOW TO PLAY", 5, 3);
    canvas.setTextColor(theme::kDim, theme::kPanel);
    canvas.setTextDatum(top_right);
    canvas.drawString(top + kVisible < kLineCount ? "\x1e\x1f scroll  ` back" : "` back",
                      cfg::kScreenW - 4, 3);
    canvas.setTextDatum(top_left);

    for (int row = 0; row < kVisible; ++row) {
        const int i = top + row;
        if (i >= kLineCount) break;
        const int y = 17 + row * kRowH;
        const Line& ln = kLines[i];
        switch (ln.kind) {
            case kHd:
                canvas.setTextColor(theme::kAmber, theme::kBg);
                canvas.drawString(ln.text, 3, y);
                canvas.drawFastHLine(3, y + 9, cfg::kScreenW - 10, theme::kLine);
                break;
            case kKey: {
                const int w = (int)strlen(ln.key) * 6;
                canvas.fillRect(kProseX - 2, y - 1, w + 4, 10, theme::kPanel);
                canvas.setTextColor(theme::kGreen, theme::kPanel);
                canvas.drawString(ln.key, kProseX, y);
                canvas.setTextColor(theme::kIdle, theme::kBg);
                canvas.drawString(ln.text, kDescX, y);
                break;
            }
            case kSub:
                canvas.setTextColor(theme::kDim, theme::kBg);
                canvas.drawString(ln.text, kDescX, y);
                break;
            case kTxt:
                canvas.setTextColor(theme::kIdle, theme::kBg);
                canvas.drawString(ln.text, kProseX, y);
                break;
            case kGap:
                break;
        }
    }

    // scrollbar
    if (kLineCount > kVisible) {
        const int trackY = 17, trackH = kVisible * kRowH;
        int thumbH = trackH * kVisible / kLineCount;
        if (thumbH < 4) thumbH = 4;
        const int thumbY = trackY + (trackH - thumbH) * top / (kLineCount - kVisible);
        canvas.fillRect(cfg::kScreenW - 2, thumbY, 2, thumbH, theme::kDim);
    }
}

#ifndef GLIDE_HOST_BUILD
void run(M5Canvas& canvas) {
    int top = 0;
    uint64_t prev = ~0ULL;  // treat keys held on entry as already-down

    for (;;) {
        M5Cardputer.update();
        uint64_t cur = 0;
        for (const auto& p : M5Cardputer.Keyboard.keyList()) cur |= 1ULL << (p.y * 14 + p.x);
        const uint64_t pressed = cur & ~prev;
        prev = cur;
        auto hit = [&](int cd) { return (pressed >> cd) & 1ULL; };
        auto held = [&](int cd) { return (cur >> cd) & 1ULL; };

        if (hit(kExit1) || hit(kExit2)) break;

        // scroll: ;/. step, hold to run (a long page wants fast scroll)
        static uint32_t lastScroll = 0;
        const uint32_t nowMs = millis();
        int dir = 0;
        if (hit(kUp)) dir = -1;
        else if (hit(kDown)) dir = +1;
        else if (held(kUp) && nowMs - lastScroll > 90) dir = -1;
        else if (held(kDown) && nowMs - lastScroll > 90) dir = +1;
        if (dir) {
            top += dir;
            if (top < 0) top = 0;
            if (top > kLineCount - kVisible) top = kLineCount - kVisible;
            if (top < 0) top = 0;
            lastScroll = nowMs;
        }

        looper::tick(nowMs);       // keep a loop / chord progression alive while
        keys::tickBacking(nowMs);  // reading help — the backing never freezes

        drawPage(canvas, top);
        canvas.pushSprite(0, 0);
        delay(16);
    }
}
#endif  // GLIDE_HOST_BUILD

}  // namespace help
