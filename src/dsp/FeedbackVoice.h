#pragma once
#ifndef IFS_FEEDBACK_VOICE_H
#define IFS_FEEDBACK_VOICE_H

#include <memory>
#include <daisysp.h>
#include "BiquadFilters.h"
#include "KarplusString.h"

namespace infrasonic {
namespace FeedbackSynth {

// One feedback loop: stereo delay line, Karplus-Strong string, overdrive, LPF/HPF, reverb
// and the optional tracking bandpass. The noise source and the echo stay in Engine.
class FeedbackVoice {

    public:
        FeedbackVoice() = default;
        ~FeedbackVoice() = default;

        // Only the first call allocates: Init also clears the state on the audio thread.
        void Init(const float sample_rate);

        void SetStringPitch(const float nn);
        void SetFeedbackGain(const float gain_dbfs);
        void SetFeedbackDelay(const float delay_s);
        void SetFeedbackDelayLag(const float time_s);
        void SetFeedbackLPFCutoff(const float cutoff_hz);
        void SetFeedbackHPFCutoff(const float cutoff_hz);

        // Plugin extension, not in the firmware (off by default): a bandpass in the feedback
        // path only, centred on the played note, so that the loop oscillates on the
        // fundamental. The output tap is not filtered.
        void SetFeedbackTracking(const bool enabled, const float center_hz, const float makeup_gain = 1.0f);

        // What the other voices send through Coupling, band-passed on this voice's note while tracking. Unfiltered,
        // low chords lock onto a high partial that two strings share, about four octaves up, through a path that
        // misses each loop's band-pass. Polyphony only: mono renders never run this filter.
        void FilterCoupling(float &couplingL, float &couplingR);
        void ClearCouplingFilter();

        // Both range 0-1
        void SetReverbMix(const float mix);
        void SetReverbFeedback(const float time);

        // samp is the loop's signal after filters and reverb, before the feedback gain and band-pass.
        void Process(float noise, float in, float couplingL, float couplingR, float &sampL, float &sampR);

    private:
        // long enough for 250ms at 48kHz
        static constexpr size_t kMaxFeedbackDelaySamp = 12000;

        float sample_rate_;
        float fb_gain_ = 0.0f;
        float verb_mix_ = 0.0f;

        float fb_delay_smooth_coef_;
        float fb_delay_samp_ = 1000.f;
        float fb_delay_samp_target_ = 64.f;

        infrasonic::KarplusString strings_[2];
        daisysp::DelayLine<float, kMaxFeedbackDelaySamp> fb_delayline_[2];
        daisysp::Overdrive overdrive_[2];

        LPF12 fb_lpf_;
        HPF12 fb_hpf_;

        BPF24 fb_track_;
        BPF24 coupling_track_;
        bool fb_track_enabled_ = false;
        float fb_track_makeup_ = 1.0f;

        using VerbPtr = std::unique_ptr<daisysp::ReverbSc>;
        VerbPtr verb_;

        FeedbackVoice(const FeedbackVoice &other) = delete;
        FeedbackVoice(FeedbackVoice &&other) = delete;
        FeedbackVoice& operator=(const FeedbackVoice &other) = delete;
        FeedbackVoice& operator=(FeedbackVoice &&other) = delete;
};

}
}

#endif
