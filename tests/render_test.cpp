// Offline sanity test for the host-independent core: renders scenarios at several
// block sizes, checks for NaN/inf, bounds and non-silence, writes WAVs for listening.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "DroseraCore.h"
#include "pitch_estimator.h"
#include "Presets.h"
#include "TapeEcho.h"
#include "SincResampler.h"

using infrasonic::DroseraCore;

static void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    uint32_t n = (uint32_t)L.size(), dataBytes = n * 4, rate = 48000, byteRate = rate * 4;
    uint32_t riff = 36 + dataBytes, sixteen = 16;
    uint16_t fmt = 1, ch = 2, bits = 16, align = 4;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) {
        int16_t a = (int16_t)(std::clamp(L[i], -1.f, 1.f) * 32767);
        int16_t b = (int16_t)(std::clamp(R[i], -1.f, 1.f) * 32767);
        fwrite(&a, 2, 1, f); fwrite(&b, 2, 1, f);
    }
    fclose(f);
}

struct Scenario {
    const char* name;
    float p[DroseraCore::NumParams];
    bool halve;
    bool expectSound;
};

static bool run(const Scenario& s, size_t blockSize, const char* outDir) {
    DroseraCore core;
    core.Init();
    for (int i = 0; i < DroseraCore::NumParams; ++i) core.SetParam((DroseraCore::Param)i, s.p[i]);
    core.SetEchoWarp(s.halve);
    const size_t total = 48000 * 8;
    std::vector<float> L(total), R(total);
    for (size_t pos = 0; pos < total; pos += blockSize) {
        size_t n = std::min(blockSize, total - pos);
        core.Render(nullptr, &L[pos], &R[pos], n);
    }
    double peak = 0, rms = 0;
    bool finite = true;
    for (size_t i = 48000; i < total; ++i) {  // skip first second
        if (!std::isfinite(L[i]) || !std::isfinite(R[i])) finite = false;
        peak = std::max({peak, (double)std::fabs(L[i]), (double)std::fabs(R[i])});
        rms += (double)L[i] * L[i] + (double)R[i] * R[i];
    }
    rms = std::sqrt(rms / (2.0 * (total - 48000)));
    bool sound = rms > 1e-4;
    bool ok = finite && peak <= 1.0001 && (sound == s.expectSound);
    printf("%-16s block=%-4zu peak=%.3f rms=%.4f finite=%d sound=%d %s\n", s.name, blockSize, peak, rms,
           finite, sound, ok ? "OK" : "FAIL");
    if (blockSize == 64 && outDir) writeWav(std::string(outDir) + "/" + s.name + ".wav", L, R);
    return ok;
}

static double rmsOf(const std::vector<float>& v, size_t a, size_t b) {
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double)v[i] * v[i];
    return std::sqrt(s / (double)(b - a));
}

static DroseraCore* makeCore(DroseraCore& core, const float* p) {
    core.Init();
    for (int i = 0; i < DroseraCore::NumParams; ++i) core.SetParam((DroseraCore::Param)i, p[i]);
    return &core;
}

