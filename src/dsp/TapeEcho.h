#pragma once
// One channel of the octamad tape echo (third_party/tapeecho) behind a float, per-sample
// interface. The module is fixed-point and works in blocks of TE_FRAMES samples, so the
// echo is that many samples late. Allocates only in the constructor.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "cpu.h"
}

namespace infrasonic {

class TapeEcho {
public:
    explicit TapeEcho(uint32_t lane) : ring_(2 * TE_RING + 32, 0) {
        params_.mix = 127;  // full wet: the engine mixes the dry signal itself
        params_.tempo = 2880;
        params_.lane = lane;
    }

    /// Next write starts fresh: the module clears its filters and ignores the old tape.
    void Reset() { state_.active = 0; }

    void SetDelayTime(float seconds, float sample_rate) {
        params_.target_q8 = (uint32_t)(std::max(seconds, 0.001f) * sample_rate * 256.0f);
    }

    /// All 0..1. Feedback reaches unity at about 0.69 and self-oscillates, bounded, above it.
    void SetFeedback(float norm) { params_.feedback = toKnob(norm); }
    void SetWow(float norm)      { params_.wow = toKnob(norm); }
    void SetAge(float norm)      { params_.age = toKnob(norm); }

    float Process(float in) {
        const float out = out_[pos_];
        const float x = std::isfinite(in) ? std::clamp(in, -1.0f, 1.0f) : 0.0f;
        in_[pos_] = (int32_t)(x * 2147483392.0f);  // largest float below 2^31
        if (++pos_ == TE_FRAMES) {
            pos_ = 0;
            processBlock();
        }
        return out;
    }

private:
    static uint32_t toKnob(float norm) { return (uint32_t)std::lround(std::clamp(norm, 0.0f, 1.0f) * 127.0f); }

    void processBlock() {
        int32_t audio[2 * TE_FRAMES], record[2 * TE_FRAMES];
        for (uint32_t i = 0; i < TE_FRAMES; ++i) audio[2 * i] = audio[2 * i + 1] = in_[i];
        te_process(&state_, &params_, ring_.data(), write_, audio, record);
        std::memcpy(&ring_[2 * write_], record, sizeof(record));
        for (uint32_t i = 0; i < TE_FRAMES; ++i) out_[i] = (float)audio[2 * i] / 2147483648.0f;
        write_ += TE_FRAMES;
        if (write_ >= TE_RING) write_ = 0;
    }

    std::vector<int32_t> ring_;
    TapeState state_ {};
    TapeParams params_ {};
    int32_t in_[TE_FRAMES] {};
    float out_[TE_FRAMES] {};
    uint32_t pos_ = 0, write_ = 0;
};

} // namespace infrasonic
