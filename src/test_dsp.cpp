// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (C) 2026 Charles Tobin (CHARL3X)
// Host-side sanity tests for the pure DSP core (pio run -e native).
// This compiling and passing on a PC is the proof that dsp/ has no hardware
// dependencies -- the porting boundary the whole architecture promises.
#ifdef GLIDE_HOST_BUILD

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "dsp/audition_plan.h"
#include "dsp/beat_detect.h"
#include "dsp/demo_gen.h"
#include "dsp/key_detect.h"
#include "dsp/morph.h"
#include "dsp/patches.h"
#include "dsp/pitch.h"
#include "dsp/quantize.h"
#include "dsp/sound_gen.h"
#include "dsp/synth.h"
#include "storage/patch_codec.h"  // host-safe codec (in env:native build_src_filter)
#include "storage/patch_name.h"   // host-safe SD naming rules (same build filter)
#include "ui/theme.h"             // host-safe palette derivation (same build filter)
#include "dsp/arp.h"
#include "dsp/fx.h"
#include "storage/glide_config.h"  // host-safe header: the TriggerAction enum + its names

using namespace dsp;

static int failures = 0;

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            failures++;                                    \
        }                                                  \
    } while (0)

static constexpr float kSr = 32000.f;
static constexpr int kBlock = 128;

static float peakOf(Synth& s, int blocks) {
    float buf[kBlock];
    float peak = 0.f;
    for (int b = 0; b < blocks; ++b) {
        s.render(buf, kBlock);
        for (int i = 0; i < kBlock; ++i) {
            const float a = fabsf(buf[i]);
            if (!std::isfinite(buf[i])) {
                CHECK(false, "non-finite sample");
                return -1.f;
            }
            if (a > peak) peak = a;
        }
    }
    return peak;
}

// Steps dsp::Arp for `blocks` render blocks and records every note-on (block
// index, pitch, id). *orderOk = every On was preceded by the previous note's
// Off and every event was flagged backing — a run, never a pile-up.
struct ArpOn { int block; float pitch; uint8_t id; };
static int runArp(Arp& a, int blocks, ArpOn* out, int cap, bool* orderOk, float bpm = 120.f) {
    int n = 0, sounding = 0;
    bool ok = true;
    for (int b = 0; b < blocks; ++b) {
        NoteEvent ev[4];
        const int k = a.advance(kBlock, kSr, bpm, ev, 4);
        for (int i = 0; i < k; ++i) {
            if (ev[i].type == NoteEvent::On) {
                if (sounding > 0 || !ev[i].backing) ok = false;
                ++sounding;
                if (n < cap) { out[n].block = b; out[n].pitch = ev[i].pitchMidi; out[n].id = ev[i].id; }
                ++n;
            } else if (ev[i].type == NoteEvent::Off) {
                --sounding;
            } else {
                ok = false;
            }
        }
    }
    if (orderOk) *orderOk = ok && sounding <= 1;
    return n;
}
static bool arpSeq(const ArpOn* on, int n, const float* want, int m) {
    if (n < m) return false;
    for (int i = 0; i < m; ++i)
        if (fabsf(on[i].pitch - want[i]) > 1e-4f) return false;
    return true;
}

// Mirrors ui/audition.cpp exactly: the fixed lick's notes and ids under the
// patch's own adaptive clock (dsp/audition_plan.h — the phrase stretches and
// the final note holds until THIS sound's character has arrived). Returns the
// phrase peak plus the final note's state just before its release, for the
// pitch-landing and audibility invariants. Leaves the synth silenced.
struct LickResult {
    float peak;
    float finalErr;    // final-note pitch error vs its target, in semitones
    bool finalActive;  // the final note still sounded at its release
};
static LickResult walkAuditionLick(Synth& sp, const GenPatch& g) {
    const AuditionPlan plan = planAudition(g.synth);
    static const uint16_t kBaseAt[9] = {0, 300, 640, 1000, 1000, 1380, 1860, 1860, 2600};
    uint16_t at[9];
    for (int i = 0; i < 9; ++i) at[i] = (uint16_t)(kBaseAt[i] * plan.stretch + 0.5f);
    at[8] = (uint16_t)(at[7] + plan.finalHoldMs);

    sp.setParams(g.synth);
    LickResult r;
    r.peak = 0.f;
    float buf[kBlock];
    int cursor = 0;
    auto runTo = [&](int ms) {  // 128-sample blocks at 32 kHz = 4 ms each
        for (; cursor + 4 <= ms; cursor += 4) {
            sp.render(buf, kBlock);
            for (int i = 0; i < kBlock; ++i) {
                const float a = fabsf(buf[i]);
                if (a > r.peak) r.peak = a;
            }
        }
    };
    sp.handleEvent(NoteEvent::make(NoteEvent::On, 251, 0xFF, false, 52.f));
    runTo(at[1]);
    sp.handleEvent(NoteEvent::make(NoteEvent::Retarget, 251, 0xFF, false, 59.f));
    runTo(at[2]);
    sp.handleEvent(NoteEvent::make(NoteEvent::Retarget, 251, 0xFF, false, 55.f));
    runTo(at[3]);
    sp.handleEvent(NoteEvent::make(NoteEvent::Off, 251, 0xFF, false, 0.f));
    sp.handleEvent(NoteEvent::make(NoteEvent::On, 252, 0xFF, false, 64.f));
    runTo(at[5]);
    sp.handleEvent(NoteEvent::make(NoteEvent::Retarget, 252, 0xFF, false, 71.f));
    runTo(at[6]);
    sp.handleEvent(NoteEvent::make(NoteEvent::Off, 252, 0xFF, false, 0.f));
    sp.handleEvent(NoteEvent::make(NoteEvent::On, 253, 0xFF, false, 60.f));
    runTo(at[8]);
    r.finalActive = sp.leadActive();
    r.finalErr = sp.leadPitchMidi() - 60.f;
    sp.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    runTo(cursor + 240);  // drain the release
    return r;
}

