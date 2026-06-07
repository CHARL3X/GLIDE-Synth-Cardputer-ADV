#include "hud.h"

#include <cstring>

#include "../config.h"
#include "theme.h"

namespace hud {

namespace {
char gLabel[20] = "";
char gValue[20] = "";
float gFill = -1.f;
bool gError = false;
uint32_t gUntil = 0;
uint32_t gShownAt = 0;
}  // namespace

void show(const char* label, const char* value, float fill01) {
    strncpy(gLabel, label, sizeof gLabel - 1);
    strncpy(gValue, value, sizeof gValue - 1);
    gLabel[sizeof gLabel - 1] = gValue[sizeof gValue - 1] = '\0';
    gFill = fill01;
    gError = false;
    gShownAt = millis();
    gUntil = gShownAt + cfg::kHudMs;
}

void showError(const char* label, const char* value) {
    show(label, value, -1.f);
    gError = true;
    gUntil = gShownAt + cfg::kHudErrMs;
}

bool active(uint32_t nowMs) {
    return nowMs < gUntil;
}

void draw(M5Canvas& c, uint32_t nowMs) {
    if (!active(nowMs)) return;

    // fade toward background over the last 200 ms
    const uint32_t remain = gUntil - nowMs;
    const uint8_t fade = remain < 200 ? (uint8_t)(255 - remain * 255 / 200) : 0;

    const uint16_t frame = theme::blend(gError ? theme::kRed : theme::kAmber, theme::kBg, fade);
    const uint16_t text = theme::blend(gError ? theme::kRed : theme::kIdle, theme::kBg, fade);
    const uint16_t labelCol = theme::blend(gError ? theme::kRed : theme::kAmberDim, theme::kBg, fade);

    const int w = 150, h = (gFill >= 0.f) ? 44 : 36;
    const int x = (cfg::kScreenW - w) / 2, y = 36;

    c.fillRoundRect(x, y, w, h, 4, theme::kPanel);
    c.drawRoundRect(x, y, w, h, 4, frame);

    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextDatum(top_left);
    c.setTextColor(labelCol, theme::kPanel);
    c.drawString(gLabel, x + 8, y + 6);

    c.setFont(&fonts::Font2);
    c.setTextColor(text, theme::kPanel);
    c.drawString(gValue, x + 8, y + 16);

    if (gFill >= 0.f) {
        const int bx = x + 8, by = y + h - 10, bw = w - 16, bh = 4;
        c.drawRect(bx, by, bw, bh, theme::kLine);
        int fw = (int)((bw - 2) * (gFill > 1.f ? 1.f : gFill));
        if (fw > 0) c.fillRect(bx + 1, by + 1, fw, bh - 2, frame);
    }
}

}  // namespace hud
