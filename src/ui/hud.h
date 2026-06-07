// Transient parameter HUD: an amber card over the scope for ~1s after any
// change. Rejected changes flash red — a failure you can see, never silence.
#pragma once
#include <M5Cardputer.h>
#include <cstdint>

namespace hud {

// fill01 in 0..1 draws a value bar; pass a negative value for none.
void show(const char* label, const char* value, float fill01);
void showError(const char* label, const char* value);
bool active(uint32_t nowMs);
void draw(M5Canvas& c, uint32_t nowMs);

}  // namespace hud
