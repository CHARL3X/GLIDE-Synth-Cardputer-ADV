#include "text_entry.h"

#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../io/keys.h"
#include "../io/looper.h"
#include "../io/sd_store.h"
#include "theme.h"

namespace textentry {

namespace {
// positional key codes (y*14+x) — same convention as keys.cpp
constexpr int kEnter = 41;
constexpr int kBack = 13;    // backspace = delete a char
constexpr int kExit1 = 0;    // `  = cancel
constexpr int kExit2 = 14;   // tab = cancel
constexpr int kFn = 28;
constexpr int kShift = 29;
constexpr int kCtrl = 42;
constexpr int kOpt = 43;
constexpr int kAlt = 44;
constexpr int kKbDrain = 12;  // drain the TCA8418 FIFO so fast typing keeps up

bool isControl(int cd) {
    return cd == kEnter || cd == kBack || cd == kExit1 || cd == kExit2 ||
           cd == kFn || cd == kShift || cd == kCtrl || cd == kOpt || cd == kAlt;
}
}  // namespace

bool run(M5Canvas& canvas, const char* title, char* buf, int cap) {
    char edit[sdstore::kMaxNameLen + 1];
    int len = 0;
    for (const char* c = buf; *c && len < sdstore::kMaxNameLen; ++c) edit[len++] = *c;
    edit[len] = '\0';

    uint64_t prev = ~0ULL;       // treat keys held on entry as already-down
    uint32_t backStart = 0, backLast = 0;  // backspace auto-repeat (hold to clear)
    for (;;) {
        M5Cardputer.update();
        for (int i = 0; i < kKbDrain; ++i) M5Cardputer.Keyboard.updateKeyList();
        const uint32_t now = millis();

        uint64_t cur = 0;
        for (const auto& p : M5Cardputer.Keyboard.keyList()) cur |= 1ULL << (p.y * 14 + p.x);
        const uint64_t pressed = cur & ~prev;
        prev = cur;
        auto hit = [&](int cd) { return (pressed >> cd) & 1ULL; };

        if (hit(kExit1) || hit(kExit2)) return false;       // cancel, buf unchanged
        if (hit(kEnter)) {                                  // confirm
            int n = 0;
            for (; edit[n] && n < cap - 1; ++n) buf[n] = edit[n];
            buf[n] = '\0';
            return true;
        }
        // backspace: delete on tap, and hold to clear fast (DAS/ARR auto-repeat)
        if (hit(kBack)) {
            if (len > 0) edit[--len] = '\0';
            backStart = backLast = now;
        } else if (((cur >> kBack) & 1ULL) && len > 0 &&
                   now - backStart >= cfg::kRepeatDelayMs && now - backLast >= cfg::kRepeatRateMs) {
            edit[--len] = '\0';
            backLast = now;
        }

        // typed characters, read by POSITION (Hard Rule #4): for each key that
        // went down this frame, take its shifted/unshifted value from the map.
        const bool shift = (cur >> kShift) & 1ULL;
        for (const auto& p : M5Cardputer.Keyboard.keyList()) {
            const int cd = p.y * 14 + p.x;
            if (!((pressed >> cd) & 1ULL) || isControl(cd)) continue;
            const KeyValue_t kv = M5Cardputer.Keyboard.getKeyValue(p);
            const char ch = shift ? kv.value_second : kv.value_first;
            if (ch >= 32 && ch < 127 && len < sdstore::kMaxNameLen) {
                edit[len++] = ch;
                edit[len] = '\0';
            }
        }

        looper::tick(now);        // keep a loop / chord progression alive while
        keys::tickBacking(now);   // we type — the backing never freezes under a modal

        // ---- draw ----
        canvas.fillScreen(theme::kBg);
        canvas.fillRect(0, 0, cfg::kScreenW, 14, theme::kPanel);
        canvas.setFont(&fonts::Font0);
        canvas.setTextDatum(top_left);
        canvas.setTextColor(theme::kAmber, theme::kPanel);
        canvas.drawString(title, 5, 3);
        canvas.setTextColor(theme::kDim, theme::kPanel);
        canvas.setTextDatum(top_right);
        canvas.drawString("enter ok  bksp del  ` cancel", cfg::kScreenW - 4, 3);
        canvas.setTextDatum(top_left);

        // the editable name, large, with a blinking cursor
        canvas.setFont(&fonts::Font2);
        canvas.setTextColor(theme::kIdle, theme::kBg);
        canvas.drawString(edit, 10, 46);
        const int cx = 10 + canvas.textWidth(edit);
        if ((now / 400) & 1) canvas.fillRect(cx + 1, 45, 2, 16, theme::kAmber);

        // sanitised filename preview — so "My Bass" -> "my-bass" is no surprise
        char stem[sdstore::kMaxNameLen + 1];
        sdstore::sanitize(edit, stem, sizeof stem);
        canvas.setFont(&fonts::Font0);
        canvas.setTextColor(theme::kDim, theme::kBg);
        char prevLine[40];
        snprintf(prevLine, sizeof prevLine, "saves as: %s", stem);
        canvas.drawString(prevLine, 10, 78);

        canvas.pushSprite(0, 0);
        delay(16);
    }
}

}  // namespace textentry
