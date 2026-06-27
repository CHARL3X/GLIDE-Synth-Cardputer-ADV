// A tiny on-device text-entry modal — for naming things (today: renaming an
// SD-library patch). Owns the canvas while open, like help/sd_browser. Reads
// typed characters POSITIONALLY via M5Cardputer.Keyboard.getKeyValue() so it
// honours Hard Rule #4 (positions, not the mutable char 'word'); shift gives the
// upper/symbol character. Keeps the jam/loop backing ticking while open.
#pragma once
#include <M5Cardputer.h>

namespace textentry {

// Edit `buf` in place (pre-filled with the current/suggested name). Returns true
// if confirmed (enter) with the edited text in `buf`, false if cancelled
// (`/tab, buf left unchanged). Editing is capped to the library's name length.
// The caller should re-seed its own key edge-state after (keys::resync()).
bool run(M5Canvas& canvas, const char* title, char* buf, int cap);

}  // namespace textentry
