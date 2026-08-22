// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// GLIDE NVS fill tool (pio run -e nvsfill -> dist/GLIDE-nvsfill.bin).
//
// Test harness for the STORAGE FULL recovery path, never shipped to players:
// stuffs the shared 16K NVS partition with junk uints (namespace "junk",
// nothing GLIDE reads) until writes fail, so a real unit reproduces the
// full-partition state a well-used device reaches naturally. Then flash
// GLIDE.bin and verify the whole rescue on hardware:
//
//   1. boot -> red STORAGE FULL warning naming the BKSP fix
//   2. hold BKSP through the splash confirm bar -> factory reset
//   3. eraseAllStorage(): whole-partition erase + identity rewrite
//   4. reboot -> no warning; o/p slots and odometer unchanged
//
// Controls: G0 (or `f`) fills until full. `c` clears the junk namespace
// (back out without a factory reset). ` does nothing here — this tool has
// no Launcher exit; power-cycle and flash the next bin.
#include <M5Cardputer.h>
#include <Preferences.h>
#include <nvs.h>

namespace {

M5Canvas gCanvas(&M5Cardputer.Display);

uint32_t gLastWrote = 0;   // junk keys written by the last fill
bool gDidFill = false;

void draw(const char* status, uint16_t statusColor) {
    nvs_stats_t st = {};
    nvs_get_stats(nullptr, &st);

    gCanvas.fillScreen(TFT_BLACK);
    gCanvas.setTextDatum(top_left);
    gCanvas.setFont(&fonts::Font2);
    gCanvas.setTextColor(TFT_ORANGE, TFT_BLACK);
    gCanvas.drawString("GLIDE  NVS FILL TOOL", 8, 4);

    gCanvas.setFont(&fonts::Font0);
    char line[64];
    gCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof line, "partition entries: %u / %u used",
             (unsigned)st.used_entries, (unsigned)st.total_entries);
    gCanvas.drawString(line, 8, 30);
    snprintf(line, sizeof line, "free: %u   namespaces: %u",
             (unsigned)st.free_entries, (unsigned)st.namespace_count);
    gCanvas.drawString(line, 8, 42);
    if (gDidFill) {
        snprintf(line, sizeof line, "junk keys written: %u", (unsigned)gLastWrote);
        gCanvas.drawString(line, 8, 54);
    }

    gCanvas.setTextColor(statusColor, TFT_BLACK);
    gCanvas.drawString(status, 8, 74);

    gCanvas.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    gCanvas.drawString("G0 / f: fill partition to the brim", 8, 100);
    gCanvas.drawString("c: clear the junk again", 8, 112);
    gCanvas.drawString("then: flash GLIDE.bin + reboot", 8, 124);

    gCanvas.pushSprite(0, 0);
}

uint32_t fillJunk() {
    Preferences p;
    if (!p.begin("junk", false)) return 0;
    uint32_t n = 0;
    char key[16];  // NVS keys <= 15 chars
    for (uint32_t i = 0; i < 4000; ++i) {  // 16K partition ~504 entries; 4000 = hard stop
        snprintf(key, sizeof key, "j%04u", (unsigned)i);
        if (p.putUInt(key, 0xC0FFEE00u + i) != sizeof(uint32_t)) break;  // full
        ++n;
        if ((n & 31u) == 0) {
            char msg[40];
            snprintf(msg, sizeof msg, "filling... %u keys", (unsigned)n);
            draw(msg, TFT_YELLOW);
        }
    }
    p.end();
    return n;
}

void clearJunk() {
    Preferences p;
    if (p.begin("junk", false)) {
        p.clear();
        p.end();
    }
}

}  // namespace

void setup() {
    auto mcfg = M5.config();
    M5Cardputer.begin(mcfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(80);
    Serial.begin(115200);

    if (!gCanvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height())) {
        M5Cardputer.Display.fillScreen(TFT_RED);  // no sprite RAM: still visible
    }
    draw("ready - this unit's NVS is untouched", TFT_WHITE);
}

void loop() {
    M5Cardputer.update();

    const bool fill = M5Cardputer.BtnA.wasPressed() ||
                      (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isKeyPressed('f'));
    const bool clear = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isKeyPressed('c');

    if (fill) {
        draw("filling...", TFT_YELLOW);
        gLastWrote = fillJunk();
        gDidFill = true;
        Serial.printf("[nvsfill] wrote %u junk keys\n", (unsigned)gLastWrote);
        draw("FULL. flash GLIDE.bin, reboot,", TFT_RED);
    } else if (clear) {
        clearJunk();
        gDidFill = false;
        Serial.println("[nvsfill] junk namespace cleared");
        draw("junk cleared - partition breathes", TFT_GREEN);
    }

    delay(10);
}
