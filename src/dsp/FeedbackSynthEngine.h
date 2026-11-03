#pragma once
#ifndef IFS_FEEDBACK_SYNTH_ENGINE_H
#define IFS_FEEDBACK_SYNTH_ENGINE_H

#include <memory>
#include <daisysp.h>
#include "EchoDelay.h"
#include "FeedbackVoice.h"
#include "StereoEcho.h"

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
        void SetFeedbackTracking(const size_t voice, const bool enabled, const float center_hz, const float makeup_gain = 1.0f, const float coupling_makeup = 1.0f);

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

        // Plugin extension, not in the firmware (off by default): voices 1 and 2 fade in and out
        // over 20 ms, or jump there if immediate. When they have faded out they are cleared, Init
        // included, so their settings must be sent again. Same thread as Process.
        void SetPolyphony(bool on, bool immediate = false);
        // Linear gain from the other voices' taps to each voice's loop input
        void SetCoupling(const float gain) { coupling_ = gain; }
        // 0-1, default 1, multiplies what that voice receives. Init leaves it alone.
        void SetCouplingReceive(const size_t voice, const float amount) { coupling_receive_[voice] = amount; }
        // True from switch-on until the ramp ends on the way off
        bool PolyRunning() const { return poly_running_; }
        // Last sample's samp, before the ramp. Zero while polyphony is not running.
        float VoiceTap(const size_t voice, const size_t channel) const { return tap_[voice][channel]; }

        // Plugin extension, not in the firmware (off by default): the echo's two lines feed each other, so its repeats
        // bounce between left and right. Ramps over 20 ms, or jumps there if immediate. Init leaves it alone. Same
        // thread as Process.
        void SetPong(bool on, bool immediate = false) { stereo_echo_->SetPong(on, immediate); }
        // 0-1
        void SetPongDrive(const float drive) { stereo_echo_->SetDrive(drive); }
        // Band-pass center
        void SetPongTone(const float hz) { stereo_echo_->SetTone(hz); }
        void SetPongWidth(const float width) { stereo_echo_->SetWidth(width); }
        void SetPongLevel(const float gain) { stereo_echo_->SetLevel(gain); }
        // 0-1, for tests
        float PongMix() const { return stereo_echo_->PongMix(); }

        void Process(float in, float &outL, float &outR);

    public:
        static constexpr size_t kNumVoices = 3;

    private:
        // long enough for 5s at 48kHz
        static constexpr size_t kMaxEchoDelaySamp = 48000 * 5;

        float echo_send_ = 0.0f;
        float output_level_ = 0.5f;
        float sample_rate_ = 0.0f;

        bool poly_on_ = false;
        bool poly_running_ = false;
        float ramp_ = 0.0f;
        float coupling_ = 0.0f;
        float coupling_receive_[kNumVoices] = { 1.0f, 1.0f, 1.0f };
        float tap_[kNumVoices][2] = {};

        FeedbackVoice voices_[kNumVoices];
        daisysp::WhiteNoise noise_[kNumVoices];

        std::unique_ptr<StereoEcho<kMaxEchoDelaySamp>> stereo_echo_;

        void ProcessPoly(float in, float &outL, float &outR);
        void ProcessEcho(float &sampL, float &sampR);
        void StopPoly();

        Engine(const Engine &other) = delete;
        Engine(Engine &&other) = delete;
        Engine& operator=(const Engine &other) = delete;
        Engine& operator=(Engine &&other) = delete;
};

}
}

#endif
