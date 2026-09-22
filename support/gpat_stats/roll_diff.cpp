// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// roll_diff — the rated rolls audit the next generator version. HOST ONLY.
//
// A rating session's lab.csv lists every Randomize press with its rating,
// seed and painted archetype. Re-roll each seed through the version that was
// rated (V5) and the version under tuning (V6), and report which fields
// moved. The contract this enforces before a second field round is spent:
//   - every roll the ear called BAD (rating 1) must change on at least one
//     field — a tuning round that leaves a bad roll untouched did nothing
//     for it;
//   - every roll the ear called GREAT (rating 4) must be bit-identical —
//     the tuning must not touch what already worked.
// Exit status 1 if either contract fails, so it can gate a build.
//
// CSV columns (0-based, header row first): 0 = rating (1..4), 4 = seed as
// 8 hex digits, 6 = archetype index, 7 = archetype name (for the report).
//
// Build (any host with g++; no SSE flags on arm64):
//   g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I ../../src \
//       roll_diff.cpp ../../src/dsp/sound_gen.cpp -o roll_diff
// Run:
//   ./roll_diff <lab.csv> [-v]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dsp/sound_gen.h"

namespace {

struct Field {
    const char* name;
    float dsp::SynthParams::*p;
    float tol;
};

const Field kFields[] = {
    {"cutoffHz", &dsp::SynthParams::cutoffHz, 0.5f},
    {"resonance", &dsp::SynthParams::resonance, 0.002f},
    {"attackS", &dsp::SynthParams::attackS, 0.001f},
    {"decayS", &dsp::SynthParams::decayS, 0.002f},
    {"sustain", &dsp::SynthParams::sustain, 0.002f},
    {"releaseS", &dsp::SynthParams::releaseS, 0.002f},
    {"glideS", &dsp::SynthParams::glideS, 0.001f},
    {"detuneCents", &dsp::SynthParams::detuneCents, 0.1f},
    {"fenvOct", &dsp::SynthParams::fenvOct, 0.01f},
    {"fenvDecS", &dsp::SynthParams::fenvDecS, 0.002f},
    {"subLevel", &dsp::SynthParams::subLevel, 0.002f},
    {"noiseLevel", &dsp::SynthParams::noiseLevel, 0.002f},
    {"drive", &dsp::SynthParams::drive, 0.01f},
    {"autoVibCents", &dsp::SynthParams::autoVibCents, 0.1f},
    {"chorusDepth", &dsp::SynthParams::chorusDepth, 0.002f},
    {"delayMix", &dsp::SynthParams::delayMix, 0.002f},
    {"reverbMix", &dsp::SynthParams::reverbMix, 0.002f},
    {"reverbSize", &dsp::SynthParams::reverbSize, 0.002f},
    {"lfo1RateHz", &dsp::SynthParams::lfo1RateHz, 0.01f},
};
constexpr int kFieldCount = (int)(sizeof(kFields) / sizeof(kFields[0]));

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: roll_diff <lab.csv> [-v]\n");
        return 2;
    }
    const bool verbose = argc > 2 && strcmp(argv[2], "-v") == 0;
    FILE* f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "roll_diff: cannot open %s\n", argv[1]);
        return 2;
    }
    char line[1024];
    if (!fgets(line, sizeof line, f)) return 2;  // header
    int rows = 0, moved = 0, badTotal = 0, badMoved = 0, greatTotal = 0, greatMoved = 0;
    int perRating[5] = {0}, perRatingMoved[5] = {0};
    while (fgets(line, sizeof line, f)) {
        char* col[12] = {nullptr};
        int nc = 0;
        for (char* p = strtok(line, ",\n"); p && nc < 12; p = strtok(nullptr, ",\n")) col[nc++] = p;
        if (nc < 8) continue;
        const int rating = atoi(col[0]);
        const uint32_t seed = (uint32_t)strtoul(col[4], nullptr, 16);
        const int arch = atoi(col[6]);
        if (rating < 1 || rating > 4 || arch < 0 || arch >= (int)dsp::Archetype::Count) continue;
        ++rows;
        const dsp::Archetype a = (dsp::Archetype)arch;
        const dsp::SynthParams v5 = dsp::generateSoundV5(seed, a).synth;
        const dsp::SynthParams v6 = dsp::generateSoundV6(seed, a).synth;
        int nd = 0;
        char detail[512] = {0};
        for (int i = 0; i < kFieldCount; ++i) {
            const float x = v5.*(kFields[i].p), y = v6.*(kFields[i].p);
            if (fabsf(x - y) > kFields[i].tol) {
                ++nd;
                char one[64];
                snprintf(one, sizeof one, " %s %.3g->%.3g", kFields[i].name, x, y);
                strncat(detail, one, sizeof detail - strlen(detail) - 1);
            }
        }
        if (v5.wave != v6.wave) {
            ++nd;
            strncat(detail, " wave", sizeof detail - strlen(detail) - 1);
        }
        ++perRating[rating];
        if (nd) ++perRatingMoved[rating];
        if (nd) ++moved;
        if (rating == 1) {
            ++badTotal;
            if (nd) ++badMoved;
        }
        if (rating == 4) {
            ++greatTotal;
            if (nd) ++greatMoved;
        }
        if (verbose || (rating == 1 && !nd) || (rating == 4 && nd))
            printf("%d %-8s %08x style %d: %s%s\n", rating, col[7], seed, dsp::styleForSeedV6(seed),
                   nd ? "moved" : "UNCHANGED", detail);
    }
    fclose(f);
    printf("rolls=%d moved=%d | bad moved %d/%d | great unchanged %d/%d | meh moved %d/%d | good moved %d/%d\n",
           rows, moved, badMoved, badTotal, greatTotal - greatMoved, greatTotal,
           perRatingMoved[2], perRating[2], perRatingMoved[3], perRating[3]);
    const bool ok = rows > 0 && badMoved == badTotal && greatMoved == 0;
    if (!ok) fprintf(stderr, "roll_diff: CONTRACT FAILED\n");
    return ok ? 0 : 1;
}
