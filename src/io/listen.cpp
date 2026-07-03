#include "listen.h"

#include <M5Cardputer.h>

#include "../config.h"
#include "audio_engine.h"

namespace listen {

namespace {

constexpr int kChunk = 1600;  // 100 ms at 16 kHz — the progress heartbeat
constexpr int kMaxRound = (int)(kRateHz * 3);  // per-round ceiling
constexpr int kMinRound = (int)(kRateHz / 2);  // floor: chroma needs only
                                               // ~0.15 s of contiguity (A2 frame)
constexpr int kMaxTotal = (int)(kRateHz * 9);  // listening budget across rounds
constexpr size_t kHeapHeadroom = 16 * 1024;    // leave room for the mic's I2S driver

}  // namespace

Result capture(bool (*progress)(void* user, float frac), void* user,
               bool (*segment)(void* user, const int16_t* mono, int n)) {
    // Size the round buffer to the heap we actually have: fragmentation is
    // real (the 65 KB canvas is resident), so ask for the largest contiguous
    // block and take what fits, down to half-second rounds. Alloc before
    // touching the audio path — a failure must leave the instrument
    // completely undisturbed, and must say its numbers out loud.
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    int total = largest > kHeapHeadroom
                    ? (int)((largest - kHeapHeadroom) / sizeof(int16_t))
                    : 0;
    if (total > kMaxRound) total = kMaxRound;
    total -= total % kChunk;
    if (total < kMinRound) {
        Serial.printf("[listen] ALLOC FAILED: largest block %u B, need %u B\n",
                      (unsigned)largest,
                      (unsigned)(kMinRound * sizeof(int16_t) + kHeapHeadroom));
        return Result::AllocFailed;
    }
    int16_t* buf =
        (int16_t*)heap_caps_malloc((size_t)total * sizeof(int16_t), MALLOC_CAP_8BIT);
    if (!buf) {
        Serial.printf("[listen] ALLOC FAILED at %u B (largest block %u B)\n",
                      (unsigned)(total * sizeof(int16_t)), (unsigned)largest);
        return Result::AllocFailed;
    }
    Serial.printf("[listen] rounds of %d samples (%.1fs), largest block %u B, free heap %u\n",
                  total, (float)total / kRateHz, (unsigned)largest,
                  (unsigned)ESP.getFreeHeap());

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
    int rounds = kMaxTotal / total;
    if (rounds < 1) rounds = 1;
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
