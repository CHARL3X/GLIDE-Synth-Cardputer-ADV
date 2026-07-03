// LISTEN: the hold-fn+k modal. Suspends the synth, records the room,
// detects the song's key, shows the chroma, and retunes the root so the
// player's current scale lands on the song's notes. Blocking, like the
// settings screen; the caller does the keys::resync() hygiene on return.
#pragma once
#include <M5Cardputer.h>

namespace listen_screen {

void run(M5Canvas& canvas);

}  // namespace listen_screen
