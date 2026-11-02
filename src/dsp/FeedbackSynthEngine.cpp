#include "FeedbackSynthEngine.h"
#include "DSPUtils.h"

using namespace infrasonic;
using namespace infrasonic::FeedbackSynth;
using namespace daisysp;

void Engine::Init(const float sample_rate) {
  using ED = EchoDelay<kMaxEchoDelaySamp>;

  // Only the first call allocates: Init also clears the state on the audio thread.
  if (!echo_delay_[0]) {
    echo_delay_[0] = std::make_unique<ED>();
    echo_delay_[1] = std::make_unique<ED>();
  }

  noise_.Init();
  noise_.SetAmp(dbfs2lin(-90.0f));

  for (auto &voice : voices_) voice.Init(sample_rate);

  for (unsigned int i = 0; i < 2; i++) {
    echo_delay_[i]->Init(sample_rate);
    echo_delay_[i]->SetDelayTime(5.0f, true);
    echo_delay_[i]->SetFeedback(0.5f);
    echo_delay_[i]->SetLagTime(0.5f);
  }
}

void Engine::SetFeedbackTracking(const size_t voice, const bool enabled, const float center_hz, const float makeup_gain) {
  voices_[voice].SetFeedbackTracking(enabled, center_hz, makeup_gain);
}

void Engine::SetFeedbackDelayLag(const size_t voice, const float time_s) {
  voices_[voice].SetFeedbackDelayLag(time_s);
}

void Engine::SetStringPitch(const size_t voice, const float nn) {
  voices_[voice].SetStringPitch(nn);
}

void Engine::SetFeedbackGain(const size_t voice, const float gain_db) {
  voices_[voice].SetFeedbackGain(gain_db);
}

void Engine::SetFeedbackDelay(const size_t voice, const float delay_s) {
  voices_[voice].SetFeedbackDelay(delay_s);
}

void Engine::SetFeedbackLPFCutoff(const float cutoff_hz) {
  for (auto &voice : voices_) voice.SetFeedbackLPFCutoff(cutoff_hz);
}

void Engine::SetFeedbackHPFCutoff(const float cutoff_hz) {
  for (auto &voice : voices_) voice.SetFeedbackHPFCutoff(cutoff_hz);
}

void Engine::SetEchoDelayTime(const float echo_time) {
  echo_delay_[0]->SetDelayTime(echo_time);
  echo_delay_[1]->SetDelayTime(echo_time);
}

void Engine::SetEchoDelayFeedback(const float echo_fb) {
  echo_delay_[0]->SetFeedback(echo_fb);
  echo_delay_[1]->SetFeedback(echo_fb);
}

void Engine::SetEchoDelaySendAmount(const float echo_send) {
  echo_send_ = echo_send;
}

void Engine::SetReverbMix(const float mix) {
  for (auto &voice : voices_) voice.SetReverbMix(mix);
}

void Engine::SetReverbFeedback(const float time) {
  for (auto &voice : voices_) voice.SetReverbFeedback(time);
}

void Engine::SetOutputLevel(const float level) { output_level_ = level; }

void Engine::Process(float in, float &outL, float &outR) {
  float sampL, sampR, echoL, echoR;
  const float noise_samp = noise_.Process();

  // ---> Feedback Loop, reverb and resonator feedback

  voices_[0].Process(noise_samp, in, 0.0f, 0.0f, sampL, sampR);

  // ---> Echo Delay

  echoL = echo_delay_[0]->Process(sampL * echo_send_);
  echoR = echo_delay_[1]->Process(sampR * echo_send_);

  sampL = 0.5f * (sampL + echoL);
  sampR = 0.5f * (sampR + echoR);

  // ---> Output
  outL = sampL * output_level_;
  outR = sampR * output_level_;
}
