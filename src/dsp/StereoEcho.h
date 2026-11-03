#pragma once
#ifndef INFS_STEREOECHO_H
#define INFS_STEREOECHO_H

#include <cmath>
#include <memory>
#include "EchoDelay.h"

namespace infrasonic {

/**
 * @brief
 * The firmware's echo: two EchoDelay lines, left into left and right into right.
 * Plugin extension, not in the firmware (off by default): PONG makes the two lines feed each other, so the repeats
 * bounce between left and right, and Drive, Tone, Width and Level shape them. PONG ramps in and out over 20 ms.
 *   - With the ramp at 0 it runs the firmware's code, bit for bit
 *   - Output is full-wet, should be mixed with dry signal externally
 *
 * @tparam MaxLength Max length of delay in samples
 */
template<size_t MaxLength>
class StereoEcho {

    public:

        StereoEcho() {}
        ~StereoEcho() {}

        /// Only the first call allocates: later calls clear the lines, on the audio thread too.
        void Init(float sample_rate)
        {
            if (!lineL_) {
                lineL_ = std::make_unique<Line>();
                lineR_ = std::make_unique<Line>();
            }
            lineL_->Init(sample_rate);
            lineR_->Init(sample_rate);
            ramp_step_ = 1.0f / (0.02f * sample_rate);
            mix_ = target_;
            shaped_ = false;  // Init put both centers back at 800 Hz
        }

        void SetLagTime(const float time_s)
        {
            lineL_->SetLagTime(time_s);
            lineR_->SetLagTime(time_s);
        }

        void SetDelayTime(const float time_s, bool immediately = false)
        {
            lineL_->SetDelayTime(time_s, immediately);
            lineR_->SetDelayTime(time_s, immediately);
        }

        void SetFeedback(const float feedback)
        {
            lineL_->SetFeedback(feedback);
            lineR_->SetFeedback(feedback);
        }

        /// Same thread as Process. With immediate the ramp jumps to its end.
        void SetPong(bool on, bool immediate = false)
        {
            target_ = on ? 1.0f : 0.0f;
            if (immediate) mix_ = target_;
        }

        /// 0..1, 0 to +12 dB into the soft clip, with each pass gaining 2^drive.
        void SetDrive(const float drive) { drive_ = drive; }
        /// Band-pass center PONG moves to.
        void SetTone(const float hz) { tone_hz_ = hz; }
        /// 0..1: 0 puts every repeat in the middle, 1 bounces fully left and right. Shapes the output only.
        void SetWidth(const float width) { width_ = width; }
        /// Linear gain on the output only, so it does not change how the loop sustains.
        void SetLevel(const float level) { level_ = level; }

        /// 0 to 1, for tests
        float PongMix() const { return mix_; }

        inline void Process(const float inL, const float inR, float &wetL, float &wetR)
        {
            if (mix_ != target_) {
                mix_ += mix_ < target_ ? ramp_step_ : -ramp_step_;
                if (fabsf(mix_ - target_) < 0.5f * ramp_step_) mix_ = target_;
            }

            // A blend with mix_ at 0 would not do: + 0 * x changes the bits of -0.0f and lets a NaN through
            if (mix_ == 0.0f) {
                if (shaped_) {
                    lineL_->SetCenter(kFirmwareCenterHz);
                    lineR_->SetCenter(kFirmwareCenterHz);
                    shaped_ = false;
                }
                wetL = lineL_->Process(inL);
                wetR = lineR_->Process(inR);
                return;
            }
            shaped_ = true;

            const float x = mix_;
            const float drive = x * drive_;
            if (drive != applied_drive_) {
                applied_drive_ = drive;
                drive_gain_ = std::pow(4.0f, drive);
                loop_trim_ = std::pow(2.0f, -drive);
            }
            // From the firmware's 800 Hz to Tone, with the ramp
            const float center = x == 1.0f ? tone_hz_ : kFirmwareCenterHz * std::pow(tone_hz_ / kFirmwareCenterHz, x);
            lineL_->SetCenter(center);
            lineR_->SetCenter(center);

            const float outL = lineL_->ReadShaped(drive_gain_);
            const float outR = lineR_->ReadShaped(drive_gain_);
            const float mono = 0.5f * (inL + inR);
            lineL_->Write(loop_trim_ * (outL + x * (outR - outL)), inL + x * (mono - inL));
            lineR_->Write(loop_trim_ * (outR + x * (outL - outR)), inR * (1.0f - x));

            // Between the two sides, so Width never makes a repeat louder than it was
            const float width = 1.0f + x * (width_ - 1.0f), level = 1.0f + x * (level_ - 1.0f);
            const float mid = 0.5f * (outL + outR), side = width * 0.5f * (outL - outR);
            wetL = level * (mid + side);
            wetR = level * (mid - side);
        }

    private:

        using Line = EchoDelay<MaxLength>;

        static constexpr float kFirmwareCenterHz = 800.0f;

        StereoEcho(const StereoEcho &other) = delete;
        StereoEcho(StereoEcho &&other) = delete;
        StereoEcho& operator=(const StereoEcho &other) = delete;
        StereoEcho& operator=(StereoEcho &&other) = delete;

        std::unique_ptr<Line> lineL_, lineR_;

        float ramp_step_ = 1.0f / 960.0f;
        float target_ = 0.0f, mix_ = 0.0f;
        bool shaped_ = false;
        float drive_ = 0.0f, tone_hz_ = kFirmwareCenterHz, width_ = 1.0f, level_ = 1.0f;
        float applied_drive_ = 0.0f, drive_gain_ = 1.0f, loop_trim_ = 1.0f;
};

}

#endif
