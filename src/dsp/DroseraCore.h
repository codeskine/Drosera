#pragma once
// Host-independent wrapper around the engine ported from the Audrey II firmware.
// Replaces FeedbackSynthControls.* and FeedbackSynth_main.cpp of the firmware:
// same parameter ranges/curves/smoothing, but fed from normalized (0..1) values
// instead of ADC readings. Runs at a fixed internal 48 kHz (ReverbSc's buffer
// and the engine's delay-line sizes are dimensioned for that rate).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <memory>
#include <daisysp.h>
#include "FeedbackSynthEngine.h"
#include "ParameterRegistry.h"

namespace infrasonic {

class DroseraCore {
public:
    static constexpr float  kRate         = 48000.0f;
    static constexpr size_t kControlBlock = 4;  // firmware block size => 12 kHz control rate

    enum Param : int {
        Frequency = 0, FeedbackGain, FeedbackBody, FeedbackLPF, FeedbackHPF,
        ReverbMix, ReverbDecay, EchoSend, EchoTime, EchoFeedback, OutputVolume,
        NumParams
    };

    DroseraCore() : engine_(std::make_unique<FeedbackSynth::Engine>()) {}

    void Init() {
        engine_->Init(kRate);
        registry_.Init(kRate / kControlBlock);
        registerParams();
        for (auto& l : limiter_) l.Init();
        phase_ = 0;
        first_ = true;
        tracking_ = false;
    }

    /// Normalized 0..1, same orientation as the firmware's knobs after ADC inversion.
    void SetParam(Param p, float norm) { norm_[p] = norm; }
    /// The firmware's delay switch. Halves the knob position before its square-law curve, so the
    /// echo gets 2.8 to 3.9 times shorter, not twice: a doppler warp rather than a tempo halving.
    void SetEchoWarp(bool on)          { echo_warp_ = on; }

    /// MIDI pitch override of the Frequency knob (note number, may be fractional).
    /// While active, Body is snapped to whole string periods and the loop is forced onto
    /// the note's fundamental (see applyPitchAndBody).
    void SetMidiPitch(bool active, float noteNumber) { midi_pitch_on_ = active; midi_pitch_ = noteNumber; }

    /// When enabled, the feedback gain is closed (-60 dB) unless `open`: the drone
    /// swells on note-on and dies away on note-off.
    void SetGate(bool enabled, bool open) { gate_enabled_ = enabled; gate_open_ = open; }

    /// What really sounds, for the interface: any thread may read these. MIDI mode gives the note, and the Body
    /// delay after snapping and phase compensation. The echo's time includes WARP.
    float PublishedNote() const        { return published_note_.load(std::memory_order_relaxed); }
    float PublishedBodySeconds() const { return published_body_s_.load(std::memory_order_relaxed); }
    float PublishedEchoSeconds() const { return published_echo_s_.load(std::memory_order_relaxed); }

    /// Renders n samples at kRate. `in` (mono excitation/audio input) may be nullptr.
    void Render(const float* in, float* outL, float* outR, size_t n) {
        pushControls();
        for (size_t i = 0; i < n; ++i) {
            if (phase_ == 0) registry_.Process();
            phase_ = (phase_ + 1) % kControlBlock;
            engine_->Process(in ? in[i] : 0.0f, outL[i], outR[i]);
        }
        limiter_[0].ProcessBlock(outL, n, 0.7f);
        limiter_[1].ProcessBlock(outR, n, 0.7f);

        // Self-oscillating feedback: never let a NaN/inf survive in the state.
        bool bad = false;
        for (size_t i = 0; i < n; ++i)
            if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) { bad = true; break; }
        if (bad) {
            Recover();
            for (size_t i = 0; i < n; ++i) outL[i] = outR[i] = 0.0f;
        }
    }

    /// Clears the engine's state and keeps the current parameters. Safe on the audio thread.
    void Recover() {
        engine_->Init(kRate);
        tracking_ = false;
        for (auto& l : limiter_) l.Init();
    }

private:
    using Registry = ParameterRegistry<Param>;

