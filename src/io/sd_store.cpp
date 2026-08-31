// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
#include "sd_store.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <cstdio>
#include <cstring>

#include "../config.h"

namespace sdstore {

namespace {
bool gAvail = false;
uint32_t gRetryAtMs = 0;  // failed-mount backoff: 0 = may try now. Keeps a
                          // card-less unit from paying a full SPI+SD init on
                          // every slot keypress (the fn+q..p path is hot).
const char* gErr = "not started";
void setErr(const char* e) { gErr = e; }

// The card stopped answering mid-session (yanked, or died). Drop the mount so
// the next op past the backoff re-attempts a full SD.begin — reinserting the
// card recovers with zero player action. This is the whole hot-plug story:
// MOUNTED -> (io failure) -> BACKOFF (ops fail in microseconds, no SPI
// traffic) -> RETRY (one mount attempt per kSdRetryMs) -> MOUNTED.
void noteIoFailure(const char* e) {
    SD.end();
    gAvail = false;
    gRetryAtMs = millis() + cfg::kSdRetryMs;
    setErr(e);
}

// "not found" is normal (an empty slot) — unless the slots directory itself
// vanished, which means the card did. Distinguish so an empty slot stays a
// silent factory fallback while a yank flips to BACKOFF.
bool yankCheck() {
    if (SD.exists(cfg::kSdSlotDir)) return false;
    noteIoFailure("card removed");
    return true;
}

// Write bytes as <path> via <path>.tmp + rename, so a failed or interrupted
// write can never destroy the file's previous contents (the old copy survives
// everything except the ~ms remove->rename window). Failures drop the mount.
bool writeAtomic(const char* path, const uint8_t* buf, size_t n) {
    char tmp[72];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) {
        setErr("name too long");
        return false;
    }
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) { noteIoFailure("open for write failed"); return false; }
    const size_t wrote = f.write(buf, n);
    f.close();
    if (wrote != n) {
        SD.remove(tmp);
        noteIoFailure("short write (card full?)");
        return false;
    }
    SD.remove(path);  // FAT rename needs the target clear
    if (!SD.rename(tmp, path)) {
        SD.remove(tmp);
        noteIoFailure("rename failed");
        return false;
    }
    return true;
}

// Read a whole small file into buf. Returns length, 0 = not found (soft),
// -1 = io/size error.
int readAll(const char* path, uint8_t* buf, size_t cap) {
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        return 0;
    }
    const size_t len = (size_t)f.size();
    if (len == 0 || len > cap) { f.close(); setErr("bad file size"); return -1; }
    const size_t got = f.read(buf, len);
    f.close();
    if (got != len) { noteIoFailure("read error"); return -1; }
    return (int)len;
}

// <kSdSlotDir>/<slot>.gpat — position-addressed; the display name travels
// INSIDE the PatchData, so library renames can never orphan a slot.
bool slotPath(int slot, char* out, int cap) {
    if (slot < 0 || slot > 9) return false;
    return snprintf(out, cap, "%s/%d%s", cfg::kSdSlotDir, slot, cfg::kSdExt) < cap;
}

// Build "<kSdDir>/<sanitised name><kSdExt>" into out (uses sanitize()).
// Returns false if it couldn't fit.
bool makePath(const char* name, char* out, int cap) {
    char safe[kMaxNameLen + 1];
    sanitize(name, safe, sizeof safe);
    const int need = (int)(strlen(cfg::kSdDir) + 1 + strlen(safe) + strlen(cfg::kSdExt) + 1);
    if (need > cap) return false;
    strcpy(out, cfg::kSdDir);
    strcat(out, "/");
    strcat(out, safe);
    strcat(out, cfg::kSdExt);
    return true;
}

// Basename: the part after the last '/'. SD's File::name() returns either a
// full path or a bare name depending on the core version — handle both.
const char* baseName(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool endsWithExt(const char* name) {
    const size_t ln = strlen(name), le = strlen(cfg::kSdExt);
    return ln >= le && strcmp(name + ln - le, cfg::kSdExt) == 0;
}
}  // namespace

