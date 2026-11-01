#pragma once
// Windowed-sinc sample rate converter for one channel, for bridging the engine's fixed 48 kHz to
// the host. Unlike a plain interpolator it filters before going down in rate, so what lies above
// the new Nyquist is removed instead of folding back as aliasing.
// Allocates only in Init. Latency: kTaps / 2 input samples.

#include <algorithm>
#include <cmath>
#include <vector>

namespace infrasonic {

class SincResampler {
public:
    static constexpr int kTaps = 96;
    static constexpr int kLatencyInputSamples = kTaps / 2;

    /// inputPerOutput: input samples consumed per output sample (input rate / output rate).
    void Init(double inputPerOutput) {
        ratio_ = inputPerOutput;
        // Centre of the transition band, relative to the input Nyquist. The 0.93 keeps the band's
        // upper edge below the new Nyquist when going down, and below the old one otherwise.
        const double cutoff = std::min(1.0, 1.0 / inputPerOutput) * 0.93;
        table_.assign((size_t)((kPhases + 1) * kTaps), 0.0f);
        for (int p = 0; p <= kPhases; ++p) {
            const double frac = (double)p / kPhases;
            double sum = 0.0;
            float* row = &table_[(size_t)(p * kTaps)];
            for (int t = 0; t < kTaps; ++t) {
                // Tap t multiplies the sample t steps before the newest one, and the read
                // position sits frac past the sample kTaps / 2 steps before the newest.
                const double x = (double)(t - kTaps / 2) + frac;
                const double w = kaiser(x / (kTaps / 2.0));
                const double h = cutoff * sinc(cutoff * x) * w;
                row[t] = (float)h;
                sum += h;
            }
            for (int t = 0; t < kTaps; ++t) row[t] = (float)(row[t] / sum);  // unity gain at DC
        }
        Reset();
    }

    void Reset() {
        std::fill(std::begin(history_), std::end(history_), 0.0f);
        pos_ = 0;
        phase_ = 0.0;
    }

    /// Produces n output samples and returns how many input samples it consumed, at most
    /// ceil(n * inputPerOutput) + 1: the caller must have that many available.
    int Process(const float* in, float* out, int n) {
        int used = 0;
        for (int i = 0; i < n; ++i) {
            phase_ += ratio_;
            while (phase_ >= 1.0) {
                phase_ -= 1.0;
                history_[pos_] = history_[pos_ + kTaps] = in[used++];
                pos_ = (pos_ + 1) % kTaps;
            }
            const double scaled = phase_ * kPhases;
            const int p = std::min((int)scaled, kPhases - 1);
            const float f = (float)(scaled - p);
            const float* a = &table_[(size_t)(p * kTaps)];
            const float* b = a + kTaps;
            const float* h = &history_[pos_];  // h[kTaps - 1] is the newest sample
            float y = 0.0f;
            for (int t = 0; t < kTaps; ++t) y += h[kTaps - 1 - t] * (a[t] + (b[t] - a[t]) * f);
            out[i] = y;
        }
        return used;
    }

private:
    static constexpr int kPhases = 512;

    static double sinc(double x) {
        const double px = M_PI * x;
        return std::fabs(px) < 1e-9 ? 1.0 : std::sin(px) / px;
    }
    static double bessel0(double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 32; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
        return sum;
    }
    static double kaiser(double u) {  // u in -1..1; beta 9 gives about 90 dB of stopband
        const double beta = 9.0;
        if (std::fabs(u) >= 1.0) return 0.0;
        return bessel0(beta * std::sqrt(1.0 - u * u)) / bessel0(beta);
    }

    double ratio_ = 1.0, phase_ = 0.0;
    std::vector<float> table_;
    float history_[2 * kTaps] {};
    int pos_ = 0;
};

} // namespace infrasonic