    void pushControls() {
        const bool imm = first_;  // jump to initial values instead of gliding from defaults
        // The Frequency parameter spans notes kNoteMin..kNoteMax. The knob keeps the firmware's
        // 16..72 range (the first 56/80 of the span); MIDI may use the whole span.
        const float freqNorm = midi_pitch_on_
            ? daisysp::fclamp((midi_pitch_ - kNoteMin) / (kNoteMax - kNoteMin), 0.0f, 1.0f)
            : norm_[Frequency] * (kKnobNoteMax - kNoteMin) / (kNoteMax - kNoteMin);
        const float gainNorm = (gate_enabled_ && !gate_open_) ? 0.0f : norm_[FeedbackGain];
        registry_.UpdateNormalized(Frequency,    freqNorm, imm);
        registry_.UpdateNormalized(FeedbackGain, gainNorm, imm);
        registry_.UpdateNormalized(FeedbackBody, norm_[FeedbackBody], imm);
        registry_.UpdateNormalized(FeedbackLPF,  norm_[FeedbackLPF], imm);
        registry_.UpdateNormalized(FeedbackHPF,  norm_[FeedbackHPF], imm);
        registry_.UpdateNormalized(ReverbMix,    norm_[ReverbMix], imm);
        // anti-exponential tension curve, as in FeedbackSynthControls::Update
        registry_.UpdateNormalized(ReverbDecay,  ftension(norm_[ReverbDecay], -3.0f), imm);
        registry_.UpdateNormalized(EchoSend,     norm_[EchoSend], imm);
        registry_.UpdateNormalized(EchoTime,     norm_[EchoTime] * (echo_warp_ ? 0.5f : 1.0f), imm);
        registry_.UpdateNormalized(EchoFeedback, norm_[EchoFeedback], imm);
        registry_.UpdateNormalized(OutputVolume, norm_[OutputVolume], imm);
        first_ = false;
    }

    // Ranges, curves and smoothing times copied from FeedbackSynthControls::registerParams
    void registerParams() {
        using namespace std::placeholders;
        using FeedbackSynth::Engine;
        Engine& e = *engine_;
        registry_.Register(Frequency, 40.0f, kNoteMin, kNoteMax,
            [this](float nn) { nn_ = nn; applyPitchAndBody(); }, 0.2f);
        registry_.Register(FeedbackGain, -60.0f, -60.0f, 12.0f,
            std::bind(&Engine::SetFeedbackGain, &e, _1));
        registry_.Register(FeedbackBody, 0.001f, 0.001f, 0.1f,
            [this](float s) { body_s_ = s; applyPitchAndBody(); }, 1.0f, daisysp::Mapping::EXP);
        registry_.Register(FeedbackLPF, 18000.0f, 100.0f, 18000.0f,
            [this, &e](float hz) { lpf_hz_ = hz; e.SetFeedbackLPFCutoff(hz); }, 0.05f, daisysp::Mapping::LOG);
        registry_.Register(FeedbackHPF, 250.0f, 10.0f, 4000.0f,
            [this, &e](float hz) { hpf_hz_ = hz; e.SetFeedbackHPFCutoff(hz); }, 0.05f, daisysp::Mapping::LOG);
        registry_.Register(ReverbMix, 0.0f, 0.0f, 1.0f,
            std::bind(&Engine::SetReverbMix, &e, _1));
        registry_.Register(ReverbDecay, 0.2f, 0.2f, 1.0f,
            std::bind(&Engine::SetReverbFeedback, &e, _1));
        registry_.Register(EchoSend, 0.0f, 0.0f, 1.0f,
            std::bind(&Engine::SetEchoDelaySendAmount, &e, _1), 0.05f, daisysp::Mapping::EXP);
        registry_.Register(EchoTime, 0.5f, 0.05f, 5.0f,
            [this, &e](float s) { published_echo_s_.store(s, std::memory_order_relaxed); e.SetEchoDelayTime(s); },
            0.1f, daisysp::Mapping::EXP);
        registry_.Register(EchoFeedback, 0.0f, 0.0f, 1.5f,
            std::bind(&Engine::SetEchoDelayFeedback, &e, _1));
        registry_.Register(OutputVolume, 0.5f, 0.0f, 1.0f,
            std::bind(&Engine::SetOutputLevel, &e, _1), 0.05f, daisysp::Mapping::EXP);
    }