bool begin() {
    if (!cfg::kSdEnabled) { setErr("SD disabled"); return false; }
    if (gAvail) return true;
    // Backoff fast path: a recent mount failure means "no card right now" —
    // answer in microseconds instead of re-running the SPI+SD init handshake
    // (which every op used to pay, per keypress, on a card-less unit).
    if (gRetryAtMs != 0 && (int32_t)(millis() - gRetryAtMs) < 0) return false;
    // Drive the SD's SPI lines explicitly (the bus is shared with the display;
    // these pins are the hardware-unverified part — see config.h).
    SPI.begin(cfg::kSdSckPin, cfg::kSdMisoPin, cfg::kSdMosiPin, cfg::kSdCsPin);
    // max_files=2, not the default 5: FATFS keeps a 4 KB sector buffer per
    // potential open file plus one for the drive, so the default mount holds
    // 27,612 B of heap FOR THE WHOLE SESSION (measured on hardware, v2.8) —
    // which starved LISTEN's capture buffer ("no memory" on fn+k). GLIDE
    // never has more than one file genuinely open (writeAtomic, load, and the
    // dir sweeps all open one at a time; directory handles live outside this
    // pool), so 2 is one real slot plus margin: a ~13.9 KB mount, and cheap
    // enough that mid-session remounts can't fail for want of contiguous heap.
    if (!SD.begin(cfg::kSdCsPin, SPI, cfg::kSdFreqHz, "/sd", 2)) {
        setErr("no card / SD init failed");
        gAvail = false;
        gRetryAtMs = millis() + cfg::kSdRetryMs;
        return false;
    }
    if ((!SD.exists(cfg::kSdDir) && !SD.mkdir(cfg::kSdDir)) ||
        (!SD.exists(cfg::kSdSlotDir) && !SD.mkdir(cfg::kSdSlotDir))) {
        setErr("cannot create /glide");
        gAvail = false;
        gRetryAtMs = millis() + cfg::kSdRetryMs;
        return false;
    }
    // Sweep tmp orphans from writes a power cut interrupted — the final files
    // they were about to replace are intact by construction (writeAtomic).
    File dir = SD.open(cfg::kSdSlotDir);
    if (dir && dir.isDirectory()) {
        char victims[8][32];
        int nv = 0;
        for (File e = dir.openNextFile(); e && nv < 8; e = dir.openNextFile()) {
            const char* bn = baseName(e.name());
            const size_t ln = strlen(bn);
            if (!e.isDirectory() && ln > 4 && strcmp(bn + ln - 4, ".tmp") == 0 &&
                ln < sizeof victims[0] - 16) {
                snprintf(victims[nv++], sizeof victims[0], "%s/%s", cfg::kSdSlotDir, bn);
            }
            e.close();
        }
        dir.close();
        for (int i = 0; i < nv; ++i) SD.remove(victims[i]);
    } else if (dir) {
        dir.close();
    }
    gAvail = true;
    gRetryAtMs = 0;
    setErr("ok");
    return true;
}

void end() {
    if (gAvail) SD.end();
    gAvail = false;
    gRetryAtMs = 0;  // an end() is deliberate, not a failure — no backoff
    setErr("not started");
}

bool available() { return gAvail; }
const char* lastError() { return gErr; }

// The rules themselves are pure and host-tested — this is the seam that lets
// env:native cover them (io/ can never be compiled on the host).
void sanitize(const char* name, char* out, int cap) {
    store::sanitizePatchName(name, out, cap);
}

bool exists(const char* name) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!makePath(name, path, sizeof path)) return false;
    return SD.exists(path);
}

bool save(const char* name, const store::PatchData& pd) {
    if (!gAvail && !begin()) return false;
    uint8_t buf[512];
    const size_t n = store::encodePatch(pd, buf, sizeof buf);
    if (n == 0) { setErr("encode failed"); return false; }
    char path[64];
    if (!makePath(name, path, sizeof path)) { setErr("name too long"); return false; }
    if (!writeAtomic(path, buf, n)) return false;  // a failed save keeps the old file
    setErr("saved");
    return true;
}

bool load(const char* name, store::PatchData& out) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!makePath(name, path, sizeof path)) { setErr("name too long"); return false; }
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) { if (f) f.close(); setErr("not found"); return false; }
    uint8_t buf[512];
    const size_t len = (size_t)f.size();
    if (len == 0 || len > sizeof buf) { f.close(); setErr("bad file size"); return false; }
    const size_t got = f.read(buf, len);
    f.close();
    if (got != len) { setErr("read error"); return false; }
    if (!store::decodePatch(buf, len, out)) { setErr("corrupt patch"); return false; }
    setErr("loaded");
    return true;
}

bool remove(const char* name) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!makePath(name, path, sizeof path)) { setErr("name too long"); return false; }
    if (!SD.exists(path)) { setErr("not found"); return false; }
    if (!SD.remove(path)) { setErr("delete failed"); return false; }
    setErr("deleted");
    return true;
}

