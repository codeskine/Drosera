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

    static constexpr size_t kNumVoices = FeedbackSynth::Engine::kNumVoices;
    // Top of Coupling's range, as linear gain. Under MIDI each voice also band-passes what it receives, with make-up,
    // see couplingMakeup and coupledChordsHoldAcrossRegisters in render_test.
    static constexpr float kMaxCoupling = 0.1f;

    DroseraCore() : engine_(std::make_unique<FeedbackSynth::Engine>()) {}

    void Init() {
        engine_->Init(kRate);
        registry_.Init(kRate / kControlBlock);
        voice_registry_.Init(kRate / kControlBlock);
        registerParams();
        for (auto& l : limiter_) l.Init();
        phase_ = 0;
        first_ = true;
        voices_running_ = false;
        tracking_.fill(false);
    }

    /// Normalized 0..1, same orientation as the firmware's knobs after ADC inversion.
    void SetParam(Param p, float norm) { norm_[p] = norm; }
    /// The firmware's delay switch. Halves the knob position before its square-law curve, so the
    /// echo gets 2.8 to 3.9 times shorter, not twice: a doppler warp rather than a tempo halving.
    void SetEchoWarp(bool on)          { echo_warp_ = on; }

    /// MIDI pitch override of the Frequency knob (note number, may be fractional).
    /// While active, Body is snapped to whole string periods and the loop is forced onto
    /// the note's fundamental (see applyPitchAndBody).
    void SetMidiPitch(bool active, float noteNumber) { midi_pitch_on_ = active; midi_pitch_[0] = noteNumber; }
    /// Voices 1 and 2: the note while SetMidiPitch is active.
    void SetVoiceNote(size_t voice, float note) { midi_pitch_[voice] = note; }

    /// When enabled, the feedback gain is closed (-60 dB) unless `open`: the drone
    /// swells on note-on and dies away on note-off.
    void SetGate(bool enabled, bool open) { SetVoiceGate(0, enabled, open); }
    void SetVoiceGate(size_t voice, bool enabled, bool open) { gate_enabled_[voice] = enabled; gate_open_[voice] = open; }

    /// Plugin extension: sound voices 1 and 2 as well. Off by default.
    void SetPolyphony(bool on) { poly_ = on; }
    /// With polyphony on and MIDI not in control: tune the knobs' voices as MIDI does, so they sound the notes they
    /// show. Off, each voice's Body follows its interval and the loops find their own resonances.
    void SetStringLock(bool on) { string_lock_ = on; }
    /// 0..1, square-law to 0..kMaxCoupling.
    void SetCoupling(float norm) { coupling_norm_ = norm; }
    /// Semitones from the knob's note, for voices 1 and 2 while MIDI pitch is not active.
    void SetIntervals(int interval2, int interval3) { interval_[1] = interval2; interval_[2] = interval3; }

    /// What really sounds, for the interface: any thread may read these. MIDI mode gives the note, and the Body
    /// delay after snapping and phase compensation.
    float PublishedNote(size_t voice = 0) const        { return published_note_[voice].load(std::memory_order_relaxed); }
    float PublishedBodySeconds(size_t voice = 0) const { return published_body_s_[voice].load(std::memory_order_relaxed); }
    /// Echo Time for a knob position, WARP included. Worked out rather than published, so it holds with no audio running.
    static float EchoTimeSeconds(float norm, bool warp) { return daisysp::fmap(norm * (warp ? 0.5f : 1.0f), kEchoMin, kEchoMax, daisysp::Mapping::EXP); }

    /// Renders n samples at kRate. `in` (mono excitation/audio input) may be nullptr.
    void Render(const float* in, float* outL, float* outR, size_t n) {
        pushControls();
        for (size_t i = 0; i < n; ++i) {
            if (phase_ == 0) {
                advanceTuningBlend();
                registry_.Process();
                if (engine_->PolyRunning()) voice_registry_.Process();
            }
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
            ++recovery_count_;
            Recover();
            for (size_t i = 0; i < n; ++i) outL[i] = outR[i] = 0.0f;
        }
    }

    /// Clears the engine's state and keeps the current parameters. Safe on the audio thread.
    void Recover() {
        engine_->Init(kRate);
        tracking_.fill(false);
        for (auto& l : limiter_) l.Init();
    }

    /// Reads as Engine::VoiceTap. For tests.
    float VoiceTap(size_t voice, size_t channel) const { return engine_->VoiceTap(voice, channel); }
    /// Times Render's NaN check fired. For tests.
    int RecoveryCount() const { return recovery_count_; }