    // Knob mode: values go straight to the engine (firmware behaviour).
    // MIDI mode: left alone the loop oscillates where the string's resonances (multiples of 1/T)
    // and the Body delay's (multiples of 1/Body) coincide, which is rarely the note. So Body is
    // snapped to n*T (both comb filters then line up on the note's harmonics), a bandpass on the
    // note in the feedback path selects the fundamental, and the Body delay glides faster.
    void applyPitchAndBody() {
        FeedbackSynth::Engine& e = *engine_;
        published_note_.store(nn_, std::memory_order_relaxed);
        if (!midi_pitch_on_) {
            published_body_s_.store(body_s_, std::memory_order_relaxed);
            if (tracking_) { e.SetFeedbackTracking(false, 0.0f); e.SetFeedbackDelayLag(kKnobDelayLag); tracking_ = false; }
            e.SetStringPitch(nn_);
            e.SetFeedbackDelay(body_s_);
            return;
        }
        if (!tracking_) { e.SetFeedbackDelayLag(kMidiDelayLag); tracking_ = true; }
        const float f = daisysp::mtof(nn_);
        const float T = 1.0f / f;
        const float n = std::max(2.0f, std::round(body_s_ / T));
        // Filters in a loop shift its phase and so its pitch; lengthen (or shorten) the delays
        // by exactly that phase, as time at the note's frequency.
        const std::complex<float> z1 = std::polar(1.0f, -2.0f * float(M_PI) * f / kRate);  // z^-1
        const float toSeconds = 1.0f / (2.0f * float(M_PI) * f);
        const auto stringLoop = stringLoopResponse(z1);
        const auto outerFilters = stringLoop * biquadResponse(z1, LP, lpf_hz_) * biquadResponse(z1, HP, hpf_hz_);
        const float stringPhase = std::arg(stringLoop);
        const float outerPhase = std::arg(outerFilters);
        const float stringPeriod = std::max(T + stringPhase * toSeconds, 1.0f / 12000.0f);
        e.SetStringPitch(69.0f + 12.0f * std::log2(1.0f / stringPeriod / 440.0f));
        const float delay = std::max(n * T + outerPhase * toSeconds, 1.0f / kRate);
        published_body_s_.store(delay, std::memory_order_relaxed);
        e.SetFeedbackDelay(delay);
        // Give back what the filters take at the note, or low notes (below the string's
        // 76 Hz DC blocker) lose the loop gain to oscillate on the fundamental.
        e.SetFeedbackTracking(true, f, std::min(1.0f / std::max(std::abs(outerFilters), 1e-3f), kMaxMakeup));
    }

    // Frequency responses at z^-1 = z1 of the filters in the engine's loops: DcBlock(gain 0.99)
    // and Tone(8 kHz) in KarplusString, the feedback LPF/HPF (Q 0.9) in FeedbackSynth::Engine.
    static std::complex<float> stringLoopResponse(std::complex<float> z1) {
        const float b = 2.0f - std::cos(2.0f * float(M_PI) * 8000.0f / kRate);
        const float c2 = b - std::sqrt(b * b - 1.0f);
        return (1.0f - z1) / (1.0f - 0.99f * z1) * ((1.0f - c2) / (1.0f - c2 * z1));
    }
    enum BiquadKind { LP, HP };
    static std::complex<float> biquadResponse(std::complex<float> z1, BiquadKind kind, float cutoff) {
        const float q = 0.9f, K = std::tan(float(M_PI) * cutoff / kRate), K2 = K * K;
        const float norm = 1.0f / (1.0f + K / q + K2);
        const float b0 = kind == LP ? K2 * norm : norm;
        const float b1 = kind == LP ? 2.0f * b0 : -2.0f * b0;
        const float a1 = 2.0f * (K2 - 1.0f) * norm, a2 = (1.0f - K / q + K2) * norm;
        return (b0 + b1 * z1 + b0 * z1 * z1) / (1.0f + a1 * z1 + a2 * z1 * z1);
    }

    static constexpr float kNoteMin = 16.0f, kKnobNoteMax = 72.0f, kNoteMax = 84.0f;  // above this the loop filters make the string run flat (-120 cents at 90)
    static constexpr float kKnobDelayLag = 0.2f;  // same as FeedbackSynth::Engine::Init
    static constexpr float kMidiDelayLag = 0.03f;
    static constexpr float kMaxMakeup = 8.0f;

    std::unique_ptr<FeedbackSynth::Engine> engine_;
    float nn_ = 40.0f, body_s_ = 0.001f, lpf_hz_ = 18000.0f, hpf_hz_ = 250.0f;
    bool tracking_ = false;
    Registry registry_;
    daisysp::Limiter limiter_[2];
    std::array<float, NumParams> norm_ {};
    bool echo_warp_ = false;
    bool midi_pitch_on_ = false, gate_enabled_ = false, gate_open_ = false;
    float midi_pitch_ = 40.0f;
    bool first_ = true;
    size_t phase_ = 0;
    std::atomic<float> published_note_ { 40.0f }, published_body_s_ { 0.001f }, published_echo_s_ { 0.5f };
};

} // namespace infrasonic