// ---- the ten performance slots ---------------------------------------------

bool slotSave(int slot, const store::PatchData& pd) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!slotPath(slot, path, sizeof path)) { setErr("bad slot"); return false; }
    uint8_t buf[512];
    const size_t n = store::encodePatch(pd, buf, sizeof buf);
    if (n == 0) { setErr("encode failed"); return false; }
    if (!writeAtomic(path, buf, n)) return false;
    setErr("saved");
    return true;
}

bool slotLoad(int slot, store::PatchData& out) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!slotPath(slot, path, sizeof path)) { setErr("bad slot"); return false; }
    uint8_t buf[512];
    const int len = readAll(path, buf, sizeof buf);
    if (len == 0) {  // empty slot is normal — unless the whole dir vanished
        if (!yankCheck()) setErr("not found");
        return false;
    }
    if (len < 0) return false;
    if (!store::decodePatch(buf, (size_t)len, out)) {
        // Never auto-delete player data: the file stays for inspection, the
        // caller's factory seed plays, the next save overwrites it atomically.
        setErr("corrupt patch");
        Serial.printf("[sd] slot %d file is corrupt — playing the fallback\n", slot);
        return false;
    }
    setErr("loaded");
    return true;
}

bool slotExists(int slot) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!slotPath(slot, path, sizeof path)) return false;
    return SD.exists(path);
}

bool slotRemove(int slot) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!slotPath(slot, path, sizeof path)) return false;
    if (!SD.exists(path)) return true;  // clearing an empty slot is a success
    return SD.remove(path);
}

// ---- boot-heal mirrors ------------------------------------------------------

namespace {
bool mirrorPatchPath(char* out, int cap) {
    return snprintf(out, cap, "%s/live%s", cfg::kSdSlotDir, cfg::kSdExt) < cap;
}
bool rigPath(char* out, int cap) {
    return snprintf(out, cap, "%s/rig.cfg", cfg::kSdDir) < cap;
}
}  // namespace

bool liveMirrorSave(const store::PatchData& pd) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!mirrorPatchPath(path, sizeof path)) return false;
    uint8_t buf[512];
    const size_t n = store::encodePatch(pd, buf, sizeof buf);
    if (n == 0) { setErr("encode failed"); return false; }
    return writeAtomic(path, buf, n);
}

bool liveMirrorLoad(store::PatchData& out) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!mirrorPatchPath(path, sizeof path)) return false;
    uint8_t buf[512];
    const int len = readAll(path, buf, sizeof buf);
    if (len <= 0) return false;
    return store::decodePatch(buf, (size_t)len, out);
}

bool liveMirrorRemove() {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!mirrorPatchPath(path, sizeof path)) return false;
    if (!SD.exists(path)) return true;
    return SD.remove(path);
}

bool rigMirrorWrite(const uint8_t* buf, size_t n) {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!rigPath(path, sizeof path)) return false;
    return writeAtomic(path, buf, n);
}

int rigMirrorRead(uint8_t* buf, size_t cap) {
    if (!gAvail && !begin()) return -1;
    char path[64];
    if (!rigPath(path, sizeof path)) return -1;
    const int len = readAll(path, buf, cap);
    return len <= 0 ? -1 : len;
}

bool rigMirrorRemove() {
    if (!gAvail && !begin()) return false;
    char path[64];
    if (!rigPath(path, sizeof path)) return false;
    if (!SD.exists(path)) return true;
    return SD.remove(path);
}

int list(char names[][kMaxNameLen + 1], int max) {
    if (!gAvail && !begin()) return -1;
    File dir = SD.open(cfg::kSdDir);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        noteIoFailure("no library dir");  // the dir existed at mount: card gone
        return -1;
    }
    int count = 0;
    const int cap = (max < kMaxList) ? max : kMaxList;
    for (File e = dir.openNextFile(); e && count < cap; e = dir.openNextFile()) {
        if (!e.isDirectory()) {
            const char* bn = baseName(e.name());
            if (endsWithExt(bn)) {
                int k = 0;
                const int stop = (int)strlen(bn) - (int)strlen(cfg::kSdExt);  // drop ".gpat"
                for (; k < stop && k < kMaxNameLen; ++k) names[count][k] = bn[k];
                names[count][k] = '\0';
                ++count;
            }
        }
        e.close();
    }
    dir.close();
    setErr("ok");
    return count;
}

}  // namespace sdstore