private:
    using Registry = ParameterRegistry<Param>;

    // Their own registry: registry_ iterates an unordered_map, so more entries could reorder its handlers.
    enum VoiceParam : int { Voice1Pitch, Voice2Pitch, Voice1Gain, Voice2Gain, Coupling, Voice0Receive, Voice1Receive, Voice2Receive };

    void pushControls() {
        const bool imm = first_;  // jump to initial values instead of gliding from defaults
        if (imm) tuning_blend_.fill(tunedToNote() ? 1.0f : 0.0f);
        // The Frequency parameter spans notes kNoteMin..kNoteMax. The knob keeps the firmware's
        // 16..72 range (the first 56/80 of the span); MIDI may use the whole span.
        const float freqNorm = midi_pitch_on_
            ? daisysp::fclamp((midi_pitch_[0] - kNoteMin) / (kNoteMax - kNoteMin), 0.0f, 1.0f)
            : norm_[Frequency] * (kKnobNoteMax - kNoteMin) / (kNoteMax - kNoteMin);
        const float gainNorm = gainNormOf(0);
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
        pushVoiceControls(imm);
        first_ = false;
    }

    bool gateClosed(size_t voice) const { return gate_enabled_[voice] && !gate_open_[voice]; }
    float gainNormOf(size_t voice) const { return gateClosed(voice) ? 0.0f : norm_[FeedbackGain]; }

    void pushVoiceControls(bool imm) {
        // Engine cleared voices that stopped, which sets their delay lag back to kKnobDelayLag.
        if (voices_running_ && !engine_->PolyRunning()) {
            voices_running_ = false;
            tracking_[1] = tracking_[2] = false;
        }
        engine_->SetPolyphony(poly_, imm);
        if (!engine_->PolyRunning()) return;
        const bool jump = imm || !voices_running_;  // a pitch must not glide from stale values
        voices_running_ = true;
        if (jump) tuning_blend_[1] = tuning_blend_[2] = tunedToNote() ? 1.0f : 0.0f;
        const float knobNote = kNoteMin + norm_[Frequency] * (kKnobNoteMax - kNoteMin);
        for (size_t v = 1; v < kNumVoices; ++v) {
            voice_registry_.Update(VoiceParam(Voice1Pitch + v - 1), midi_pitch_on_ ? midi_pitch_[v] : knobNote + (float)interval_[v], jump);
            voice_registry_.UpdateNormalized(VoiceParam(Voice1Gain + v - 1), gainNormOf(v), jump);
        }
        voice_registry_.UpdateNormalized(Coupling, coupling_norm_, jump);
        // A closed voice hears nothing, or Coupling would defeat MIDI Gate.
        for (size_t v = 0; v < kNumVoices; ++v)
            voice_registry_.Update(VoiceParam(Voice0Receive + v), gateClosed(v) ? 0.0f : 1.0f, jump);
    }

    // Ranges, curves and smoothing times copied from FeedbackSynthControls::registerParams
    void registerParams() {
        using namespace std::placeholders;
        using FeedbackSynth::Engine;
        Engine& e = *engine_;
        registry_.Register(Frequency, 40.0f, kNoteMin, kNoteMax,
            [this](float nn) { nn_[0] = nn; applyPitchAndBody(0); }, 0.2f);
        registry_.Register(FeedbackGain, -60.0f, -60.0f, 12.0f,
            [&e](float db) { e.SetFeedbackGain(0, db); });
        registry_.Register(FeedbackBody, 0.001f, 0.001f, 0.1f,
            [this](float s) { body_s_ = s; applyPitchAndBody(0); }, 1.0f, daisysp::Mapping::EXP);
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
        registry_.Register(EchoTime, 0.5f, kEchoMin, kEchoMax,
            std::bind(&Engine::SetEchoDelayTime, &e, _1), 0.1f, daisysp::Mapping::EXP);
        registry_.Register(EchoFeedback, 0.0f, 0.0f, 1.5f,
            std::bind(&Engine::SetEchoDelayFeedback, &e, _1));
        registry_.Register(OutputVolume, 0.5f, 0.0f, 1.0f,
            std::bind(&Engine::SetOutputLevel, &e, _1), 0.05f, daisysp::Mapping::EXP);
        // Voices 1 and 2 pick up Body and the filters' values in applyPitchAndBody, every control tick.
        for (size_t v = 1; v < kNumVoices; ++v) {
            voice_registry_.Register(VoiceParam(Voice1Pitch + v - 1), 40.0f, kNoteMin, kNoteMax,
                [this, v](float nn) { nn_[v] = nn; applyPitchAndBody(v); }, 0.2f);
            voice_registry_.Register(VoiceParam(Voice1Gain + v - 1), -60.0f, -60.0f, 12.0f,
                [&e, v](float db) { e.SetFeedbackGain(v, db); });
        }
        // Square-law: under MIDI most of the change is at the top of the range, in knob mode it comes much sooner.
        voice_registry_.Register(Coupling, 0.0f, 0.0f, kMaxCoupling,
            [&e](float gain) { e.SetCoupling(gain); }, 0.05f, daisysp::Mapping::EXP);
        for (size_t v = 0; v < kNumVoices; ++v)
            voice_registry_.Register(VoiceParam(Voice0Receive + v), 1.0f, 0.0f, 1.0f,
                [&e, v](float amount) { e.SetCouplingReceive(v, amount); });
    }

    // Knob mode: values go straight to the engine (firmware behaviour), with voices 1 and 2's Body following their interval.
    // MIDI mode, and the knobs with polyphony and String Lock on: left alone the loop oscillates where the string's resonances (multiples of 1/T)
    // and the Body delay's (multiples of 1/Body) coincide, which is rarely the note. So Body is
    // snapped to n*T (both comb filters then line up on the note's harmonics), a bandpass on the
    // note in the feedback path selects the fundamental, and the Body delay glides faster.
    void applyPitchAndBody(size_t voice) {
        FeedbackSynth::Engine& e = *engine_;
        published_note_[voice].store(nn_[voice], std::memory_order_relaxed);
        if (!tunedToNote()) {
            const float delay = voice == 0 ? body_s_ : body_s_ * std::pow(2.0f, (nn_[0] - nn_[voice]) / 12.0f);
            published_body_s_[voice].store(delay, std::memory_order_relaxed);
            if (tracking_[voice]) { e.SetFeedbackTracking(voice, false, 0.0f); e.SetFeedbackDelayLag(voice, kKnobDelayLag); tracking_[voice] = false; }
            e.SetStringPitch(voice, tuning_blend_[voice] > 0.0f ? blendedStringNote(voice) : nn_[voice]);
            e.SetFeedbackDelay(voice, delay);
            return;
        }
        if (!tracking_[voice]) { e.SetFeedbackDelayLag(voice, kMidiDelayLag); tracking_[voice] = true; }
        const float f = daisysp::mtof(nn_[voice]);
        const float T = 1.0f / f;
        const float n = std::max(2.0f, std::round(body_s_ / T));
        // Filters in a loop shift its phase and so its pitch; lengthen (or shorten) the delays
        // by exactly that phase, as time at the note's frequency.
        const std::complex<float> z1 = std::polar(1.0f, -2.0f * float(M_PI) * f / kRate);  // z^-1
        const float toSeconds = 1.0f / (2.0f * float(M_PI) * f);
        const auto stringLoop = stringLoopResponse(z1);
        const auto outerFilters = stringLoop * biquadResponse(z1, LP, lpf_hz_) * biquadResponse(z1, HP, hpf_hz_);
        const float outerPhase = std::arg(outerFilters);
        e.SetStringPitch(voice, blendedStringNote(voice));
        const float delay = std::max(n * T + outerPhase * toSeconds, 1.0f / kRate);
        published_body_s_[voice].store(delay, std::memory_order_relaxed);
        e.SetFeedbackDelay(voice, delay);
        // Give back what the filters take at the note, or low notes (below the string's
        // 76 Hz DC blocker) lose the loop gain to oscillate on the fundamental.
        e.SetFeedbackTracking(voice, true, f, std::min(1.0f / std::max(std::abs(outerFilters), 1e-3f), kMaxMakeup), couplingMakeup());
    }

    bool tunedToNote() const { return midi_pitch_on_ || (string_lock_ && poly_); }

    // The string's period is lengthened by the phase its own loop filters add at the note. Going in or out of tuning to
    // the note fades this in or out, or the string's delay would jump by that amount, which clicks.
    float blendedStringNote(size_t voice) const {
        const float f = daisysp::mtof(nn_[voice]);
        const std::complex<float> z1 = std::polar(1.0f, -2.0f * float(M_PI) * f / kRate);
        const float stringPhase = std::arg(stringLoopResponse(z1));
        const float stringPeriod = std::max(1.0f / f + stringPhase / (2.0f * float(M_PI) * f), 1.0f / 12000.0f);
        const float tuned = 69.0f + 12.0f * std::log2(1.0f / stringPeriod / 440.0f);
        return tuning_blend_[voice] >= 1.0f ? tuned : nn_[voice] + tuning_blend_[voice] * (tuned - nn_[voice]);
    }

    void advanceTuningBlend() {
        const float target = tunedToNote() ? 1.0f : 0.0f;
        if (!poly_) { tuning_blend_.fill(target); return; }  // one voice behaves as it always has
        for (float& b : tuning_blend_) b += daisysp::fclamp(target - b, -kTuningBlendStep, kTuningBlendStep);
    }

    // Gives back what the band-pass on received Coupling takes from the other notes (a fifth away it passes 0.39), or
    // Coupling would barely change a chord. A Body past kCouplingFullBody packs its resonances close around the note,
    // where Coupling pushes a voice onto the next one, so the make-up falls away fast. See coupledChordsHoldAtLongBody.
    float couplingMakeup() const {
        const float r = std::min(1.0f, kCouplingFullBody / body_s_);
        return kCouplingMakeup * r * r * r;
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
    static constexpr float kEchoMin = 0.05f, kEchoMax = 5.0f;
    static constexpr float kKnobDelayLag = 0.2f;  // same as FeedbackSynth::Engine::Init
    static constexpr float kMidiDelayLag = 0.03f;
    static constexpr float kMaxMakeup = 8.0f;
    static constexpr float kTuningBlendStep = 1.0f / (0.05f * kRate / kControlBlock);  // 50 ms
    static constexpr float kCouplingMakeup = 2.5f, kCouplingFullBody = 0.025f;

    std::unique_ptr<FeedbackSynth::Engine> engine_;
    std::array<float, kNumVoices> nn_ { 40.0f, 40.0f, 40.0f };
    float body_s_ = 0.001f, lpf_hz_ = 18000.0f, hpf_hz_ = 250.0f;
    std::array<bool, kNumVoices> tracking_ {};
    Registry registry_;
    ParameterRegistry<VoiceParam> voice_registry_;
    daisysp::Limiter limiter_[2];
    std::array<float, NumParams> norm_ {};
    bool echo_warp_ = false;
    bool midi_pitch_on_ = false;
    std::array<bool, kNumVoices> gate_enabled_ {}, gate_open_ {};
    std::array<float, kNumVoices> midi_pitch_ { 40.0f, 40.0f, 40.0f };
    bool poly_ = false, voices_running_ = false, string_lock_ = false;
    std::array<float, kNumVoices> tuning_blend_ {};
    float coupling_norm_ = 0.0f;
    std::array<int, kNumVoices> interval_ { 0, 7, 12 };
    int recovery_count_ = 0;
    bool first_ = true;
    size_t phase_ = 0;
    std::atomic<float> published_note_[kNumVoices] { { 40.0f }, { 40.0f }, { 40.0f } };
    std::atomic<float> published_body_s_[kNumVoices] { { 0.001f }, { 0.001f }, { 0.001f } };
};

} // namespace infrasonic
