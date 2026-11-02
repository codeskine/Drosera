#include "FeedbackVoice.h"
#include "DSPUtils.h"

using namespace infrasonic;
using namespace infrasonic::FeedbackSynth;
using namespace daisysp;

static constexpr float kFeedbackTrackingQ = 1.5f;

void FeedbackVoice::Init(const float sample_rate) {
  if (!verb_) {
    verb_ = std::make_unique<ReverbSc>();
  }

  sample_rate_ = sample_rate;
  fb_delay_smooth_coef_ = onepole_coef(0.2f, sample_rate);

  for (unsigned int i = 0; i < 2; i++) {

    strings_[i].Init(sample_rate);
    strings_[i].SetBrightness(0.98f);
    strings_[i].SetFreq(mtof(40.0f));
    strings_[i].SetDamping(0.4f);

    fb_delayline_[i].Init();

    overdrive_[i].Init();
    overdrive_[i].SetDrive(0.4);
  }

  verb_->Init(sample_rate);
  verb_->SetFeedback(0.85f);
  verb_->SetLpFreq(12000.0f);

  fb_lpf_.Init(sample_rate);
  fb_lpf_.SetQ(0.9f);
  fb_lpf_.SetCutoff(18000.0f);

  fb_hpf_.Init(sample_rate);
  fb_hpf_.SetQ(0.9f);
  fb_hpf_.SetCutoff(60.f);

  fb_track_.Init(sample_rate);
  fb_track_.SetParams(440.0f, kFeedbackTrackingQ);
  fb_track_enabled_ = false;
}

void FeedbackVoice::SetFeedbackTracking(const bool enabled, const float center_hz, const float makeup_gain) {
  fb_track_enabled_ = enabled;
  fb_track_makeup_ = makeup_gain;
  if (enabled) fb_track_.SetParams(center_hz, kFeedbackTrackingQ);
}

void FeedbackVoice::SetFeedbackDelayLag(const float time_s) {
  fb_delay_smooth_coef_ = onepole_coef(time_s, sample_rate_);
}

void FeedbackVoice::SetStringPitch(const float nn) {
  const auto freq = mtof(nn);
  strings_[0].SetFreq(freq);
  strings_[1].SetFreq(freq);
}

void FeedbackVoice::SetFeedbackGain(const float gain_db) {
  fb_gain_ = dbfs2lin(gain_db);
}

void FeedbackVoice::SetFeedbackDelay(const float delay_s) {
  fb_delay_samp_target_ =
      DSY_CLAMP(delay_s * sample_rate_, 1.0f,
                static_cast<float>(kMaxFeedbackDelaySamp - 1));
}

void FeedbackVoice::SetFeedbackLPFCutoff(const float cutoff_hz) {
  fb_lpf_.SetCutoff(cutoff_hz);
}

void FeedbackVoice::SetFeedbackHPFCutoff(const float cutoff_hz) {
  fb_hpf_.SetCutoff(cutoff_hz);
}

void FeedbackVoice::SetReverbMix(const float mix) {
  verb_mix_ = fclamp(mix, 0.0f, 1.0f);
}

void FeedbackVoice::SetReverbFeedback(const float time) { verb_->SetFeedback(time); }

void FeedbackVoice::Process(float noise, float in, float couplingL, float couplingR,
                            float &sampL, float &sampR) {
  // --- Update audio-rate-smoothed control params ---

  fonepole(fb_delay_samp_, fb_delay_samp_target_, fb_delay_smooth_coef_);

  float inL, inR, verbL, verbR;

  // ---> Feedback Loop

  // Get noise + feedback output
  inL = fb_delayline_[0].Read(fb_delay_samp_) + noise + in + couplingL;
  inR = fb_delayline_[1].Read(daisysp::fmax(1.0f, fb_delay_samp_ - 4.f)) +
        noise + in + couplingR;

  // Process through KS resonator
  sampL = strings_[0].Process(inL);
  sampR = strings_[1].Process(inR);

  // Distort + Clip
  sampL = overdrive_[0].Process(sampL);
  sampR = overdrive_[1].Process(sampR);

  // Filter in feedback loop
  fb_lpf_.ProcessStereo(sampL, sampR);
  fb_hpf_.ProcessStereo(sampL, sampR);

  // ---> Reverb

  verb_->Process(sampL, sampR, &verbL, &verbR);

  //       (sampL * (1.0f - verb_mix_)) + verbL * verb_mix_;
  //       sampL - sampL * verb_mix + verbL * verb_mix_;
  sampL -= (sampL - verbL) * verb_mix_;
  sampR -= (sampR - verbR) * verb_mix_;

  // ---> Resonator feedback

  // Write back into delay with attenuation
  float fbL = sampL * fb_gain_;
  float fbR = sampR * fb_gain_;
  if (fb_track_enabled_) {
    fb_track_.ProcessStereo(fbL, fbR);
    fbL *= fb_track_makeup_;
    fbR *= fb_track_makeup_;
  }
  fb_delayline_[0].Write(fbL);
  fb_delayline_[1].Write(fbR);
}
