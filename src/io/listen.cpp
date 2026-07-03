#include "listen.h"

#include <M5Cardputer.h>

#include "../config.h"
#include "audio_engine.h"

namespace listen {

namespace {

constexpr int kChunk = 1600;  // 100 ms at 16 kHz — the progress heartbeat
// Preferred per-round buffer lengths, longest first; the first one the heap
// affords wins. detectKey stays accurate down to ~1.5 s rounds. All exact
// chunk multiples, and all divide the 9 s total evenly.
constexpr int kTrySamples[] = {(int)(kRateHz * 3), (int)(kRateHz * 2),
                               (int)(kRateHz * 3 / 2)};
constexpr int kMaxTotal = (int)(kRateHz * 9);  // listening budget across rounds

}  // namespace

Result capture(bool (*progress)(void* user, float frac), void* user,
               bool (*segment)(void* user, const int16_t* mono, int n)) {
    // Alloc before touching the audio path: an alloc failure must leave the
    // instrument completely undisturbed.
    int16_t* buf = nullptr;
    int total = 0;
    for (size_t i = 0; i < sizeof(kTrySamples) / sizeof(kTrySamples[0]); ++i) {
        buf = (int16_t*)heap_caps_malloc((size_t)kTrySamples[i] * sizeof(int16_t),
                                         MALLOC_CAP_8BIT);
        if (buf) {
            total = kTrySamples[i];
            break;
        }
    }
    if (!buf) return Result::AllocFailed;
    Serial.printf("[listen] capture %d samples (%.1fs), free heap %u\n", total,
                  (float)total / kRateHz, (unsigned)ESP.getFreeHeap());

    audio::suspend();

    auto& mic = M5Cardputer.Mic;
    // Mutate the config M5Unified built (pins + ES8311 record callback are
    // in there); never rebuild it from scratch.
    auto mc = mic.config();
    mc.sample_rate = kRateHz;
    mc.task_pinned_core = cfg::kRenderCore;  // keep core 1 for the UI
    mic.config(mc);

    if (!mic.isEnabled() || !mic.begin()) {
        heap_caps_free(buf);
        if (!audio::resume()) return Result::ResumeFailed;
        return Result::NoMic;
    }

    // Rounds: fill the buffer, hand it to the analyzer, repeat until it says
    // it has heard enough or the 9 s budget runs out. The brief analysis gap
    // between rounds costs nothing — chroma evidence is a running sum, not a
    // continuous recording.
    const int chunks = total / kChunk;
    const int rounds = kMaxTotal / total;
    bool cancelled = false;
    bool enough = false;
    for (int round = 0; round < rounds && !cancelled && !enough; ++round) {
        // Two chunks in flight (the mic's record queue is 2 deep, same as
        // the speaker's) so recording stays gapless while we draw.
        int queued = 0, doneChunks = 0;
        while (queued < chunks && queued < 2) {
            mic.record(buf + queued * kChunk, (size_t)kChunk, kRateHz);
            ++queued;
        }
        while (doneChunks < chunks && !cancelled) {
            while (mic.isRecording() >= 2) delay(1);
            ++doneChunks;  // ~one chunk ahead at the tail; the wait below covers it
            if (queued < chunks) {
                mic.record(buf + queued * kChunk, (size_t)kChunk, kRateHz);
                ++queued;
            }
            const float frac =
                (float)(round * chunks + doneChunks) / (float)(rounds * chunks);
            if (progress && !progress(user, frac)) cancelled = true;
        }
        while (mic.isRecording()) delay(1);  // buffer fully written past here
        if (!cancelled && segment) enough = !segment(user, buf, total);
    }
    mic.end();

    if (!audio::resume()) {
        heap_caps_free(buf);
        return Result::ResumeFailed;
    }
    heap_caps_free(buf);
    return cancelled ? Result::Cancelled : Result::Ok;
}

}  // namespace listen
