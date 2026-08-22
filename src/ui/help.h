// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// On-device cheat sheet — the manual, in your pocket. A scrollable read-only
// reference for the keymap and gestures, so the instrument explains itself with
// no internet and no printed manual. Blocking; returns to the caller.
#pragma once
#include <M5Cardputer.h>

namespace help {
void run(M5Canvas& canvas);

// The pure page draw + its extent, split out so support/viz_render can render
// the manual on the host (drawPage has no input/timing dependencies).
void drawPage(M5Canvas& canvas, int top);
int lineCount();
int visibleLines();
}
