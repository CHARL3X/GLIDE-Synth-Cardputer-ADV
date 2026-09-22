// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// gpat_stats — the bad-roll harvest tool. HOST ONLY, never firmware.
//
// Reads every .gpat file in a directory (a card's /glide/ folders, or any
// folder the team dropped saves into) and prints one CSV row per patch:
// name, roll provenance (seed / painted archetype / generator version, when
// the save carries the T_rollProv record), both classifier verdicts, and
// every continuous field. The point: when the team saves 100 deliberately
// BAD rolls, this turns the pile into a spreadsheet that maps each one back
// to the paint window that produced it — range tuning becomes data work
// instead of ear forensics.
//
// Build (any host with g++; no SSE flags on arm64 — see docs):
//   g++ -std=gnu++14 -O2 -DGLIDE_HOST_BUILD -I ../../src \
//       gpat_stats.cpp ../../src/storage/patch_codec.cpp \
//       ../../src/dsp/sound_gen.cpp -o gpat_stats
// Run:
//   ./gpat_stats <directory> [more directories...] > rolls.csv
#include <cstdio>
#include <cstring>
#include <dirent.h>

#include "dsp/sound_gen.h"
#include "storage/patch_codec.h"

namespace {

bool endsWithGpat(const char* n) {
    const size_t len = strlen(n);
    if (len < 5) return false;
    const char* e = n + len - 5;
    return (e[0] == '.') && (e[1] == 'g' || e[1] == 'G') && (e[2] == 'p' || e[2] == 'P') &&
           (e[3] == 'a' || e[3] == 'A') && (e[4] == 't' || e[4] == 'T');
}

void printRow(const char* file, const store::PatchData& pd) {
    dsp::GenPatch g;
    g.synth = pd.synth;
    g.tiltRoute = pd.tiltRoute;
    g.tiltDepth = pd.tiltDepth;
    g.tiltRouteB = pd.tiltRouteB;
    g.tiltDepthB = pd.tiltDepthB;
    const dsp::SynthParams& s = pd.synth;
    printf("%s,%s,", file, pd.name[0] ? pd.name : "(unnamed)");
    if (pd.rollVer != 0) {
        printf("%08x,%s,%u,", pd.rollSeed,
               pd.rollArch < (uint8_t)dsp::Archetype::Count
                   ? dsp::archetypeName((dsp::Archetype)pd.rollArch)
                   : "?",
               pd.rollVer);
        // genver>=5 rolls also expose which style recolored them
        if (pd.rollVer >= 6) printf("%d,", dsp::styleForSeedV6(pd.rollSeed));
        else if (pd.rollVer >= 5) printf("%d,", dsp::styleForSeedV5(pd.rollSeed));
        else printf(",");
    } else {
        printf(",,,,");  // no provenance (hand-built, or predates the record)
    }
    printf("%s,%s,", dsp::archetypeName(dsp::classifySound(s)),
           dsp::archetypeName(dsp::classifySoundV2(s)));
    printf("%s,%u,%u,", dsp::waveformName(s.wave), (unsigned)s.glideMode, s.filterMode);
    printf("%.1f,%.3f,%.4f,%.3f,%.3f,%.3f,%.4f,", s.cutoffHz, s.resonance, s.attackS,
           s.decayS, s.sustain, s.releaseS, s.glideS);
    printf("%.1f,%.4f,%.3f,%.2f,%.3f,%.3f,%.2f,%.1f,%.1f,", s.detuneCents, s.fenvAtkS,
           s.fenvDecS, s.fenvOct, s.subLevel, s.noiseLevel, s.drive, s.autoVibCents,
           s.driftCents);
    printf("%.3f,%.3f,%.3f,%.3f,%u,%.3f,%.3f,", s.chorusDepth, s.delayMix, s.delayTimeS,
           s.delayFb, s.delaySync, s.reverbMix, s.reverbSize);
    printf("%.2f,%u,%u,%.2f,%u,%u,", s.lfo1RateHz, s.lfo1Shape, s.lfo1Sync, s.lfo2RateHz,
           s.lfo2Shape, s.lfo2Sync);
    int nmod = 0;
    for (int i = 0; i < dsp::kModSlots; ++i)
        if (s.slots[i].src != 0) ++nmod;
    printf("%d,%u,%.2f,%u,%.2f\n", nmod, pd.tiltRoute, pd.tiltDepth, pd.tiltRouteB,
           pd.tiltDepthB);
}

int scanDir(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) {
        fprintf(stderr, "gpat_stats: cannot open %s\n", dir);
        return 0;
    }
    int rows = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (!endsWithGpat(e->d_name)) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE* f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "gpat_stats: cannot read %s\n", path);
            continue;
        }
        uint8_t buf[512];
        const size_t len = fread(buf, 1, sizeof buf, f);
        fclose(f);
        store::PatchData pd;  // default-seeded: absent tags keep neutral values
        if (!store::decodePatch(buf, len, pd)) {
            fprintf(stderr, "gpat_stats: %s is not a .gpat stream\n", path);
            continue;
        }
        printRow(e->d_name, pd);
        ++rows;
    }
    closedir(d);
    return rows;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: gpat_stats <directory> [more directories...]\n");
        return 2;
    }
    printf("file,name,rollSeed,rollArch,rollVer,rollStyle,classify,classifyV2,"
           "wave,glideMode,filterMode,cutoffHz,resonance,attackS,decayS,sustain,"
           "releaseS,glideS,detuneCents,fenvAtkS,fenvDecS,fenvOct,subLevel,"
           "noiseLevel,drive,autoVibCents,driftCents,chorusDepth,delayMix,"
           "delayTimeS,delayFb,delaySync,reverbMix,reverbSize,lfo1RateHz,"
           "lfo1Shape,lfo1Sync,lfo2RateHz,lfo2Shape,lfo2Sync,nMods,tiltRoute,"
           "tiltDepth,tiltRouteB,tiltDepthB\n");
    int rows = 0;
    for (int i = 1; i < argc; ++i) rows += scanDir(argv[i]);
    fprintf(stderr, "gpat_stats: %d patches\n", rows);
    return rows > 0 ? 0 : 1;
}
