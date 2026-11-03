#pragma once
#ifndef INFS_ECHODELAY_H
#define INFS_ECHODELAY_H

#include <daisysp.h>
#include "BiquadFilters.h"
#include "DSPUtils.h"

namespace infrasonic {

/**
 * @brief
 * Tape-ish echo delay.
 *   - Feedback is unbounded, but signal is soft-clipped
 *   - Output is full-wet, should be mixed with dry signal externally
 *
 * @tparam MaxLength Max length of delay in samples
 */
template<size_t MaxLength>
class EchoDelay {

    public:

        EchoDelay() {}
        ~EchoDelay() {}

        void Init(float sample_rate)
        {
            sample_rate_ = sample_rate;
            delayLine_.Init();
            bpf_.Init(sample_rate);
            bpf_.SetParams(kCenterHz, 1.0f);
            center_hz_ = kCenterHz;
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
         * @param time_s Delay time in seconds. Will be truncated to MaxLength.
         * @param immediately If true, sets delay time immediately with no smoothing.
         */
        void SetDelayTime(const float time_s, bool immediately = false)
        {
            delay_time_target_ = time_s;
            if (immediately) delay_time_current_ = time_s;
        }

        /**
         * @brief
         * Set the feedback amount (linear multiplier).
         * This can be >1 in magnitude for saturated swells, or negative.
         *
         * NOTE: This is not internally smoothed. Use external smoothing if desired.
         *
         * @param feedback
         */
        void SetFeedback(const float feedback)
        {
            feedback_ = feedback;
        }

        inline float Process(const float in)
        {
            const float out = Read();
            Write(out, in);
            return out;
        }

        /**
         * @brief Advances the delay time smoothing, then reads, band-passes and soft-clips.
         * Each call must be followed by one Write.
         */
        inline float Read()
        {
            float out = readLine();
            out = bpf_.Process(out);
            out = daisysp::SoftClip(out);
            return out;
        }

        /// As Read, with drive_gain between the band-pass and the soft clip.
        inline float ReadShaped(const float drive_gain)
        {
            return daisysp::SoftClip(drive_gain * bpf_.Process(readLine()));
        }

        /// Band-pass center, 800 Hz from Init. Recomputes the filter only when it changes, so returning to 800 Hz
        /// gives the coefficients Init gave.
        void SetCenter(const float hz)
        {
            if (hz == center_hz_) return;
            center_hz_ = hz;
            bpf_.SetParams(hz, 1.0f);
        }

        /// Stores fed_back * feedback + in. Process feeds back its own output; StereoEcho can feed back the other line's.
        inline void Write(const float fed_back, const float in)
        {
            delayLine_.Write(fed_back * feedback_ + in);
        }

    private:

        static constexpr float kCenterHz = 800.0f;

        inline float readLine()
        {
            daisysp::fonepole(delay_time_current_, delay_time_target_, delay_smooth_coef_);
            delayLine_.SetDelay(delay_time_current_ * sample_rate_);
            return delayLine_.Read();
        }

        EchoDelay(const EchoDelay &other) = delete;
        EchoDelay(EchoDelay &&other) = delete;
        EchoDelay& operator=(const EchoDelay &other) = delete;
        EchoDelay& operator=(EchoDelay &&other) = delete;

        float sample_rate_;
        float delay_time_current_;
        float delay_time_target_;
        float delay_smooth_coef_;

        float feedback_;
        float center_hz_;

        daisysp::DelayLine<float, MaxLength> delayLine_;
        BPF12 bpf_;
};

}

#endif
