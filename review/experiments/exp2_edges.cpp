// DSP edge cases on the host-independent core.
//  A) Echo start-up: the echo's delay line starts at 5 s and glides to the knob; measure the first repeat's delay
//     of an impulse at t=0 against one at t=6 s.
//  B) Extreme settings for minutes: reverb decay 1.0, feedback max, echo feedback 1.5, pong drive max.
//     Count non-finite recoveries and samples above 1.0 after the limiter.
//  C) Resampler: gain at 1 kHz and 18 kHz, and image/alias power, 48k -> 44.1k.
//  D) Interval clamp: knob at the top with +24 st.
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
#include "DroseraCore.h"
#include "Presets.h"
#include "SincResampler.h"

using infrasonic::DroseraCore;

static void apply(DroseraCore& c, const infrasonic::Preset& p) {
    for (int i = 0; i < DroseraCore::NumParams; ++i) c.SetParam((DroseraCore::Param) i, p.knob[i]);
    c.SetEchoWarp(p.echoHalve); c.SetPolyphony(p.poly); c.SetCoupling(p.coupling);
    c.SetIntervals(p.interval2, p.interval3); c.SetStringLock(p.stringLock);
    c.SetEchoPong(p.echoPong); c.SetEchoDrive(p.echoDrive); c.SetEchoTone(p.echoTone);
    c.SetEchoWidth(p.echoWidth); c.SetEchoLevel(p.echoLevel);
}

static void render(DroseraCore& c, const float* in, std::vector<float>& L, std::vector<float>& R, size_t n) {
    L.assign(n, 0.0f); R.assign(n, 0.0f);
    for (size_t pos = 0; pos < n; pos += 256) c.Render(in ? in + pos : nullptr, &L[pos], &R[pos], std::min<size_t>(256, n - pos));
}

static void echoStartup() {
    // Feedback closed, no reverb, echo send 1, echo feedback 0, echo time norm 0.2 -> 0.05 + 0.04*4.95 = 0.248 s
    const float p[] = {0.43f, 0.00f, 0.30f, 0.90f, 0.20f, 0.00f, 0.50f, 1.00f, 0.20f, 0.00f, 0.70f};
    const float expect = DroseraCore::EchoTimeSeconds(0.2f, false);
    for (double at : {0.0, 2.0, 6.0}) {
        DroseraCore c; c.Init();
        for (int i = 0; i < DroseraCore::NumParams; ++i) c.SetParam((DroseraCore::Param) i, p[i]);
        const size_t n = 48000 * 12;
        std::vector<float> in(n, 0.0f), L, R;
        in[(size_t) (at * 48000)] = 0.5f;
        render(c, in.data(), L, R, n);
        // the direct sound is the string's ring; find the echo as the loudest sample after 0.1 s past the impulse
        size_t start = (size_t) (at * 48000) + 4800, best = start; double peak = 0;
        for (size_t i = start; i < n; ++i) if (std::fabs(L[i]) > peak) { peak = std::fabs(L[i]); best = i; }
        std::printf("A) impulse at %.0f s: first repeat after %.3f s (knob says %.3f s), level %.3f\n", at, (best - at * 48000) / 48000.0, expect, peak);
    }
}

static void extremes() {
    struct Case { const char* name; float p[11]; bool pong; float drive; bool poly; float coupling; };
    const Case cases[] = {
        {"everything max, reverb 1.0", {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1}, false, 0, false, 0},
        {"max + pong drive max", {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1}, true, 1, false, 0},
        {"max + poly + coupling max", {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1}, true, 1, true, 1},
        {"low + poly + coupling max", {0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1}, false, 0, true, 1},
    };
    for (const auto& k : cases) {
        DroseraCore c; c.Init();
        for (int i = 0; i < DroseraCore::NumParams; ++i) c.SetParam((DroseraCore::Param) i, k.p[i]);
        c.SetEchoPong(k.pong); c.SetEchoDrive(k.drive); c.SetPolyphony(k.poly); c.SetCoupling(k.coupling);
        const size_t n = 48000 * 180;  // three minutes
        std::vector<float> L(256), R(256);
        size_t over = 0; double peak = 0, sumSq = 0;
        for (size_t pos = 0; pos < n; pos += 256) {
            c.Render(nullptr, L.data(), R.data(), 256);
            for (size_t i = 0; i < 256; ++i) { const double a = std::fabs(L[i]); if (a > 1.0) ++over; if (a > peak) peak = a; if (pos > n / 2) sumSq += a * a; }
        }
        std::printf("B) %-28s 3 min: peak %.3f, samples > 1.0: %zu, rms(2nd half) %.3f, recoveries %d\n", k.name, peak, over, std::sqrt(sumSq / (n / 2.0)), c.RecoveryCount());
    }
}

static void resampler() {
    infrasonic::SincResampler r; r.Init(48000.0 / 44100.0);
    for (double hz : {1000.0, 10000.0, 18000.0, 21000.0, 23000.0}) {
        r.Reset();
        const size_t nIn = 48000 * 2, nOut = 44100 * 2 - 200;
        std::vector<float> in(nIn), out(nOut);
        for (size_t i = 0; i < nIn; ++i) in[i] = (float) std::sin(2.0 * M_PI * hz * i / 48000.0);
        r.Process(in.data(), out.data(), (int) nOut);
        // amplitude at hz (if below 22.05k) via Goertzel over the second half, and total power elsewhere
        const size_t a = nOut / 2, len = nOut - a;
        std::complex<double> acc = 0; double total = 0;
        for (size_t i = a; i < nOut; ++i) { acc += (double) out[i] * std::polar(1.0, -2.0 * M_PI * hz * (double) i / 44100.0); total += (double) out[i] * out[i]; }
        const double amp = 2.0 * std::abs(acc) / (double) len;
        const double rms = std::sqrt(total / (double) len);
        std::printf("C) 48k->44.1k %6.0f Hz: amplitude %.4f (%.2f dB), output rms %.4f\n", hz, amp, 20 * std::log10(amp + 1e-12), rms);
    }
}

static void intervalClamp() {
    DroseraCore c; c.Init();
    apply(c, infrasonic::kFactoryPresets[12]);
    c.SetParam(DroseraCore::Frequency, 1.0f);  // note 72
    c.SetIntervals(24, 12);
    std::vector<float> L, R; render(c, nullptr, L, R, 48000);
    std::printf("D) knob note 72 with +24 st: voice 1 publishes note %.1f (voice 0 %.1f), voice 2 (+12) %.1f\n", c.PublishedNote(1), c.PublishedNote(0), c.PublishedNote(2));
}

int main() { echoStartup(); resampler(); intervalClamp(); extremes(); return 0; }
