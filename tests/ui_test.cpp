// Tests for the interface: the spectrum path to the display, the readouts and the editor's behaviour.
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>
#include "pitch_estimator.h"
#include "SpectrumTap.h"
#include "ui/SpectrumBands.h"

using namespace drosera;

static int failures = 0;
static void check(bool pass, const char* name) {
    std::printf("%-40s %s\n", name, pass ? "OK" : "FAIL");
    if (!pass) ++failures;
}

// Hann-windowed magnitudes of a sine, as the display computes them.
static std::vector<float> sineMagnitudes(double hz, double amplitude, double sampleRate, int fftSize = 2048) {
    std::vector<std::complex<double>> a((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / fftSize);
        a[(size_t) i] = w * amplitude * std::sin(2.0 * M_PI * hz * i / sampleRate);
    }
    fft(a);
    std::vector<float> m((size_t) fftSize / 2 + 1);
    for (size_t k = 0; k < m.size(); ++k) m[k] = (float) std::abs(a[k]);
    return m;
}

static std::vector<float> bandsOf(const std::vector<float>& magnitudes, double sampleRate, int fftSize = 2048) {
    std::vector<float> out(kNumBands);
    computeBands(magnitudes.data(), fftSize, sampleRate, out.data());
    return out;
}

static int peakBand(const std::vector<float>& bands) {
    return (int) (std::max_element(bands.begin(), bands.end()) - bands.begin());
}

static bool othersBelow(const std::vector<float>& bands, int peak, float dB) {
    for (int i = 0; i < kNumBands; ++i)
        if (std::abs(i - peak) > 1 && bands[(size_t) i] > bands[(size_t) peak] - dB) return false;
    return true;
}

static void tapTests() {
    {
        SpectrumTap tap;
        tap.setActive(true);
        std::thread producer([&tap] {
            float block[64], silence[64] = {};
            for (int value = 0; value < 200000;) {
                for (float& s : block) s = 2.0f * (float) ++value;  // doubled: the tap's mono sum halves L + R
                tap.push(block, silence, 64);
                if (value % 6400 == 0) std::this_thread::yield();
            }
        });
        bool ordered = true;
        float last = 0.0f, got[512];
        int total = 0;
        auto drain = [&] {
            const size_t n = tap.pop(got, 512);
            for (size_t i = 0; i < n; ++i) { ordered &= got[i] > last; last = got[i]; }
            total += (int) n;
            return n;
        };
        for (int spins = 0; spins < 200000 && total < 150000; ++spins)
            if (drain() == 0) std::this_thread::yield();
        producer.join();
        while (drain() > 0) {}
        check(ordered && total > 0, "tapKeepsOrder");
    }
    {
        SpectrumTap tap;
        tap.setActive(true);
        std::vector<float> in(5000), silence(5000, 0.0f), out(6000);
        for (size_t i = 0; i < in.size(); ++i) in[i] = 2.0f * (float) i;
        tap.push(in.data(), silence.data(), in.size());
        const size_t n = tap.pop(out.data(), out.size());
        check(n == 4096 && out[0] == 0.0f && out[4095] == 4095.0f, "tapDropsWhenFull");
    }
    {
        SpectrumTap tap;
        float one[4] = {1, 1, 1, 1}, out[4];
        tap.push(one, one, 4);
        check(tap.pop(out, 4) == 0, "tapInactiveIgnores");
    }
    {
        SpectrumTap tap;
        tap.setActive(true);
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        float l[3] = {nan, inf, 1.0f}, r[3] = {0.0f, 0.0f, nan}, out[3];
        tap.push(l, r, 3);
        check(tap.pop(out, 3) == 3 && out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "tapSanitises");
    }
    {
        SpectrumTap tap;
        tap.setActive(true);
        std::vector<float> zeros(100, 0.0f), out(100);
        tap.push(zeros.data(), zeros.data(), 100);
        tap.requestFlush();
        check(tap.pop(out.data(), 100) == 0, "tapFlush");
    }
    {
        SpectrumTap tap;
        tap.setActive(true);
        float l = 1.0f, r = 0.0f, out = 0.0f;
        tap.push(&l, &r, 1);
        check(tap.pop(&out, 1) == 1 && out == 0.5f, "tapMonoSum");
    }
}

