#pragma once
#ifndef IFS_FEEDBACK_SYNTH_ENGINE_H
#define IFS_FEEDBACK_SYNTH_ENGINE_H

#include <memory>
#include <daisysp.h>
#include "EchoDelay.h"
#include "FeedbackVoice.h"

namespace infrasonic {
namespace FeedbackSynth {

class Engine {

    public:
        Engine() = default;
        ~Engine() = default;

        void Init(const float sample_rate);

        // Per-voice setters
        void SetStringPitch(const size_t voice, const float nn);
        void SetFeedbackGain(const size_t voice, const float gain_dbfs);
        void SetFeedbackDelay(const size_t voice, const float delay_s);
        void SetFeedbackDelayLag(const size_t voice, const float time_s);
        // Plugin extension, see FeedbackVoice::SetFeedbackTracking
        void SetFeedbackTracking(const size_t voice, const bool enabled, const float center_hz, const float makeup_gain = 1.0f);

        // Go to all voices
        void SetFeedbackLPFCutoff(const float cutoff_hz);
        void SetFeedbackHPFCutoff(const float cutoff_hz);

        void SetEchoDelayTime(const float echo_time);
        void SetEchoDelayFeedback(const float echo_fb);
        void SetEchoDelaySendAmount(const float echo_send);

        // Both range 0-1, go to all voices
        void SetReverbMix(const float mix);
        void SetReverbFeedback(const float time);

        void SetOutputLevel(const float level);

        void Process(float in, float &outL, float &outR);

    public:
        static constexpr size_t kNumVoices = 1;

    private:
        // long enough for 5s at 48kHz
        static constexpr size_t kMaxEchoDelaySamp = 48000 * 5;

        float echo_send_ = 0.0f;
        float output_level_ = 0.5f;

        FeedbackVoice voices_[kNumVoices];
        daisysp::WhiteNoise noise_;

        using EchoDelayPtr = std::unique_ptr<EchoDelay<kMaxEchoDelaySamp>>;
        EchoDelayPtr echo_delay_[2];

        Engine(const Engine &other) = delete;
        Engine(Engine &&other) = delete;
        Engine& operator=(const Engine &other) = delete;
        Engine& operator=(Engine &&other) = delete;
};

}
}

#endif
