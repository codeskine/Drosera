#include "FeedbackSynthEngine.h"
#include "DSPUtils.h"

using namespace infrasonic;
using namespace infrasonic::FeedbackSynth;
using namespace daisysp;

static constexpr float kFeedbackTrackingQ = 1.5f;

void Engine::Init(const float sample_rate) {
  using ED = EchoDelay<kMaxEchoDelaySamp>;

  echo_delay_[0] = std::make_unique<ED>();
  echo_delay_[1] = std::make_unique<ED>();
  verb_ = std::make_unique<ReverbSc>();
  tape_echo_[0] = std::make_unique<TapeEcho>(0u);
  tape_echo_[1] = std::make_unique<TapeEcho>(1u);
  echo_tape_ = false;

  sample_rate_ = sample_rate;
  fb_delay_smooth_coef_ = onepole_coef(0.2f, sample_rate);

  noise_.Init();
  noise_.SetAmp(dbfs2lin(-90.0f));

  for (unsigned int i = 0; i < 2; i++) {

    strings_[i].Init(sample_rate);
    strings_[i].SetBrightness(0.98f);
    strings_[i].SetFreq(mtof(40.0f));
    strings_[i].SetDamping(0.4f);

    fb_delayline_[i].Init();

    echo_delay_[i]->Init(sample_rate);
    echo_delay_[i]->SetDelayTime(5.0f, true);
    echo_delay_[i]->SetFeedback(0.5f);
    echo_delay_[i]->SetLagTime(0.5f);

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

void Engine::SetFeedbackTracking(const bool enabled, const float center_hz, const float makeup_gain) {
  fb_track_enabled_ = enabled;
  fb_track_makeup_ = makeup_gain;
  if (enabled) fb_track_.SetParams(center_hz, kFeedbackTrackingQ);
}

void Engine::SetFeedbackDelayLag(const float time_s) {
  fb_delay_smooth_coef_ = onepole_coef(time_s, sample_rate_);
}

void Engine::SetStringPitch(const float nn) {
  const auto freq = mtof(nn);
  strings_[0].SetFreq(freq);
  strings_[1].SetFreq(freq);
}

void Engine::SetFeedbackGain(const float gain_db) {
  fb_gain_ = dbfs2lin(gain_db);
}

void Engine::SetFeedbackDelay(const float delay_s) {
  fb_delay_samp_target_ =
      DSY_CLAMP(delay_s * sample_rate_, 1.0f,
                static_cast<float>(kMaxFeedbackDelaySamp - 1));
}

void Engine::SetFeedbackLPFCutoff(const float cutoff_hz) {
  fb_lpf_.SetCutoff(cutoff_hz);
}

void Engine::SetFeedbackHPFCutoff(const float cutoff_hz) {
  fb_hpf_.SetCutoff(cutoff_hz);
}

void Engine::SetEchoDelayTime(const float echo_time) {
  echo_delay_[0]->SetDelayTime(echo_time);
  echo_delay_[1]->SetDelayTime(echo_time);
  tape_echo_[0]->SetDelayTime(echo_time, sample_rate_);
  tape_echo_[1]->SetDelayTime(echo_time, sample_rate_);
}

void Engine::SetEchoDelayFeedback(const float echo_fb) {
  echo_delay_[0]->SetFeedback(echo_fb);
  echo_delay_[1]->SetFeedback(echo_fb);
  // Same knob: echo_fb spans 0..1.5, and 1.0 lands next to the tape's unity point.
  tape_echo_[0]->SetFeedback(echo_fb / 1.5f);
  tape_echo_[1]->SetFeedback(echo_fb / 1.5f);
}

void Engine::SetEchoDelaySendAmount(const float echo_send) {
  echo_send_ = echo_send;
}

void Engine::SetEchoTapeMode(const bool enabled) {
  if (enabled && !echo_tape_) {
    tape_echo_[0]->Reset();
    tape_echo_[1]->Reset();
  }
  echo_tape_ = enabled;
}

void Engine::SetTapeWow(const float norm) {
  tape_echo_[0]->SetWow(norm);
  tape_echo_[1]->SetWow(norm);
}

void Engine::SetTapeAge(const float norm) {
  tape_echo_[0]->SetAge(norm);
  tape_echo_[1]->SetAge(norm);
}

void Engine::SetReverbMix(const float mix) {
  verb_mix_ = fclamp(mix, 0.0f, 1.0f);
}

void Engine::SetReverbFeedback(const float time) { verb_->SetFeedback(time); }

void Engine::SetOutputLevel(const float level) { output_level_ = level; }

void Engine::Process(float in, float &outL, float &outR) {
  // --- Update audio-rate-smoothed control params ---

  fonepole(fb_delay_samp_, fb_delay_samp_target_, fb_delay_smooth_coef_);

  // --- Process Samples ---

  float inL, inR, sampL, sampR, echoL, echoR, verbL, verbR;
  const float noise_samp = noise_.Process();

  // ---> Feedback Loop

  // Get noise + feedback output
  inL = fb_delayline_[0].Read(fb_delay_samp_) + noise_samp + in;
  inR = fb_delayline_[1].Read(daisysp::fmax(1.0f, fb_delay_samp_ - 4.f)) +
        noise_samp + in;

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

  // ---> Echo Delay

  if (echo_tape_) {
    echoL = tape_echo_[0]->Process(sampL * echo_send_);
    echoR = tape_echo_[1]->Process(sampR * echo_send_);
  } else {
    echoL = echo_delay_[0]->Process(sampL * echo_send_);
    echoR = echo_delay_[1]->Process(sampR * echo_send_);
  }

  sampL = 0.5f * (sampL + echoL);
  sampR = 0.5f * (sampR + echoR);

  // ---> Output
  outL = sampL * output_level_;
  outR = sampR * output_level_;
}
