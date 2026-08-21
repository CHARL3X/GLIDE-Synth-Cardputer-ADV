// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// The personal patch library on microSD — the SD half of "everyone has their
// own sounds." The ten NVS slots (fn+q..p) are fast favourites; this is the
// unlimited collection behind them: roll or tweak a sound, save it to the card
// with an evocative auto-name, and browse them all back later.
//
// HARDWARE BOUNDARY: this is the ONLY code that touches the SD/SPI bus, kept
// here in io/ (M5/Arduino allowed) and out of dsp/. Every file is the SAME
// tagged patch stream the NVS slots use (storage/patch_codec) — so a sound
// saved to a card is byte-compatible with a saved slot and survives firmware
// updates the same way.
//
// FAILURE-VISIBLE (Hard Rule #3): if the card can't mount, available() stays
// false and every call no-ops with lastError() set — the caller shows it. The
// instrument is fully playable without a card; SD only extends the library.
#pragma once
#include <cstdint>

#include "../storage/patch_codec.h"  // store::PatchData + the tagged codec
#include "../storage/patch_name.h"   // the (pure, host-tested) naming rules

namespace sdstore {

constexpr int kMaxNameLen = store::kMaxPatchNameLen;  // patch name, sans extension
constexpr int kMaxList    = 96;  // browser listing cap (bounds RAM use)

// Mount the card. Safe to call more than once. Returns available(). On failure
// lastError() explains why (no card, wrong pins, fs error) — never fatal.
bool begin();
bool available();
const char* lastError();

// Save `pd` as <kSdDir>/<name><kSdExt>, overwriting a same-named file. `name`
// is sanitised to filename-safe chars and truncated to kMaxNameLen. false +
// lastError() on any failure (no card, write error, full).
bool save(const char* name, const store::PatchData& pd);

// Load a patch by name (no extension). `out` MUST be pre-seeded by the caller
// (e.g. from the GLIDE factory patch) so fields the file predates keep a sane
// default — exactly the NVS slot contract. false + lastError() if missing/bad.
bool load(const char* name, store::PatchData& out);

// Delete a patch by name. false + lastError() if it didn't exist or SD is down.
bool remove(const char* name);

// List patch names (no extension) into `names` (each buffer >= kMaxNameLen+1).
// Returns the count written (<= max, <= kMaxList), or -1 on error.
int list(char names[][kMaxNameLen + 1], int max);

// Reduce `name` to the on-card filename stem — CASE-PRESERVING, spaces allowed;
// see store::sanitizePatchName in storage/patch_name.h for the exact rules. For
// the rename preview ("saves as: Big Bass") and collision checks. out must be
// >= kMaxNameLen+1.
void sanitize(const char* name, char* out, int cap);

// True if a patch file with this (sanitised) name already exists on the card.
bool exists(const char* name);

}  // namespace sdstore