int main() {
    // ---- pitch / layout math -------------------------------------------
    Layout l;
    l.octave = 4;  // pin oct 4 here so the A4=69 reference math below stays put
                   // (the shipped default is oct 3; the math is the same shifted)
    CHECK(fabsf(gridToMidi(l, 0, 0, false) - 69.f) < 1e-4, "string0 col0 = A4 (69)");
    CHECK(fabsf(gridToMidi(l, 0, 1, false) - 72.f) < 1e-4, "A min pent degree 1 = C5");
    CHECK(fabsf(gridToMidi(l, 0, 5, false) - 81.f) < 1e-4, "degree 5 wraps the octave = A5");
    CHECK(rowDegrees(l) == 2, "pentatonic row interval = 2 degrees (a fourth)");
    CHECK(fabsf(gridToMidi(l, 1, 0, false) - gridToMidi(l, 0, 2, false)) < 1e-4,
          "string 1 starts 2 degrees up");
    CHECK(fabsf(gridToMidi(l, 0, 1, true) - 70.f) < 1e-4, "chromatic override = semitones");
    CHECK(fabsf(midiToFreq(69.f) - 440.f) < 0.01f, "A4 = 440 Hz");

    char name[8];
    int cents;
    midiToNoteCents(69.3f, name, sizeof name, cents);
    CHECK(name[0] == 'A' && cents == 30, "note+cents readout");

    // ---- diatonic chord builder (the auto-progression backing) ----------
    {
        float ch[3];
        // A min pent (default), lock on: triads come from the HARMONY PARENT
        // (natural minor), in the backing register — jamOctave +1 by default,
        // an octave OVER the grid. base = 12*(4+1)+9+12 = 81 (A5).
        // deg 0 -> snap to minor degree 0 -> minor triad 0,3,7 = A5,C6,E6.
        const int nc = chordPitches(l, 0, 0, false, ch, 3);
        CHECK(nc == 3, "chord builds three tones");
        CHECK(fabsf(ch[0] - 81.f) < 1e-4, "chord root = A5 (the +1 jam octave over A4)");
        CHECK(fabsf(ch[1] - 84.f) < 1e-4 && fabsf(ch[2] - 88.f) < 1e-4,
              "min pent backing = a real minor triad (not a quartal pile)");
        // The backing register is a layout knob: -2..+2 octaves over the grid.
        {
            Layout lj = l;
            CHECK(fabsf(backingShift(lj) - 12.f) < 1e-4, "default backing = +1 oct");
            lj.jamOctave = -1;
            chordPitches(lj, 0, 0, false, ch, 3);
            CHECK(fabsf(ch[0] - 57.f) < 1e-4, "jam oct -1 = the old bass pad (A3)");
            lj.jamOctave = 0;
            chordPitches(lj, 0, 0, false, ch, 3);
            CHECK(fabsf(ch[0] - 69.f) < 1e-4, "jam oct 0 = on the grid (A4)");
            // Going LOW is never folded: a player who wants mud asked for mud.
            lj.jamOctave = -2; lj.octave = 1;
            CHECK(fabsf(backingShift(lj) + 24.f) < 1e-4, "jam oct -2 at oct 1 stays -2");
            // The ceiling: the shift folds down an octave at a time until the
            // grid's base note sits under C7, and never below -1 oct.
            lj.jamOctave = 1; lj.octave = 5;   // base A5 (81) + 12 = 93 <= 96
            CHECK(fabsf(backingShift(lj) - 12.f) < 1e-4, "oct 5 + 1: no fold");
            lj.octave = 6;                     // base A6 (93) + 12 = 105 > 96
            CHECK(fabsf(backingShift(lj)) < 1e-4, "oct 6 + 1 folds to the grid");
            lj.octave = 7; lj.jamOctave = 2;   // base A7 (105): 129/117/105 all > 96
            CHECK(fabsf(backingShift(lj) + 12.f) < 1e-4, "oct 7 + 2 folds to -1 and stops");
            chordPitches(lj, 0, 0, false, ch, 3);
            CHECK(ch[0] <= kBackingCeilMidi, "a folded chord roots under the ceiling");
            // One fold per LAYOUT: every cell in a progression shares the
            // register, so I-IV-V never inverts across the fold.
            lj.octave = 6; lj.jamOctave = 1;
            float lo[3], hi[3];
            chordPitches(lj, 0, 0, false, lo, 3);
            chordPitches(lj, 0, 9, false, hi, 3);
            CHECK(hi[0] > lo[0], "far-right step still sits above the root step");
        }
        CHECK(ch[1] > ch[0] && ch[2] > ch[1], "chord tones ascend");
        // chromatic fallback (lock off / shift) = power voicing root+5th+8ve
        chordPitches(l, 0, 0, true, ch, 3);
        CHECK(fabsf(ch[1] - ch[0] - 7.f) < 1e-4, "chromatic chord has a fifth");
        CHECK(fabsf(ch[2] - ch[0] - 12.f) < 1e-4, "chromatic chord has an octave");
        CHECK(pitchClassName(57.f)[0] == 'A', "pitch-class label");

        // Blues: the ♭5 "blue note" must never end up a chord tone — every
        // backing triad is a consonant natural-minor triad (root, ♭3, 5), the
        // same notes a min-pent progression makes, so solo-in-blues lines up.
        Layout lb = l; lb.scaleIdx = SC_BLUES;
        for (int colq = 0; colq < kScales[SC_BLUES].len; ++colq) {
            const int bn = chordPitches(lb, 0, colq, false, ch, 3);
            CHECK(bn == 3, "blues chord builds three tones");
            const int third = (int)(ch[1] - ch[0] + 0.5f);
            const int fifth = (int)(ch[2] - ch[0] + 0.5f);
            CHECK((third == 3 || third == 4) && fifth == 7,
                  "blues backing is a plain major/minor triad, never the ♭5");
            for (int t = 0; t < 3; ++t) {
                const int pcb = (((int)(ch[t] + 0.5f) - lb.rootSemis) % 12 + 12) % 12;
                CHECK(pcb != 6, "no blue note (♭5) as a chord tone in the backing");
            }
        }
        // A 7-note scale is its own parent: diatonic triads are untouched.
        Layout lm = l; lm.scaleIdx = SC_MINOR;
        chordPitches(lm, 0, 0, false, ch, 3);
        CHECK(fabsf(ch[1] - ch[0] - 3.f) < 1e-4 && fabsf(ch[2] - ch[0] - 7.f) < 1e-4,
              "natural-minor i is still a minor triad");
    }

    // ---- every scale's harmony parent is a sane, consonant 7-note scale -----
    for (int si = 0; si < kScaleCount; ++si) {
        const Scale& hp = kScales[kScales[si].harm];
        CHECK(kScales[si].harm < kScaleCount, "harmony parent index in range");
        CHECK(hp.len == 7, "harmony parent is a full 7-note diatonic scale");
    }

    // ---- chord builder is well-formed for EVERY scale and EVERY grid cell ---
    // (edge sweep: no scale/position must ever produce garbage, a cluster, or a
    // non-triad — the backing's "you can't hit a wrong note" promise, total.)
    {
        float ch[3];
        bool sawDim = false;
        for (int si = 0; si < kScaleCount; ++si) {
            Layout ls = l; ls.scaleIdx = (uint8_t)si;
            for (int st = 0; st < kGridStrings; ++st)
                for (int co = 0; co < kGridCols; ++co) {
                    // scale-lock branch: always a stack of two diatonic thirds
                    const int nd = chordPitches(ls, st, co, false, ch, 3);
                    CHECK(nd == 3, "every cell builds a 3-note chord");
                    CHECK(ch[0] < ch[1] && ch[1] < ch[2], "chord tones strictly ascend");
                    CHECK(ch[0] > 0.f && ch[2] < 200.f, "chord pitches are finite & in range");
                    const int t1 = (int)(ch[1] - ch[0] + 0.5f);
                    const int t2 = (int)(ch[2] - ch[1] + 0.5f);
                    CHECK(t1 >= 3 && t1 <= 4 && t2 >= 3 && t2 <= 4,
                          "chord is a real triad: two stacked thirds, never a cluster");
                    if (t1 == 3 && t2 == 3) sawDim = true;  // diminished is allowed
                    // chromatic branch: a power voicing, every cell, every scale
                    chordPitches(ls, st, co, true, ch, 3);
                    CHECK(fabsf(ch[1] - ch[0] - 7.f) < 1e-4 &&
                          fabsf(ch[2] - ch[0] - 12.f) < 1e-4,
                          "chromatic voicing is root+5th+8ve everywhere");
                }
        }
        CHECK(sawDim, "the diatonic vii° still voices as a diminished triad");
        // maxOut is honored (a smaller voice budget never overruns)
        float two[2] = {-1.f, -1.f};
        CHECK(chordPitches(l, 0, 0, false, two, 2) == 2, "respects maxOut < 3");
        CHECK(chordPitches(l, 0, 0, false, two, 0) == 0, "maxOut 0 writes nothing");
    }

    // ---- Roman numerals: the label shares chordDegree with the triad, so the
    // name can never disagree with what the backing actually plays ------------
    {
        char rn[8];
        // Major: the seven diatonic qualities, textbook order.
        Layout lM = l; lM.scaleIdx = SC_MAJOR;
        const char sevenDim[5] = {'v', 'i', 'i', kDegreeGlyph, '\0'};
        const char* expMaj[7] = {"I", "ii", "iii", "IV", "V", "vi", sevenDim};
        for (int co = 0; co < 7; ++co) {
            CHECK(chordRomanNumeral(lM, 0, co, false, rn, sizeof rn) &&
                      strcmp(rn, expMaj[co]) == 0,
                  "major reads I ii iii IV V vi vii(dim)");
        }
        // Harmonic minor: the parent with the famous odd qualities — the
        // augmented III and BOTH diminished chords must come out right.
        Layout lH = l; lH.scaleIdx = SC_HARM_MIN;
        const char twoDim[4] = {'i', 'i', kDegreeGlyph, '\0'};
        CHECK(chordRomanNumeral(lH, 0, 1, false, rn, sizeof rn) && strcmp(rn, twoDim) == 0,
              "harmonic minor degree 2 = ii(dim)");
        CHECK(chordRomanNumeral(lH, 0, 2, false, rn, sizeof rn) && strcmp(rn, "III+") == 0,
              "harmonic minor degree 3 = III+ (augmented)");
        CHECK(chordRomanNumeral(lH, 0, 6, false, rn, sizeof rn) && strcmp(rn, sevenDim) == 0,
              "harmonic minor degree 7 = vii(dim)");
        // Every scale, every cell: a numeral exists and its degree matches the
        // pitch class chordPitches actually roots the triad on.
        for (int si = 0; si < kScaleCount; ++si) {
            Layout ls = l; ls.scaleIdx = (uint8_t)si;
            const Scale& hsc = kScales[kScales[si].harm];
            for (int st = 0; st < kGridStrings; ++st)
                for (int co = 0; co < kGridCols; ++co) {
                    CHECK(chordRomanNumeral(ls, st, co, false, rn, sizeof rn),
                          "every locked cell has a numeral");
                    float ch3[3];
                    chordPitches(ls, st, co, false, ch3, 3);
                    const int rootPc =
                        (((int)(ch3[0] + 0.5f) - ls.rootSemis) % 12 + 12) % 12;
                    CHECK(rootPc == hsc.steps[chordDegree(ls, st, co)] % 12,
                          "numeral degree = the triad's actual root");
                }
        }
        // No diatonic degree to name: chromatic strikes and lock-off.
        CHECK(!chordRomanNumeral(l, 0, 0, true, rn, sizeof rn),
              "chromatic (power voicing) has no numeral");
        Layout lu = l; lu.scaleLock = false;
        CHECK(!chordRomanNumeral(lu, 0, 0, false, rn, sizeof rn),
              "lock off has no numeral");
        CHECK(!chordRomanNumeral(l, 0, 0, false, rn, 4), "tiny caps refuse safely");
    }

    // ---- scale tables are well-formed (incl. the v0.5 additions) ---------
    for (int si = 0; si < kScaleCount; ++si) {
        const Scale& sc = kScales[si];
        CHECK(sc.len >= 5 && sc.len <= 12, "scale length sane");
        CHECK(sc.steps[0] == 0, "scale starts at the root");
        for (int j = 1; j < sc.len; ++j)
            CHECK(sc.steps[j] > sc.steps[j - 1] && sc.steps[j] < 12,
                  "scale steps ascend within the octave");
    }

    // ---- synth: silence -> sound -> silence ------------------------------
    Synth s;
    s.init(kSr);
    SynthParams p;
    p.glideS = 0.05f;
    s.setParams(p);

    CHECK(peakOf(s, 4) < 1e-6f, "silent before any note");

    NoteEvent on = NoteEvent::make(NoteEvent::On, 10, 0, false, 69.f);
    s.handleEvent(on);
    CHECK(peakOf(s, 30) > 0.02f, "audible after noteOn");
    CHECK(s.leadActive(), "lead active");
    CHECK(fabsf(s.leadPitchMidi() - 69.f) < 0.01f, "lead pitch settled at target");

    // ---- glide: legato hand-off slews monotonically ----------------------
    NoteEvent leg = NoteEvent::make(NoteEvent::On, 11, 0, true, 75.f);
    s.handleEvent(leg);
    float buf[kBlock];
    float prev = s.leadPitchMidi();
    bool monotonic = true;
    bool moved = false;
    // glideS is the slew time constant; run ~8 tau so the exponential lands
    for (int b = 0; b < 100; ++b) {
        s.render(buf, kBlock);
        const float cur = s.leadPitchMidi();
        if (cur < prev - 1e-4f) monotonic = false;
        if (cur > prev + 1e-4f) moved = true;
        prev = cur;
    }
    CHECK(monotonic && moved, "glide slews upward without overshoot");
    CHECK(fabsf(s.leadPitchMidi() - 75.f) < 0.05f, "glide arrives at target");
    CHECK(s.heldVoices() == 1, "legato hand-off reuses the lane voice");

    // ---- release decays to silence ---------------------------------------
    NoteEvent off = NoteEvent::make(NoteEvent::Off, 11, 0, false, 0.f);
    s.handleEvent(off);
    peakOf(s, 90);  // run well past the 250 ms release tail (90 blocks = 360 ms)
    CHECK(peakOf(s, 4) < 1e-4f, "silent after release tail");

    // ---- regression: two notes on one row struck twice in quick succession --
    // A lane carries exactly ONE voice, and its release sends a note-off only
    // for the stack owner. A legato re-press must therefore GLIDE that lane
    // voice, never resurrect a still-fading same-id tail as a SECOND held voice
    // — otherwise the non-owner key's release sends no off and that voice is
    // stranded droning. This is the real "G+H, then G+H rapidly, one sticks"
    // bug; round 2 fires while round 1's release tail is still ringing.
    {
        p = SynthParams();
        p.glideS = 0.03f;
        p.releaseS = 0.3f;  // a long-ish tail so round 2 overlaps it
        s.setParams(p);
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 100);  // start from a truly empty pool
        CHECK(s.activeVoices() == 0, "pool empty before the row re-strike test");

        const uint8_t kG = 30, kH = 31;  // two keys on one row -> same lane 0
        // round 1: press G, then H legato; release H (G is a non-owner: no off)
        s.handleEvent(NoteEvent::make(NoteEvent::On, kG, 0, false, 67.f));
        s.handleEvent(NoteEvent::make(NoteEvent::On, kH, 0, true, 69.f));
        peakOf(s, 4);
        s.handleEvent(NoteEvent::make(NoteEvent::Off, kH, 0, false, 0.f));
        peakOf(s, 1);  // barely into H's release tail — still active
        // round 2: same gesture while round 1's tail still rings
        s.handleEvent(NoteEvent::make(NoteEvent::On, kG, 0, false, 67.f));
        s.handleEvent(NoteEvent::make(NoteEvent::On, kH, 0, true, 69.f));
        CHECK(s.heldLeadVoices() == 1, "row re-strike keeps one held voice on the lane");
        peakOf(s, 4);
        s.handleEvent(NoteEvent::make(NoteEvent::Off, kH, 0, false, 0.f));
        peakOf(s, 150);  // run past every release tail
        CHECK(s.activeVoices() == 0, "no voice stranded after the row re-strike");
        // restore the fast glide the sections below were written against
        p = SynthParams();
        p.glideS = 0.05f;
        s.setParams(p);
    }

    // ---- voice-cap nearest steal (the free-mode chord slide) -------------
    p.voiceCount = 2;
    s.setParams(p);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 1, 0xFF, false, 60.f));
    s.handleEvent(NoteEvent::make(NoteEvent::On, 2, 0xFF, false, 67.f));
    peakOf(s, 8);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 3, 0xFF, false, 69.f));  // steals 67
    CHECK(s.heldVoices() == 2, "voice cap respected via steal");
    peakOf(s, 60);
    CHECK(fabsf(s.leadPitchMidi() - 69.f) < 0.05f, "stolen voice glided to new pitch");

    // ---- staccato never squats in the pool (constant-rate release) -------
    // A note released early in its attack must free its voice in millis,
    // not sit silently for the full releaseS exhausting the pool.
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 20);  // drain the previous section's voices completely
    p = SynthParams();
    p.attackS = 2.f;     // long attack: 1 block in -> level ~0.002
    p.releaseS = 0.25f;
    s.setParams(p);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 20, 0xFF, false, 69.f));
    peakOf(s, 1);        // 4 ms of attack
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 20, 0xFF, false, 0.f));
    peakOf(s, 3);        // 12 ms — proportional release frees it here
    CHECK(s.activeVoices() == 0, "staccato release frees the voice quickly");
    p = SynthParams();   // restore defaults for the sections below
    s.setParams(p);

    // ---- panic ------------------------------------------------------------
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 20);
    CHECK(peakOf(s, 4) < 1e-4f, "panic silences everything");

    // ---- every waveform renders finite ------------------------------------
    for (int w = 0; w < (int)Waveform::Count; ++w) {
        p.wave = (Waveform)w;
        p.voiceCount = 6;
        s.setParams(p);
        s.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(50 + w), 0xFF, false, 64.f));
        const float pk = peakOf(s, 20);
        CHECK(pk > 0.01f && pk < 1.3f, "waveform renders in range");
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 40);
    }

    // ---- tempo-synced delay: division math + bounded render ----------------
    CHECK(delaySyncBeats(2) > 0.74f && delaySyncBeats(2) < 0.76f, "dotted-eighth = 0.75 beat");
    CHECK(delaySyncBeats(0) == 0.f, "division 0 = free");
    CHECK(delaySyncName(3)[0] == '1' && delaySyncName(0)[0] == 'f', "division labels");
    {
        p = SynthParams();
        p.delayMix = 0.5f;
        p.delayFb = 0.5f;
        p.delaySync = 2;     // dotted-eighth
        p.tempoBpm = 120.f;  // beat 500 ms -> echo 375 ms, fits the 600 ms line
        s.setParams(p);
        s.handleEvent(NoteEvent::make(NoteEvent::On, 5, 0xFF, false, 64.f));
        const float pk = peakOf(s, 40);
        CHECK(pk > 0.01f && pk < 1.4f, "synced delay renders in range");
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 20);
        p = SynthParams();
        s.setParams(p);
    }

    // ---- solo/backing split: each layer renders with its own sound ---------
    // Lead voices use the `lead` params, the backing layer uses `back`. Prove
    // the routing by silencing one bus and confirming only the other sounds.
    {
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 40);
        SynthParams lead;
        SynthParams back = lead;
        back.masterVol = 0.f;            // silence the backing bus
        s.setParams(lead, back);
        peakOf(s, 30);                   // let the backing volume ramp settle to 0
        NoteEvent dr = NoteEvent::make(NoteEvent::On, 40, 0xFF, false, 60.f);
        dr.drone = true;
        s.handleEvent(dr);               // a drone -> backing bus (silenced)
        CHECK(peakOf(s, 20) < 1e-3f, "backing routed to its own (silenced) bus");
        s.handleEvent(NoteEvent::make(NoteEvent::On, 1, 0xFF, false, 64.f));  // lead bus
        CHECK(peakOf(s, 20) > 0.02f, "lead routed to the audible lead bus");

        // flip: silence lead, voice the backing, replay the drone fresh
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 40);
        lead.masterVol = 0.f;
        back.masterVol = 0.7f;
        s.setParams(lead, back);
        peakOf(s, 30);                   // settle both ramps (no voices yet)
        s.handleEvent(dr);               // drone again, now on the audible backing bus
        CHECK(peakOf(s, 20) > 0.02f, "backing bus uses its own volume, not the lead's");
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 40);
        p = SynthParams();
        s.setParams(p);
    }

    // ---- drones: the layering jam ------------------------------------------
    // A latched drone must survive the lead's voice-cap stealing and let go
    // with a drawn-out tail.
    p = SynthParams();
    p.voiceCount = 2;
    p.releaseS = 0.05f;
    s.setParams(p);
    NoteEvent dr = NoteEvent::make(NoteEvent::On, 40, 0xFF, false, 45.f);
    dr.drone = true;
    s.handleEvent(dr);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 41, 0xFF, false, 69.f));
    s.handleEvent(NoteEvent::make(NoteEvent::On, 42, 0xFF, false, 72.f));
    s.handleEvent(NoteEvent::make(NoteEvent::On, 43, 0xFF, false, 76.f));  // cap steal
    peakOf(s, 100);  // let the stolen voice finish its glide to the target
    CHECK(s.heldVoices() == 3, "drone exempt from the lead voice cap (2 lead + 1 drone)");
    CHECK(fabsf(s.leadPitchMidi() - 76.f) < 0.2f, "steal hit a lead voice, not the drone");
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 40, 0xFF, false, 0.f));
    peakOf(s, 25);  // 100 ms: lead release (50 ms) done, drone tail (4x+0.4s) still alive
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 41, 0xFF, false, 0.f));
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 42, 0xFF, false, 0.f));
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 43, 0xFF, false, 0.f));
    peakOf(s, 25);
    CHECK(s.activeVoices() >= 1, "drone release is drawn out");
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 40);

    // ---- backing (loop-pedal playback): drone-grade protection, normal tail
    p = SynthParams();
    p.voiceCount = 1;
    p.releaseS = 0.05f;
    s.setParams(p);
    NoteEvent bk = NoteEvent::make(NoteEvent::On, 50, 4, false, 50.f);  // loop lane 4
    bk.backing = true;
    s.handleEvent(bk);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 1, 0, false, 69.f));
    s.handleEvent(NoteEvent::make(NoteEvent::On, 2, 0xFF, false, 72.f));  // cap steal
    peakOf(s, 100);
    CHECK(s.heldVoices() == 2, "backing exempt from the cap (1 lead + 1 backing)");
    CHECK(s.heldLeadVoices() == 1, "lead counter ignores the backing");
    CHECK(fabsf(s.leadPitchMidi() - 72.f) < 0.2f, "steal hit the lead, not the loop");
    s.handleEvent(NoteEvent::make(NoteEvent::LeadsOff, 0, 0xFF, false, 0.f));
    peakOf(s, 4);
    CHECK(s.heldVoices() == 1, "LeadsOff keeps the loop playing");
    s.handleEvent(NoteEvent::make(NoteEvent::Off, 50, 0xFF, false, 0.f));
    peakOf(s, 25);  // 100 ms — far past the 50 ms release, well short of a drone tail
    CHECK(s.activeVoices() == 0, "backing releases at the normal rate");
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 40);

    // ---- backing survives a saturated pool (auto-progression under a solo) --
    // 3 drone-flagged backing voices + a full lead chord on the 8-voice pool
    // must not drop the backing: alloc evicts the oldest LEAD, never the
    // foundation. (The backing has the lowest seq, so the old policy would
    // have robbed it first — this guards the chord progression.)
    p = SynthParams();
    p.voiceCount = 8;
    p.releaseS = 0.05f;
    s.setParams(p);
    for (int i = 0; i < 3; ++i) {  // a 3-voice backing chord
        NoteEvent b = NoteEvent::make(NoteEvent::On, (uint8_t)(120 + i), 0xFF, false, 50.f + i * 4);
        b.drone = true;
        s.handleEvent(b);
    }
    for (int i = 0; i < 8; ++i)  // hammer 8 lead notes at the pool
        s.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(i + 1), 0xFF, false, 60.f + i));
    peakOf(s, 8);
    CHECK(s.heldVoices() == 8, "pool saturated");
    CHECK(s.heldVoices() - s.heldLeadVoices() == 3, "all 3 backing voices survive the solo");
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 40);

    // ---- LeadsOff: sound switches / settings clear the solo, not the jam --
    dr = NoteEvent::make(NoteEvent::On, 45, 0xFF, false, 45.f);
    dr.drone = true;
    s.handleEvent(dr);
    s.handleEvent(NoteEvent::make(NoteEvent::On, 46, 0xFF, false, 69.f));
    peakOf(s, 4);
    s.handleEvent(NoteEvent::make(NoteEvent::LeadsOff, 0, 0xFF, false, 0.f));
    peakOf(s, 4);
    CHECK(s.heldVoices() == 1, "LeadsOff keeps the drone ringing");
    s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
    peakOf(s, 40);

    // ---- factory sound bank: every patch is alive and sane ----------------
    // Exercises the engine paths: sub-osc (BASS/CELLO), noise (CELLO),
    // filter envelope (PLUCK/ACID/CELLO), drive (ACID), auto-vib (WHISTLE),
    // PWM pulse (STRINGS).
    const Patch* bank = factoryPatches();
    for (int i = 0; i < kPatchCount; ++i) {
        CHECK(bank[i].name[0] != '\0', "patch has a name");
        CHECK(bank[i].tiltDepth >= 0.f && bank[i].tiltDepth <= 1.f, "tilt depth in range");
        CHECK(bank[i].tiltDepthB >= 0.f && bank[i].tiltDepthB <= 1.f, "tilt depth B in range");
        for (int j = 0; j < i; ++j)
            CHECK(strcmp(bank[i].name, bank[j].name) != 0, "patch names unique");

        s.setParams(bank[i].synth);
        s.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(100 + i), 0, false, 69.f));
        // slide it too — every sound must survive a glide
        s.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(120 + i), 0, true, 72.f));
        const float pk = peakOf(s, 60);  // 240 ms covers slow PAD/GHOST attacks
        CHECK(pk > 0.005f && pk < 1.4f, "patch renders audible and bounded");
        s.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(s, 80);  // drain long releases before the next patch
    }

    // ---- modulation matrix ------------------------------------------------
    {
        ModSlot ms = ModSlot::make(ModSource::LFO1, ModDest::Amp, 0.5f);
        CHECK(ms.src == (uint8_t)ModSource::LFO1 && ms.dest == (uint8_t)ModDest::Amp &&
                  fabsf(ms.depth - 0.5f) < 1e-6f,
              "ModSlot::make round-trips its fields");

        // windowed peak swing over a sustained note: steady => mn≈mx, tremolo => mx>>mn
        auto windowSwing = [](Synth& syn, float& mn, float& mx) {
            mn = 1e9f;
            mx = 0.f;
            float b[kBlock];
            for (int w = 0; w < 50; ++w) {
                float wp = 0.f;
                for (int k = 0; k < 4; ++k) {  // 16 ms window
                    syn.render(b, kBlock);
                    for (int i = 0; i < kBlock; ++i) {
                        const float a = fabsf(b[i]);
                        if (a > wp) wp = a;
                    }
                }
                if (wp < mn) mn = wp;
                if (wp > mx) mx = wp;
            }
        };

        SynthParams base;  // a steady sustained tone to watch the matrix act on
        base.attackS = 0.005f;
        base.decayS = 0.01f;
        base.sustain = 1.f;
        base.releaseS = 0.2f;
        base.wave = Waveform::Saw;

        // neutral (no routing) -> amplitude is steady
        Synth sn;
        sn.init(kSr);
        sn.setParams(base);
        sn.handleEvent(NoteEvent::make(NoteEvent::On, 5, 0xFF, false, 69.f));
        peakOf(sn, 20);  // settle into sustain
        float mn0, mx0;
        windowSwing(sn, mn0, mx0);
        CHECK(mx0 > 1e-4f && mx0 < mn0 * 1.5f, "neutral sustain is steady (matrix inert)");

        // deep LFO1 -> Amp -> obvious tremolo
        Synth sm;
        sm.init(kSr);
        SynthParams mod = base;
        mod.lfo1RateHz = 8.f;
        mod.lfo1Shape = (uint8_t)LfoShape::Sine;
        mod.slots[0] = ModSlot::make(ModSource::LFO1, ModDest::Amp, 0.9f);
        sm.setParams(mod);
        sm.handleEvent(NoteEvent::make(NoteEvent::On, 5, 0xFF, false, 69.f));
        peakOf(sm, 20);
        float mn1, mx1;
        windowSwing(sm, mn1, mx1);
        CHECK(mx1 > mn1 * 3.f, "LFO1->Amp produces deep tremolo");

        // ModEnv -> Cutoff stays audible and bounded after a fresh attack
        SynthParams me = base;
        me.modEnvAtkS = 0.01f;
        me.modEnvDecS = 0.4f;
        me.slots[0] = ModSlot::make(ModSource::ModEnv, ModDest::Cutoff, 0.8f);
        sm.setParams(me);
        sm.handleEvent(NoteEvent::make(NoteEvent::On, 6, 0xFF, false, 64.f));
        const float pk = peakOf(sm, 60);
        CHECK(pk > 0.005f && pk < 1.4f, "ModEnv->Cutoff renders audible & bounded");

        // new sources/dests render finite & bounded: tilt->drive, random->reverb,
        // LFO->delay, and the tilt->pitch guard (must not crash / blow up).
        SynthParams nx = base;
        nx.tiltAVal = 0.8f;  // pretend the device is leaned over
        nx.slots[0] = ModSlot::make(ModSource::TiltA, ModDest::Drive, 0.7f);
        nx.slots[1] = ModSlot::make(ModSource::Random, ModDest::Reverb, 0.5f);
        nx.slots[2] = ModSlot::make(ModSource::LFO1, ModDest::Delay, 0.4f);
        nx.slots[3] = ModSlot::make(ModSource::TiltA, ModDest::Pitch, 1.0f);  // refused (no-op)
        sm.setParams(nx);
        sm.handleEvent(NoteEvent::make(NoteEvent::On, 7, 0xFF, false, 69.f));
        const float pk2 = peakOf(sm, 40);
        CHECK(pk2 > 0.005f && pk2 < 1.6f, "new sources/dests render bounded");
        sm.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
        peakOf(sm, 80);
    }

    // ---- filter modes (LP/HP/BP/notch) render finite & bounded ------------
    {
        Synth sf;
        sf.init(kSr);
        for (int m = 0; m < (int)FilterMode::Count; ++m) {
            SynthParams fp;
            fp.glideS = 0.05f;
            fp.cutoffHz = 1200.f;
            fp.resonance = 0.5f;
            fp.filterMode = (uint8_t)m;
            sf.setParams(fp);
            sf.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(80 + m), 0xFF, false, 69.f));
            const float pk = peakOf(sf, 30);
            CHECK(pk > 0.f && pk < 1.5f, "filter mode renders bounded");
            sf.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
            peakOf(sf, 60);
        }
    }

    // ---- generative sound engine (sound_gen) ------------------------------
    // The soul of the "your instrument is yours" feature: every field a roll
    // produces must be in range and playable, the roll must be deterministic
    // (so a per-device seed gives a stable unique bank), and a mutate must stay
    // in bounds while amount==0 is an exact identity.
    {
        // validate every field of a generated patch is inside the engine's
        // musical/persisted bounds — a roll must NEVER make a dead or blown sound.
        auto validParams = [](const SynthParams& s) -> bool {
            return s.cutoffHz >= 80.f && s.cutoffHz <= 12000.f &&
                   s.resonance >= 0.f && s.resonance <= 0.95f &&
                   s.attackS >= 0.f && s.attackS <= 2.f &&
                   s.decayS >= 0.f && s.decayS <= 2.f &&
                   s.sustain >= 0.f && s.sustain <= 1.f &&
                   s.releaseS >= 0.f && s.releaseS <= 3.f &&
                   s.glideS >= 0.f && s.glideS <= 2.f &&
                   s.detuneCents >= 0.f && s.detuneCents <= 50.f &&
                   s.fenvOct >= 0.f && s.fenvOct <= 6.f &&
                   s.fenvDecS >= 0.01f && s.fenvDecS <= 2.f &&
                   s.subLevel >= 0.f && s.subLevel <= 1.f &&
                   s.noiseLevel >= 0.f && s.noiseLevel <= 1.f &&
                   s.drive >= 1.f && s.drive <= 8.f &&
                   s.driftCents >= 0.f && s.driftCents <= 12.f &&
                   s.reverbMix >= 0.f && s.reverbMix <= 1.f &&
                   s.delayMix >= 0.f && s.delayMix <= 1.f &&
                   s.chorusDepth >= 0.f && s.chorusDepth <= 1.f &&
                   s.delaySync < kDelaySyncCount &&
                   s.lfo1RateHz > 0.f && s.lfo2RateHz > 0.f &&
                   (int)s.wave < (int)Waveform::Count &&
                   s.filterMode < (uint8_t)FilterMode::Count &&
                   s.lfo1Shape < (uint8_t)LfoShape::Count &&
                   s.lfo2Shape < (uint8_t)LfoShape::Count;
        };
        auto validSlots = [](const SynthParams& s) -> bool {
            for (int i = 0; i < kModSlots; ++i) {
                if (s.slots[i].src >= (uint8_t)ModSource::Count) return false;
                if (s.slots[i].dest >= (uint8_t)ModDest::Count) return false;
                if (s.slots[i].depth < -1.f || s.slots[i].depth > 1.f) return false;
            }
            return true;
        };
        auto validTilt = [](const GenPatch& g) -> bool {
            return g.tiltRoute < (uint8_t)TiltRoute::Count &&
                   g.tiltRouteB < (uint8_t)TiltRoute::Count &&
                   g.tiltDepth >= 0.f && g.tiltDepth <= 1.f &&
                   g.tiltDepthB >= 0.f && g.tiltDepthB <= 1.f;
        };
        // field-wise equality (NOT memcmp — GenPatch has padding bytes between
        // its uint8/float members that two separate constructions needn't match)
        auto genEq = [](const GenPatch& a, const GenPatch& b) -> bool {
            const SynthParams& x = a.synth;
            const SynthParams& y = b.synth;
            bool eq = x.wave == y.wave && x.glideMode == y.glideMode &&
                      x.attackS == y.attackS && x.decayS == y.decayS && x.sustain == y.sustain &&
                      x.releaseS == y.releaseS && x.glideS == y.glideS && x.cutoffHz == y.cutoffHz &&
                      x.resonance == y.resonance && x.filterMode == y.filterMode &&
                      x.detuneCents == y.detuneCents && x.fenvOct == y.fenvOct && x.fenvDecS == y.fenvDecS &&
                      x.subLevel == y.subLevel && x.noiseLevel == y.noiseLevel && x.drive == y.drive &&
                      x.autoVibCents == y.autoVibCents && x.driftCents == y.driftCents &&
                      x.chorusDepth == y.chorusDepth && x.delayMix == y.delayMix && x.delayFb == y.delayFb &&
                      x.delaySync == y.delaySync && x.reverbMix == y.reverbMix && x.reverbSize == y.reverbSize &&
                      x.lfo1RateHz == y.lfo1RateHz && x.lfo1Shape == y.lfo1Shape &&
                      x.lfo2RateHz == y.lfo2RateHz && x.lfo2Shape == y.lfo2Shape &&
                      x.modEnvAtkS == y.modEnvAtkS && x.modEnvDecS == y.modEnvDecS &&
                      a.tiltRoute == b.tiltRoute && a.tiltDepth == b.tiltDepth &&
                      a.tiltRouteB == b.tiltRouteB && a.tiltDepthB == b.tiltDepthB;
            for (int i = 0; i < kModSlots; ++i)
                eq = eq && x.slots[i].src == y.slots[i].src && x.slots[i].dest == y.slots[i].dest &&
                     x.slots[i].depth == y.slots[i].depth;
            return eq;
        };

        // determinism: same seed -> identical patch (the per-device-bank
        // contract — a unit's slots are reproducible from its stored seed).
        GenPatch a1 = generateSound(0xC0FFEEu);
        GenPatch a2 = generateSound(0xC0FFEEu);
        CHECK(genEq(a1, a2), "generateSound is deterministic");
        GenPatch b1 = generateSound(0xC0FFEFu);
        CHECK(!genEq(a1, b1), "different seeds -> different patch");

        // sweep many seeds: every roll is in range and renders finite & bounded
        Synth sg;
        sg.init(kSr);
        bool sawGlideAlways = false, sawMod = false;
        for (uint32_t seed = 1; seed <= 200; ++seed) {
            GenPatch g = generateSound(seed * 2654435761u + 1u);
            CHECK(validParams(g.synth), "generated params in range");
            CHECK(validSlots(g.synth), "generated mod slots in range");
            CHECK(validTilt(g), "generated tilt in range");
            if (g.synth.glideMode == GlideMode::Always) sawGlideAlways = true;
            for (int i = 0; i < kModSlots; ++i)
                if (g.synth.slots[i].src != (uint8_t)ModSource::None) sawMod = true;
            sg.setParams(g.synth);
            sg.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(seed & 0x7F), 0, false, 64.f));
            const float pk = peakOf(sg, 24);
            CHECK(pk >= 0.f && pk < 1.6f, "generated patch renders finite & bounded");
            sg.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
            peakOf(sg, 40);
        }
        CHECK(sawGlideAlways, "some rolls are always-glide");
        CHECK(sawMod, "some rolls wire the mod matrix");

        // mutate: amount 0 is an exact identity
        GenPatch base = generateSound(42u);
        GenPatch m0 = mutateSound(base, 0.f, 123u);
        CHECK(genEq(base, m0), "mutate amount=0 is identity");

        // mutate stays in range + renders bounded, at gentle and wild amounts
        for (uint32_t seed = 1; seed <= 100; ++seed) {
            const float amt = (seed % 2) ? 0.15f : 0.9f;  // gentle vs wild
            GenPatch m = mutateSound(base, amt, seed * 40503u + 7u);
            CHECK(validParams(m.synth), "mutated params in range");
            CHECK(validSlots(m.synth), "mutated mod slots in range");
            CHECK(validTilt(m), "mutated tilt in range");
            sg.setParams(m.synth);
            sg.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(seed & 0x7F), 0, false, 62.f));
            const float pk = peakOf(sg, 24);
            CHECK(pk >= 0.f && pk < 1.6f, "mutated patch renders finite & bounded");
            sg.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
            peakOf(sg, 40);
        }

        // a gentle mutate keeps the patch's character: most continuous params
        // should stay close to the base (it's a neighbour, not a new roll).
        GenPatch gentle = mutateSound(base, 0.12f, 999u);
        int near = 0, tot = 0;
        auto rel = [&](float a, float b, float span) { ++tot; if (fabsf(a - b) <= 0.35f * span) ++near; };
        rel(gentle.synth.cutoffHz, base.synth.cutoffHz, 11000.f);
        rel(gentle.synth.resonance, base.synth.resonance, 0.9f);
        rel(gentle.synth.sustain, base.synth.sustain, 1.f);
        rel(gentle.synth.drive, base.synth.drive, 5.f);
        rel(gentle.synth.releaseS, base.synth.releaseS, 2.f);
        CHECK(near >= tot - 1, "a gentle mutate preserves character (neighbour, not a new roll)");

        // ---- patch naming: deterministic, terminated, content-sensitive -----
        CHECK(patchHash(base) == patchHash(base), "patchHash deterministic");
        CHECK(patchHash(base) != patchHash(generateSound(7u)), "patchHash separates patches");
        char nm[24], nm2[24];
        nameForSeed(patchHash(base), nm, sizeof nm);
        nameForSeed(patchHash(base), nm2, sizeof nm2);
        CHECK(strcmp(nm, nm2) == 0, "nameForSeed deterministic");
        CHECK(strlen(nm) > 0 && strlen(nm) < sizeof nm, "name fits and is non-empty");
        bool hasDash = false;
        for (const char* c = nm; *c; ++c) {
            const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-';
            CHECK(ok, "name uses only filename-safe chars");
            if (*c == '-') hasDash = true;
        }
        CHECK(hasDash, "name has the adj-noun-hex shape");
        // a tiny cap must still be safely terminated, never overrun
        char tiny[4];
        nameForSeed(0xABCDEF12u, tiny, sizeof tiny);
        CHECK(tiny[3] == '\0' || strlen(tiny) < 4, "name respects a small cap");

        // short label (slot display): one word, fits the cramped status bar,
        // shares its noun with the full name so they read as the same sound.
        char sh[8];
        shortNameForSeed(patchHash(base), sh, sizeof sh);
        CHECK(strlen(sh) > 0 && strlen(sh) <= 6, "short name is a compact (<=6) label");
        for (const char* c = sh; *c; ++c)
            CHECK(*c >= 'a' && *c <= 'z', "short name is a single lowercase word");
        CHECK(strstr(nm, sh) != nullptr, "short name's noun appears in the full name");
    }

    // ---- the archetype engine: variety is a tested property, not a vibe -----
    {
        // The FROZEN legacy generator, pinned by golden values captured from the
        // pre-archetype build. Existing devices regenerate their o/p slots with
        // it (storage gates on "genver"), so ANY drift here silently changes
        // sounds players already have. The two hashes cover essentially every
        // field between them; the enums pin the categorical draws directly.
        //
        // The NAME hashes below are the original captures and have never moved —
        // they are the ones that decide a re-derived slot's label. The FULL
        // hashes were re-captured once, when driftCents joined patchHashFull:
        // folding any field into that hash changes it for every patch, even at
        // an unchanged value. Before re-capturing, every persisted field of
        // these six patches was dumped and diffed against the previous build and
        // came back bit-identical, so the generators themselves did not move.
        // Do not re-capture again without repeating that proof.
        {
            const GenPatch l1 = generateSoundLegacy(0xC0FFEEu);
            CHECK(patchHash(l1) == 0x023AE176u && patchHashFull(l1) == 0x5F569A26u,
                  "legacy generator frozen (seed 0xC0FFEE)");
            CHECK(l1.synth.wave == Waveform::Sine && l1.synth.filterMode == (uint8_t)FilterMode::LP,
                  "legacy categorical draws frozen (seed 0xC0FFEE)");
            const GenPatch l2 = generateSoundLegacy(42u);
            CHECK(patchHash(l2) == 0x96F5ADD4u && patchHashFull(l2) == 0x4FA5F21Cu,
                  "legacy generator frozen (seed 42)");
            const GenPatch l3 = generateSoundLegacy(0xDEADBEEFu);
            CHECK(patchHash(l3) == 0x00BF90FEu && patchHashFull(l3) == 0xFFAB9265u,
                  "legacy generator frozen (seed 0xDEADBEEF)");
        }

        // The v2 (nine-archetype) engine is FROZEN the same way: genver-2
        // devices re-derive their o/p slots through generateSound(seed) every
        // boot, so ANY drift — in archetypeForSeed, the nine paint windows, or
        // sanitizePatch — silently changes sounds players already live with.
        // (The expanded pool ships as generateSoundV3 precisely so these can
        // hold.) Golden values captured from the first archetype build.
        {
            const Archetype kGoldenArchC0FFEE   = Archetype::Bass;
            const uint32_t kGoldenV2NameC0FFEE  = 0x89CF064Au, kGoldenV2FullC0FFEE  = 0xA36B8F50u;
            const uint32_t kGoldenV2Name42      = 0x3415AEE2u, kGoldenV2Full42      = 0xA93388E2u;
            const uint32_t kGoldenV2NameDEADBEEF = 0x2F3E1BA5u, kGoldenV2FullDEADBEEF = 0x43F6310Fu;
            const GenPatch v1 = generateSound(0xC0FFEEu);
            CHECK(archetypeForSeed(0xC0FFEEu) == kGoldenArchC0FFEE,
                  "v2 archetype pick frozen (seed 0xC0FFEE)");
            CHECK(patchHash(v1) == kGoldenV2NameC0FFEE && patchHashFull(v1) == kGoldenV2FullC0FFEE,
                  "v2 generator frozen (seed 0xC0FFEE)");
            const GenPatch v2 = generateSound(42u);
            CHECK(patchHash(v2) == kGoldenV2Name42 && patchHashFull(v2) == kGoldenV2Full42,
                  "v2 generator frozen (seed 42)");
            const GenPatch v3 = generateSound(0xDEADBEEFu);
            CHECK(patchHash(v3) == kGoldenV2NameDEADBEEF && patchHashFull(v3) == kGoldenV2FullDEADBEEF,
                  "v2 generator frozen (seed 0xDEADBEEF)");
        }

        // the bare-seed roll IS the (seed, archetype-of-seed) roll — the
        // determinism contract a future "roll style" picker relies on
        {
            const uint32_t sd = 0xBADC0DEu;
            const GenPatch a = generateSound(sd);
            const GenPatch b = generateSound(sd, archetypeForSeed(sd));
            CHECK(patchHashFull(a) == patchHashFull(b),
                  "generateSound(seed) == generateSound(seed, archetypeForSeed(seed))");
        }

        // sweep: the variety the archetypes exist for must actually occur, and
        // the guardrails must hold on EVERY roll. (All seeded — deterministic.)
        bool sawPluck = false, sawBell = false, sawPad = false, sawSubBass = false;
        bool sawSweep = false, sawSwell = false, sawVib = false, sawSynced = false;
        bool waveSeen[(int)Waveform::Count] = {false};
        bool archSeen[(int)Archetype::Count] = {false};
        for (uint32_t k = 1; k <= 400; ++k) {
            const uint32_t sd = k * 2654435761u + 17u;
            const GenPatch g = generateSound(sd);
            const SynthParams& s = g.synth;
            archSeen[(int)archetypeForSeed(sd)] = true;
            waveSeen[(int)s.wave] = true;
            if (s.sustain <= 0.22f && s.attackS < 0.05f && s.fenvOct > 0.5f) sawPluck = true;
            if (s.sustain <= 0.06f && s.decayS >= 0.5f) sawBell = true;
            if (s.attackS >= 0.25f && s.sustain >= 0.6f && s.releaseS >= 0.8f) sawPad = true;
            if (s.subLevel >= 0.4f) sawSubBass = true;
            if (s.fenvOct >= 2.2f && s.resonance >= 0.55f) sawSweep = true;
            if (s.fenvAtkS >= 0.02f) sawSwell = true;
            if (s.autoVibCents >= 2.f) sawVib = true;
            if (s.delaySync != 0 || s.lfo1Sync != 0) sawSynced = true;

            // guardrails — each of these is a known way a roll turns to trash
            if (s.filterMode == (uint8_t)FilterMode::HP)
                CHECK(s.cutoffHz <= 1800.5f, "no whisper rolls: HP keeps a body");
            if (s.filterMode == (uint8_t)FilterMode::BP)
                CHECK(s.cutoffHz >= 299.5f && s.cutoffHz <= 4500.5f, "BP stays on the melodic band");
            if (s.resonance > 0.7f)
                CHECK(s.drive <= 3.51f, "screaming reso never stacks on heavy drive");
            CHECK(s.delayMix + s.reverbMix <= 0.81f, "echo+hall can't wash out jointly");
            if (s.sustain < 0.1f)
                CHECK(s.decayS >= 0.249f, "struck sounds keep a decay body (no clicks)");
            if (s.attackS > 0.5f)
                CHECK(s.sustain >= 0.499f && s.releaseS >= 0.399f, "slow swells hold and release");
            if (s.glideMode == GlideMode::Always)
                CHECK(s.glideS <= 0.161f, "always-glide rolls stay quick enough to land");
            for (int i = 0; i < kModSlots; ++i)
                if (s.slots[i].src != (uint8_t)ModSource::None &&
                    s.slots[i].dest == (uint8_t)ModDest::Pitch)
                    CHECK(s.slots[i].depth >= -0.081f && s.slots[i].depth <= 0.081f,
                          "pitch modulation stays within ~a semitone (no atonal warble)");
        }
        CHECK(sawPluck, "rolls include plucks (sustain near zero + fast attack)");
        CHECK(sawBell, "rolls include struck bells (no sustain, long decay)");
        CHECK(sawPad, "rolls include slow-swell pads");
        CHECK(sawSubBass, "rolls include sub-heavy basses");
        CHECK(sawSweep, "rolls include deep resonant acid sweeps");
        CHECK(sawSwell, "rolls include brass-style filter-attack swells");
        CHECK(sawVib, "rolls include singing built-in vibrato");
        CHECK(sawSynced, "rolls include tempo-synced movement");
        for (int w = 0; w < (int)Waveform::Count; ++w)
            CHECK(waveSeen[w], "every waveform appears across the sweep");
        // the v2 pool holds exactly the original nine — the second wave must
        // NOT leak into it (genver-2 devices regenerate o/p from this pool)
        for (int a = 0; a < kArchetypeCountV2; ++a)
            CHECK(archSeen[a], "every v2 archetype appears across the sweep");
        for (int a = kArchetypeCountV2; a < (int)Archetype::Count; ++a)
            CHECK(!archSeen[a], "the frozen v2 pool never rolls a second-wave archetype");

        // ---- the roll must PLAY: audition-phrase pitch landing --------------
        // Regression for "rolls glide so long the preview never hits a note":
        // walk every roll through ui/audition.cpp's exact lick — the notes,
        // the per-note ids (fresh attacks land like real key presses; only the
        // Retarget slides ride the glide), and each patch's own ADAPTIVE clock
        // — and assert the lead has LANDED its final pitch by the release.
        {
            Synth sp;
            sp.init(kSr);
            for (uint32_t k = 1; k <= 150; ++k) {
                const GenPatch g = generateSound(k * 2654435761u + 12345u);
                const LickResult r = walkAuditionLick(sp, g);
                CHECK(r.finalActive, "preview's final note still sounds at its release");
                CHECK(r.finalErr > -0.35f && r.finalErr < 0.35f,
                      "preview lands its final note (no flat between-pitch smear)");
            }
        }

        // ---- the expanded (genver-3) pool: the second wave ------------------
        // Five new archetypes join the roll — whistle / organ / keys / wobble /
        // strings — via archetypeForSeedV3, leaving the v2 pool untouched. The
        // properties that made the first wave good must hold for the second:
        // determinism, every guardrail, bounded rendering, glide that LANDS,
        // and names that stay inside the frozen classifier's families.
        {
            // determinism + the same picker contract v2 has
            CHECK(patchHashFull(generateSoundV3(0xB0BA7EAu)) ==
                      patchHashFull(generateSoundV3(0xB0BA7EAu)),
                  "generateSoundV3 deterministic");
            const uint32_t sd0 = 0x51D3C0DEu;
            CHECK(patchHashFull(generateSoundV3(sd0)) ==
                      patchHashFull(generateSoundV3(sd0, archetypeForSeedV3(sd0))),
                  "generateSoundV3(seed) == generateSoundV3(seed, archetypeForSeedV3(seed))");

            // V4 = V3 + a rolled drift, and NOTHING else. This is the assertion
            // that lets V4 exist at all: genver-3 devices re-derive their o/p
            // slots through V3 every boot, so if V4's extra draw had touched the
            // shared paint stream, every one of those sounds would have shifted.
            // The drift roll takes its own Rng precisely so this holds.
            {
                CHECK(patchHashFull(generateSoundV4(0xB0BA7EAu)) ==
                          patchHashFull(generateSoundV4(0xB0BA7EAu)),
                      "generateSoundV4 deterministic");
                CHECK(patchHashFull(generateSoundV4(sd0)) ==
                          patchHashFull(generateSoundV4(sd0, archetypeForSeedV3(sd0))),
                      "generateSoundV4(seed) == generateSoundV4(seed, archetypeForSeedV3(seed))");
                bool sameElse = true, sawDrift = false, sawStill = false;
                float driftMax = 0.f;
                for (uint32_t i = 0; i < 300u; ++i) {
                    const uint32_t sd = 0x9E3779B9u * (i + 7u);
                    GenPatch v3 = generateSoundV3(sd);
                    const GenPatch v4 = generateSoundV4(sd);
                    if (v4.synth.driftCents > 0.f) sawDrift = true; else sawStill = true;
                    if (v4.synth.driftCents > driftMax) driftMax = v4.synth.driftCents;
                    if (v4.synth.driftCents < 0.f || v4.synth.driftCents > 12.f)
                        sameElse = false;
                    // compare everything EXCEPT the field V4 is allowed to move
                    v3.synth.driftCents = v4.synth.driftCents;
                    if (patchHashFull(v3) != patchHashFull(v4)) sameElse = false;
                }
                CHECK(sameElse, "V4 differs from V3 in driftCents and nothing else");
                CHECK(sawDrift && sawStill, "V4 rolls both drifting and dead-still sounds");
                CHECK(driftMax > 5.f, "V4 reaches the wide end of the drift window");
            }

            // sweep the expanded pool: all fourteen archetypes occur, every
            // roll obeys every guardrail, and a sample renders finite & bounded
            bool archSeen3[(int)Archetype::Count] = {false};
            Synth sv;
            sv.init(kSr);
            for (uint32_t k = 1; k <= 600; ++k) {
                const uint32_t sd = k * 2654435761u + 77u;
                const GenPatch g = generateSoundV3(sd);
                const SynthParams& s = g.synth;
                archSeen3[(int)archetypeForSeedV3(sd)] = true;
                if (s.filterMode == (uint8_t)FilterMode::HP)
                    CHECK(s.cutoffHz <= 1800.5f, "V3: no whisper rolls (HP keeps a body)");
                if (s.filterMode == (uint8_t)FilterMode::BP)
                    CHECK(s.cutoffHz >= 299.5f && s.cutoffHz <= 4500.5f, "V3: BP stays melodic");
                if (s.resonance > 0.7f)
                    CHECK(s.drive <= 3.51f, "V3: screaming reso never stacks on heavy drive");
                CHECK(s.delayMix + s.reverbMix <= 0.81f, "V3: echo+hall can't wash out jointly");
                if (s.sustain < 0.1f)
                    CHECK(s.decayS >= 0.249f, "V3: struck sounds keep a decay body");
                if (s.attackS > 0.5f)
                    CHECK(s.sustain >= 0.499f && s.releaseS >= 0.399f, "V3: slow swells hold");
                if (s.glideMode == GlideMode::Always)
                    CHECK(s.glideS <= 0.161f, "V3: always-glide rolls stay quick enough to land");
                for (int i = 0; i < kModSlots; ++i)
                    if (s.slots[i].src != (uint8_t)ModSource::None &&
                        s.slots[i].dest == (uint8_t)ModDest::Pitch)
                        CHECK(s.slots[i].depth >= -0.081f && s.slots[i].depth <= 0.081f,
                              "V3: pitch modulation stays within ~a semitone");
                if (k <= 150) {
                    sv.setParams(s);
                    sv.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(k & 0x7F), 0, false, 62.f));
                    const float pk = peakOf(sv, 24);
                    CHECK(pk >= 0.f && pk < 1.6f, "V3 roll renders finite & bounded");
                    sv.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
                    peakOf(sv, 40);
                }
            }
            for (int a = 0; a < (int)Archetype::Count; ++a)
                CHECK(archSeen3[a], "every archetype (second wave included) appears in the V3 pool");

            // a held note must be AUDIBLE on every second-wave roll — the spice
            // twist once flipped pure-wave patches into an HP/BP whose passband
            // sat above the note's only partial (~-40 dB: a dead roll). The
            // pure-wave polish reverts those; this floor keeps them reverted.
            Synth sa;
            sa.init(kSr);
            float hbuf[128];
            auto heldPeak = [&](const GenPatch& gg) {
                sa.setParams(gg.synth);
                sa.handleEvent(NoteEvent::make(NoteEvent::On, 10, 0, false, 57.f));
                float pk = 0.f;
                for (int b = 0; b < 250; ++b) {  // one full second
                    sa.render(hbuf, 128);
                    for (int i = 0; i < 128; ++i) {
                        const float v = fabsf(hbuf[i]);
                        if (v > pk) pk = v;
                    }
                }
                sa.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
                for (int b = 0; b < 40; ++b) sa.render(hbuf, 128);
                return pk;
            };

            // each second-wave archetype holds its character AND lands in the
            // frozen naming family its windows were designed for — that mapping
            // is the relabel-safety contract (classifySound never changes)
            for (uint32_t k = 1; k <= 40; ++k) {
                const uint32_t sd = k * 747796405u + 11u;
                {
                    const GenPatch g = generateSoundV3(sd, Archetype::Whistle);
                    const SynthParams& s = g.synth;
                    CHECK(s.wave == Waveform::Sine || s.wave == Waveform::Triangle,
                          "whistle is a pure wave");
                    CHECK(s.noiseLevel >= 0.02f, "whistle carries breath");
                    CHECK(s.autoVibCents >= 4.f, "whistle sings");
                    CHECK(s.sustain >= 0.75f, "whistle holds its tone");
                    CHECK(classifySound(s) == Archetype::Lead, "whistle names from the lead bank");
                    CHECK(heldPeak(g) >= 0.04f, "a whistle roll is never near-silent");
                }
                {
                    const GenPatch g = generateSoundV3(sd, Archetype::Organ);
                    const SynthParams& s = g.synth;
                    CHECK(s.attackS <= 0.006f && s.sustain >= 0.9f, "organ is instant-on and held");
                    CHECK(s.fenvOct == 0.f, "organ never blooms per note (its one identity)");
                    CHECK(s.subLevel >= 0.4f, "organ stacks the 16' drawbar");
                    bool rotary = false;
                    for (int i = 0; i < kModSlots; ++i)
                        if (s.slots[i].src == (uint8_t)ModSource::LFO1 &&
                            s.slots[i].dest == (uint8_t)ModDest::Amp)
                            rotary = true;
                    CHECK(rotary, "organ spins a rotary tremolo");
                    const Archetype c = classifySound(s);
                    CHECK(c == Archetype::Wild || c == Archetype::Bass,
                          "organ names from the choir/cavern or depth banks");
                    CHECK(heldPeak(g) >= 0.04f, "an organ roll is never near-silent");
                }
                {
                    const GenPatch g = generateSoundV3(sd, Archetype::Keys);
                    const SynthParams& s = g.synth;
                    CHECK(s.sustain >= 0.28f && s.sustain <= 0.48f,
                          "keys hold the middle sustain no pluck reaches");
                    CHECK(s.fenvOct >= 0.5f, "keys keep the tine bark");
                    CHECK(classifySound(s) == Archetype::Wild, "keys name from the generic bank");
                    CHECK(heldPeak(g) >= 0.04f, "a keys roll is never near-silent");
                }
                {
                    const GenPatch g = generateSoundV3(sd, Archetype::Wobble);
                    const SynthParams& s = g.synth;
                    CHECK(s.lfo1Sync != 0, "wobble locks its LFO to the jam clock");
                    bool wob = false;
                    for (int i = 0; i < kModSlots; ++i)
                        if (s.slots[i].src == (uint8_t)ModSource::LFO1 &&
                            s.slots[i].dest == (uint8_t)ModDest::Cutoff &&
                            s.slots[i].depth >= 0.249f)
                            wob = true;
                    CHECK(wob, "wobble routes a deep synced LFO into the cutoff");
                    CHECK(s.subLevel >= 0.5f, "wobble carries sub weight");
                    CHECK(classifySound(s) == Archetype::Bass, "wobble names from the bass bank");
                    CHECK(s.filterMode != (uint8_t)FilterMode::HP,
                          "wobble never loses its sub to a highpass");
                    CHECK(heldPeak(g) >= 0.04f, "a wobble roll is never near-silent");
                }
                {
                    const GenPatch g = generateSoundV3(sd, Archetype::Strings);
                    const SynthParams& s = g.synth;
                    CHECK(s.attackS >= 0.10f && s.attackS <= 0.26f,
                          "strings bow in between pluck and pad");
                    CHECK(s.autoVibCents >= 2.5f, "strings keep the section vibrato");
                    const Archetype c = classifySound(s);
                    CHECK(c == Archetype::Pad || c == Archetype::Lead,
                          "strings name from the pad or lead banks");
                    char nm3[24];
                    soundNameForPatch(g, nm3, sizeof nm3);
                    CHECK(strlen(nm3) > 0 && strchr(nm3, '-') != nullptr,
                          "a second-wave roll names like any other");
                    CHECK(heldPeak(g) >= 0.04f, "a strings roll is never near-silent");
                }
            }

            // the glide-must-land law, second wave included: walk V3 rolls
            // through the audition lick's exact notes, ids, and adaptive clock
            {
                Synth sp;
                sp.init(kSr);
                for (uint32_t k = 1; k <= 80; ++k) {
                    const GenPatch g = generateSoundV3(k * 2654435761u + 4242u);
                    const LickResult r = walkAuditionLick(sp, g);
                    CHECK(r.finalActive, "V3 preview's final note still sounds at its release");
                    CHECK(r.finalErr > -0.35f && r.finalErr < 0.35f,
                          "V3 preview lands its final note (no between-pitch smear)");
                }
            }

            // ---- if a roll PLAYS, it must PREVIEW ---------------------------
            // The field report behind this: rolls whose audition phrase read
            // faint or near-silent turned out worth keeping when played by
            // hand. Two causes, both fixed and both pinned here: broken-quiet
            // patches (a pure wave stranded behind an HP/BP — removed by the
            // V3 rollPolish), and characters that need TIME the fixed clock
            // didn't give them (slow swells, blooms, flutter — served by the
            // adaptive clock the walk helper mirrors). The property: a roll
            // audible under normal playing is never near-silent in preview.
            {
                Synth sq;
                sq.init(kSr);
                float qbuf[128];
                auto peakMs = [&](int ms) {
                    float pk = 0.f;
                    for (int b = 0; b < ms / 4; ++b) {
                        sq.render(qbuf, 128);
                        for (int i = 0; i < 128; ++i) {
                            const float v = fabsf(qbuf[i]);
                            if (v > pk) pk = v;
                        }
                    }
                    return pk;
                };
                for (uint32_t k = 1; k <= 250; ++k) {
                    const GenPatch g = generateSoundV3(k * 2654435761u + 90210u);
                    const LickResult r = walkAuditionLick(sq, g);
                    if (r.peak >= 0.02f) continue;  // preview audible — fine
                    // preview near-silent: normal playing must be quiet too
                    // (held notes, low through high — the "esc and noodle" test)
                    float play = 0.f;
                    const float notes[3] = {45.f, 60.f, 76.f};
                    for (int i = 0; i < 3; ++i) {
                        sq.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(40 + i), 0, false, notes[i]));
                        const float p = peakMs(1600);
                        if (p > play) play = p;
                        sq.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
                        peakMs(160);
                    }
                    CHECK(play < 0.06f,
                          "a roll that plays audibly never previews near-silent");
                }
            }
        }

        // ---- character-aware naming: the words follow the sound -------------
        {
            char n1[24], n2[24];
            const GenPatch p1 = generateSound(0x5EED1234u);
            soundNameForPatch(p1, n1, sizeof n1);
            soundNameForPatch(p1, n2, sizeof n2);
            CHECK(strcmp(n1, n2) == 0, "soundNameForPatch deterministic");
            bool dash = false;
            for (const char* c = n1; *c; ++c) {
                CHECK((*c >= 'a' && *c <= 'z') || *c == '-', "patch name filename-safe");
                if (*c == '-') dash = true;
            }
            CHECK(dash && strlen(n1) < sizeof n1, "patch name keeps the adj-noun shape");

            auto nounIn = [](const char* name, const char* const* bank) {
                const char* d = strchr(name, '-');
                if (!d) return false;
                for (int i = 0; i < 8; ++i)
                    if (strcmp(d + 1, bank[i]) == 0) return true;
                return false;
            };
            // craft unmistakable family members; the noun must come from the
            // matching bank (these lists double as a freeze pin: re-derived
            // names — o/p slots, nameless saves — must stay stable)
            static const char* kBellNouns[8] = {"bell", "chime", "glass", "halo", "frost", "prism", "hymn", "star"};
            static const char* kBassNouns[8] = {"root", "rumble", "depth", "boom", "core", "anchor", "fathom", "loam"};
            static const char* kAcidNouns[8] = {"acid", "wasp", "fizz", "venom", "spiral", "worm", "zap", "sting"};
            GenPatch q;
            q.synth.wave = Waveform::Sine; q.synth.sustain = 0.f; q.synth.decayS = 1.f;
            q.synth.cutoffHz = 5000.f;
            CHECK(classifySound(q.synth) == Archetype::Bell, "classifier hears a bell");
            soundNameForPatch(q, n1, sizeof n1);
            CHECK(nounIn(n1, kBellNouns), "a bell is named like a bell");
            q = GenPatch();
            q.synth.subLevel = 0.6f; q.synth.cutoffHz = 500.f; q.synth.sustain = 0.8f;
            CHECK(classifySound(q.synth) == Archetype::Bass, "classifier hears a bass");
            soundNameForPatch(q, n1, sizeof n1);
            CHECK(nounIn(n1, kBassNouns), "a bass is named like a bass");
            q = GenPatch();
            q.synth.resonance = 0.8f; q.synth.fenvOct = 3.f; q.synth.sustain = 0.6f;
            CHECK(classifySound(q.synth) == Archetype::Acid, "classifier hears acid");
            soundNameForPatch(q, n1, sizeof n1);
            CHECK(nounIn(n1, kAcidNouns), "an acid patch is named like acid");
            // the neutral GLIDE tone stays character-neutral (generic bank)
            CHECK(classifySound(SynthParams()) == Archetype::Wild,
                  "the neutral default classifies as no strong family");

            // across a sweep, names spread far wider than the old 256 combos'
            // effective use — and at least six families get named
            bool famNamed[(int)Archetype::Count] = {false};
            int distinct = 0;
            char seen[160][24];
            for (uint32_t k = 1; k <= 300; ++k) {
                const GenPatch g = generateSound(k * 2654435761u + 901u);
                famNamed[(int)classifySound(g.synth)] = true;
                soundNameForPatch(g, n1, sizeof n1);
                bool dup = false;
                for (int i = 0; i < distinct && !dup; ++i)
                    if (strcmp(seen[i], n1) == 0) dup = true;
                if (!dup && distinct < 160) {
                    strncpy(seen[distinct], n1, 23);
                    seen[distinct][23] = '\0';
                    ++distinct;
                }
            }
            int nfam = 0;
            for (int a = 0; a < (int)Archetype::Count; ++a) nfam += famNamed[a] ? 1 : 0;
            CHECK(nfam >= 6, "the classifier spreads rolls across >=6 families");
            CHECK(distinct >= 80, "300 rolls yield a wide spread of distinct names");
        }

        // legacy rolls stay valid too (devices still regenerate o/p with them):
        // in-range, and a sample renders finite & bounded
        Synth sl;
        sl.init(kSr);
        for (uint32_t k = 1; k <= 100; ++k) {
            const GenPatch g = generateSoundLegacy(k * 40503u + 3u);
            // same validators the new-engine sweep uses (defined above)
            CHECK(g.synth.cutoffHz >= 80.f && g.synth.cutoffHz <= 12000.f &&
                      g.synth.resonance >= 0.f && g.synth.resonance <= 0.95f,
                  "legacy roll in range");
            if (k <= 30) {
                sl.setParams(g.synth);
                sl.handleEvent(NoteEvent::make(NoteEvent::On, (uint8_t)(k & 0x7F), 0, false, 60.f));
                const float pk = peakOf(sl, 24);
                CHECK(pk >= 0.f && pk < 1.6f, "legacy roll renders finite & bounded");
                sl.handleEvent(NoteEvent::make(NoteEvent::AllOff, 0, 0xFF, false, 0.f));
                peakOf(sl, 40);
            }
        }
    }

    // ---- the dirty hash: every persisted field counts; the name hash is frozen
    // patchHash() names sounds and must NEVER change coverage (it would rename
    // players' saved slots). patchHashFull() is the unsaved-edit/same-sound
    // hash: each field the name hash deliberately omits must still flip it —
    // those edits used to be invisible to store::liveDirty and could be lost.
    {
        const GenPatch p = generateSound(42u);
        const uint32_t name0 = patchHash(p);
        const uint32_t full0 = patchHashFull(p);
        CHECK(patchHashFull(p) == full0, "patchHashFull deterministic");
        CHECK(patchHashFull(generateSound(7u)) != full0, "patchHashFull separates patches");

        auto flips = [&](const GenPatch& q, const char* msg) {
            CHECK(patchHashFull(q) != full0, msg);
            CHECK(patchHash(q) == name0, "name hash unmoved (frozen coverage)");
        };
        GenPatch q;
        q = p; q.synth.voiceCount = (uint8_t)(q.synth.voiceCount == 8 ? 1 : q.synth.voiceCount + 1);
        flips(q, "voiceCount flips the dirty hash");
        q = p; q.synth.fenvAtkS += 0.05f;   flips(q, "fenvAtkS flips the dirty hash");
        q = p; q.synth.fenvDecS += 0.05f;   flips(q, "fenvDecS flips the dirty hash");
        q = p; q.synth.noiseLevel += 0.1f;  flips(q, "noiseLevel flips the dirty hash");
        q = p; q.synth.autoVibCents += 5.f; flips(q, "autoVibCents flips the dirty hash");
        q = p; q.synth.driftCents += 4.f; flips(q, "driftCents flips the dirty hash");
        q = p; q.synth.delayTimeS += 0.05f; flips(q, "delayTimeS flips the dirty hash");
        q = p; q.synth.delayFb += 0.1f;     flips(q, "delayFb flips the dirty hash");
        q = p; q.synth.delaySync = (uint8_t)((q.synth.delaySync + 1) % kDelaySyncCount);
        flips(q, "delaySync flips the dirty hash");
        q = p; q.synth.reverbSize += 0.1f;  flips(q, "reverbSize flips the dirty hash");
        q = p; q.synth.lfo1Shape = (uint8_t)((q.synth.lfo1Shape + 1) % (int)LfoShape::Count);
        flips(q, "lfo1Shape flips the dirty hash");
        q = p; q.synth.lfo1Sync = (uint8_t)((q.synth.lfo1Sync + 1) % kDelaySyncCount);
        flips(q, "lfo1Sync flips the dirty hash");
        q = p; q.synth.lfo2Shape = (uint8_t)((q.synth.lfo2Shape + 1) % (int)LfoShape::Count);
        flips(q, "lfo2Shape flips the dirty hash");
        q = p; q.synth.lfo2Sync = (uint8_t)((q.synth.lfo2Sync + 1) % kDelaySyncCount);
        flips(q, "lfo2Sync flips the dirty hash");
        q = p; q.synth.modEnvAtkS += 0.05f; flips(q, "modEnvAtkS flips the dirty hash");
        q = p; q.synth.modEnvDecS += 0.05f; flips(q, "modEnvDecS flips the dirty hash");
        q = p; q.tiltDepth += 0.1f;         flips(q, "tiltDepth flips the dirty hash");
        q = p; q.tiltDepthB += 0.1f;        flips(q, "tiltDepthB flips the dirty hash");

        // ...and what must NOT count: the player's volume and the live-mod
        // frame state (riding a knob or a bend is never an "unsaved edit").
        q = p; q.synth.masterVol += 0.2f;
        CHECK(patchHashFull(q) == full0, "masterVol excluded (the player's, not the sound's)");
        q = p; q.synth.bendCents = 500.f; q.synth.vibratoCents = 40.f;
        q.synth.cutoffModOct = 1.f; q.synth.volMod = 0.5f; q.synth.tempoBpm = 87.f;
        CHECK(patchHashFull(q) == full0, "live-mod fields excluded from the dirty hash");
    }

    // ---- loop quantize: the pedal's close tap snaps to the jam clock --------
    {
        // 100 bpm: beat = 600 ms, bar = 2400 ms
        CHECK(quantizeLoopMs(2500, 100.f, 2) == 2400, "late tap -> 1 bar");
        CHECK(quantizeLoopMs(1300, 100.f, 2) == 2400, "just past half a bar -> 1 bar");
        CHECK(quantizeLoopMs(900,  100.f, 2) == 2400, "under half a bar -> still min 1 bar");
        CHECK(quantizeLoopMs(4700, 100.f, 2) == 4800, "near 2 bars -> 2 bars");
        CHECK(quantizeLoopMs(4700, 100.f, 1) == 4800, "beat mode: 7.83 beats -> 8 beats");
        CHECK(quantizeLoopMs(610,  100.f, 1) == 600,  "beat mode rounds to the nearest beat");
        CHECK(quantizeLoopMs(4700, 100.f, 0) == 4700, "off = untouched");
        CHECK(quantizeLoopMs(4700, 0.f,  2) == 4700, "no tempo = untouched");
    }

    // ---- patch codec: the tagged save format (names + forward-compat) -------
    {
        using store::PatchData;
        uint8_t buf[512];

        // (a) a human name and a scalar field both round-trip
        PatchData a;
        a.synth.cutoffHz = 1234.f;
        std::strcpy(a.name, "my-bass");
        const size_t na = store::encodePatch(a, buf, sizeof buf);
        CHECK(na > 0, "encode produced a stream");
        PatchData out;
        out.synth.cutoffHz = 999.f;  // seeded different -> proves overwrite
        CHECK(store::decodePatch(buf, na, out), "decode accepts the stream");
        CHECK(fabsf(out.synth.cutoffHz - 1234.f) < 0.5f, "scalar field round-trips");
        CHECK(strcmp(out.name, "my-bass") == 0, "name round-trips");

        // (a2) tag 30 (driftCents) round-trips, and a stream written before the
        // tag existed leaves the field at its default rather than zeroing it.
        // That second half is what decides what happens to a player's existing
        // rack: every patch saved before this feature gains the default drift.
        {
            PatchData d1;
            d1.synth.driftCents = 9.f;
            const size_t n1 = store::encodePatch(d1, buf, sizeof buf);
            PatchData r1;
            r1.synth.driftCents = 0.f;
            CHECK(store::decodePatch(buf, n1, r1) &&
                      fabsf(r1.synth.driftCents - 9.f) < 0.01f,
                  "driftCents round-trips (tag 30)");
            // an "old" stream: encode, then decode into a fresh struct after
            // stripping the drift record by re-encoding a patch that never set
            // it is not possible (the tag always emits), so assert the decoder
            // contract directly — untouched fields keep the destination value.
            PatchData r2;
            r2.synth.autoVibCents = 7.f;  // a field the stream below never names
            uint8_t tiny[3] = {buf[0], buf[1], buf[2]};  // magic + version only
            CHECK(store::decodePatch(tiny, sizeof tiny, r2) &&
                      fabsf(r2.synth.autoVibCents - 7.f) < 0.01f,
                  "absent tags leave the destination field untouched");
        }

        // (b) an empty name emits NO extra bytes and decodes back empty
        PatchData b;
        b.synth.cutoffHz = 1234.f;
        const size_t nb = store::encodePatch(b, buf, sizeof buf);
        CHECK(nb > 0 && nb < na, "empty name adds no record (shorter stream)");
        PatchData out2;
        CHECK(store::decodePatch(buf, nb, out2) && out2.name[0] == '\0',
              "empty name decodes empty (status quo)");

        // (c) an over-long name truncates to the field cap, never overruns
        PatchData c;
        memset(c.name, 'x', sizeof c.name - 1);
        c.name[sizeof c.name - 1] = '\0';
        const size_t nc = store::encodePatch(c, buf, sizeof buf);
        PatchData out3;
        CHECK(store::decodePatch(buf, nc, out3) && strlen(out3.name) <= 20,
              "long name truncated to <=20");

        // (d) forward-compat: an UNKNOWN string record is skipped, scalars before
        // it survive. The name is emitted last, so flipping its tag to an unknown
        // value models an OLD firmware meeting a NEW string field.
        PatchData d;
        d.synth.cutoffHz = 1234.f;
        const size_t nd0 = store::encodePatch(d, buf, sizeof buf);  // no name yet
        std::strcpy(d.name, "test");
        const size_t nd1 = store::encodePatch(d, buf, sizeof buf);  // name record at [nd0..]
        CHECK(nd1 > nd0 && buf[nd0] == 110, "name record is last (tag 110)");
        buf[nd0] = 200;  // unknown tag, still the T_STR type byte
        PatchData out4;
        out4.synth.cutoffHz = 999.f;
        CHECK(store::decodePatch(buf, nd1, out4), "stream with an unknown string tag decodes");
        CHECK(fabsf(out4.synth.cutoffHz - 1234.f) < 0.5f, "scalars before the unknown tag applied");
        CHECK(out4.name[0] == '\0', "unknown string tag skipped, name left unset");
    }

    // ---- synth morph: perceptual interpolation between two sounds ----------
    {
        SynthParams a;  // neutral defaults
        SynthParams b;
        b.wave = Waveform::Square;
        b.cutoffHz = 400.f;
        b.attackS = 0.5f;
        b.sustain = 0.2f;
        b.resonance = 0.9f;
        b.chorusDepth = 1.f;
        b.voiceCount = 2;
        b.slots[0] = ModSlot::make(ModSource::LFO1, ModDest::Cutoff, 0.8f);

        const SynthParams m0 = morphParams(a, b, 0.f);
        CHECK(m0.cutoffHz == a.cutoffHz && m0.wave == a.wave && m0.sustain == a.sustain,
              "morph t=0 is exactly a");
        const SynthParams m1 = morphParams(a, b, 1.f);
        CHECK(m1.cutoffHz == b.cutoffHz && m1.wave == b.wave && m1.sustain == b.sustain,
              "morph t=1 is b's sound");
        CHECK(m1.voiceCount == a.voiceCount, "voiceCount never morphs (no voice yanking)");
        SynthParams bLoud = b;
        bLoud.masterVol = 0.1f;
        CHECK(morphParams(a, bLoud, 1.f).masterVol == a.masterVol &&
                  morphParams(a, bLoud, 0.5f).masterVol == a.masterVol,
              "masterVol is the player's: volume keys work at any blend depth");

        const SynthParams mh = morphParams(a, b, 0.5f);
        const float gm = sqrtf((a.cutoffHz + 1e-3f) * (b.cutoffHz + 1e-3f)) - 1e-3f;
        CHECK(fabsf(mh.cutoffHz - gm) < 1.f, "cutoff lerps geometrically");
        CHECK(fabsf(mh.sustain - 0.45f) < 0.01f, "sustain lerps linearly");
        CHECK(morphParams(a, b, 0.49f).wave == a.wave && mh.wave == b.wave,
              "discretes switch at the midpoint");
        CHECK(mh.resonance >= a.resonance && mh.resonance <= b.resonance,
              "linear lerp stays inside the endpoints");
        CHECK(mh.attackS > a.attackS && mh.attackS < b.attackS, "times move monotonically");
        // mismatched mod routing: depth breathes out, swaps, breathes in
        CHECK(fabsf(mh.slots[0].depth) < 0.05f, "mismatched slot depth ~0 at midpoint");
        CHECK(morphParams(a, b, 0.75f).slots[0].depth > 0.3f, "b's slot fades in past midpoint");
        // live-mod fields ride from a, never blended
        SynthParams a2 = a;
        a2.bendCents = 123.f;
        CHECK(morphParams(a2, b, 1.f).bendCents == 123.f, "live-mod fields stay the caller's");
        a2.metroOn = 1;
        a2.metroBeats = 3;
        a2.metroLevel = 42;
        const SynthParams am = morphParams(a2, b, 1.f);
        CHECK(am.metroOn == 1 && am.metroBeats == 3 && am.metroLevel == 42,
              "metronome is the player's: a full-depth morph can't silence the click");
    }

    // ---- metronome: patch-independent click, sample-accurate, off = silent --
    {
        float buf[kBlock];
        auto blockPeak = [&](Synth& s) {
            s.render(buf, kBlock);
            float pk = 0.f;
            for (int i = 0; i < kBlock; ++i) {
                CHECK(std::isfinite(buf[i]), "metronome render stays finite");
                const float a = fabsf(buf[i]);
                if (a > pk) pk = a;
            }
            return pk;
        };
        Synth s;
        s.init(kSr);
        SynthParams p;  // defaults: metroOn = 0, tempo 120, beats 4
        s.setParams(p);
        CHECK(peakOf(s, 20) == 0.f, "metronome off (the default) renders exact silence");

        p.metroOn = 1;
        s.setParams(p);
        const float pkAccent = blockPeak(s);
        CHECK(pkAccent > 0.01f, "toggle-on clicks immediately (audible confirmation)");
        // fully decayed well before the next beat (120 bpm = 500 ms)...
        (void)peakOf(s, 24);  // ride out the click (~100 ms)
        CHECK(peakOf(s, 25) < 1e-3f, "click decays to silence between beats");
        // ...and the next free-run beat arrives on schedule (~block 125)
        CHECK(peakOf(s, 90) > 0.01f, "free-running beat fires at tempo, no UI clock");

        // flam guard: a sync landing right on a click's heels is swallowed —
        // the free-runner already fired, a double-hit would flam
        Synth s2;
        s2.init(kSr);
        s2.setParams(p);
        const float pk0 = blockPeak(s2);  // rising-edge click (the downbeat)
        for (int b2 = 0; b2 < 4; ++b2) (void)blockPeak(s2);  // ~20 ms in
        s2.handleEvent(NoteEvent::make(NoteEvent::MetroSync, 1));
        CHECK(peakOf(s2, 4) < pk0 * 0.25f, "near-coincident sync is flam-guarded");
        // a due sync clicks — and beat 2 sits under the accented downbeat
        (void)peakOf(s2, 70);  // past the half-period refractory, click long gone
        s2.handleEvent(NoteEvent::make(NoteEvent::MetroSync, 1));
        const float pkPlain = peakOf(s2, 8);
        CHECK(pkPlain > 0.01f, "a due sync fires the click");
        CHECK(pkPlain < pk0 * 0.95f && pkPlain > pk0 * 0.5f,
              "beat 2 is audibly softer than the downbeat, not gone");

        // level zero = silent even while running; and the off edge disarms
        Synth s3;
        s3.init(kSr);
        SynthParams pz;
        pz.metroOn = 1;
        pz.metroLevel = 0;
        s3.setParams(pz);
        CHECK(peakOf(s3, 130) == 0.f, "metroLevel 0 is exact silence");
        pz.metroLevel = 60;
        pz.metroOn = 0;
        s3.setParams(pz);
        CHECK(peakOf(s3, 130) == 0.f, "toggled off mid-run: silent again");
    }

    // ---- demo melody generator: phrases, not a random walk ------------------
    {
        DemoMelody m1, m2;
        m1.seed(42);
        m2.seed(42);
        int slides = 0, attacks = 0, rests = 0;
        uint32_t pos16 = 0;  // cumulative 16ths — phrases must land on bars
        bool inRange = true, sameSeq = true, barAligned = true, resolves = true;
        bool restsShort = true;  // the turnaround breathes for ONE beat, never
                                 // a whole bar — the screen must stay alive
        bool runsFollow = true;  // ...and every breath is chased by the pickup run
        bool hookHolds = true;   // within a section every phrase repeats the hook
        bool evolves = false;    // across sections the hook re-rolls — the solo
                                 // keeps developing instead of looping forever
        int lastDeg = -1;
        uint8_t lastDur = 0;
        bool prevWasRest = false;
        // bar-0 duration rhythm per phrase (a phrase = 4 bars = 64 sixteenths):
        // compare phrases inside one 4-phrase section, and sections to each other
        uint8_t secHook[8];
        int secHookLen = -1;
        uint8_t curHook[8];
        int curHookLen = 0;
        uint32_t curPh = 0;
        auto closePhrase = [&](uint32_t ph) {
            if (ph % 4 == 0) {  // a section-opening phrase defines the hook
                if (secHookLen >= 0) {
                    bool same = curHookLen == secHookLen;
                    for (int k = 0; same && k < curHookLen; ++k) same = curHook[k] == secHook[k];
                    if (!same) evolves = true;
                }
                secHookLen = curHookLen;
                for (int k = 0; k < curHookLen; ++k) secHook[k] = curHook[k];
            } else if (secHookLen >= 0) {
                hookHolds = hookHolds && curHookLen == secHookLen;
                for (int k = 0; k < curHookLen && k < secHookLen; ++k)
                    hookHolds = hookHolds && curHook[k] == secHook[k];
            }
            curHookLen = 0;
        };
        for (int i = 0; i < 600; ++i) {
            const DemoNote a = m1.next(5);  // pentatonic-sized scale
            const DemoNote b = m2.next(5);
            sameSeq = sameSeq && a.type == b.type && a.degree == b.degree &&
                      a.steps16 == b.steps16;
            inRange = inRange && a.degree >= 0 && a.degree <= 15;
            const uint32_t ph = pos16 / 64;
            if (ph != curPh) {
                closePhrase(curPh);
                curPh = ph;
            }
            if ((pos16 / 16) % 4 == 0 && curHookLen < 8)  // event starts in bar 0
                curHook[curHookLen++] = a.steps16;
            if (a.type == DemoNote::Rest) {
                ++rests;
                // the breath opens the turnaround bar: it must start ON a
                // barline, follow the long root note (the resolution; B phrases
                // resolve to the lifted root an octave up), and stay short
                barAligned = barAligned && (pos16 % 16) == 0;
                resolves = resolves && (lastDeg == 5 || lastDeg == 10) && lastDur == 12;
                restsShort = restsShort && a.steps16 <= 4;
            } else {
                if (prevWasRest)  // the pickup run strikes right after the breath
                    runsFollow = runsFollow && a.type == DemoNote::Attack && a.steps16 == 2;
                if (a.type == DemoNote::Slide) ++slides;
                else ++attacks;
            }
            prevWasRest = a.type == DemoNote::Rest;
            lastDeg = a.degree;
            lastDur = a.steps16;
            pos16 += a.steps16;
        }
        CHECK(sameSeq, "demo melody is deterministic per seed");
        CHECK(inRange, "demo degrees stay inside three octaves");
        CHECK(barAligned, "every phrase lands exactly on a bar boundary");
        CHECK(resolves, "every phrase resolves: a long root before the breath");
        CHECK(restsShort, "the breath is a beat, never a dark bar");
        CHECK(runsFollow, "every breath is chased by the pickup run");
        CHECK(hookHolds, "one hook per section: its phrases repeat the rhythm");
        CHECK(evolves, "sections re-roll the hook: the solo develops");
        CHECK(rests >= 3, "phrases breathe (a short rest per phrase)");
        CHECK(slides > 20 && attacks > 20, "slides carry the line; downbeats re-attack");
    }

    // ---- key detection (LISTEN) ------------------------------------------
    {
        constexpr float sr = 16000.f;
        constexpr int nCap = (int)(sr * 3);  // 3 s, the on-device capture
        static int16_t cap[nCap];

        // Render a chord progression: each chord a sum of tones (fundamental
        // + two harmonics), ~0.75 s per chord — a crude but honest "song".
        // `amp` is the per-tone level in int16 counts (2200 = the loud case).
        auto renderProg = [](int16_t* out, int n, const int* midi, int chords,
                             int tonesPerChord, float detune, float amp) {
            for (int i = 0; i < n; ++i) out[i] = 0;
            const int chordLen = n / chords;
            for (int c = 0; c < chords; ++c) {
                for (int t = 0; t < tonesPerChord; ++t) {
                    const float f =
                        440.f * powf(2.f, (midi[c * tonesPerChord + t] - 69) / 12.f) *
                        detune;
                    for (int i = 0; i < chordLen; ++i) {
                        const float ph = 6.2831853f * f * (float)i / 16000.f;
                        const float v = sinf(ph) + 0.5f * sinf(2.f * ph) +
                                        0.25f * sinf(3.f * ph);
                        out[c * chordLen + i] += (int16_t)(v * amp);
                    }
                }
            }
        };

        // C major: C-F-G-C.  A minor: Am-Dm-Em-Am (same pitch-class set as
        // C major — the relative-pair tiebreak the K-S tonic weight decides).
        const int cMajor[12] = {48, 52, 55, 53, 57, 60, 55, 59, 62, 48, 52, 55};
        const int aMinor[12] = {45, 48, 52, 50, 53, 57, 52, 55, 59, 45, 48, 52};
        const float upCents = powf(2.f, 30.f / 1200.f);
        const float dnCents = powf(2.f, -30.f / 1200.f);

        renderProg(cap, nCap, cMajor, 4, 3, 1.f, 2200.f);
        KeyGuess g = detectKey(cap, nCap, sr);
        CHECK(g.valid && g.rootPc == 0 && !g.minor, "C major progression detected");
        CHECK(g.confidence > 0.05f, "clean progression has real confidence");
        CHECK(g.chroma[0] > 0.99f || g.chroma[7] > 0.99f || g.chroma[4] > 0.99f,
              "chroma peak sits on a C-major chord tone");

        renderProg(cap, nCap, aMinor, 4, 3, 1.f, 2200.f);
        g = detectKey(cap, nCap, sr);
        CHECK(g.valid && g.rootPc == 9 && g.minor,
              "A minor progression detected (relative-pair tiebreak)");

        renderProg(cap, nCap, cMajor, 4, 3, upCents, 2200.f);
        g = detectKey(cap, nCap, sr);
        CHECK(g.valid && g.rootPc == 0 && !g.minor, "C major survives +30 cents");
        renderProg(cap, nCap, aMinor, 4, 3, dnCents, 2200.f);
        g = detectKey(cap, nCap, sr);
        CHECK(g.valid && g.rootPc == 9 && g.minor, "A minor survives -30 cents");

        // A short capture (the 1.5 s heap-fallback case) still detects.
        renderProg(cap, nCap / 2, cMajor, 4, 3, 1.f, 2200.f);
        g = detectKey(cap, nCap / 2, sr);
        CHECK(g.valid && g.rootPc == 0 && !g.minor, "1.5 s capture still detects");

        // Silence and near-silence must refuse, never hallucinate.
        for (int i = 0; i < nCap; ++i) cap[i] = 0;
        g = detectKey(cap, nCap, sr);
        CHECK(!g.valid, "all-zero capture reports no signal");
        uint32_t rng = 12345u;
        for (int i = 0; i < nCap; ++i) {
            rng = rng * 1664525u + 1013904223u;
            cap[i] = (int16_t)((rng >> 20) & 15) - 8;  // tiny noise floor
        }
        g = detectKey(cap, nCap, sr);
        CHECK(!g.valid, "noise-floor capture reports no signal");
        // Loud noise passes the gate but must confess weak confidence.
        for (int i = 0; i < nCap; ++i) {
            rng = rng * 1664525u + 1013904223u;
            cap[i] = (int16_t)((rng >> 16) & 4095) - 2048;
        }
        g = detectKey(cap, nCap, sr);
        CHECK(!g.valid || g.confidence < 0.5f, "loud noise: invalid or low confidence");

        // Incremental path (the device listens in rounds — normalized, since
        // that's what listen_screen feeds): summing a progression's chroma
        // segment by segment must agree with the single-shot detector, and
        // evidence must ACCUMULATE — two rounds of the same song classify at
        // least as confidently as one.
        renderProg(cap, nCap, cMajor, 4, 3, 1.f, 2200.f);
        CHECK(segmentAudible(cap, nCap), "music rises above the silence floor");
        {
            float acc[12] = {0.f};
            const int half = nCap / 2;
            accumulateChromaNormalized(cap, half, sr, acc);
            const KeyGuess one = classifyChroma(acc);
            accumulateChromaNormalized(cap + half, nCap - half, sr, acc);
            const KeyGuess two = classifyChroma(acc);
            CHECK(two.valid && two.rootPc == 0 && !two.minor,
                  "segment-summed chroma detects C major");
            // More evidence may legitimately NARROW an inflated early margin
            // (the first half never heard G major's B/D, so its rival scored
            // artificially low) — but a clear verdict must stay clear.
            CHECK(one.valid && one.confidence >= 0.5f && two.confidence >= 0.5f,
                  "the verdict stays clearly lockable as evidence accumulates");
        }
        for (int i = 0; i < nCap; ++i) cap[i] = 0;
        CHECK(!segmentAudible(cap, nCap), "silence stays under the floor");

        // Quiet music (a phone across the room): far below the old loudness
        // gate but perfectly tonal — must DETECT, not report NO SIGNAL. The
        // flatness gate is what refuses noise; loudness was never the test.
        renderProg(cap, nCap, cMajor, 4, 3, 1.f, 15.f);
        CHECK(segmentAudible(cap, nCap), "quiet music passes the audibility gate");
        g = detectKey(cap, nCap, sr);
        CHECK(g.valid && g.rootPc == 0 && !g.minor, "quiet C major still detected");

        // Percussive/broadband contamination: the same song under loud noise
        // (SNR ~8 dB). The per-octave floor subtraction must keep this
        // classifiable instead of letting the pedestal read as "flat / no key".
        renderProg(cap, nCap, cMajor, 4, 3, 1.f, 2200.f);
        {
            uint32_t nr = 999u;
            for (int i = 0; i < nCap; ++i) {
                nr = nr * 1664525u + 1013904223u;
                int v = (int)cap[i] + (int)((nr >> 16) & 4095) - 2048;
                cap[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
            }
        }
        g = detectKey(cap, nCap, sr);
        CHECK(g.valid && g.rootPc == 0 && !g.minor, "C major survives broadband noise");

        // One loud wrong chord must not out-vote a quiet honest song: rounds
        // accumulate NORMALIZED, so each audible round is one vote regardless
        // of level (classification itself was always level-invariant).
        {
            float acc[12] = {0.f};
            const int fsharp[3] = {66, 70, 73};  // F# major: maximally foreign to C
            renderProg(cap, nCap, fsharp, 1, 3, 1.f, 2200.f);
            accumulateChromaNormalized(cap, nCap, sr, acc);
            renderProg(cap, nCap, cMajor, 4, 3, 1.f, 60.f);
            accumulateChromaNormalized(cap, nCap, sr, acc);
            accumulateChromaNormalized(cap, nCap, sr, acc);
            const KeyGuess ng = classifyChroma(acc);
            CHECK(ng.valid && ng.rootPc == 0 && !ng.minor,
                  "two quiet honest rounds out-vote one loud wrong chord");
        }

        // Outcome-aware confidence: the relative twin (C maj <-> A min) maps
        // to the SAME applied root under the player's scale, so its closeness
        // is harmless and must not deflate the lock confidence. A genuinely
        // different key (a fifth off) still must.
        {
            float acc[12] = {0.f};
            renderProg(cap, nCap, aMinor, 4, 3, 1.f, 2200.f);
            accumulateChroma(cap, nCap, sr, acc);
            const KeyGuess plain = classifyChroma(acc);
            const KeyGuess aware = classifyChromaForScale(acc, SC_MIN_PENT);
            CHECK(aware.valid && aware.rootPc == plain.rootPc &&
                      aware.minor == plain.minor,
                  "scale-aware classify agrees on the key");
            CHECK(aware.confidence >= plain.confidence,
                  "excluding same-outcome rivals never lowers confidence");
            // A hand-built ambiguous chroma is where the twin actually bites
            // (clean renders saturate both margins at the 1.0 clamp): an
            // A-minor-leaning profile whose C-major twin runs a close second.
            // Plain confidence is deflated by the twin; the scale-aware one is
            // not — that gap is the whole point of the function.
            float amb[12] = {0.95f, 0.08f, 0.30f, 0.08f, 0.90f, 0.30f,
                             0.08f, 0.45f, 0.08f, 1.00f, 0.08f, 0.20f};
            const KeyGuess p1 = classifyChroma(amb);
            const KeyGuess a1 = classifyChromaForScale(amb, SC_MIN_PENT);
            CHECK(p1.valid && a1.valid, "ambiguous chroma still classifies");
            CHECK((p1.rootPc == 9 && p1.minor) || (p1.rootPc == 0 && !p1.minor),
                  "ambiguous chroma lands on the relative pair");
            CHECK(p1.confidence < 1.f, "twin keeps the plain margin off the clamp");
            CHECK(a1.confidence > p1.confidence,
                  "the lock hardens once the twin stops counting");
            float zero[12] = {0.f};
            CHECK(!classifyChromaForScale(zero, SC_MIN_PENT).valid,
                  "scale-aware classify still refuses silence");
        }

        // Relative-key mapping: the player's scale decides the applied root.
        CHECK(scaleIsMinorish(SC_MIN_PENT), "minor pent is minorish");
        CHECK(scaleIsMinorish(SC_DORIAN), "dorian's parent has a minor third");
        CHECK(scaleIsMinorish(SC_BLUES), "blues borrows the minor parent");
        CHECK(!scaleIsMinorish(SC_MAJ_PENT), "major pent is majorish");
        CHECK(!scaleIsMinorish(SC_PHR_DOM), "phrygian dominant is majorish");
        CHECK(!scaleIsMinorish(SC_CHROM), "chromatic borrows the major parent");
        CHECK(!scaleIsMinorish(SC_WHOLE), "whole tone borrows lydian");
        CHECK(applyRootForScale(0, false, SC_MIN_PENT) == 9,
              "C major song + minor pent -> root A (relative minor)");
        CHECK(applyRootForScale(9, true, SC_MAJ_PENT) == 0,
              "A minor song + major pent -> root C (relative major)");
        CHECK(applyRootForScale(0, false, SC_MAJOR) == 0,
              "C major song + major scale -> root C untouched");
        CHECK(applyRootForScale(9, true, SC_MIN_PENT) == 9,
              "A minor song + minor pent -> root A untouched");

        // Auto-scale: the four vanilla scales swap to their opposite-mode
        // sibling when the detected mode disagrees, so the home key can land
        // on the song's true tonic. Exotic scales — a deliberate flavor
        // choice — never move.
        CHECK(applyScaleForKey(SC_MAJOR, true) == SC_MINOR,
              "minor song swaps Major -> Natural minor");
        CHECK(applyScaleForKey(SC_MINOR, false) == SC_MAJOR,
              "major song swaps Natural minor -> Major");
        CHECK(applyScaleForKey(SC_MAJ_PENT, true) == SC_MIN_PENT,
              "minor song swaps Maj pent -> Min pent");
        CHECK(applyScaleForKey(SC_MIN_PENT, false) == SC_MAJ_PENT,
              "major song swaps Min pent -> Maj pent");
        CHECK(applyScaleForKey(SC_MAJOR, false) == SC_MAJOR,
              "matching mode leaves Major alone");
        CHECK(applyScaleForKey(SC_MIN_PENT, true) == SC_MIN_PENT,
              "matching mode leaves Min pent alone");
        CHECK(applyScaleForKey(SC_BLUES, false) == SC_BLUES &&
                  applyScaleForKey(SC_BLUES, true) == SC_BLUES,
              "Blues never moves");
        CHECK(applyScaleForKey(SC_DORIAN, false) == SC_DORIAN &&
                  applyScaleForKey(SC_CHROM, true) == SC_CHROM &&
                  applyScaleForKey(SC_HIRA, false) == SC_HIRA,
              "exotic scales never move");
        CHECK(applyScaleForKey(-1, true) == -1 &&
                  applyScaleForKey(kScaleCount, false) == kScaleCount,
              "out-of-range scale index passes through");
        // Composition: for the vanilla scales, applyRootForScale under the
        // POST-swap scale is the identity — the applied root IS the detected
        // tonic, both modes, every pitch class.
        {
            const int vanilla[4] = {SC_MAJOR, SC_MINOR, SC_MAJ_PENT, SC_MIN_PENT};
            bool tonicAlways = true;
            for (int s = 0; s < 4; ++s)
                for (int m = 0; m < 2; ++m)
                    for (int pc = 0; pc < 12; ++pc)
                        if (applyRootForScale(pc, m != 0,
                                              applyScaleForKey(vanilla[s], m != 0)) != pc)
                            tonicAlways = false;
            CHECK(tonicAlways,
                  "post-swap root is the true tonic for every vanilla scale");
        }

        // Chroma-refined auto-scale: within the plain seven-note canvases the
        // distinguishing degree's own energy picks the MODE — the Krumhansl
        // profiles only ever answer major-or-minor, and a Dorian vamp under
        // Natural minor plays a sour b6 (the "right key, wrong scale" failure).
        {
            // A Dorian: strong natural 6 (F#, pc 6), b6 (F, pc 5) absent.
            float dor[12] = {0.f};
            dor[9] = 1.f; dor[0] = .6f; dor[2] = .5f; dor[4] = .7f;
            dor[6] = .45f; dor[7] = .5f; dor[11] = .3f; dor[5] = .02f;
            CHECK(applyScaleForKeyChroma(SC_MINOR, true, dor, 9) == SC_DORIAN,
                  "strong natural 6 moves Natural minor -> Dorian");
            CHECK(applyScaleForKeyChroma(SC_DORIAN, true, dor, 9) == SC_DORIAN,
                  "a Dorian player stays in Dorian");
            // A natural minor: strong b6 (F, pc 5), natural 6 (F#) absent.
            float nat[12] = {0.f};
            nat[9] = 1.f; nat[0] = .6f; nat[2] = .5f; nat[4] = .7f;
            nat[5] = .45f; nat[7] = .5f; nat[11] = .3f; nat[6] = .02f;
            CHECK(applyScaleForKeyChroma(SC_DORIAN, true, nat, 9) == SC_MINOR,
                  "strong b6 moves Dorian -> Natural minor");
            CHECK(applyScaleForKeyChroma(SC_MINOR, true, nat, 9) == SC_MINOR,
                  "Natural minor stays under a b6 song");
            // G Mixolydian: strong b7 (F, pc 5), major 7 (F#, pc 6) absent.
            float mix[12] = {0.f};
            mix[7] = 1.f; mix[11] = .6f; mix[2] = .7f; mix[0] = .5f;
            mix[5] = .5f; mix[9] = .4f; mix[4] = .3f; mix[6] = .02f;
            CHECK(applyScaleForKeyChroma(SC_MAJOR, false, mix, 7) == SC_MIXO,
                  "strong b7 moves Major -> Mixolydian");
            CHECK(applyScaleForKeyChroma(SC_MIXO, false, mix, 7) == SC_MIXO,
                  "a Mixolydian player stays put");
            // G major: strong major 7, b7 absent — moves a Mixo player home.
            float gmaj[12] = {0.f};
            gmaj[7] = 1.f; gmaj[11] = .6f; gmaj[2] = .7f; gmaj[0] = .5f;
            gmaj[6] = .45f; gmaj[9] = .4f; gmaj[4] = .3f; gmaj[5] = .02f;
            CHECK(applyScaleForKeyChroma(SC_MIXO, false, gmaj, 7) == SC_MAJOR,
                  "strong major 7 moves Mixolydian -> Major");
            // Weak evidence (a pentatonic-ish song voicing neither 6th): no
            // switch on a coin flip — the player's mode holds if it's on the
            // detected side, else the side's plain default.
            float pentish[12] = {0.f};
            pentish[9] = 1.f; pentish[0] = .6f; pentish[2] = .5f;
            pentish[4] = .7f; pentish[7] = .5f;
            CHECK(applyScaleForKeyChroma(SC_DORIAN, true, pentish, 9) == SC_DORIAN,
                  "no 6th evidence: Dorian holds");
            CHECK(applyScaleForKeyChroma(SC_MINOR, true, pentish, 9) == SC_MINOR,
                  "no 6th evidence: Natural minor holds");
            CHECK(applyScaleForKeyChroma(SC_MIXO, true, pentish, 9) == SC_MINOR,
                  "crossing sides without evidence lands on the plain default");
            // Pentatonics and deliberate flavors: exactly the frozen behavior.
            CHECK(applyScaleForKeyChroma(SC_MIN_PENT, false, dor, 9) == SC_MAJ_PENT,
                  "pent swap is untouched by the refinement");
            CHECK(applyScaleForKeyChroma(SC_BLUES, true, dor, 9) == SC_BLUES &&
                      applyScaleForKeyChroma(SC_HIRA, false, dor, 9) == SC_HIRA,
                  "deliberate flavors still never move");
            // The tonic invariant extends to the refined modes: Dorian is
            // minorish and Mixolydian majorish, so applyRootForScale under
            // the refined scale still returns the song's true tonic.
            CHECK(applyRootForScale(9, true, SC_DORIAN) == 9 &&
                      applyRootForScale(7, false, SC_MIXO) == 7,
                  "refined modes keep the true tonic");
        }

        // The full listen verdict (applyListen): mode + tonic tiebreak +
        // family mapping. This is the "shuffle a playlist, hit fn+k, land
        // right in YOUR scale" contract.
        {
            auto makeGuess = [](int rootPc, bool minor, const float ch[12]) {
                KeyGuess g = KeyGuess::make();
                g.valid = true;
                g.rootPc = rootPc;
                g.minor = minor;
                for (int i = 0; i < 12; ++i) g.chroma[i] = ch[i];
                return g;
            };

            // A Dorian evidence at a detected A minor: every family lands
            // tonic-home on A; the plain canvas plays Dorian itself.
            float dor[12] = {0.f};
            dor[9] = 1.f; dor[0] = .6f; dor[2] = .5f; dor[4] = .7f;
            dor[6] = .45f; dor[7] = .5f; dor[11] = .3f; dor[5] = .02f;
            const KeyGuess gAm = makeGuess(9, true, dor);
            ListenApply ap = applyListen(SC_MINOR, gAm);
            CHECK(ap.scaleIdx == SC_DORIAN && ap.rootPc == 9 && ap.modal,
                  "canvas player: Dorian song lands in Dorian at the tonic");
            ap = applyListen(SC_BLUES, gAm);
            CHECK(ap.scaleIdx == SC_BLUES && ap.rootPc == 9,
                  "Blues stays Blues and re-centres on the Dorian tonic");
            ap = applyListen(SC_MIN_PENT, gAm);
            CHECK(ap.scaleIdx == SC_MIN_PENT && ap.rootPc == 9,
                  "min pent rides the Dorian tonic");
            ap = applyListen(SC_HIRA, gAm);
            CHECK(ap.scaleIdx == SC_HIRA && ap.rootPc == 9,
                  "exotic flavors keep the frozen relative behavior");

            // The tonic tiebreak: the SAME A Dorian world heard as "D major"
            // (the Am7-D9 vamp's other honest reading). Strong C natural
            // marks it mixo-flavoured; the Dorian twin at A runs close, so
            // the tonic re-seats — and Blues lands on A, not B.
            float oye[12] = {0.f};
            oye[9] = 1.f;  oye[2] = .85f; oye[4] = .75f; oye[0] = .55f;
            oye[7] = .6f;  oye[6] = .4f;  oye[11] = .25f;
            const KeyGuess gDmaj = makeGuess(2, false, oye);
            ap = applyListen(SC_BLUES, gDmaj);
            CHECK(ap.tiebreak && ap.mode == LM_DOR && ap.tonicPc == 9,
                  "mixo-flavoured D major re-seats as the A Dorian vamp");
            CHECK(ap.scaleIdx == SC_BLUES && ap.rootPc == 9,
                  "Blues over Oye Como Va lands home on A");
            ap = applyListen(SC_MINOR, gDmaj);
            CHECK(ap.scaleIdx == SC_DORIAN && ap.rootPc == 9,
                  "canvas player gets Dorian at A from the D-major reading");
            // End-to-end: whatever twin the profiles pick from this chroma,
            // the applied root must be A.
            const KeyGuess gAuto = classifyChroma(oye);
            CHECK(gAuto.valid, "the vamp chroma classifies");
            ap = applyListen(SC_BLUES, gAuto);
            CHECK(ap.rootPc == 9,
                  "whichever twin wins the profiles, Blues lands on A");

            // A genuinely tonic-clear Mixolydian song (a G7 vamp): the twin
            // is far, so NO tiebreak — canvas gets Mixo at G, Blues gets the
            // dominant-blues tonic, maj pent rides tonic-home in-set.
            float gmix[12] = {0.f};
            gmix[7] = 1.f; gmix[11] = .55f; gmix[2] = .6f; gmix[5] = .45f;
            gmix[0] = .35f; gmix[4] = .3f; gmix[9] = .25f; gmix[6] = .02f;
            const KeyGuess gG = makeGuess(7, false, gmix);
            ap = applyListen(SC_MAJOR, gG);
            CHECK(ap.scaleIdx == SC_MIXO && ap.rootPc == 7 && !ap.tiebreak,
                  "tonic-clear mixo: canvas plays Mixolydian at the tonic");
            ap = applyListen(SC_BLUES, gG);
            CHECK(ap.scaleIdx == SC_BLUES && ap.rootPc == 7,
                  "dominant blues: Blues sits on the mixo tonic");
            ap = applyListen(SC_MAJ_PENT, gG);
            CHECK(ap.scaleIdx == SC_MAJ_PENT && ap.rootPc == 7,
                  "maj pent rides the mixo tonic, fully in-set");

            // A plain Ionian song: Blues keeps the boxes trick (relative
            // minor root), pents sit tonic-home — the frozen promises.
            float ion[12] = {0.f};
            ion[0] = 1.f; ion[4] = .7f; ion[7] = .65f; ion[11] = .45f;
            ion[2] = .4f; ion[5] = .35f; ion[9] = .3f; ion[10] = .02f;
            const KeyGuess gC = makeGuess(0, false, ion);
            ap = applyListen(SC_BLUES, gC);
            CHECK(ap.scaleIdx == SC_BLUES && ap.rootPc == 9,
                  "Ionian song: Blues keeps the relative-minor boxes trick");
            ap = applyListen(SC_MIN_PENT, gC);
            CHECK(ap.scaleIdx == SC_MAJ_PENT && ap.rootPc == 0,
                  "Ionian song: pent swaps and sits tonic-home");

            // No readable evidence: EVERY family collapses to the frozen
            // behavior exactly.
            float pentish[12] = {0.f};
            pentish[9] = 1.f; pentish[0] = .6f; pentish[2] = .5f;
            pentish[4] = .7f; pentish[7] = .5f;
            const KeyGuess gPent = makeGuess(9, true, pentish);
            const int frozenScales[5] = {SC_MINOR, SC_BLUES, SC_MIN_PENT,
                                         SC_MAJ_PENT, SC_HIRA};
            bool frozenOk = true;
            for (int i = 0; i < 5; ++i) {
                const ListenApply a2 = applyListen(frozenScales[i], gPent);
                const int fs = applyScaleForKey(frozenScales[i], true);
                const int fr = applyRootForScale(9, true, fs);
                if (a2.scaleIdx != fs || a2.rootPc != fr) frozenOk = false;
            }
            CHECK(frozenOk, "no evidence: applyListen == the frozen behavior");

            // The Ionian-parent re-seat: a G-major song heard D-first (the
            // Wish You Were Here capture). The C natural is REAL (the verse
            // chords), so the D reading is honestly mixo-flavoured — but the
            // set's Ionian parent G runs close while the Dorian twin is far,
            // so the tonic re-seats HOME and every family lands where the
            // record's own solos do.
            float wy[12] = {0.f};
            wy[2] = 1.f; wy[9] = .7f; wy[6] = .55f; wy[7] = .68f;
            wy[0] = .5f; wy[4] = .45f; wy[11] = .35f; wy[5] = .03f; wy[1] = .03f;
            const KeyGuess gWy = makeGuess(2, false, wy);
            ap = applyListen(SC_MAJOR, gWy);
            CHECK(ap.tiebreak && ap.mode == LM_ION && ap.tonicPc == 7,
                  "D-heard G-major song re-seats to the Ionian parent");
            CHECK(ap.scaleIdx == SC_MAJOR && ap.rootPc == 7,
                  "canvas lands plain Major at G");
            ap = applyListen(SC_BLUES, gWy);
            CHECK(ap.scaleIdx == SC_BLUES && ap.rootPc == 4,
                  "Blues lands on E, where the record's own solos live");

            // Clean verdicts never trip the safety layers.
            CHECK(!applyListen(SC_MINOR, gAm).safe &&
                      !applyListen(SC_MAJOR, gG).safe &&
                      !applyListen(SC_MAJOR, gWy).safe,
                  "clean landings are never flagged safe");

            // CONFLICTED DEGREE: a song that audibly plays BOTH 6ths
            // (borrowed chords, melodic-minor lines) makes any seven-note
            // landing a coin flip — canvas players retreat to the side's
            // pentatonic at the tonic, which omits the clash degree.
            float con6[12] = {0.f};
            con6[9] = 1.f; con6[0] = .6f; con6[2] = .5f; con6[4] = .7f;
            con6[7] = .5f; con6[6] = .5f; con6[5] = .45f; con6[11] = .3f;
            const KeyGuess gCon6 = makeGuess(9, true, con6);
            ap = applyListen(SC_MINOR, gCon6);
            CHECK(ap.scaleIdx == SC_MIN_PENT && ap.rootPc == 9 && ap.safe,
                  "both 6ths audible: Natural minor retreats to min pent");
            ap = applyListen(SC_DORIAN, gCon6);
            CHECK(ap.scaleIdx == SC_MIN_PENT && ap.rootPc == 9 && ap.safe,
                  "both 6ths audible: Dorian retreats to min pent");
            ap = applyListen(SC_BLUES, gCon6);
            CHECK(ap.scaleIdx == SC_BLUES && ap.rootPc == 9 && !ap.safe,
                  "flavor scales are never demoted by a conflict");
            ap = applyListen(SC_MIN_PENT, gCon6);
            CHECK(ap.scaleIdx == SC_MIN_PENT && ap.rootPc == 9,
                  "pent players are already on the safe landing");

            // Both 7ths audible on the major side: same retreat. The b7 arm
            // of the conflict answers to the phantom-aware 0.30 floor.
            float con7[12] = {0.f};
            con7[7] = 1.f; con7[11] = .55f; con7[2] = .6f; con7[0] = .4f;
            con7[4] = .35f; con7[9] = .3f; con7[6] = .45f; con7[5] = .4f;
            const KeyGuess gCon7 = makeGuess(7, false, con7);
            ap = applyListen(SC_MAJOR, gCon7);
            CHECK(ap.scaleIdx == SC_MAJ_PENT && ap.rootPc == 7 && ap.safe,
                  "both 7ths audible: Major retreats to maj pent");

            // SOURNESS: the clash can live on a degree the 6th/7th evidence
            // never looks at. A Lydian song's #4 indicts the canvas P4 —
            // maj pent omits the 4 entirely, so the landing retreats.
            float lyd[12] = {0.f};
            lyd[0] = 1.f; lyd[2] = .5f; lyd[4] = .7f; lyd[6] = .5f;
            lyd[7] = .6f; lyd[9] = .35f; lyd[11] = .4f; lyd[5] = .03f;
            const KeyGuess gLyd = makeGuess(0, false, lyd);
            ap = applyListen(SC_MAJOR, gLyd);
            CHECK(ap.scaleIdx == SC_MAJ_PENT && ap.rootPc == 0 && ap.safe,
                  "Lydian song: Major's sour P4 retreats to maj pent");

            // A Phrygian song's b2 indicts the canvas natural 2 — min pent
            // omits the 2, so a Natural-minor landing retreats.
            float phr[12] = {0.f};
            phr[4] = 1.f; phr[5] = .5f; phr[7] = .6f; phr[9] = .55f;
            phr[11] = .5f; phr[0] = .45f; phr[2] = .4f; phr[6] = .03f;
            phr[1] = .02f;
            const KeyGuess gPhr = makeGuess(4, true, phr);
            ap = applyListen(SC_MINOR, gPhr);
            CHECK(ap.scaleIdx == SC_MIN_PENT && ap.rootPc == 4 && ap.safe,
                  "Phrygian song: minor's sour 2 retreats to min pent");

            // ALTERNATES: primary first (== applyListen), then the side's
            // sibling readings at the tonic and the relative twin — all
            // distinct (scale, root) pairs. Feeds the card's one-tap nudge.
            {
                ListenApply alts[4];
                const int na = listenAlternates(SC_MINOR, gAm, alts, 4);
                const ListenApply prim = applyListen(SC_MINOR, gAm);
                CHECK(na == 4 && alts[0].scaleIdx == prim.scaleIdx &&
                          alts[0].rootPc == prim.rootPc,
                      "alternates lead with the primary verdict");
                bool distinct = true;
                for (int i = 0; i < na; ++i)
                    for (int j = i + 1; j < na; ++j)
                        if (alts[i].scaleIdx == alts[j].scaleIdx &&
                            alts[i].rootPc == alts[j].rootPc)
                            distinct = false;
                CHECK(distinct, "alternates never repeat a (scale, root)");
                bool hasPent = false, hasTwin = false;
                for (int i = 0; i < na; ++i) {
                    if (alts[i].scaleIdx == SC_MIN_PENT && alts[i].rootPc == 9)
                        hasPent = true;
                    if (alts[i].scaleIdx == SC_MAJOR && alts[i].rootPc == 0)
                        hasTwin = true;
                }
                CHECK(hasPent && hasTwin,
                      "alternates offer the safe pent and the relative twin");
                // Flavor players keep their scale: only the root can move.
                ListenApply balts[4];
                const int nb = listenAlternates(SC_BLUES, gC, balts, 4);
                bool bluesOnly = nb >= 2;
                for (int i = 0; i < nb; ++i)
                    if (balts[i].scaleIdx != SC_BLUES) bluesOnly = false;
                CHECK(bluesOnly && balts[0].rootPc == 9 && balts[1].rootPc == 0,
                      "Blues alternates move the root, never the scale");
            }

            // RUNNER-UP KEYS: the sibling alternates all sit at the primary
            // tonic (or its relative), so a verdict whose TONIC is wrong
            // leaves space cycling with nothing that can reach the song's
            // true home. The field shape: an E natural minor song heard as
            // A minor — A Dorian is E minor's exact pitch set, so nothing
            // plays sour and no guard fires, yet home is a fourth off. With
            // room past the siblings (cap 6), the card must offer the
            // detector's own runner-up keys, and E minor must be among them.
            {
                float em[12] = {0.f};
                em[4] = 1.f; em[7] = .7f; em[11] = .65f; em[9] = .5f;
                em[2] = .5f; em[0] = .45f; em[6] = .4f;
                const KeyGuess gEmWrong = makeGuess(9, true, em);
                ListenApply ralts[6];
                const int nr = listenAlternates(SC_MINOR, gEmWrong, ralts, 6);
                CHECK(nr > 4, "runner-up keys extend past the sibling set");
                bool distinct = true;
                for (int i = 0; i < nr; ++i)
                    for (int j = i + 1; j < nr; ++j)
                        if (ralts[i].scaleIdx == ralts[j].scaleIdx &&
                            ralts[i].rootPc == ralts[j].rootPc)
                            distinct = false;
                CHECK(distinct, "runner-up alternates never repeat a landing");
                bool hasTrueHome = false;
                for (int i = 0; i < nr; ++i)
                    if (ralts[i].scaleIdx == SC_MINOR && ralts[i].rootPc == 4)
                        hasTrueHome = true;
                CHECK(hasTrueHome,
                      "wrong-tonic verdict: space can reach E minor");
                // The old cap keeps the old contract exactly: siblings fill
                // it and no runner-up displaces them.
                ListenApply capped[4];
                const int ncap = listenAlternates(SC_MINOR, gEmWrong, capped, 4);
                CHECK(ncap == 4 && capped[3].scaleIdx == SC_MAJOR,
                      "cap 4 still ends on the relative twin, unchanged");
                // Flavor players: runner-ups move the root only, the chosen
                // scale never changes even with room to spare.
                ListenApply falts[6];
                const int nf = listenAlternates(SC_BLUES, gC, falts, 6);
                bool fBluesOnly = nf > 2;
                for (int i = 0; i < nf; ++i)
                    if (falts[i].scaleIdx != SC_BLUES) fBluesOnly = false;
                CHECK(fBluesOnly,
                      "flavor runner-ups keep the scale, offer new roots");
            }
        }

        // The 7th-harmonic trap: a bright tonic paints its own b7 two
        // octaves up (7f = two octaves + a minor 7th - 31 cents, inside the
        // constant-Q bin), so guitar-bright major music kept reading
        // Mixolydian (measured: Wish You Were Here locked G MIXO on a song
        // with no F natural in it). A plain major chord with strong odd
        // partials must not leave enough phantom b7 to clear the
        // mode-evidence floor.
        {
            // The tonic in two octaves (a song leans on its root) plus the
            // triad, all with guitar-bright partials through a small
            // speaker's distortion: harmonics 2, 3, and a strong 7th.
            const int chord[4] = {48, 60, 52, 55};  // C3 C4 E3 G3
            for (int i = 0; i < nCap; ++i) cap[i] = 0;
            for (int t = 0; t < 4; ++t) {
                const float f = 440.f * powf(2.f, (chord[t] - 69) / 12.f);
                for (int i = 0; i < nCap; ++i) {
                    const float ph = 6.2831853f * f * (float)i / sr;
                    cap[i] = (int16_t)((float)cap[i] +
                                       (sinf(ph) + .5f * sinf(2.f * ph) +
                                        .25f * sinf(3.f * ph) +
                                        .5f * sinf(7.f * ph)) *
                                           1800.f);
                }
            }
            float acc[12] = {0.f};
            accumulateChroma(cap, nCap, sr, acc);
            float pk = 0.f;
            for (int i = 0; i < 12; ++i)
                if (acc[i] > pk) pk = acc[i];
            CHECK(pk > 0.f && acc[10] < 0.20f * pk,
                  "bright partials: the phantom b7 stays under the mode floor");
        }
    }

    // ---- tempo detection (LISTEN) ----------------------------------------
    {
        constexpr float sr = 16000.f;
        constexpr int nRound = (int)(sr * 3);  // fed in 3 s rounds, like the device
        static int16_t tcap[nRound];

        // Clicks on a beat grid: a 4 ms 2 kHz burst per beat, optionally with
        // weaker offbeat clicks (strongEvery) — the eighth-note texture real
        // music has. t0 is the round's global sample offset so rounds join
        // into one continuous grid.
        auto renderClicks = [](int16_t* out, int n, float clickBpm, long t0,
                               int strongEvery, float weakAmp) {
            const float period = sr * 60.f / clickBpm;
            for (int i = 0; i < n; ++i) {
                const long gpos = t0 + i;
                const long beat = (long)((float)gpos / period);
                const float ph = (float)gpos - (float)beat * period;
                float a = 0.f;
                if (ph < 64.f) {
                    const bool strong = strongEvery <= 1 || (beat % strongEvery) == 0;
                    a = (strong ? 9000.f : weakAmp) * (1.f - ph / 64.f);
                }
                out[i] = (int16_t)(a * sinf(6.2831853f * 2000.f * (float)gpos / sr));
            }
        };
        auto tempoOf = [&](float clickBpm, int rounds, int strongEvery,
                           float weakAmp) {
            BeatState st = BeatState::make();
            for (int r = 0; r < rounds; ++r) {
                renderClicks(tcap, nRound, clickBpm, (long)r * nRound, strongEvery,
                             weakAmp);
                accumulateOnsets(st, tcap, nRound, sr);
            }
            return estimateTempo(st);
        };

        TempoGuess t = tempoOf(120.f, 2, 1, 0.f);
        CHECK(t.valid && fabsf(t.bpm - 120.f) < 3.f, "120 BPM clicks land on 120");
        CHECK(t.confidence > 0.35f, "a clean click track is a confident beat");
        t = tempoOf(90.f, 2, 1, 0.f);
        CHECK(t.valid && fabsf(t.bpm - 90.f) < 3.f,
              "90 BPM lands despite the off-grid period");
        t = tempoOf(132.f, 2, 1, 0.f);
        CHECK(t.valid && fabsf(t.bpm - 132.f) < 3.f, "132 BPM lands");

        // Eighth-note texture: weak offbeats between strong beats. The strong
        // period must win over the twice-as-fast click grid (the octave-error
        // case the prior + alternating autocorrelation exist for).
        t = tempoOf(240.f, 2, 2, 3500.f);
        CHECK(t.valid && fabsf(t.bpm - 120.f) < 3.f,
              "eighth-note offbeats: the quarter-note pulse wins");

        // Beatless content must refuse, never guess: a steady tone has no
        // onsets, and broadband noise has no periodic ones.
        {
            BeatState st = BeatState::make();
            for (int i = 0; i < nRound; ++i)
                tcap[i] = (int16_t)(6000.f * sinf(6.2831853f * 220.f * (float)i / sr));
            accumulateOnsets(st, tcap, nRound, sr);
            accumulateOnsets(st, tcap, nRound, sr);
            CHECK(!estimateTempo(st).valid, "a steady tone reports no beat");
        }
        {
            BeatState st = BeatState::make();
            uint32_t rng = 77777u;
            for (int r = 0; r < 2; ++r) {
                for (int i = 0; i < nRound; ++i) {
                    rng = rng * 1664525u + 1013904223u;
                    tcap[i] = (int16_t)((rng >> 16) & 8191) - 4096;
                }
                accumulateOnsets(st, tcap, nRound, sr);
            }
            const TempoGuess n = estimateTempo(st);
            CHECK(!n.valid || n.confidence < 0.35f,
                  "noise: no beat, or one too weak to apply");
        }

        // Too little evidence refuses (the guard, not a guess).
        {
            BeatState st = BeatState::make();
            renderClicks(tcap, (int)(sr * 0.5f), 120.f, 0, 1, 0.f);
            accumulateOnsets(st, tcap, (int)(sr * 0.5f), sr);
            CHECK(!estimateTempo(st).valid, "half a second cannot name a tempo");
        }

        // Determinism: the same capture names the same tempo, exactly.
        const TempoGuess a = tempoOf(120.f, 2, 1, 0.f);
        const TempoGuess b = tempoOf(120.f, 2, 1, 0.f);
        CHECK(a.valid && b.valid && a.bpm == b.bpm && a.confidence == b.confidence,
              "tempo estimation is deterministic");
    }

    // ---- SD patch-name rules -------------------------------------------
    // Case and spaces are preserved (the card's FatFs has long filenames on);
    // FAT's case-INSENSITIVE lookup is what patchNameEqualsFold guards.
    {
        char out[store::kMaxPatchNameLen + 1];
        auto san = [&](const char* in) {
            store::sanitizePatchName(in, out, sizeof out);
            return (const char*)out;
        };
        CHECK(strcmp(san("Big Bass"), "Big Bass") == 0, "capitals and spaces survive");
        CHECK(strcmp(san("warm-haze-3f2a"), "warm-haze-3f2a") == 0,
              "the generated auto-names are unchanged by the new rules");
        CHECK(strcmp(san("My/Bass?!"), "MyBass") == 0, "FAT-illegal chars are dropped");
        CHECK(strcmp(san("Big / Bass"), "Big Bass") == 0,
              "spaces AROUND a dropped char still separate the words");
        CHECK(strcmp(san("   Pad   "), "Pad") == 0, "leading/trailing spaces trimmed");
        CHECK(strcmp(san("Big   Bass"), "Big Bass") == 0, "runs of spaces collapse");
        CHECK(strcmp(san(""), "patch") == 0, "empty name -> patch");
        CHECK(strcmp(san("!!!"), "patch") == 0, "all-illegal name -> patch");
        CHECK(strcmp(san("   "), "patch") == 0, "all-space name -> patch");
        // truncation must never leave a name ending in a space: "12345678901234567890"
        // is exactly the limit, so the 20-char boundary lands mid-word here
        CHECK((int)strlen(san("ABCDEFGHIJKLMNOPQRSTUVWXYZ")) == store::kMaxPatchNameLen,
              "over-long name truncates to the limit");
        CHECK(strcmp(san("ABCDEFGHIJKLMNOPQRS TUV"), "ABCDEFGHIJKLMNOPQRS") == 0,
              "truncation drops the trailing space rather than ending on one");
        // idempotence: sanitize runs again inside makePath/exists/remove, so a
        // second pass must be a no-op or a rename could chase its own tail
        {
            const char* cases[] = {"Big Bass", "My/Bass?!", "   Pad   ", "Big   Bass",
                                   "", "ABCDEFGHIJKLMNOPQRS TUV", "warm-haze-3f2a"};
            bool stable = true;
            for (const char* c : cases) {
                char once[store::kMaxPatchNameLen + 1], twice[store::kMaxPatchNameLen + 1];
                store::sanitizePatchName(c, once, sizeof once);
                store::sanitizePatchName(once, twice, sizeof twice);
                if (strcmp(once, twice) != 0) stable = false;
            }
            CHECK(stable, "sanitizePatchName is idempotent");
        }
        // the fold compare: this is what stops a case-only rename from deleting
        // the file it just wrote (ui/sd_browser.cpp)
        CHECK(store::patchNameEqualsFold("big", "BIG"), "fold: big == BIG");
        CHECK(store::patchNameEqualsFold("Big Bass", "big bass"), "fold: spaces fold too");
        CHECK(store::patchNameEqualsFold("", ""), "fold: empty == empty");
        CHECK(!store::patchNameEqualsFold("big", "bigg"), "fold: prefix is not equal");
        CHECK(!store::patchNameEqualsFold("bigg", "big"), "fold: longer is not equal");
        CHECK(!store::patchNameEqualsFold("big", "bag"), "fold: different names differ");
    }

    // ---- the send-effects block (dsp/fx.cpp) -----------------------------
    // A golden checksum over the whole fx chain. This arrived as the identity
    // half of a reverb-freeze experiment that was cut on the hardware verdict;
    // the guard is worth keeping on its own, because until now nothing pinned
    // the room's output at all and it is the easiest thing in the engine to
    // perturb by accident.
    // The value is bound to the native gate's toolchain (the pio mingw gcc with
    // the SSE flags): both this input signal and the chorus LFO go through
    // libm's sinf, and glibc and mingw disagree in the last ulp, which the
    // bit-hash then amplifies. The first pin (2743211789) was captured in a
    // Linux session and never matched here although fx.cpp was byte-identical
    // to the pre-freeze source it claimed to pin; re-pinned on the gate
    // 2026-09-04. If it moves again with fx.cpp untouched, suspect the
    // toolchain before the room.
    {
        Fx fx;
        fx.init(kSr);
        SynthParams p;
        p.reverbMix = 0.5f;
        p.delayMix = 0.25f;
        p.chorusDepth = 0.4f;
        unsigned h = 2166136261u;
        for (int b = 0; b < 400; ++b) {
            float buf[kBlock];
            for (int i = 0; i < kBlock; ++i) {
                const int n = b * kBlock + i;
                buf[i] = n < 8000 ? 0.5f * sinf(6.2831853f * 220.f * (float)n / kSr) : 0.f;
            }
            fx.process(buf, kBlock, p);
            for (int i = 0; i < kBlock; ++i) {
                unsigned bits;
                memcpy(&bits, &buf[i], 4);
                h = (h ^ bits) * 16777619u;
            }
        }
        CHECK(h == 136409909u, "chorus+delay+reverb output is unchanged");
    }

    // ---- the G0 trigger macro -------------------------------------------
    {
        CHECK((int)store::TriggerAction::Count == 7,
              "the trigger action list is what the settings row expects");
        bool named = true, tagged = true;
        for (int a = 0; a < (int)store::TriggerAction::Count; ++a) {
            if (strcmp(store::triggerActionName((uint8_t)a), "?") == 0) named = false;
            if (strlen(store::triggerActionTag((uint8_t)a)) > 6) tagged = false;
        }
        CHECK(named, "every trigger action has a settings label");
        CHECK(tagged, "every trigger tag fits the 6-char scope badge");
    }

    // ---- wah + gate: the G0 MOTION macros --------------------------------
    // These run in the DSP because the 30 fps UI frame cannot place a gate edge.
    // What is pinned here is that they MOVE. The action they replaced was real,
    // measurable and imperceptible, so "audible" is the property worth a test —
    // "wired up correctly" was never the thing that went wrong.
    {
        // Per-block RMS, smoothed into an ENVELOPE. Raw block RMS is useless
        // here: a 110 Hz saw fits ~0.44 cycles in a 4 ms block, so it jitters
        // 7x on its own and buries the modulation being measured.
        auto envelope = [](uint8_t kind, float amt, float* env, int nb) {
            Synth sp;
            sp.init(kSr);
            SynthParams p;
            p.masterVol = 0.9f;
            p.cutoffHz = 1200.f;
            p.resonance = 0.2f;
            p.tempoBpm = 120.f;
            p.attackS = 0.002f;
            p.sustain = 1.f;
            sp.setParams(p);
            sp.setTrigger(kind, amt);
            sp.handleEvent(NoteEvent::make(NoteEvent::On, 251, 0xFF, false, 45.f));
            static float raw[640];
            float rawMin = 1e9f;
            for (int b = 0; b < nb; ++b) {
                float buf[kBlock];
                sp.setTrigger(kind, amt);   // republished per block, as the engine does
                sp.render(buf, kBlock);
                double a = 0.0;
                for (int i = 0; i < kBlock; ++i) a += (double)buf[i] * buf[i];
                raw[b] = (float)sqrt(a / kBlock);
                if (b > 140 && raw[b] < rawMin) rawMin = raw[b];
            }
            const int W = 16;   // 64 ms: well inside the 1 s sweep and the 125 ms chop
            for (int b = 0; b < nb; ++b) {
                float a = 0.f;
                int c = 0;
                for (int k = b - W + 1; k <= b; ++k)
                    if (k >= 0) { a += raw[k]; ++c; }
                env[b] = a / c;
            }
            return rawMin;
        };
        auto stats = [](const float* e, int nb, float& mean) {
            float lo = 1e9f, hi = 0.f;
            double sum = 0.0;
            int c = 0;
            for (int b = 140; b < nb; ++b) {   // past the attack
                if (e[b] < lo) lo = e[b];
                if (e[b] > hi) hi = e[b];
                sum += e[b]; ++c;
            }
            mean = (float)(sum / c);
            return lo > 1e-7f ? hi / lo : 1e9f;
        };

        const int kNB = 640;   // ~2.5 s
        static float eOff[640], eWah[640], eGate[640], eZero[640];
        float mOff, mWah, mGate, mZero;
        envelope((uint8_t)TrigMod::None, 0.f, eOff, kNB);
        envelope((uint8_t)TrigMod::Wah, 1.f, eWah, kNB);
        const float gateMin = envelope((uint8_t)TrigMod::Gate, 1.f, eGate, kNB);
        envelope((uint8_t)TrigMod::Gate, 0.f, eZero, kNB);

        const float sOff  = stats(eOff, kNB, mOff);
        const float sWah  = stats(eWah, kNB, mWah);
        const float sGate = stats(eGate, kNB, mGate);
        stats(eZero, kNB, mZero);

        CHECK(sOff < 1.2f, "a held note with no macro is a steady envelope");
        // measured 1.89x once the wah OWNED the filter; it was 1.20x while it
        // merely offset the patch cutoff, which is not a wah, it is a wobble
        CHECK(sWah > 1.5f, "wah swings the envelope — it sweeps, it does not nudge");
        CHECK(mWah > mOff, "the wah's resonant peak adds presence rather than dulling");
        CHECK(sGate > 50.f, "gate chops, hard");
        CHECK(gateMin < 0.02f * mOff, "the gate's closed phase really is closed");
        CHECK(mGate < 0.7f * mOff, "the chop actually removes energy");

        // depth 0 is a true bypass: the action selected, the button not pressed
        bool same = true;
        for (int b = 0; b < kNB; ++b)
            if (fabsf(eZero[b] - eOff[b]) > 1e-6f) same = false;
        CHECK(same, "an unpressed macro leaves the instrument exactly as it was");

        bool finite = true;
        for (int b = 0; b < kNB; ++b)
            if (!std::isfinite(eWah[b]) || !std::isfinite(eGate[b])) finite = false;
        CHECK(finite, "the motion macros stay finite");
    }

    // ---- the arpeggiator (dsp/arp) ---------------------------------------
    // 1/8 at 120 bpm = 8000 samples = 62.5 blocks per step: the walk, the
    // clock, the gate, and the restart-on-chord rule.
    {
        ArpConfig c;
        c.pitches[0] = 60.f; c.pitches[1] = 64.f; c.pitches[2] = 67.f;
        c.n = 3; c.on = 1; c.gen = 1;
        ArpOn on[16];
        bool ok = false;
        {
            Arp a; a.set(c);
            const int n = runArp(a, 260, on, 16, &ok);
            CHECK(n == 5, "1/8 at 120 bpm: a note every 62.5 blocks -> 5 in 260");
            CHECK(ok, "one note at a time: every On follows the last Off, all flagged backing");
            const float up[5] = {60.f, 64.f, 67.f, 72.f, 60.f};
            CHECK(arpSeq(on, n, up, 5), "up walks 1-3-5-8 and wraps");
            CHECK(on[0].block == 0, "the first strike lands on the block the chord arrives");
            bool spacing = true;
            for (int i = 1; i < 5 && i < n; ++i) {
                const int d = on[i].block - on[i - 1].block;
                if (d < 62 || d > 63) spacing = false;
            }
            CHECK(spacing, "steps are 62-63 blocks apart (8000 samples), no drift");
            CHECK(n >= 3 && on[0].id != on[1].id && on[0].id == on[2].id,
                  "ids alternate so a release tail overlaps the next attack");
            CHECK(on[0].id == kArpIdA || on[0].id == kArpIdB, "ids are the reserved arp pair");
        }
        {
            Arp a; c.pattern = 1; a.set(c);
            const int n = runArp(a, 260, on, 16, &ok);
            const float dn[5] = {72.f, 67.f, 64.f, 60.f, 72.f};
            CHECK(ok && arpSeq(on, n, dn, 5), "down walks 8-5-3-1 and wraps");
        }
        {
            Arp a; c.pattern = 2; a.set(c);
            const int n = runArp(a, 470, on, 16, &ok);
            const float ud[8] = {60.f, 64.f, 67.f, 72.f, 67.f, 64.f, 60.f, 64.f};
            CHECK(ok && arpSeq(on, n, ud, 8), "up/down bounces without repeating the ends");
        }
        {
            Arp a; c.pattern = 0; c.span = 2; a.set(c);
            const int n = runArp(a, 470, on, 16, &ok);
            const float two[8] = {60.f, 64.f, 67.f, 72.f, 76.f, 79.f, 84.f, 60.f};
            CHECK(ok && arpSeq(on, n, two, 8), "span 2 walks 1-3-5-8-10-12-15");
            c.span = 1;
        }
        {
            Arp a; c.rate = 5; a.set(c);  // 1/16: 4000 samples = 31.25 blocks
            const int n = runArp(a, 130, on, 16, &ok);
            bool spacing = n == 5;
            for (int i = 1; i < 5 && i < n; ++i) {
                const int d = on[i].block - on[i - 1].block;
                if (d < 31 || d > 32) spacing = false;
            }
            CHECK(spacing, "1/16 halves the step: 31-32 blocks, 5 notes in 130");
            c.rate = 9;
            Arp b; b.set(c);
            CHECK(runArp(b, 260, on, 16, &ok) == 5, "an unknown division falls back to eighths");
            c.rate = kArpDefaultRate;
        }
        {
            Arp a; a.set(c);
            CHECK(runArp(a, 100, on, 16, &ok, 1000.f) == 4,
                  "bpm clamps at 300: 3200-sample steps, 4 notes in 100 blocks");
        }
        {
            // a new chord (gen bump) restarts the walk on that very block
            Arp a; a.set(c);
            runArp(a, 100, on, 16, &ok);  // notes at 0 and 63
            ArpConfig c2 = c;
            c2.pitches[0] = 65.f; c2.pitches[1] = 69.f; c2.pitches[2] = 72.f;
            c2.gen = 2;
            a.set(c2);
            const int n = runArp(a, 1, on, 16, &ok);
            CHECK(n == 1 && on[0].block == 0 && fabsf(on[0].pitch - 65.f) < 1e-4f && ok,
                  "a chord change strikes the new root at once (the downbeat)");
        }
        {
            // pattern/rate edits do NOT restart: the walk carries on from where it is
            Arp a; a.set(c);
            runArp(a, 40, on, 16, &ok);  // one note so far (block 0)
            ArpConfig c2 = c; c2.pattern = 1;
            a.set(c2);
            const int n = runArp(a, 60, on, 16, &ok);  // next step lands at block 63 overall
            CHECK(n == 1 && fabsf(on[0].pitch - 67.f) < 1e-4f,
                  "switching to down mid-walk continues from step 1 (67), no re-strike");
        }
        {
            // the off edge releases exactly once, then the arp costs nothing
            Arp a; a.set(c);
            runArp(a, 10, on, 16, &ok);
            ArpConfig c2 = c; c2.on = 0;
            a.set(c2);
            NoteEvent ev[4];
            int offs = 0, total = 0;
            for (int b = 0; b < 10; ++b) {
                const int k = a.advance(kBlock, kSr, 120.f, ev, 4);
                total += k;
                for (int i = 0; i < k; ++i) if (ev[i].type == NoteEvent::Off) ++offs;
            }
            CHECK(offs == 1 && total == 1, "off edge: one Off, then silence");
            CHECK(!a.running(), "idle after the off edge");
            // ...and a chord of nothing never sounds
            ArpConfig c3 = c; c3.n = 0;
            Arp z; z.set(c3);
            CHECK(runArp(z, 50, on, 16, &ok) == 0, "no chord, no notes");
        }
        {
            // a voicing handed out of order still walks ascending
            ArpConfig c2 = c;
            c2.pitches[0] = 67.f; c2.pitches[1] = 60.f; c2.pitches[2] = 64.f;
            Arp a; a.set(c2);
            const int n = runArp(a, 260, on, 16, &ok);
            const float up[4] = {60.f, 64.f, 67.f, 72.f};
            CHECK(arpSeq(on, n, up, 4), "the walk sorts its chord low to high");
        }
        CHECK(strcmp(arpPatternName(2), "up/down") == 0 && strcmp(arpPatternName(7), "up") == 0,
              "pattern names, with a safe fallback");
    }

    // ---- the custom palette (ui/theme.cpp) -------------------------------
    // The player edits five dials; eleven roles are derived. Nothing here is
    // taste -- it is the guarantee that no combination of dials can render the
    // instrument unreadable, which is what lets us hand the player the dials
    // at all. Same role sanitizeSound plays for the generative sounds.
    {
        auto sepFromBg = [](uint16_t c) {
            return abs((int)theme::luma(c) - (int)theme::luma(theme::kBg));
        };
        // Applying a look is what publishes the derived roles to the globals.
        auto apply = [](const theme::Look& l) {
            theme::setLook(l);
            theme::setTheme(theme::customIndex());
        };
        // Every floor here is <= 100, and the ground always leaves >= 110 of
        // luminance range on the ink side (>= 145 on a dark ground), so these
        // are reachable by construction, never merely "usually".
        auto legible = [&](const char* why) {
            CHECK(sepFromBg(theme::kGreen) >= 65, why);
            CHECK(sepFromBg(theme::kAmber) >= 58, why);
            CHECK(sepFromBg(theme::kSteel) >= 45, why);
            CHECK(sepFromBg(theme::kIdle) >= 100, why);
            CHECK(sepFromBg(theme::kDim) >= 38, why);
            CHECK(sepFromBg(theme::kRed) >= 55, why);
            // grounds must SEPARATE without shouting: a panel that reads as a
            // band or a graticule that reads as a box both wreck the calm
            CHECK(sepFromBg(theme::kPanel) >= 5 && sepFromBg(theme::kPanel) <= 22, why);
            CHECK(sepFromBg(theme::kLine) >= 12 && sepFromBg(theme::kLine) <= 52, why);
        };

        // pack/unpack: the whole recipe is one u32, one NVS entry
        {
            bool ok = true, clamps = true;
            for (int h = 0; h <= theme::kLookHueMax; ++h)
                for (int g = 0; g <= theme::kLookGroundMax; g += 7) {
                    theme::Look l = {(uint8_t)h, (uint8_t)((h * 3) % 72),
                                     (uint8_t)(h % 21), (uint8_t)g, (uint8_t)(h % 21)};
                    const theme::Look r = theme::unpackLook(theme::packLook(l));
                    if (r.hue != l.hue || r.accent != l.accent || r.vivid != l.vivid ||
                        r.ground != l.ground || r.contrast != l.contrast)
                        ok = false;
                }
            CHECK(ok, "packLook/unpackLook round-trips every dial position");
            CHECK(theme::packLook({71, 71, 20, 40, 20}) < (1u << 30),
                  "the packed recipe fits in 30 bits (one NVS entry)");
            // any u32 must decode to a PLAYABLE recipe -- a corrupt or foreign
            // NVS value can never hand derive() an out-of-range dial
            for (uint32_t v = 0; v < 4096; ++v) {
                const theme::Look l = theme::unpackLook(v * 1048573u);
                if (l.hue > theme::kLookHueMax || l.accent > theme::kLookAccentMax ||
                    l.vivid > theme::kLookVividMax || l.ground > theme::kLookGroundMax ||
                    l.contrast > theme::kLookContrastMax)
                    clamps = false;
            }
            CHECK(clamps, "unpackLook clamps every dial, so any u32 is a valid recipe");
        }

        // the guardrail, swept: every rolled look, and the corners no roll picks
        {
            int darkRolls = 0, lightRolls = 0, distinct = 0;
            const int kRolls = 2000;
            for (int i = 0; i < kRolls; ++i) {
                const theme::Look l = theme::rollLook((uint32_t)(i * 2654435761u + 12345u));
                apply(l);
                legible("a rolled look is always legible");
                if (theme::darkGround()) ++darkRolls; else ++lightRolls;
                // a roll never sets accent below 8, so its three colour roles
                // must read as three colours, not one smeared hue
                if (theme::kGreen != theme::kAmber && theme::kAmber != theme::kSteel &&
                    theme::kGreen != theme::kSteel)
                    ++distinct;
            }
            CHECK(darkRolls > kRolls / 10, "rolls produce dark-ground looks");
            CHECK(lightRolls > kRolls / 10, "rolls produce light-ground looks (print/stock)");
            CHECK(distinct == kRolls, "a rolled look's three colour roles stay distinct");
        }
        {
            // the adversarial corners: fully grey, both ground rails, no
            // contrast at all -- the dial positions a player WILL try
            const theme::Look corners[] = {
                {0, 0, 0, 0, 0},    {0, 0, 0, 40, 0},   {36, 36, 20, 0, 0},
                {36, 36, 20, 40, 0}, {48, 0, 20, 20, 20}, {48, 71, 0, 21, 20},
                {12, 30, 20, 27, 14}, {12, 30, 20, 28, 14},  // straddles dark/light
            };
            for (const theme::Look& l : corners) {
                apply(l);
                legible("an extreme dial corner is still legible");
            }
            // and the whole ground sweep at max vividness, where the ink has
            // the least luminance room to move in
            for (int g = 0; g <= theme::kLookGroundMax; ++g)
                for (int h = 0; h <= theme::kLookHueMax; h += 6) {
                    apply({(uint8_t)h, 30, 20, (uint8_t)g, 0});
                    legible("max vividness / min contrast stays legible at every ground");
                }
        }

        // "custom" opens as a copy of the preset you were on
        {
            bool polarity = true, groundClose = true, legibleFit = true;
            for (int i = 0; i < theme::presetCount(); ++i) {
                theme::setTheme((uint8_t)i);
                const bool wasDark = theme::darkGround();
                const int wasBgL = theme::luma(theme::kBg);
                apply(theme::recipeForPreset((uint8_t)i));
                if (theme::darkGround() != wasDark) polarity = false;
                if (abs((int)theme::luma(theme::kBg) - wasBgL) > 40) groundClose = false;
                if (sepFromBg(theme::kGreen) < 65 || sepFromBg(theme::kIdle) < 100)
                    legibleFit = false;
            }
            CHECK(polarity, "recipeForPreset keeps every preset's dark/light polarity");
            CHECK(groundClose, "recipeForPreset lands near every preset's ground");
            CHECK(legibleFit, "a recipe fitted from a preset is itself legible");
        }

        // the ten authored palettes are FROZEN -- none of this touches them
        {
            theme::setTheme(0);
            CHECK(theme::kBg == 0x0000 && theme::kGreen == 0x07E0 &&
                      theme::kAmber == 0xFD60 && theme::kSteel == 0x42BF,
                  "phosphor is byte-identical after the custom slot was added");
            theme::setTheme(9);
            CHECK(theme::kBg == 0xAD12 && theme::kIdle == 0x0000 && !theme::darkGround(),
                  "paper is byte-identical and still reads as a light ground");
            CHECK(theme::count() == theme::presetCount() + 1,
                  "the cycle is the presets plus exactly one custom slot");
            CHECK(theme::customIndex() == (uint8_t)theme::presetCount(),
                  "custom is APPENDED, so no stored themeid changes meaning");
            CHECK(strcmp(theme::name(theme::customIndex()), "custom") == 0,
                  "the custom slot names itself");
            theme::setTheme(200);
            CHECK(theme::current() == 0, "an out-of-range themeid falls back to phosphor");
        }
    }

    if (failures == 0) {
        printf("GLIDE dsp: all checks passed\n");
        return 0;
    }
    printf("GLIDE dsp: %d FAILURES\n", failures);
    return 1;
}

#endif  // GLIDE_HOST_BUILD

