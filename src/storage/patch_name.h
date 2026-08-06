// Patch-name rules for the SD library — what a human name may contain and what
// it becomes on the card.
//
// PURE C++ (no Arduino, no SD, no M5), for the same reason patch_codec is: the
// rules are fiddly (case, spaces, truncation, trimming) and io/sd_store.cpp
// cannot be host-built, so they live here and env:native tests them.
//
// Names PRESERVE CASE and allow spaces. The card's FatFs has long filenames
// enabled (CONFIG_FATFS_LFN_STACK, max 255), so "Big Bass.gpat" is a perfectly
// legal file — the old all-lowercase slug was a self-imposed limit, not a
// filesystem one. What is NOT negotiable is FAT's case-INSENSITIVE lookup:
// "Big" and "big" are the same file, which is why patchNameEqualsFold exists
// (see the rename path in ui/sd_browser.cpp).
#pragma once

namespace store {

// Longest patch name, sans extension. io/sd_store.h's kMaxNameLen tracks this.
constexpr int kMaxPatchNameLen = 20;

// Reduce a human name to its on-card filename stem, in place-safe fashion
// (`out` must not alias `name`). The rules, in order:
//   - keep A-Z a-z 0-9 space '-' '_'; DROP everything else (FAT-illegal
//     characters, punctuation, control bytes) — case is preserved
//   - runs of spaces collapse to one, and leading/trailing spaces are dropped:
//     a filename cannot usefully differ from another by invisible whitespace
//   - truncate to kMaxPatchNameLen (never leaving a trailing space behind)
//   - never empty -> "patch"
// Idempotent: sanitizePatchName(sanitizePatchName(x)) == sanitizePatchName(x).
// `cap` includes the terminator; out must be >= 6 for the "patch" fallback.
void sanitizePatchName(const char* name, char* out, int cap);

// True if two names would be THE SAME FILE on the card — an ASCII
// case-insensitive compare, because FAT lookup is case-insensitive. Callers
// that are about to delete an "old" file after writing a "new" one must use
// this, not strcmp, or a rename that only changes capitalisation deletes the
// file it just wrote.
bool patchNameEqualsFold(const char* a, const char* b);

}  // namespace store
