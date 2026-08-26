// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// LISTEN: the hold-fn+k modal. Suspends the synth, records the room,
// detects the song's key, shows the chroma, and retunes the root so the
// player's current scale lands on the song's notes. Blocking, like the
// settings screen; the caller does the keys::resync() hygiene on return.
#pragma once
#include <M5Cardputer.h>

#include "../dsp/key_detect.h"

namespace listen_screen {

void run(M5Canvas& canvas);

// The two screen draws, kept pure — no input, no timing, no hardware — so
// support/viz_render can render both on the host across every palette. They
// are designed screens; they get reviewed as pixels, not as source.
void drawListening(M5Canvas& c, float frac, const float* chroma,
                   const dsp::KeyGuess& guess, int rounds, bool pulse);
void drawResult(M5Canvas& c, const dsp::KeyGuess& g, const dsp::ListenApply& ap,
                const dsp::ListenApply& sel, int prevRoot, int prevScale,
                int altIdx, int altCount, int bpm, const float* wave, int waveN);

}  // namespace listen_screen
