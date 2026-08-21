// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// The "stop and configure" screen (tab). Everything not worth a knob in the
// quick-edit layer lives here. Blocking; returns to the perform screen.
#pragma once
#include <M5Cardputer.h>

namespace settings {
void run(M5Canvas& canvas);
}
