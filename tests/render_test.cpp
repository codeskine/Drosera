// Offline sanity test for the host-independent core: renders scenarios at several
// block sizes, checks for NaN/inf, bounds and non-silence, writes WAVs for listening.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include "DroseraCore.h"
#include "pitch_estimator.h"
#include "Presets.h"
#include "SincResampler.h"

using infrasonic::DroseraCore;

// Counts allocations while gCountAllocations is on, to prove the audio path makes none.
static std::atomic<bool> gCountAllocations { false };
static std::atomic<int> gAllocations { 0 };
void* operator new(std::size_t size) {
    if (gCountAllocations.load()) ++gAllocations;
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

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

//                           freq   gain   body   lpf    hpf    rvMix  rvDec  eSend  eTime  eFb    vol
static const float kDryBase[] = {0.43f, 0.75f, 0.30f, 0.90f, 0.20f, 0.00f, 0.50f, 0.00f, 0.50f, 0.00f, 0.70f};

// Audio input, MIDI pitch and gate features of the core.
static bool featureTests() {
    bool ok = true;
    const auto& base = kDryBase;
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

static bool presetTests(const char* outDir) {
    bool ok = true;
    for (const auto& pr : infrasonic::kFactoryPresets) {
        DroseraCore core; core.Init();
        for (int i = 0; i < DroseraCore::NumParams; ++i) core.SetParam((DroseraCore::Param)i, pr.knob[i]);
        core.SetEchoWarp(pr.echoHalve);
        core.SetPolyphony(pr.poly);
        core.SetCoupling(pr.coupling);
        core.SetIntervals(pr.interval2, pr.interval3);
        core.SetStringLock(pr.stringLock);
        if (pr.midiGate) {  // as if a note were held, or a chord on a polyphonic preset
            core.SetMidiPitch(true, 48.0f);
            core.SetGate(true, true);
            if (pr.poly) {
                core.SetVoiceNote(1, 52.0f); core.SetVoiceNote(2, 55.0f);
                core.SetVoiceGate(1, true, true); core.SetVoiceGate(2, true, true);
            }
        }
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
        if (pr.poly && !pr.midiPitch) {  // three strings that lock onto one partial sound as one tone
            const size_t n = 1 << 17, from = 48000 * 5;
            std::vector<std::complex<double>> a(n);
            for (size_t i = 0; i < n; ++i) a[i] = (0.5 - 0.5 * std::cos(2.0 * M_PI * (double)i / (double)n)) * L[from + i];
            fft(a);
            std::vector<double> power(n / 2);
            double all = 0;
            for (size_t k = 1; k < n / 2; ++k) { power[k] = std::norm(a[k]); all += power[k]; }
            const size_t peak = (size_t)(std::max_element(power.begin(), power.end()) - power.begin());
            double near = 0;
            for (size_t k = peak > 2 ? peak - 2 : 1; k <= peak + 2 && k < n / 2; ++k) near += power[k];
            const bool spread = near / all < 0.9;
            printf("threeStringsSound %-18s strongest partial %.1f Hz holds %.0f%% of the power %s\n", pr.name, (double)peak * 48000.0 / (double)n, 100.0 * near / all, spread ? "OK" : "FAIL");
            ok &= spread;
        }
        if (outDir && !pr.needsInput) {
            std::string n = pr.name; for (char& c : n) if (c == ' ' || c == '(' || c == ')') c = '_';
            writeWav(std::string(outDir) + "/preset_" + n + ".wav", L, R);
        }
    }
    return ok;
}

static void applyPreset(DroseraCore& core, const infrasonic::Preset& pr) {
    for (int k = 0; k < DroseraCore::NumParams; ++k) core.SetParam((DroseraCore::Param)k, pr.knob[k]);
    core.SetPolyphony(pr.poly);
    core.SetCoupling(pr.coupling);
    core.SetIntervals(pr.interval2, pr.interval3);
    core.SetStringLock(pr.stringLock);
}

static bool noAllocationOnRender() {
    static DroseraCore core;
    core.Init();
    applyPreset(core, infrasonic::kFactoryPresets[0]);
    static float L[64], R[64], in[64];
    gAllocations = 0;
    gCountAllocations = true;
    for (int block = 0; block < 1500; ++block) {  // 2 s
        const bool on = (block / 75) % 2 == 1;    // toggles every 100 ms
        core.SetMidiPitch(on, 48.0f);
        core.SetGate(on, true);
        core.SetEchoWarp(on);
        core.SetStringLock((block / 40) % 2 == 1);
        core.SetPolyphony(((block + 37) / 75) % 2 == 1);  // out of step, so that polyphony meets both pitch modes
        core.SetCoupling(on ? 1.0f : 0.0f);
        core.SetVoiceNote(1, 48.0f + (float)(block / 50 % 12));
        core.SetVoiceNote(2, 60.0f - (float)(block / 30 % 12));
        core.SetVoiceGate(1, on, block / 40 % 2 == 0);
        core.SetVoiceGate(2, on, block / 60 % 2 == 0);
        in[0] = block == 750 ? std::numeric_limits<float>::quiet_NaN() : 0.0f;
        if (block == 800) core.Recover();
        core.Render(in, L, R, 64);
    }
    gCountAllocations = false;
    const int count = gAllocations.load();
    printf("allocations while rendering, NaN recovery included: %d %s\n", count, count == 0 ? "OK" : "FAIL");
    return count == 0;
}

static bool recoversFromNan() {
    DroseraCore core;
    core.Init();
    applyPreset(core, infrasonic::kFactoryPresets[0]);
    const size_t after = 48000 * 3;
    std::vector<float> L(after), R(after);
    float l[64], r[64], in[64] = {};
    for (int block = 0; block < 750; ++block) core.Render(nullptr, l, r, 64);
    in[0] = std::numeric_limits<float>::quiet_NaN();
    core.Render(in, l, r, 64);
    core.Recover();
    for (size_t pos = 0; pos + 64 <= after; pos += 64) core.Render(nullptr, &L[pos], &R[pos], 64);
    bool finite = true;
    for (size_t i = 0; i + 64 <= after; ++i) finite &= std::isfinite(L[i]) && std::isfinite(R[i]);
    const double r2 = rmsOf(L, after - 48000, after - 64);
    const bool pass = finite && r2 > 0.01;
    printf("recovery from NaN: finite=%d rms of last second=%.4f %s\n", (int)finite, r2, pass ? "OK" : "FAIL");
    return pass;
}

static bool filtersClearOnInit() {
    infrasonic::LPF12 lpf;
    lpf.Init(48000.0f);
    lpf.SetQ(0.9f);
    lpf.SetCutoff(1000.0f);
    float a = std::numeric_limits<float>::quiet_NaN(), b = a;
    lpf.ProcessStereo(a, b);
    lpf.Init(48000.0f);
    lpf.SetQ(0.9f);
    lpf.SetCutoff(1000.0f);
    a = b = 0.5f;
    lpf.ProcessStereo(a, b);
    const bool pass = std::isfinite(a) && std::isfinite(b);
    printf("filter state cleared by Init: %s\n", pass ? "OK" : "FAIL");
    return pass;
}

// What the interface reads: the values that really sound, not the knobs.
static bool publishedValues() {
    float l[64], r[64];
    auto settle = [&](DroseraCore& core, double seconds) {
        for (int b = 0; b < (int)(seconds * 48000.0 / 64.0); ++b) core.Render(nullptr, l, r, 64);
    };
    bool ok = true;
    {
        DroseraCore core; core.Init();
        applyPreset(core, infrasonic::kFactoryPresets[0]);
        core.SetParam(DroseraCore::FeedbackBody, 0.30f);
        settle(core, 2.0);
        const float knob = daisysp::fmap(0.30f, 0.001f, 0.1f, daisysp::Mapping::EXP);
        const bool pass = std::fabs(core.PublishedBodySeconds() - knob) < knob * 0.01f;
        printf("published Body, knob: %.5f s (knob %.5f) %s\n", core.PublishedBodySeconds(), knob, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    {
        DroseraCore core; core.Init();
        applyPreset(core, infrasonic::kFactoryPresets[0]);
        core.SetParam(DroseraCore::FeedbackBody, 0.30f);
        core.SetMidiPitch(true, 60.0f);
        settle(core, 2.0);
        const float T = 1.0f / 261.63f;  // the loop's filters can move the delay by up to half a period
        const bool pass = std::fabs(core.PublishedNote() - 60.0f) < 0.01f && std::fabs(core.PublishedBodySeconds() - 3.0f * T) < 0.5f * T;
        printf("published note %.3f, Body %.5f s (3 periods %.5f) %s\n", core.PublishedNote(), core.PublishedBodySeconds(), 3.0f * T, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    {
        const float seconds = DroseraCore::EchoTimeSeconds(0.5f, true);
        const float expected = daisysp::fmap(0.25f, 0.05f, 5.0f, daisysp::Mapping::EXP);
        const bool pass = seconds == expected && DroseraCore::EchoTimeSeconds(0.5f, false) > seconds;
        printf("Echo Time with WARP: %.3f s (expected %.3f) %s\n", seconds, expected, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    return ok;
}

// The Engine's three voices, driven directly.
using infrasonic::FeedbackSynth::Engine;

static std::unique_ptr<Engine> makeEngine(float n0, float n1, float n2) {
    auto e = std::make_unique<Engine>();
    e->Init(48000.0f);
    const float notes[] = { n0, n1, n2 };
    for (size_t v = 0; v < Engine::kNumVoices; ++v) {
        e->SetStringPitch(v, notes[v]);
        e->SetFeedbackGain(v, 0.0f);
    }
    e->SetReverbMix(0.0f);
    e->SetEchoDelaySendAmount(0.0f);
    return e;
}

static float largestJump(const std::vector<float>& v, size_t a, size_t b) {
    float largest = 0.0f;
    for (size_t i = std::max(a, size_t(1)); i < b; ++i) largest = std::max(largest, std::fabs(v[i] - v[i - 1]));
    return largest;
}

static bool engineIndependentAtZeroCoupling() {
    auto a = makeEngine(48.0f, 55.0f, 64.0f);
    auto b = makeEngine(48.0f, 40.0f, 70.0f);
    bool same = true, different = false;
    for (auto* e : { a.get(), b.get() }) { e->SetPolyphony(true, true); e->SetCoupling(0.0f); }
    for (int i = 0; i < 48000 * 2; ++i) {
        float l, r;
        a->Process(0.0f, l, r);
        b->Process(0.0f, l, r);
        same &= a->VoiceTap(0, 0) == b->VoiceTap(0, 0) && a->VoiceTap(0, 1) == b->VoiceTap(0, 1);
        different |= a->VoiceTap(1, 0) != b->VoiceTap(1, 0);
    }
    const bool pass = same && different;
    printf("engine, voice 0 independent at Coupling 0: same=%d others differ=%d %s\n", (int)same, (int)different, pass ? "OK" : "FAIL");
    return pass;
}

static bool engineUnisonNotIdentical() {
    auto e = makeEngine(48.0f, 48.0f, 48.0f);
    e->SetPolyphony(true, true);
    bool differ10 = false, differ21 = false;
    for (int i = 0; i < 48000 * 2; ++i) {
        float l, r;
        e->Process(0.0f, l, r);
        if (i < 24000) continue;
        differ10 |= e->VoiceTap(1, 0) != e->VoiceTap(0, 0);
        differ21 |= e->VoiceTap(2, 0) != e->VoiceTap(1, 0);
    }
    const bool pass = differ10 && differ21;
    printf("engine, unison voices differ: 1/0=%d 2/1=%d %s\n", (int)differ10, (int)differ21, pass ? "OK" : "FAIL");
    return pass;
}

static bool engineCouplingRingsAClosedVoice() {
    const auto closedVoiceRms = [](float coupling) {
        auto e = makeEngine(48.0f, 48.0f, 48.0f);
        e->SetFeedbackGain(1, -60.0f);
        e->SetFeedbackGain(2, -60.0f);
        e->SetPolyphony(true, true);
        e->SetCoupling(coupling);
        std::vector<float> tap(48000 * 3);
        for (size_t i = 0; i < tap.size(); ++i) {
            float l, r;
            e->Process(0.0f, l, r);
            tap[i] = e->VoiceTap(1, 0);
        }
        return rmsOf(tap, 48000 * 2, tap.size());
    };
    const double without = closedVoiceRms(0.0f), with = closedVoiceRms(0.5f);
    const double db = 20.0 * std::log10(with / std::max(without, 1e-12));
    const bool pass = db >= 6.0;
    printf("engine, Coupling rings a closed voice: %.6f -> %.6f (%+.1f dB) %s\n", without, with, db, pass ? "OK" : "FAIL");
    return pass;
}

static bool engineRampClickFree() {
    bool ok = true;
    // The open filter is the loop as it stands. Its steady signal already jumps by a lot and would
    // hide a step, so the low cutoff is what catches one.
    for (const float lpf : { 18000.0f, 800.0f }) {
        auto e = makeEngine(48.0f, 55.0f, 64.0f);
        e->SetFeedbackLPFCutoff(lpf);
        std::vector<float> L(48000 * 5), R(L.size());
        for (size_t i = 0; i < L.size(); ++i) {
            if (i == 48000 * 2) e->SetPolyphony(true);
            if (i == 48000 * 4) e->SetPolyphony(false);
            e->Process(0.0f, L[i], R[i]);
        }
        for (const size_t start : { size_t(48000 * 2), size_t(48000 * 4) }) {
            for (const auto* v : { &L, &R }) {
                const float before = largestJump(*v, start - 48000, start), during = largestJump(*v, start, start + 960);
                const bool pass = during <= 1.5f * before;
                printf("engine, LPF %5.0f Hz, ramp at %d s: largest jump %.5f, before %.5f %s\n", lpf, (int)(start / 48000), during, before, pass ? "OK" : "FAIL");
                ok &= pass;
            }
        }
    }
    return ok;
}

static bool engineRampEnds() {
    auto e = makeEngine(48.0f, 55.0f, 64.0f);
    float l, r;
    const bool offByDefault = !e->PolyRunning();
    e->SetPolyphony(true);
    const bool onAtOnce = e->PolyRunning();
    for (int i = 0; i < 4800; ++i) e->Process(0.0f, l, r);
    const bool onDuring = e->PolyRunning();
    e->SetPolyphony(false);
    for (int i = 0; i < 959; ++i) e->Process(0.0f, l, r);
    const bool onAt959 = e->PolyRunning();
    e->Process(0.0f, l, r);
    e->Process(0.0f, l, r);
    const bool offAt961 = !e->PolyRunning();
    // Init jumps a ramp in progress to its end.
    e->SetPolyphony(true);
    e->Process(0.0f, l, r);
    e->SetPolyphony(false);
    e->Init(48000.0f);
    const bool offAfterInit = !e->PolyRunning();
    const bool pass = offByDefault && onAtOnce && onDuring && onAt959 && offAt961 && offAfterInit;
    printf("engine, ramp end: default off=%d on=%d during=%d at 959=%d off at 961=%d off after Init=%d %s\n",
           (int)offByDefault, (int)onAtOnce, (int)onDuring, (int)onAt959, (int)offAt961, (int)offAfterInit, pass ? "OK" : "FAIL");
    return pass;
}

// Once the ramp has ended the output is the plain sum, with no movement left in the ramp.
static bool engineRampSettles() {
    auto e = makeEngine(48.0f, 55.0f, 64.0f);
    e->SetPolyphony(true);
    float l, r, worst = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        e->Process(0.0f, l, r);
        const float sum = e->VoiceTap(0, 0) + e->VoiceTap(1, 0) + e->VoiceTap(2, 0);
        if (i >= 960) worst = std::max(worst, std::fabs(l - 0.25f * sum * (1.0f / std::sqrt(3.0f))));
    }
    const bool pass = worst < 1e-5f;
    printf("engine, ramp settles on the sum: largest error %.7f %s\n", worst, pass ? "OK" : "FAIL");
    return pass;
}

static bool engineRestartsFromSilence() {
    auto e = makeEngine(48.0f, 55.0f, 64.0f);
    float l, r;
    e->SetPolyphony(true);
    float loud = 0.0f;
    for (int i = 0; i < 48000; ++i) { e->Process(0.0f, l, r); loud = std::max(loud, std::fabs(e->VoiceTap(1, 0))); }
    e->SetPolyphony(false);
    for (int i = 0; i < 961; ++i) e->Process(0.0f, l, r);
    e->SetPolyphony(true);
    float peak = std::fabs(e->VoiceTap(1, 0));
    for (int i = 0; i < 10; ++i) { e->Process(0.0f, l, r); peak = std::max(peak, std::fabs(e->VoiceTap(1, 0))); }
    const bool pass = loud > 0.01f && peak < 0.01f;
    printf("engine, restart from silence: peak %.6f (was %.3f) %s\n", peak, loud, pass ? "OK" : "FAIL");
    return pass;
}

static bool engineRampReverses() {
    bool ok = true;
    for (const bool startOn : { true, false }) {  // the ramp stays near the top, or returns to 0 and clears the voices
        auto e = makeEngine(48.0f, 55.0f, 64.0f);
        e->SetPolyphony(startOn, true);
        std::vector<float> L(48000 * 3), R(L.size());
        bool on = startOn, finite = true;
        for (size_t i = 0; i < L.size(); ++i) {
            if (i >= 48000 * 2 && (i - 48000 * 2) % 240 == 0) { on = !on; e->SetPolyphony(on); }
            e->Process(0.0f, L[i], R[i]);
            finite &= std::isfinite(L[i]) && std::isfinite(R[i]);
        }
        const float before = largestJump(L, 48000, 48000 * 2), during = largestJump(L, 48000 * 2, L.size());
        const bool pass = finite && during <= 1.5f * before;
        printf("engine, ramp reversing from %s: finite=%d largest jump %.5f, before %.5f %s\n", startOn ? "on" : "off", (int)finite, during, before, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    return ok;
}

static bool engineVoiceTests() {
    bool ok = true;
    ok &= engineIndependentAtZeroCoupling();
    ok &= engineUnisonNotIdentical();
    ok &= engineCouplingRingsAClosedVoice();
    ok &= engineRampClickFree();
    ok &= engineRampEnds();
    ok &= engineRampSettles();
    ok &= engineRestartsFromSilence();
    ok &= engineRampReverses();
    return ok;
}

// The three voices, driven through DroseraCore. Tap-based tests render with block size 1.
struct PolyRender { std::vector<float> L, R, tap[3]; };

template <typename AtSample>
static PolyRender renderTaps(DroseraCore& core, size_t n, AtSample atSample) {
    PolyRender out;
    out.L.resize(n); out.R.resize(n);
    for (auto& t : out.tap) t.resize(n);
    for (size_t i = 0; i < n; ++i) {
        atSample(i);
        core.Render(nullptr, &out.L[i], &out.R[i], 1);
        for (size_t v = 0; v < 3; ++v) out.tap[v][i] = core.VoiceTap(v, 0);
    }
    return out;
}
static PolyRender renderTaps(DroseraCore& core, size_t n) { return renderTaps(core, n, [](size_t) {}); }

static void setChord(DroseraCore& core, float a, float b, float c) {
    core.SetPolyphony(true);
    core.SetMidiPitch(true, a);
    core.SetVoiceNote(1, b);
    core.SetVoiceNote(2, c);
    for (size_t v = 0; v < 3; ++v) core.SetVoiceGate(v, true, true);
}

static double seconds(double s) { return s * 48000.0; }
static double tapRms(const PolyRender& r, size_t v, double from, double to) { return rmsOf(r.tap[v], (size_t)seconds(from), (size_t)seconds(to)); }
static double dbOf(double a, double b) { return 20.0 * std::log10(std::max(a, 1e-12) / std::max(b, 1e-12)); }
static double centsOff(const std::vector<float>& tap, float note) {
    return 1200.0 * std::log2(dominantHz(tap, 48000 * 2) / (440.0 * std::pow(2.0, (note - 69.0) / 12.0)));
}

static bool chordTuning() {
    bool ok = true;
    const float chords[][3] = { {36, 43, 52}, {45, 52, 57}, {48, 55, 64}, {60, 64, 67}, {64, 69, 76} };
    for (const float coupling : { 0.0f, 1.0f }) {
        for (const auto& c : chords) {
            DroseraCore core; makeCore(core, kDryBase);
            setChord(core, c[0], c[1], c[2]);
            core.SetCoupling(coupling);
            const auto r = renderTaps(core, 48000 * 5);
            for (size_t v = 0; v < 3; ++v) {
                const double cents = centsOff(r.tap[v], c[v]);
                const bool pass = std::fabs(cents) < 25.0;
                printf("poly chord %.0f %.0f %.0f, Coupling %.0f%%, voice %zu: %+.1f c %s\n", c[0], c[1], c[2], coupling * 100.0f, v, cents, pass ? "OK" : "FAIL");
                ok &= pass;
            }
        }
    }
    return ok;
}

// Coupling at maximum must not throw a chord up to a higher partial, in any register.
static bool coupledChordsHoldAcrossRegisters() {
    bool ok = true;
    const int shapes[][2] = { {4, 7}, {3, 7}, {7, 12}, {5, 7}, {2, 7}, {1, 2}, {7, 16}, {12, 24} };
    for (int root = 24; root <= 60; root += 6) {
        for (const auto& s : shapes) {
            const float c[3] = { (float)root, (float)(root + s[0]), (float)(root + s[1]) };
            DroseraCore core; makeCore(core, kDryBase);
            setChord(core, c[0], c[1], c[2]);
            core.SetCoupling(1.0f);
            const auto r = renderTaps(core, 48000 * 5);
            for (size_t v = 0; v < 3; ++v) {
                const double cents = centsOff(r.tap[v], c[v]);
                const bool pass = std::fabs(cents) < 25.0;
                printf("poly chord %.0f %.0f %.0f holds at Coupling 100%%, voice %zu: %+.1f c %s\n", c[0], c[1], c[2], v, cents, pass ? "OK" : "FAIL");
                ok &= pass;
            }
        }
    }
    return ok;
}

// The preset's own knobs and Coupling. Reverb Mix detunes MIDI notes, since its phase is not compensated: this holds it down.
static bool chordKeysInTune() {
    bool ok = true;
    const infrasonic::Preset* preset = nullptr;
    for (const auto& pr : infrasonic::kFactoryPresets)
        if (std::string(pr.name) == "Chord Keys") preset = &pr;
    if (preset == nullptr) { printf("preset Chord Keys: missing FAIL\n"); return false; }
    const int shapes[][2] = { {4, 7}, {3, 7}, {7, 12}, {5, 7}, {2, 7} };
    for (int root = 36; root <= 60; root += 6) {
        for (const auto& s : shapes) {
            const float c[3] = { (float)root, (float)(root + s[0]), (float)(root + s[1]) };
            DroseraCore core; makeCore(core, kDryBase);
            applyPreset(core, *preset);
            setChord(core, c[0], c[1], c[2]);
            const auto r = renderTaps(core, 48000 * 5);
            for (size_t v = 0; v < 3; ++v) {
                const double cents = centsOff(r.tap[v], c[v]);
                const bool pass = std::fabs(cents) < 25.0;
                printf("preset Chord Keys, chord %.0f %.0f %.0f, voice %zu: %+.1f c %s\n", c[0], c[1], c[2], v, cents, pass ? "OK" : "FAIL");
                ok &= pass;
            }
        }
    }
    return ok;
}

static bool chordTuningAfterRestart() {
    DroseraCore core; makeCore(core, kDryBase);
    setChord(core, 48, 55, 64);
    const size_t stop = 48000 * 2, start = stop + 4800;
    const float next[] = { 45, 52, 57 };
    float noteAtOnce[3] = {};
    const auto r = renderTaps(core, start + 48000 * 5, [&](size_t i) {
        if (i == stop) core.SetPolyphony(false);
        if (i == start) setChord(core, next[0], next[1], next[2]);
        if (i == start + 8) for (size_t v = 1; v < 3; ++v) noteAtOnce[v] = core.PublishedNote(v);
    });
    bool ok = true;
    // No glide from the old chord: the first control tick already has the new notes.
    const bool jumped = std::fabs(noteAtOnce[1] - next[1]) < 0.01f && std::fabs(noteAtOnce[2] - next[2]) < 0.01f;
    printf("poly restart starts on the new notes: %.2f %.2f %s\n", noteAtOnce[1], noteAtOnce[2], jumped ? "OK" : "FAIL");
    ok &= jumped;
    for (size_t v = 0; v < 3; ++v) {
        const std::vector<float> after(r.tap[v].begin() + (std::ptrdiff_t)start, r.tap[v].end());
        const double cents = centsOff(after, next[v]);
        const bool pass = std::fabs(cents) < 25.0;
        printf("poly restart with a new chord, voice %zu: %+.1f c %s\n", v, cents, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    return ok;
}

// A voice's delay lag has to return to MIDI's short one on restart, or the delay is still gliding from the old
// chord's and the loop takes longer to build. Tuning 5 s later cannot show this. A fresh start is the yardstick.
static bool restartBuildsAsFast() {
    const auto onset = [](const std::vector<float>& tap, size_t start) {  // samples to the first 50 ms at full level
        size_t a = start;
        while (a + 2400 < tap.size() && rmsOf(tap, a, a + 2400) < 0.5) a += 2400;
        return a - start;
    };
    const float chord[] = { 36, 43, 43 };  // voices 1 and 2 both have far to glide from 67 and 72
    DroseraCore fresh; makeCore(fresh, kDryBase);
    setChord(fresh, chord[0], chord[1], chord[2]);
    const auto f = renderTaps(fresh, 48000 * 2);
    DroseraCore core; makeCore(core, kDryBase);
    setChord(core, 60, 67, 72);
    const size_t stop = 48000 * 2, start = stop + 4800;
    const auto r = renderTaps(core, start + 48000 * 2, [&](size_t i) {
        if (i == stop) core.SetPolyphony(false);
        if (i == start) setChord(core, chord[0], chord[1], chord[2]);
    });
    bool ok = true;
    for (size_t v = 1; v < 3; ++v) {
        const size_t restarted = onset(r.tap[v], start), first = onset(f.tap[v], 0);
        const bool pass = restarted <= first + 2400;
        printf("poly restart builds as fast, voice %zu: %.2f s, fresh %.2f s %s\n", v, (double)restarted / 48000.0, (double)first / 48000.0, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    return ok;
}

// FeedbackVoice::Init restores LPF and HPF defaults: a restarted voice must sound as a fresh one does.
// The overdrive keeps the level almost the same whatever the filters do, so this compares the slope's RMS, which is brightness.
static double slopeRms(const std::vector<float>& tap, size_t a, size_t b) {
    std::vector<float> slope(b - a);
    for (size_t i = a; i < b; ++i) slope[i - a] = tap[i] - tap[i - 1];
    return rmsOf(slope, 0, slope.size());
}

static bool restartKeepsFilters() {
    float p[DroseraCore::NumParams]; std::copy(kDryBase, kDryBase + DroseraCore::NumParams, p);
    p[DroseraCore::FeedbackLPF] = 0.4f;  // about 800 Hz
    p[DroseraCore::FeedbackHPF] = 0.6f;
    DroseraCore fresh; makeCore(fresh, p);
    setChord(fresh, 48, 55, 64);
    const double expected = slopeRms(renderTaps(fresh, 48000 * 4).tap[1], 48000 * 3, 48000 * 4);
    DroseraCore core; makeCore(core, p);
    setChord(core, 48, 55, 64);
    const size_t stop = 48000, start = stop + 4800;
    const auto r = renderTaps(core, start + 48000 * 4, [&](size_t i) {
        if (i == stop) core.SetPolyphony(false);
        if (i == start) core.SetPolyphony(true);
    });
    const double got = slopeRms(r.tap[1], start + 48000 * 3, start + 48000 * 4);
    const bool pass = std::fabs(dbOf(got, expected)) < 1.0;
    printf("poly restart keeps the filters: voice 1 slope rms %.5f, fresh %.5f %s\n", got, expected, pass ? "OK" : "FAIL");
    return pass;
}

static bool knobIntervals() {
    DroseraCore core; makeCore(core, kDryBase);
    core.SetPolyphony(true);
    core.SetIntervals(7, 12);
    float l[64], r[64];
    for (int b = 0; b < 1500; ++b) core.Render(nullptr, l, r, 64);
    const float d1 = core.PublishedNote(1) - core.PublishedNote(0), d2 = core.PublishedNote(2) - core.PublishedNote(0);
    const bool pass = std::fabs(d1 - 7.0f) < 0.01f && std::fabs(d2 - 12.0f) < 0.01f;
    printf("poly knob intervals: %+.3f %+.3f %s\n", d1, d2, pass ? "OK" : "FAIL");
    return pass;
}

static bool intervalsClamp() {
    bool ok = true;
    DroseraCore core; makeCore(core, kDryBase);
    core.SetPolyphony(true);
    float l[64], r[64];
    struct Case { float freq; int interval; float expected; };
    for (const Case c : { Case{1.0f, 24, 84.0f}, Case{0.0f, -24, 16.0f} }) {
        core.SetParam(DroseraCore::Frequency, c.freq);
        core.SetIntervals(c.interval, c.interval);
        bool finite = true, inRange = true;
        for (int b = 0; b < 2250; ++b) {
            core.Render(nullptr, l, r, 64);
            for (int i = 0; i < 64; ++i) finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
            for (size_t v = 0; v < 3; ++v) inRange &= core.PublishedNote(v) >= 16.0f && core.PublishedNote(v) <= 84.0f;
        }
        const bool reached = std::fabs(core.PublishedNote(1) - c.expected) < 0.01f;
        const bool pass = finite && inRange && reached;
        printf("poly interval %+d at Frequency %.1f: voice 1 note %.2f finite=%d in range=%d %s\n", c.interval, c.freq, core.PublishedNote(1), (int)finite, (int)inRange, pass ? "OK" : "FAIL");
        ok &= pass;
    }
    return ok;
}

static bool neverAssignedStaysClosed() {
    DroseraCore core; makeCore(core, kDryBase);
    core.SetPolyphony(true);
    core.SetMidiPitch(true, 48.0f);
    core.SetVoiceGate(0, true, true);
    core.SetVoiceGate(1, true, false);
    core.SetVoiceGate(2, true, false);
    const auto r = renderTaps(core, 48000 * 5);
    const double db = dbOf(tapRms(r, 1, 3.0, 5.0), tapRms(r, 0, 3.0, 5.0));
    const bool pass = db <= -40.0;
    printf("poly never assigned voice stays closed: %.1f dB below voice 0 %s\n", db, pass ? "OK" : "FAIL");
    return pass;
}

static bool perVoiceGate() {
    DroseraCore core; makeCore(core, kDryBase);
    setChord(core, 48, 55, 64);
    const auto r = renderTaps(core, 48000 * 6, [&](size_t i) { if (i == 48000 * 3) core.SetVoiceGate(1, true, false); });
    const double closed = dbOf(tapRms(r, 1, 5.5, 6.0), tapRms(r, 1, 2.5, 3.0));
    const double open = dbOf(tapRms(r, 0, 5.5, 6.0), tapRms(r, 0, 2.5, 3.0));
    const bool pass = closed < -20.0 && open > -3.0;
    printf("poly gate per voice: closed voice %.1f dB, open voice %.1f dB %s\n", closed, open, pass ? "OK" : "FAIL");
    return pass;
}

static bool chordIsAudibleImmediately() {
    DroseraCore core; makeCore(core, kDryBase);
    setChord(core, 48, 55, 64);
    const auto r = renderTaps(core, 48000 * 3);
    bool ok = true;
    for (size_t v = 0; v < 3; ++v) ok &= tapRms(r, v, 2.0, 3.0) > 1e-3;
    bool early = false;
    for (size_t i = 0; i < 48000; ++i) early |= r.tap[1][i] != 0.0f;
    // A ramp would weight voices 1 and 2 less than voice 0, so the output would stop being
    // proportional to the taps' sum. The first 10 ms are too quiet for the limiter to bend.
    double sumSum = 0.0, outSum = 0.0, residual = 0.0, level = 0.0;
    std::vector<double> sum(480);
    for (size_t i = 0; i < 480; ++i) sum[i] = (double)r.tap[0][i] + r.tap[1][i] + r.tap[2][i];
    for (size_t i = 0; i < 480; ++i) { sumSum += sum[i] * sum[i]; outSum += sum[i] * r.L[i]; level += (double)r.L[i] * r.L[i]; }
    const double k = outSum / std::max(sumSum, 1e-30);
    for (size_t i = 0; i < 480; ++i) residual += (r.L[i] - k * sum[i]) * (r.L[i] - k * sum[i]);
    const double error = std::sqrt(residual / std::max(level, 1e-30));
    const bool pass = ok && early && error < 0.01;
    printf("poly chord audible at once: rms %.4f %.4f %.4f, voice 1 early=%d, no ramp (error %.5f) %s\n",
           tapRms(r, 0, 2.0, 3.0), tapRms(r, 1, 2.0, 3.0), tapRms(r, 2, 2.0, 3.0), (int)early, error, pass ? "OK" : "FAIL");
    return pass;
}

// With Coupling and Feedback at maximum, across the extreme notes and filters, under MIDI and from the knobs.
static bool stabilityAtMaximum() {
    bool ok = true;
    const auto check = [&](const std::string& name, DroseraCore& core) {
        core.SetCoupling(1.0f);
        core.SetParam(DroseraCore::FeedbackGain, 1.0f);
        bool finite = true;
        double peak = 0.0;
        float l[64], r[64];
        for (int setting = 0; setting < 8; ++setting) {  // 7.5 s each, 60 s in all
            core.SetParam(DroseraCore::ReverbMix, (setting & 1) ? 1.0f : 0.0f);
            core.SetParam(DroseraCore::FeedbackLPF, (setting & 2) ? 1.0f : 0.0f);
            core.SetParam(DroseraCore::FeedbackHPF, (setting & 4) ? 1.0f : 0.0f);
            for (int b = 0; b < 5625; ++b) {
                core.Render(nullptr, l, r, 64);
                for (int i = 0; i < 64; ++i) {
                    finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
                    peak = std::max({peak, (double)std::fabs(l[i]), (double)std::fabs(r[i])});
                }
            }
        }
        const bool pass = finite && peak <= 1.0001 && core.RecoveryCount() == 0;
        printf("poly stability at maximum, %s: finite=%d peak=%.3f recoveries=%d %s\n", name.c_str(), (int)finite, peak, core.RecoveryCount(), pass ? "OK" : "FAIL");
        ok &= pass;
    };
    const float chords[][3] = { {16, 50, 84}, {48, 48, 48} };
    for (const auto& c : chords) {
        DroseraCore core; makeCore(core, kDryBase);
        setChord(core, c[0], c[1], c[2]);
        check("chord " + std::to_string((int)c[0]) + " " + std::to_string((int)c[1]) + " " + std::to_string((int)c[2]), core);
    }
    {
        DroseraCore core; makeCore(core, kDryBase);
        core.SetPolyphony(true);
        core.SetStringLock(true);
        core.SetIntervals(-24, 24);
        check("LOCK from the knobs", core);
    }
    for (const float body : { 0.0f, 1.0f }) {
        DroseraCore core; makeCore(core, kDryBase);
        core.SetPolyphony(true);
        core.SetIntervals(0, 12);
        core.SetParam(DroseraCore::FeedbackBody, body);
        check(std::string("knobs, Body ") + (body > 0.5f ? "1" : "0"), core);
    }
    return ok;
}

// A closed voice hears none of the others: MIDI Gate closes it whatever Coupling does.
static bool closedVoiceStaysFaint() {
    bool ok = true;
    struct Gates { const char* name; bool open[3]; };
    const Gates gates[] = { {"voice 0 open", {true, false, false}}, {"voices 0 and 1 open", {true, true, false}}, {"voices 1 and 2 open", {false, true, true}} };
    for (const float reverb : { 0.0f, 0.5f }) {
        float p[DroseraCore::NumParams]; std::copy(kDryBase, kDryBase + DroseraCore::NumParams, p);
        p[DroseraCore::ReverbMix] = reverb;
        for (const Gates& g : gates) {
            for (const int interval : { 0, 7, 6 }) {
                DroseraCore core; makeCore(core, p);
                float notes[3];
                float open = 48.0f;
                for (size_t v = 0; v < 3; ++v) { notes[v] = g.open[v] ? open : 48.0f + (float)interval; if (g.open[v]) open += 12.0f; }
                setChord(core, notes[0], notes[1], notes[2]);
                for (size_t v = 0; v < 3; ++v) core.SetVoiceGate(v, true, g.open[v]);
                core.SetCoupling(1.0f);
                const auto r = renderTaps(core, 48000 * 5);
                double loudestOpen = 0.0, loudestClosed = 0.0;
                for (size_t v = 0; v < 3; ++v) (g.open[v] ? loudestOpen : loudestClosed) = std::max(g.open[v] ? loudestOpen : loudestClosed, tapRms(r, v, 3.0, 5.0));
                const double db = dbOf(loudestClosed, loudestOpen);
                const bool pass = db <= -40.0;
                printf("poly closed voice at Coupling 100%%, reverb %.1f, %s, interval %d: %.1f dB %s\n", reverb, g.name, interval, db, pass ? "OK" : "FAIL");
                ok &= pass;
            }
        }
    }
    return ok;
}

// The same holds from the first sample of a restart: a voice that stopped open and restarts closed must not glide shut.
static bool closedVoiceStaysFaintFromRestart() {
    DroseraCore core; makeCore(core, kDryBase);
    setChord(core, 48, 48, 48);
    core.SetCoupling(1.0f);
    const size_t stop = 48000 * 2, start = stop + 4800;
    const auto r = renderTaps(core, start + 24000, [&](size_t i) {
        if (i == stop) core.SetPolyphony(false);
        if (i == start) { core.SetPolyphony(true); core.SetVoiceGate(1, true, false); core.SetVoiceGate(2, true, false); }
    });
    const double db = dbOf(rmsOf(r.tap[1], start, r.tap[1].size()), rmsOf(r.tap[0], start, r.tap[0].size()));
    const bool pass = db <= -40.0;
    printf("poly voice restarting closed at Coupling 100%%: %.1f dB %s\n", db, pass ? "OK" : "FAIL");
    return pass;
}

static std::vector<double> magnitudeSpectrum(const std::vector<float>& x, size_t start) {
    const size_t N = 1 << 17;
    std::vector<std::complex<double>> a(N);
    for (size_t i = 0; i < N; ++i) a[i] = (double)x[start + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double)i / (double)N));
    fft(a);
    std::vector<double> m(N / 2);
    for (size_t k = 0; k < N / 2; ++k) m[k] = std::abs(a[k]);
    return m;
}

// ||A-B|| / ||A||, on magnitudes: a voice locked to its note can shift in phase with no change in sound, and a
// difference of waveforms would count that.
static double spectralRatio(const std::vector<double>& A, const std::vector<double>& B) {
    double difference = 0.0, level = 0.0;
    for (size_t k = 0; k < A.size(); ++k) { difference += (A[k] - B[k]) * (A[k] - B[k]); level += A[k] * A[k]; }
    return std::sqrt(difference / level);
}

static bool couplingChangesTheSpectrum() {
    bool ok = true;
    const auto compare = [&](const std::string& name, auto setup) {
        const auto output = [&](float coupling) {
            DroseraCore core; makeCore(core, kDryBase);
            setup(core);
            core.SetCoupling(coupling);
            return magnitudeSpectrum(renderTaps(core, 48000 * 6).L, 48000 * 3);
        };
        const double ratio = spectralRatio(output(1.0f), output(0.0f));
        const bool pass = ratio > 0.1;
        printf("poly Coupling 100%% changes the spectrum, %s: %.3f %s\n", name.c_str(), ratio, pass ? "OK" : "FAIL");
        ok &= pass;
    };
    compare("chord 48 55 64", [](DroseraCore& core) { setChord(core, 48, 55, 64); });
    compare("chord 60 64 67", [](DroseraCore& core) { setChord(core, 60, 64, 67); });
    compare("knobs +7 +12", [](DroseraCore& core) { core.SetPolyphony(true); core.SetIntervals(7, 12); });
    compare("LOCK, knobs +7 +12", [](DroseraCore& core) { core.SetPolyphony(true); core.SetStringLock(true); core.SetIntervals(7, 12); });
    return ok;
}

// A long Body packs its resonances close around the note, so Coupling can push a voice onto the next one. A voice that
// holds its note at Coupling 0 must still hold it with Coupling up. Some don't hold at 0, with no help from Coupling.
static bool coupledChordsHoldAtLongBody() {
    bool ok = true;
    const int shapes[][2] = { {4, 7}, {3, 7}, {7, 12}, {2, 7} };
    const auto chordAt = [](float body, const float* c, float coupling) {
        float p[DroseraCore::NumParams]; std::copy(kDryBase, kDryBase + DroseraCore::NumParams, p);
        p[DroseraCore::FeedbackBody] = body;
        DroseraCore core; makeCore(core, p);
        setChord(core, c[0], c[1], c[2]);
        core.SetCoupling(coupling);
        return renderTaps(core, 48000 * 5);
    };
    for (const float body : { 0.7f, 0.8f, 0.9f, 1.0f }) {
        for (const float coupling : { 0.8f, 1.0f }) {
            int voices = 0, moved = 0;
            double worst = 0.0;
            for (int root = 24; root <= 60; root += 6) {
                for (const auto& s : shapes) {
                    const float c[3] = { (float)root, (float)(root + s[0]), (float)(root + s[1]) };
                    const auto alone = chordAt(body, c, 0.0f), coupled = chordAt(body, c, coupling);
                    for (size_t v = 0; v < 3; ++v) {
                        if (std::fabs(centsOff(alone.tap[v], c[v])) >= 25.0) continue;
                        const double cents = std::fabs(centsOff(coupled.tap[v], c[v]));
                        ++voices;
                        worst = std::max(worst, cents);
                        if (cents >= 25.0) ++moved;
                    }
                }
            }
            const bool pass = moved == 0;
            printf("poly Body %.1f, Coupling %.0f%%: %d of %d voices moved off their notes, worst %.1f c %s\n", body, coupling * 100.0f, moved, voices, worst, pass ? "OK" : "FAIL");
            ok &= pass;
        }
    }
    return ok;
}

// What keeps long Bodies in tune must leave the middle of the knob alone.
static bool couplingStillActsAtMidBody() {
    float p[DroseraCore::NumParams]; std::copy(kDryBase, kDryBase + DroseraCore::NumParams, p);
    p[DroseraCore::FeedbackBody] = 0.5f;
    const auto output = [&](float coupling) {
        DroseraCore core; makeCore(core, p);
        setChord(core, 60, 64, 67);
        core.SetCoupling(coupling);
        return magnitudeSpectrum(renderTaps(core, 48000 * 6).L, 48000 * 3);
    };
    const double ratio = spectralRatio(output(1.0f), output(0.0f));
    const bool pass = ratio > 0.1;
    printf("poly Coupling 100%% changes the spectrum at Body 0.5: %.3f %s\n", ratio, pass ? "OK" : "FAIL");
    return pass;
}

// LOCK from the knobs: Frequency and the intervals are the notes that sound (rule 3: by the dominant partial).
static bool lockedKnobsPlayTheirNotes() {
    bool ok = true;
    for (const float frequency : { 0.3f, 0.43f, 0.55f, 0.7f }) {
        for (const float body : { 0.3f, 0.6f, 0.9f }) {
            float p[DroseraCore::NumParams]; std::copy(kDryBase, kDryBase + DroseraCore::NumParams, p);
            p[DroseraCore::Frequency] = frequency;
            p[DroseraCore::FeedbackBody] = body;
            DroseraCore core; makeCore(core, p);
            core.SetPolyphony(true);
            core.SetStringLock(true);
            core.SetIntervals(7, 12);
            const auto r = renderTaps(core, 48000 * 5);
            const float root = 16.0f + frequency * 56.0f;
            const float notes[3] = { root, root + 7.0f, root + 12.0f };
            double worst = 0.0;
            for (size_t v = 0; v < 3; ++v) worst = std::max(worst, std::fabs(centsOff(r.tap[v], notes[v])));
            const bool pass = worst < 25.0;
            printf("poly LOCK from the knobs, Frequency %.2f Body %.1f: worst voice %.1f c %s\n", frequency, body, worst, pass ? "OK" : "FAIL");
            ok &= pass;
        }
    }
    return ok;
}

// LOCK off: each voice's Body follows its interval, so the voices do not pile onto one of the shared Body's resonances.
static bool freeVoicesSpreadOut() {
    const float wet[] = {0.43f, 0.75f, 0.30f, 0.90f, 0.20f, 0.30f, 0.50f, 0.20f, 0.50f, 0.40f, 0.70f};
    int together = 0, cells = 0;
    for (const float frequency : { 0.1f, 0.2f, 0.3f, 0.43f, 0.55f, 0.7f, 0.85f }) {
        for (const float body : { 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f }) {
            float p[DroseraCore::NumParams]; std::copy(wet, wet + DroseraCore::NumParams, p);
            p[DroseraCore::Frequency] = frequency;
            p[DroseraCore::FeedbackBody] = body;
            DroseraCore core; makeCore(core, p);
            core.SetPolyphony(true);
            core.SetIntervals(7, 12);
            const auto r = renderTaps(core, 48000 * 5);
            const double a = dominantHz(r.tap[0], 48000 * 2), b = dominantHz(r.tap[1], 48000 * 2), c = dominantHz(r.tap[2], 48000 * 2);
            const auto near = [](double x, double y) { return std::fabs(1200.0 * std::log2(x / y)) < 50.0; };
            if (near(a, b) || near(a, c) || near(b, c)) ++together;
            ++cells;
        }
    }
    const bool pass = together <= 8;
    printf("poly free voices on one pitch: %d of %d settings %s\n", together, cells, pass ? "OK" : "FAIL");
    return pass;
}

// A known limit: LOCK holds the loop on the note, so a note far outside the filters cannot sustain, where the free drone
// just finds another resonance. Should this change, the manual's known limits should too.
static bool lockFallsSilentOutsideTheFilters() {
    const auto rmsWith = [](bool lock) {
        float p[DroseraCore::NumParams]; std::copy(kDryBase, kDryBase + DroseraCore::NumParams, p);
        p[DroseraCore::FeedbackHPF] = 0.8f;  // about 1200 Hz, notes at about 80 Hz
        DroseraCore core; makeCore(core, p);
        core.SetPolyphony(true);
        core.SetStringLock(lock);
        core.SetIntervals(7, 12);
        const auto r = renderTaps(core, 48000 * 5);
        return rmsOf(r.L, 48000 * 3, 48000 * 5);
    };
    const double locked = rmsWith(true), free = rmsWith(false);
    const bool pass = locked < 1e-3 && free > 0.05;
    printf("poly LOCK at Highpass 0.8: rms %.5f, free %.3f %s\n", locked, free, pass ? "OK" : "FAIL");
    return pass;
}

// The string's delay moves between its two tunings when LOCK switches. Unfaded, the output steps at the switch.
static bool lockSwitchIsClickFree() {
    bool ok = true;
    for (const float coupling : { 0.0f, 1.0f }) {
        for (const float frequency : { 0.3f, 0.43f }) {
            for (const float body : { 0.3f, 0.7f }) {
                float p[DroseraCore::NumParams]; std::copy(kDryBase, kDryBase + DroseraCore::NumParams, p);
                p[DroseraCore::Frequency] = frequency;
                p[DroseraCore::FeedbackBody] = body;
                DroseraCore core; makeCore(core, p);
                core.SetPolyphony(true);
                core.SetStringLock(true);
                core.SetIntervals(7, 12);
                core.SetCoupling(coupling);
                const size_t off = 48000 * 4, n = off + 480;  // the free drone soon moves much more on its own
                std::vector<float> L(n), R(n);
                for (size_t i = 0; i < n; ++i) {
                    if (i == off) core.SetStringLock(false);
                    core.Render(nullptr, &L[i], &R[i], 1);
                }
                // A second difference: a step or a kink shows here, where the settled chord moves little.
                const auto kink = [&](size_t a, size_t b) { float k = 0.0f; for (size_t i = a; i < b; ++i) k = std::max(k, std::fabs(L[i] - 2.0f * L[i - 1] + L[i - 2])); return k; };
                const float before = kink(off - 48000, off), during = kink(off, n);
                // The floor: on a very clean chord the free drone starts by itself within this window. Its kinks reach 0.01
                // to 0.02 by 20 ms, and an unfaded switch reads 0.04 to 0.05.
                const bool pass = during <= std::max(1.5f * before, 0.01f);
                printf("poly LOCK off at Coupling %.0f%%, Frequency %.2f Body %.1f: largest kink %.5f, before %.5f %s\n", coupling * 100.0f, frequency, body, during, before, pass ? "OK" : "FAIL");
                ok &= pass;
            }
        }
    }
    return ok;
}

static bool coupledRampClickFree() {
    float p[DroseraCore::NumParams]; std::copy(kDryBase, kDryBase + DroseraCore::NumParams, p);
    p[DroseraCore::FeedbackLPF] = 0.4f;  // about 800 Hz
    DroseraCore core; makeCore(core, p);
    setChord(core, 48, 55, 64);
    core.SetPolyphony(false);
    core.SetCoupling(1.0f);
    const size_t on = 48000 * 2, off = 48000 * 4, n = 48000 * 5;
    std::vector<float> L(n), R(n);
    for (size_t i = 0; i < n; ++i) {
        if (i == on) core.SetPolyphony(true);
        if (i == off) core.SetPolyphony(false);
        if (i == off + 240) core.SetPolyphony(true);   // reverses at 3/4 of the ramp
        if (i == off + 480) core.SetPolyphony(false);
        core.Render(nullptr, &L[i], &R[i], 1);
    }
    bool ok = true;
    bool finite = true;
    for (size_t i = 0; i < n; ++i) finite &= std::isfinite(L[i]) && std::isfinite(R[i]);
    struct Window { const char* name; size_t start, length; };
    for (const Window w : { Window{"on", on, 960}, Window{"off, reversing", off, 480 + 960} }) {
        for (const auto* v : { &L, &R }) {
            const float before = largestJump(*v, w.start - 48000, w.start), during = largestJump(*v, w.start, w.start + w.length);
            const bool pass = finite && during <= 1.5f * before;
            printf("poly ramp %s at Coupling 100%%, %s: largest jump %.5f, before %.5f, finite=%d %s\n", w.name, v == &L ? "L" : "R", during, before, (int)finite, pass ? "OK" : "FAIL");
            ok &= pass;
        }
    }
    return ok;
}

static bool recoverRestoresAllVoices() {
    DroseraCore core; makeCore(core, kDryBase);
    setChord(core, 48, 55, 64);
    const auto r = renderTaps(core, 48000 * 4, [&](size_t i) { if (i == 48000) core.Recover(); });
    const bool pass = tapRms(r, 0, 3.0, 4.0) > 1e-3 && tapRms(r, 1, 3.0, 4.0) > 1e-3 && tapRms(r, 2, 3.0, 4.0) > 1e-3;
    printf("poly Recover restores all voices: rms %.4f %.4f %.4f %s\n", tapRms(r, 0, 3.0, 4.0), tapRms(r, 1, 3.0, 4.0), tapRms(r, 2, 3.0, 4.0), pass ? "OK" : "FAIL");
    return pass;
}

static void cpuReport() {
    for (const bool poly : { false, true }) {
        DroseraCore core; makeCore(core, kDryBase);
        setChord(core, 48, 55, 64);
        core.SetPolyphony(poly);
        float l[64], r[64];
        const std::clock_t start = std::clock();
        for (int b = 0; b < 7500; ++b) core.Render(nullptr, l, r, 64);  // 10 s
        const double cpu = (double)(std::clock() - start) / CLOCKS_PER_SEC;
        printf("CPU, polyphony %s: %.4f s per second of audio\n", poly ? "on" : "off", cpu / 10.0);
    }
}

static bool polyphonyTests() {
    bool ok = true;
    ok &= chordTuning();
    ok &= coupledChordsHoldAcrossRegisters();
    ok &= chordKeysInTune();
    ok &= chordTuningAfterRestart();
    ok &= restartBuildsAsFast();
    ok &= restartKeepsFilters();
    ok &= knobIntervals();
    ok &= intervalsClamp();
    ok &= neverAssignedStaysClosed();
    ok &= perVoiceGate();
    ok &= chordIsAudibleImmediately();
    ok &= stabilityAtMaximum();
    ok &= closedVoiceStaysFaint();
    ok &= closedVoiceStaysFaintFromRestart();
    ok &= lockedKnobsPlayTheirNotes();
    ok &= freeVoicesSpreadOut();
    ok &= lockFallsSilentOutsideTheFilters();
    ok &= lockSwitchIsClickFree();
    ok &= couplingChangesTheSpectrum();
    ok &= coupledChordsHoldAtLongBody();
    ok &= couplingStillActsAtMidBody();
    ok &= coupledRampClickFree();
    ok &= recoverRestoresAllVoices();
    cpuReport();
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
    ok &= presetTests(outDir);
    ok &= noAllocationOnRender();
    ok &= recoversFromNan();
    ok &= filtersClearOnInit();
    ok &= publishedValues();
    ok &= engineVoiceTests();
    ok &= polyphonyTests();
    printf(ok ? "ALL OK\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
