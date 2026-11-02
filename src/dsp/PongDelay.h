#pragma once
#ifndef INFS_PONGDELAY_H
#define INFS_PONGDELAY_H

#include <cmath>
#include <daisysp.h>
#include "DSPUtils.h"

namespace infrasonic {

/**
 * @brief
 * Plugin extension, not in the firmware: an echo whose repeats bounce between left and right and saturate.
 *   - Input enters the left line, so the first repeat is on the left. Delay time is the time between bounces.
 *   - Feedback is unbounded, but signal is soft-clipped
 *   - Output is full-wet, should be mixed with dry signal externally
 *
 * @tparam MaxLength Max length of delay in samples
 */
template<size_t MaxLength>
class PongDelay {

    public:

        PongDelay() {}
        ~PongDelay() {}

        void Init(float sample_rate)
        {
            sample_rate_ = sample_rate;
            lineL_.Init();
            lineR_.Init();
            hp_coef_ = lowpassCoef(kHighpassHz);
            hpL_ = hpR_ = lpL_ = lpR_ = 0.0f;
            feedback_ = 0.5f;
            setDriveNow(0.5f);
            setToneNow(kDefaultToneHz);
        }

        /**
         * @brief Set the approximate lag time (smoothing) for delay time changes, in seconds
         */
        void SetLagTime(const float time_s)
        {
            delay_smooth_coef_ = onepole_coef(time_s, sample_rate_);
        }

        /**
         * @brief Set the Delay Time in seconds
         *
         * @param time_s Time between bounces, in seconds. Will be truncated to MaxLength.
         * @param immediately If true, sets delay time immediately with no smoothing.
         */
        void SetDelayTime(const float time_s, bool immediately = false)
        {
            delay_time_target_ = time_s;
            if (immediately) delay_time_current_ = time_s;
        }

        /**
         * @brief
         * Set the feedback amount (linear multiplier). At small signals each bounce has gain feedback * sqrt(drive gain),
         * so the loop runs away from 1 at Drive 0 and from 0.5 at Drive 1.
         */
        void SetFeedback(const float feedback)
        {
            feedback_ = feedback;
        }

        /// 0..1, 0 to +12 dB into the soft clip.
        void SetDrive(const float drive)
        {
            if (drive != drive_) setDriveNow(drive);
        }

        /// Cutoff of the lowpass in the loop.
        void SetTone(const float hz)
        {
            if (hz != tone_hz_) setToneNow(hz);
        }

        inline void Process(const float in, float &outL, float &outR)
        {
            // One time for both lines keeps the bounces evenly spaced
            daisysp::fonepole(delay_time_current_, delay_time_target_, delay_smooth_coef_);
            const float delay = delay_time_current_ * sample_rate_;
            lineL_.SetDelay(delay);
            lineR_.SetDelay(delay);
            outL = shape(lineL_.Read(), hpL_, lpL_);
            outR = shape(lineR_.Read(), hpR_, lpR_);
            lineL_.Write(in + feedback_ * loop_trim_ * outR);
            lineR_.Write(feedback_ * loop_trim_ * outL);
        }

    private:

        static constexpr float kHighpassHz = 20.0f;
        static constexpr float kDefaultToneHz = 3795.0f;

        PongDelay(const PongDelay &other) = delete;
        PongDelay(PongDelay &&other) = delete;
        PongDelay& operator=(const PongDelay &other) = delete;
        PongDelay& operator=(PongDelay &&other) = delete;

        float lowpassCoef(const float hz) const
        {
            return 1.0f - std::exp(-2.0f * PI_F * hz / sample_rate_);
        }

        void setDriveNow(const float drive)
        {
            drive_ = drive;
            drive_gain_ = std::pow(4.0f, drive);
            loop_trim_ = std::pow(2.0f, -drive);
        }

        void setToneNow(const float hz)
        {
            tone_hz_ = hz;
            tone_coef_ = lowpassCoef(hz);
        }

        inline float shape(const float in, float &hp, float &lp) const
        {
            daisysp::fonepole(hp, in, hp_coef_);  // the state follows the lows, which the highpass removes
            daisysp::fonepole(lp, in - hp, tone_coef_);
            return daisysp::SoftClip(drive_gain_ * lp);
        }

        float sample_rate_ = 48000.0f;
        float delay_time_current_ = 0.0f;
        float delay_time_target_ = 0.0f;
        float delay_smooth_coef_ = 1.0f;

        float feedback_ = 0.5f;
        float drive_ = 0.5f, drive_gain_ = 2.0f, loop_trim_ = 0.70710678f;
        float tone_hz_ = kDefaultToneHz, tone_coef_ = 0.0f, hp_coef_ = 0.0f;
        float hpL_ = 0.0f, hpR_ = 0.0f, lpL_ = 0.0f, lpR_ = 0.0f;

        daisysp::DelayLine<float, MaxLength> lineL_, lineR_;
};

}

#endif