// Audio input, MIDI pitch and gate features of the core.
static bool featureTests() {
    bool ok = true;
    //                  freq   gain   body   lpf    hpf    rvMix  rvDec  eSend  eTime  eFb    vol
    const float base[] = {0.43f, 0.75f, 0.30f, 0.90f, 0.20f, 0.00f, 0.50f, 0.00f, 0.50f, 0.00f, 0.70f};
    const size_t N = 48000 * 6;

    {   // 1) audio input is audible and bounded even with feedback closed
        float p[DroseraCore::NumParams]; std::copy(base, base + DroseraCore::NumParams, p); p[DroseraCore::FeedbackGain] = 0.0f;
        DroseraCore core; makeCore(core, p);
        std::vector<float> in(N), L(N), R(N);
        for (size_t i = 0; i < N; ++i) in[i] = 0.5f * std::sin(2.0 * M_PI * 220.0 * (double)i / 48000.0);
        for (size_t pos = 0; pos < N; pos += 256) core.Render(&in[pos], &L[pos], &R[pos], 256 < N - pos ? 256 : N - pos);
        double r = rmsOf(L, 48000, N);
        bool pass = r > 1e-3 && std::isfinite(r);
        printf("input passthrough      rms=%.4f %s\n", r, pass ? "OK" : "FAIL"); ok &= pass;
    }
    {   // 2) MIDI pitch: the strongest partial must be the played note (not just one of its harmonics)
        auto sweep = [&](const char* name, std::array<float, DroseraCore::NumParams> p, int minOnNotePercent) {
            int on = 0, cnt = 0; double worst = 0;
            for (int note = 16; note <= 84; ++note) {
                DroseraCore core; makeCore(core, p.data());
                core.SetMidiPitch(true, (float)note);
                std::vector<float> L(48000 * 5), R(48000 * 5);
                for (size_t pos = 0; pos < L.size(); pos += 128) core.Render(nullptr, &L[pos], &R[pos], 128);
                const double nominal = 440.0 * std::pow(2.0, (note - 69) / 12.0);
                const double cents = std::fabs(1200.0 * std::log2(dominantHz(L, 48000 * 2) / nominal));
                ++cnt;
                if (cents < 25.0) { ++on; worst = std::max(worst, cents); }
            }
            const bool pass = on * 100 >= minOnNotePercent * cnt;
            printf("midi pitch %-14s on note (<25c) %d/%d, worst of those %.1f c %s\n", name, on, cnt, worst, pass ? "OK" : "FAIL");
            ok &= pass;
        };
        std::array<float, DroseraCore::NumParams> p; std::copy(base, base + DroseraCore::NumParams, p.begin());
        sweep("dry", p, 95);
        p[DroseraCore::FeedbackBody] = 0.0f; sweep("Body min", p, 95);
        p[DroseraCore::FeedbackBody] = 1.0f; sweep("Body max", p, 90);
        const float def[] = {0.43f, 0.75f, 0.30f, 0.90f, 0.20f, 0.30f, 0.50f, 0.20f, 0.50f, 0.40f, 0.70f};
        std::copy(def, def + DroseraCore::NumParams, p.begin());
        sweep("Default preset", p, 75);  // reverb and echo smear the pitch: only a floor
    }
    {   // 3) gate: open -> loud, closed -> decays by >20 dB within 3 s
        DroseraCore core; makeCore(core, base);
        core.SetGate(true, true);
        std::vector<float> L(N), R(N);
        size_t half = 48000 * 3;
        for (size_t pos = 0; pos < N; pos += 128) {
            if (pos == half) core.SetGate(true, false);
            core.Render(nullptr, &L[pos], &R[pos], 128);
        }
        double open = rmsOf(L, half - 24000, half), closed = rmsOf(L, N - 24000, N);
        double db = 20.0 * std::log10((closed + 1e-12) / (open + 1e-12));
        bool pass = open > 1e-2 && db < -20.0;
        printf("gate open rms=%.4f closed rms=%.5f (%.1f dB) %s\n", open, closed, db, pass ? "OK" : "FAIL"); ok &= pass;
    }
    return ok;
}

