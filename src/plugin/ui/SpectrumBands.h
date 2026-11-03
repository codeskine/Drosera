#pragma once
// From FFT magnitudes to the plant's bands. No JUCE, so it can be tested alone.

#include <algorithm>
#include <cmath>

namespace drosera {

constexpr int kNumBands = 54;
constexpr float kFloorDb = -54.0f;  // lower, the loop's noise and far harmonics kept every tentacle half open

/// Log-spaced from 30 Hz to 16 kHz.
inline float bandCentreHz(int band) {
    return 30.0f * std::pow(16000.0f / 30.0f, (float) band / (float) (kNumBands - 1));
}

/// The band nearest a note's fundamental, or -1 when it falls outside the bands.
inline int bandOfNote(float midiNote) {
    const float hz = 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
    if (hz < 30.0f || hz > 16000.0f) return -1;
    return (int) std::lround(std::log(hz / 30.0f) / std::log(16000.0f / 30.0f) * (float) (kNumBands - 1));
}

/// magnitudes: unnormalised |X[k]| of a Hann-windowed FFT, k = 0..fftSize/2. A full-scale sine gives 0 dBFS. A band with
/// no bin inside it, as low bands are narrower than a bin, takes the nearest bin. Bands above Nyquist stay at rest.
inline void computeBands(const float* magnitudes, int fftSize, double sampleRate, float* outDb) {
    std::fill(outDb, outDb + kNumBands, kFloorDb);
    if (sampleRate <= 0.0 || fftSize <= 0) return;
    const float binHz = (float) sampleRate / (float) fftSize;
    const float norm = 4.0f / (float) fftSize;  // the Hann window's coherent gain is one half
    const float halfStep = std::pow(16000.0f / 30.0f, 0.5f / (float) (kNumBands - 1));
    const int lastBin = fftSize / 2;
    for (int band = 0; band < kNumBands; ++band) {
        const float centre = bandCentreHz(band);
        if (centre > (float) sampleRate * 0.5f) continue;
        const int lo = (int) std::ceil(centre / halfStep / binHz);
        const int hi = std::min((int) std::ceil(centre * halfStep / binHz) - 1, lastBin);
        float peak = 0.0f;
        if (lo <= hi)
            for (int k = lo; k <= hi; ++k) peak = std::max(peak, magnitudes[k]);
        else
            peak = magnitudes[std::min((int) std::lround(centre / binHz), lastBin)];
        const float linear = peak * norm;
        outDb[band] = linear > 0.0f ? std::max(20.0f * std::log10(linear), kFloorDb) : kFloorDb;
    }
}

/// One step of the display's ballistics: 40 ms attack, 300 ms release, as time constants of real elapsed time, so timer
/// jitter does not change the response.
inline float smoothLevel(float current, float target, float dtSeconds) {
    const float tau = target > current ? 0.040f : 0.300f;
    return current + (target - current) * (1.0f - std::exp(-dtSeconds / tau));
}

/// kFloorDb..0 dBFS to 0..1, clamped.
inline float bandAmount(float db) {
    return std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
}

} // namespace drosera
