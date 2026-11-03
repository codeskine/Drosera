#pragma once
#ifndef INFS_STEREOECHO_H
#define INFS_STEREOECHO_H

#include <memory>
#include "EchoDelay.h"

namespace infrasonic {

/**
 * @brief
 * The firmware's echo: two EchoDelay lines, left into left and right into right.
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

        inline void Process(const float inL, const float inR, float &wetL, float &wetR)
        {
            wetL = lineL_->Process(inL);
            wetR = lineR_->Process(inR);
        }

    private:

        using Line = EchoDelay<MaxLength>;

        StereoEcho(const StereoEcho &other) = delete;
        StereoEcho(StereoEcho &&other) = delete;
        StereoEcho& operator=(const StereoEcho &other) = delete;
        StereoEcho& operator=(StereoEcho &&other) = delete;

        std::unique_ptr<Line> lineL_, lineR_;
};

}

#endif
