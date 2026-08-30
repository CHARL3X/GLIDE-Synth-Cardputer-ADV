// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Transient parameter HUD: an amber card over the scope for ~1s after any
// change. Rejected changes flash red — a failure you can see, never silence.
#pragma once
#include <M5Cardputer.h>
#include <cstdint>

namespace hud {

// fill01 in 0..1 draws a value bar; pass a negative value for none.
void show(const char* label, const char* value, float fill01);
// A non-empty detail adds a small third line naming the way out ("save to SD,
// then BKSP at boot") and holds the card long enough to read it — an error a
// player can't act on is only half-visible.
void showError(const char* label, const char* value, const char* detail = nullptr);
bool active(uint32_t nowMs);
void draw(M5Canvas& c, uint32_t nowMs);

}  // namespace hud
