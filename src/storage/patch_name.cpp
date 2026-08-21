// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "patch_name.h"

namespace store {

namespace {

// Space is handled separately (it collapses and trims), so it is NOT here.
bool allowedChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

char foldChar(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

}  // namespace

void sanitizePatchName(const char* name, char* out, int cap) {
    const int lim = (cap - 1 < kMaxPatchNameLen) ? cap - 1 : kMaxPatchNameLen;
    int n = 0;
    bool pendingSpace = false;  // a space is only committed once a real character
                                // follows it — that is what trims the trailing
                                // ones and collapses runs, in one pass
    for (const char* c = name; *c; ++c) {
        const char ch = *c;
        if (ch == ' ') {
            if (n > 0) pendingSpace = true;  // n == 0 -> still leading: drop it
            continue;
        }
        if (!allowedChar(ch)) continue;  // dropped outright; any spaces AROUND it
                                         // still stand, so "Big / Bass" reads
                                         // "Big Bass" and "Big/Bass" reads "BigBass"
        if (pendingSpace) {
            if (n + 2 > lim) break;  // no room for the space AND the character it
                                     // belongs to -> stop rather than end on a space
            out[n++] = ' ';
            pendingSpace = false;
        }
        if (n + 1 > lim) break;
        out[n++] = ch;
    }
    if (n == 0 && cap > 5) {  // nothing survived -> never write an empty filename
        out[n++] = 'p'; out[n++] = 'a'; out[n++] = 't'; out[n++] = 'c'; out[n++] = 'h';
    }
    out[n] = '\0';
}

bool patchNameEqualsFold(const char* a, const char* b) {
    for (;; ++a, ++b) {
        if (foldChar(*a) != foldChar(*b)) return false;
        if (!*a) return true;
    }
}

}  // namespace store