// Resampler: passband flat, what lies above the new Nyquist removed, DC and block-splitting exact.
static bool resamplerTests() {
    bool ok = true;
    using infrasonic::SincResampler;
    auto level = [](double hzIn, double rateIn, double rateOut, double hzMeasure) {
        SincResampler r; r.Init(rateIn / rateOut);
        const int n = 1 << 17;
        std::vector<float> in((size_t)(n * rateIn / rateOut) + 4096), out((size_t)n);
        for (size_t i = 0; i < in.size(); ++i) in[i] = (float)std::sin(2.0 * M_PI * hzIn * (double)i / rateIn);
        for (int pos = 0, used = 0; pos < n;) { const int c = std::min(512, n - pos); used += r.Process(&in[(size_t)used], &out[(size_t)pos], c); pos += c; }
        double re = 0, im = 0;  // single-bin DFT with a Hann window
        for (int i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / n), a = 2.0 * M_PI * hzMeasure * i / rateOut; re += w * out[(size_t)i] * std::cos(a); im += w * out[(size_t)i] * std::sin(a); }
        return 20.0 * std::log10(std::sqrt(re * re + im * im) / (n * 0.25) + 1e-12);
    };
    struct Case { const char* name; double hz, in, out, measure, lo, hi; };
    const Case cases[] = {
        {"48k->44.1k 1 kHz passes", 1000, 48000, 44100, 1000, -0.05, 0.05},
        {"48k->44.1k 15 kHz passes", 15000, 48000, 44100, 15000, -0.5, 0.5},
        {"48k->44.1k 23 kHz folded image", 23000, 48000, 44100, 44100 - 23000, -300, -60},
        {"48k->96k 1 kHz passes", 1000, 48000, 96000, 1000, -0.05, 0.05},
        {"48k->96k 10 kHz image at 38 kHz", 10000, 48000, 96000, 38000, -300, -60},
        {"96k->48k 30 kHz folded image", 30000, 96000, 48000, 18000, -300, -60},
        {"44.1k->48k 1 kHz passes", 1000, 44100, 48000, 1000, -0.05, 0.05},
    };
    for (const auto& c : cases) {
        const double db = level(c.hz, c.in, c.out, c.measure);
        const bool pass = db >= c.lo && db <= c.hi;
        printf("resampler %-32s %7.2f dB %s\n", c.name, db, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    return ok;
}

// Tape echo module through its float wrapper: delay accuracy, decay, bounded self-oscillation, bad input.
static bool tapeTests() {
    bool ok = true;
    using infrasonic::TapeEcho;
    for (float seconds : {0.05f, 0.25f, 1.0f, 5.0f}) {
        auto t = std::make_unique<TapeEcho>(0u);
        t->SetDelayTime(seconds, 48000.0f); t->SetFeedback(0.0f); t->SetWow(0.0f); t->SetAge(0.0f);
        const size_t total = (size_t)(seconds * 48000.0f) + 48000;
        double peak = 0; long at = -1;
        for (size_t i = 0; i < total; ++i) {
            const float y = t->Process(i >= 1600 && i < 1608 ? 0.5f : 0.0f);
            if (i > 1700 && std::fabs(y) > peak) { peak = std::fabs(y); at = (long)i; }
        }
        const double ms = (at - 1604) / 48.0;
        const bool pass = std::fabs(ms - seconds * 1000.0 - 16.0 / 48.0) < 2.0 && peak > 0.02;
        printf("tape delay %.2f s: echo at %.1f ms, peak %.3f %s\n", seconds, ms, peak, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    for (float fb : {0.0f, 0.4f, 0.62f, 0.8f, 1.0f}) {
        auto t = std::make_unique<TapeEcho>(0u);
        t->SetDelayTime(0.25f, 48000.0f); t->SetFeedback(fb); t->SetWow(0.35f); t->SetAge(0.3f);
        const size_t total = 48000 * 30;
        double mx = 0, early = 0, late = 0; bool finite = true;
        for (size_t i = 0; i < total; ++i) {
            const float in = i < 12000 ? 0.25f * (float)std::sin(2.0 * M_PI * 440.0 * (double)i / 48000.0) : 0.0f;
            const float y = t->Process(in);
            finite &= std::isfinite(y);
            mx = std::max(mx, (double)std::fabs(y));
            if (i > 48000 * 2 && i < 48000 * 4) early += (double)y * y;
            if (i > 48000 * 27) late += (double)y * y;
        }
        early = std::sqrt(early / (48000.0 * 2)); late = std::sqrt(late / (48000.0 * 3));
        const bool pass = finite && mx <= 1.0;
        printf("tape feedback %.2f: rms 2-4 s %.4f, rms 27-30 s %.5f, peak %.3f %s\n", fb, early, late, mx, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    {
        auto t = std::make_unique<TapeEcho>(0u);
        t->SetDelayTime(0.1f, 48000.0f); t->SetFeedback(0.6f);
        bool finite = true;
        for (int i = 0; i < 96000; ++i) finite &= std::isfinite(t->Process(i == 100 ? NAN : i == 200 ? INFINITY : i == 300 ? 1e30f : 0.1f));
        printf("tape bad input %s\n", finite ? "OK" : "FAIL"); ok &= finite;
    }
    return ok;
}

// Every factory preset must be finite and bounded, and audible unless it needs an input.
static bool presetTests(const char* outDir) {
    bool ok = true;
    for (const auto& pr : infrasonic::kFactoryPresets) {
        DroseraCore core; core.Init();
        for (int i = 0; i < DroseraCore::NumParams; ++i) core.SetParam((DroseraCore::Param)i, pr.knob[i]);
        core.SetEchoWarp(pr.echoHalve);
        core.SetEchoTape(pr.echoTape, pr.tapeWow, pr.tapeAge);
        if (pr.midiGate) { core.SetMidiPitch(true, 48.0f); core.SetGate(true, true); }  // as if a note were held
        const size_t total = 48000 * 10;
        std::vector<float> L(total), R(total);
        for (size_t pos = 0; pos < total; pos += 64) core.Render(nullptr, &L[pos], &R[pos], 64);
        double peak = 0; bool finite = true;
        for (size_t i = 0; i < total; ++i) { finite &= std::isfinite(L[i]) && std::isfinite(R[i]); peak = std::max({peak, (double)std::fabs(L[i]), (double)std::fabs(R[i])}); }
        const double r = rmsOf(L, total / 2, total);
        const bool audible = r > 1e-3;
        const bool pass = finite && peak <= 1.0001 && (audible != pr.needsInput);
        printf("preset %-24s rms=%.4f (%6.1f dBFS) peak=%.3f %s\n", pr.name, r, 20 * std::log10(r + 1e-12), peak, pass ? "OK" : "FAIL");
        ok &= pass;
        if (outDir && !pr.needsInput) {
            std::string n = pr.name; for (char& c : n) if (c == ' ' || c == '(' || c == ')') c = '_';
            writeWav(std::string(outDir) + "/preset_" + n + ".wav", L, R);
        }
    }
    return ok;
}

int main(int argc, char** argv) {
    const char* outDir = argc > 1 ? argv[1] : nullptr;
    //                  freq   gain   body   lpf    hpf    rvMix  rvDec  eSend  eTime  eFb    vol
    Scenario sc[] = {
        {"default_drone",  {0.43f, 0.75f, 0.30f, 0.90f, 0.20f, 0.30f, 0.50f, 0.20f, 0.50f, 0.40f, 0.70f}, false, true},
        {"low_dark_verb",  {0.05f, 0.80f, 0.60f, 0.40f, 0.10f, 0.70f, 0.80f, 0.30f, 0.60f, 0.60f, 0.80f}, false, true},
        {"echo_runaway",   {0.60f, 0.70f, 0.20f, 1.00f, 0.00f, 0.50f, 0.90f, 1.00f, 0.80f, 1.00f, 1.00f}, true, true},
        {"everything_max", {1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f}, false, true},
        {"gain_zero",      {0.43f, 0.00f, 0.30f, 0.90f, 0.20f, 0.30f, 0.50f, 0.20f, 0.50f, 0.40f, 0.70f}, false, false},
    };
    bool ok = true;
    for (auto& s : sc)
        for (size_t b : {4, 64, 512, 480}) ok &= run(s, b, outDir);
    ok &= featureTests();
    ok &= resamplerTests();
    ok &= tapeTests();
    ok &= presetTests(outDir);
    printf(ok ? "ALL OK\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