static void bandTests() {
    check(std::abs(bandCentreHz(0) - 30.0f) < 0.01f && std::abs(bandCentreHz(53) - 16000.0f) < 1.0f, "bandEnds");

    const auto at1k = bandsOf(sineMagnitudes(1000.0, 1.0, 48000.0), 48000.0);
    const auto at5k = bandsOf(sineMagnitudes(5000.0, 1.0, 48000.0), 48000.0);
    check(peakBand(at1k) == 30 && othersBelow(at1k, 30, 30.0f) && peakBand(at5k) == 43 && othersBelow(at5k, 43, 30.0f),
          "sineLightsItsBand 48 kHz");
    // At 96 kHz bins are 47 Hz wide, and 1 kHz sits near a band edge: one band off is the resolution there.
    const auto at1k96 = bandsOf(sineMagnitudes(1000.0, 1.0, 96000.0), 96000.0);
    const auto at5k96 = bandsOf(sineMagnitudes(5000.0, 1.0, 96000.0), 96000.0);
    check(std::abs(peakBand(at1k96) - 30) <= 1 && othersBelow(at1k96, peakBand(at1k96), 20.0f)
              && peakBand(at5k96) == 43 && othersBelow(at5k96, 43, 20.0f), "sineLightsItsBand 96 kHz");

    // Exactly on a bin: between bins a Hann window reads up to 1.4 dB low.
    const double onBin = 44.0 * 48000.0 / 2048.0;
    const auto loud = bandsOf(sineMagnitudes(onBin, 1.0, 48000.0), 48000.0);
    const auto quiet = bandsOf(sineMagnitudes(onBin, std::pow(10.0, -24.0 / 20.0), 48000.0), 48000.0);
    check(std::abs(loud[30]) < 0.5f && std::abs(quiet[30] + 24.0f) < 1.0f, "calibration");

    std::vector<float> zeros(1025, 0.0f);
    const auto silent = bandsOf(zeros, 48000.0);
    check(std::all_of(silent.begin(), silent.end(), [](float d) { return d == kFloorDb; }), "silenceRests");

    const auto low = bandsOf(sineMagnitudes(1000.0, 1.0, 22050.0), 22050.0);
    bool aboveAtRest = true;
    for (int i = 0; i < kNumBands; ++i) if (bandCentreHz(i) > 11025.0f) aboveAtRest &= low[(size_t) i] == kFloorDb;
    check(aboveAtRest && low[30] > -3.0f, "aboveNyquistRests");

    const auto noRate = bandsOf(sineMagnitudes(1000.0, 1.0, 48000.0), 0.0);
    check(std::all_of(noRate.begin(), noRate.end(), [](float d) { return d == kFloorDb; }), "zeroRateRests");

    const auto at60 = bandsOf(sineMagnitudes(60.0, 1.0, 48000.0), 48000.0);
    int nearest = 0;
    for (int i = 0; i < kNumBands; ++i)
        if (std::abs(std::log(bandCentreHz(i) / 60.0f)) < std::abs(std::log(bandCentreHz(nearest) / 60.0f))) nearest = i;
    check(at60[(size_t) nearest] > -6.0f, "lowBandsHaveNoHoles");

    const float up = smoothLevel(-72.0f, 0.0f, 0.040f), down = smoothLevel(0.0f, -72.0f, 0.300f);
    const float twoSteps = smoothLevel(smoothLevel(-72.0f, 0.0f, 0.020f), 0.0f, 0.020f);
    check(std::abs((up + 72.0f) / 72.0f - 0.632f) < 0.01f && std::abs(-down / 72.0f - 0.632f) < 0.01f
              && std::abs(twoSteps - up) < 0.1f, "ballistics");

    check(bandAmount(-72.0f) == 0.0f && bandAmount(0.0f) == 1.0f && bandAmount(-100.0f) == 0.0f && bandAmount(6.0f) == 1.0f,
          "bandAmountRange");
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    tapTests();
    bandTests();
    std::printf(failures == 0 ? "ALL OK\n" : "SOME FAILED\n");
    return failures == 0 ? 0 : 1;
}
