#pragma once
// The ticks of the filter band arc and the Tone arc: each tick's length draws a filter's gain at its frequency. No JUCE,
// so it can be tested alone.

#include <cmath>

namespace drosera::arcs {

constexpr int kBandTicks = 26, kToneTicks = 11;
constexpr float kBandStub = 3.0f, kBandSpan = 14.0f;  // a cut tick shrinks to the stub and never vanishes
constexpr float kToneStub = 2.0f, kToneSpan = 9.0f;

/// The loop's two 12 dB/octave filters together.
inline float filterGain(float hz, float highpassHz, float lowpassHz) {
    return 1.0f / std::sqrt(1.0f + std::pow(highpassHz / hz, 4.0f)) / std::sqrt(1.0f + std::pow(hz / lowpassHz, 4.0f));
}

/// Log-spaced from 10 Hz to 18 kHz, the span of the two knobs.
inline float bandTickHz(int i) { return 10.0f * std::pow(18000.0f / 10.0f, (float) i / (float) (kBandTicks - 1)); }

inline void bandArc(float highpassHz, float lowpassHz, float* lengths) {
    for (int i = 0; i < kBandTicks; ++i) lengths[i] = kBandStub + kBandSpan * filterGain(bandTickHz(i), highpassHz, lowpassHz);
}

/// Log-spaced from 100 Hz to 6.4 kHz, around Tone's 200 Hz to 3.2 kHz.
inline float toneTickHz(int i) { return 100.0f * std::pow(6400.0f / 100.0f, (float) i / (float) (kToneTicks - 1)); }

inline void toneArc(float centreHz, float* lengths) {
    for (int i = 0; i < kToneTicks; ++i) {
        const float d = std::log2(toneTickHz(i) / centreHz);
        lengths[i] = kToneStub + kToneSpan * std::exp(-0.9f * d * d);
    }
}

} // namespace drosera::arcs
