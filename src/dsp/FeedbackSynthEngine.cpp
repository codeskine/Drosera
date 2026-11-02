#include "FeedbackSynthEngine.h"
#include "DSPUtils.h"

using namespace infrasonic;
using namespace infrasonic::FeedbackSynth;
using namespace daisysp;

static constexpr float kRampStep = 1.0f / 960.0f;  // 20 ms at 48 kHz
static constexpr float kInvSqrt3 = 0.57735027f;
// WhiteNoise always seeds with 1: each voice starts further along the sequence, or unisons are identical.
static constexpr int kNoiseOffset[Engine::kNumVoices] = { 0, 20011, 40009 };

void Engine::Init(const float sample_rate) {
  using ED = EchoDelay<kMaxEchoDelaySamp>;

  // Only the first call allocates: Init also clears the state on the audio thread.
  if (!echo_delay_[0]) {
    echo_delay_[0] = std::make_unique<ED>();
    echo_delay_[1] = std::make_unique<ED>();
  }

  sample_rate_ = sample_rate;

  for (size_t v = 0; v < kNumVoices; v++) {
    noise_[v].Init();
    noise_[v].SetAmp(dbfs2lin(-90.0f));
    for (int i = 0; i < kNoiseOffset[v]; i++) noise_[v].Process();
  }

  for (auto &voice : voices_) voice.Init(sample_rate);

  ramp_ = poly_on_ ? 1.0f : 0.0f;
  poly_running_ = poly_on_;
  for (auto &tap : tap_) tap[0] = tap[1] = 0.0f;

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

void Engine::SetPolyphony(bool on, bool immediate) {
  poly_on_ = on;
  if (on) poly_running_ = true;
  if (!immediate) return;
  ramp_ = on ? 1.0f : 0.0f;
  if (!on && poly_running_ && sample_rate_ > 0.0f) StopPoly();
}

void Engine::StopPoly() {
  poly_running_ = false;
  voices_[1].Init(sample_rate_);
  voices_[2].Init(sample_rate_);
  for (auto &tap : tap_) tap[0] = tap[1] = 0.0f;
}

void Engine::ProcessPoly(float in, float &outL, float &outR) {
  float sampL, sampR, echoL, echoR;

  const float target = poly_on_ ? 1.0f : 0.0f;
  if (ramp_ != target) {
    ramp_ += ramp_ < target ? kRampStep : -kRampStep;
    if (fabsf(ramp_ - target) < 0.5f * kRampStep) ramp_ = target;
  }

  // ---> Feedback Loops, coupled through the previous sample's taps

  // Voice 0 counts in full, so the fading voices keep hearing it
  const float weight[kNumVoices] = { 1.0f, ramp_, ramp_ };
  float next[kNumVoices][2];

  for (size_t v = 0; v < kNumVoices; v++) {
    const size_t a = (v + 1) % kNumVoices;
    const size_t b = (v + 2) % kNumVoices;
    const float receive = coupling_ * coupling_receive_[v];
    const float couplingL = receive * (weight[a] * tap_[a][0] + weight[b] * tap_[b][0]);
    const float couplingR = receive * (weight[a] * tap_[a][1] + weight[b] * tap_[b][1]);
    voices_[v].Process(noise_[v].Process(), in, couplingL, couplingR, next[v][0], next[v][1]);
  }

  for (size_t v = 0; v < kNumVoices; v++) {
    tap_[v][0] = next[v][0];
    tap_[v][1] = next[v][1];
  }

  const float scale = 1.0f + ramp_ * (kInvSqrt3 - 1.0f);
  sampL = (next[0][0] + ramp_ * (next[1][0] + next[2][0])) * scale;
  sampR = (next[0][1] + ramp_ * (next[1][1] + next[2][1])) * scale;

  // ---> Echo Delay

  echoL = echo_delay_[0]->Process(sampL * echo_send_);
  echoR = echo_delay_[1]->Process(sampR * echo_send_);

  sampL = 0.5f * (sampL + echoL);
  sampR = 0.5f * (sampR + echoR);

  // ---> Output
  outL = sampL * output_level_;
  outR = sampR * output_level_;

  if (ramp_ == 0.0f && !poly_on_) StopPoly();
}

void Engine::Process(float in, float &outL, float &outR) {
  if (poly_running_) {
    ProcessPoly(in, outL, outR);
    return;
  }

  float sampL, sampR, echoL, echoR;
  const float noise_samp = noise_[0].Process();

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
