// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// keylab_eval — replay a key-lab CSV through the SHIPPED landing. HOST ONLY.
//
// A key-lab build logs, per listen, the song's true key (a human's call) and
// the 12-bin chroma the device accumulated. classifyChroma() is a pure
// function of that chroma, so everything from the chroma onward — the
// Krumhansl verdict, the landing (mode, tonic, pentatonic retreats) and the
// space-key alternates — can be scored on the host against real rooms before
// anything is flashed. The front end (Goertzel bands, floor and harmonic
// subtraction) is NOT replayed: that needs the raw audio.
//
// Scoring is what a player feels, not a label match:
//   nothing sour : every note of the applied scale is in the song's pitch set
//   home right   : the applied root is the song's tonic
//   both         : the landing a player would call correct
// plus, for the alternates, the cumulative count of songs whose FIRST
// in-song-and-home landing sits at position 0..5 of the space walk.
//
// CSV columns (0-based): 4 = true key ("F major", "D dorian", "E mixolydian",
// "Eb minor"; flats accepted), 16..27 = chroma C..B (peak-normalized),
// 28 = title (last column, so commas in titles are harmless). Any other
// columns are ignored, so a lab build may log whatever else it likes.
//
// Build (any host with g++; no SSE flags on arm64 — see docs):
//   g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I ../../src \
//       keylab_eval.cpp ../../src/dsp/key_detect.cpp -o keylab_eval
// Run:
//   ./keylab_eval <keylab.csv> [-v]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "dsp/key_detect.h"
#include "dsp/scales.h"

namespace {

using namespace dsp;

int noteIndex(std::string s) {
    if (s == "Db") s = "C#";
    if (s == "Eb") s = "D#";
    if (s == "Gb") s = "F#";
    if (s == "Ab") s = "G#";
    if (s == "Bb") s = "A#";
    for (int i = 0; i < 12; ++i)
        if (s == kNoteNames[i]) return i;
    return -1;
}

int modeScale(const std::string& m) {
    if (m == "major") return SC_MAJOR;
    if (m == "minor") return SC_MINOR;
    if (m == "dorian") return SC_DORIAN;
    if (m == "mixolydian") return SC_MIXO;
    if (m == "lydian") return SC_LYDIAN;
    return -1;
}

unsigned pcSet(int scale, int root) {
    unsigned m = 0;
    for (int i = 0; i < kScales[scale].len; ++i)
        m |= 1u << ((root + kScales[scale].steps[i]) % 12);
    return m;
}

struct Row {
    std::string title, trueKey;
    int tRoot, tScale;
    float chroma[12];
};

bool parseRow(char* line, Row& r) {
    std::vector<std::string> c;
    for (char* p = strtok(line, ",\n"); p; p = strtok(nullptr, ",\n")) c.push_back(p);
    if (c.size() < 29) return false;
    r.title = c[28];
    r.trueKey = c[4];
    const size_t sp = r.trueKey.find(' ');
    if (sp == std::string::npos) return false;
    r.tRoot = noteIndex(r.trueKey.substr(0, sp));
    r.tScale = modeScale(r.trueKey.substr(sp + 1));
    if (r.tRoot < 0 || r.tScale < 0) return false;
    for (int i = 0; i < 12; ++i) r.chroma[i] = (float)atof(c[16 + i].c_str());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: keylab_eval <keylab.csv> [-v]\n");
        return 2;
    }
    const bool verbose = argc > 2 && strcmp(argv[2], "-v") == 0;
    FILE* f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "keylab_eval: cannot open %s\n", argv[1]);
        return 2;
    }
    std::vector<Row> rows;
    char line[4096];
    if (!fgets(line, sizeof line, f)) return 2;  // header
    int skipped = 0;
    while (fgets(line, sizeof line, f)) {
        Row r;
        if (parseRow(line, r)) rows.push_back(r);
        else ++skipped;
    }
    fclose(f);
    if (skipped) fprintf(stderr, "keylab_eval: skipped %d unparsable rows\n", skipped);

    constexpr int kCap = 8;
    int n = 0, sour = 0, home = 0, both = 0, safe = 0, modal = 0, tie = 0;
    int firstGood[kCap] = {0};
    int firstInSong[kCap] = {0};
    for (const Row& r : rows) {
        const KeyGuess g = classifyChromaSong(r.chroma);
        if (!g.valid) {
            if (verbose) printf("  NO KEY   %s\n", r.title.c_str());
            continue;
        }
        ++n;
        const unsigned tset = pcSet(r.tScale, r.tRoot);
        ListenApply alts[kCap];
        const int na = listenAlternates(g, alts, kCap);
        const ListenApply& a = alts[0];
        const bool in = (pcSet(a.scaleIdx, a.rootPc) & ~tset) == 0;
        const bool hm = a.rootPc == r.tRoot;
        sour += !in;
        home += hm;
        both += in && hm;
        safe += a.safe;
        modal += a.modal;
        tie += a.tiebreak;
        int fg = -1, fi = -1;
        for (int k = 0; k < na; ++k) {
            const bool i2 = (pcSet(alts[k].scaleIdx, alts[k].rootPc) & ~tset) == 0;
            if (fi < 0 && i2) fi = k;
            if (fg < 0 && i2 && alts[k].rootPc == r.tRoot) fg = k;
        }
        if (fg >= 0) ++firstGood[fg];
        if (fi >= 0) ++firstInSong[fi];
        if (verbose) {
            printf("  %-30s true %-13s -> %-2s %-6s mode=%s conf=%.2f%s%s%s  in=%d home=%d firstGood=%d\n",
                   r.title.c_str(), r.trueKey.c_str(), kNoteNames[a.rootPc],
                   kScales[a.scaleIdx].shortName, listenModeName(a.mode), g.confidence,
                   a.safe ? " SAFE" : "", a.modal ? " modal" : "", a.tiebreak ? " tie" : "", in,
                   hm, fg);
        }
    }
    printf("songs=%d  nothing-sour=%d  home-right=%d  both=%d   (safe=%d modal=%d tiebreak=%d)\n",
           n, n - sour, home, both, safe, modal, tie);
    printf("first in-song+home landing, cumulative by space position 0..%d: ", kCap - 1);
    for (int k = 0, c = 0; k < kCap; ++k) {
        c += firstGood[k];
        printf("%d ", c);
    }
    printf("\nfirst merely in-song landing, cumulative:                  ");
    for (int k = 0, c = 0; k < kCap; ++k) {
        c += firstInSong[k];
        printf("%d ", c);
    }
    printf("\n");
    return n > 0 ? 0 : 1;
}
