// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// The home screen. You live here: oscilloscope centerpiece, note+cents
// readout tracking the lead voice through every glide and bend, mini
// grid-map, transient HUD. run() never returns.
#pragma once
#include <M5Cardputer.h>  // M5Canvas (a typedef — not forward-declarable)

namespace perform {
// Claim the 65 KB frame-buffer sprite. MUST be the first big heap allocation
// of the boot — main.cpp calls it right after audio::begin and BEFORE the SD
// card is ever touched, so no driver residue or fragmentation can ever starve
// it ("UI ALLOC FAILED" was measured twice when the SD mounted first). True =
// the buffer is held for the life of the process.
bool preallocUi();
// The claimed frame buffer (nullptr only if preallocUi failed). The splash
// borrows it — after preallocUi there is no headroom for a second full-screen
// sprite, and there never needs to be: exactly one screen draws at a time.
M5Canvas* uiCanvas();
void run();
}
