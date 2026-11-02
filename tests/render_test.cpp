// Offline sanity test for the host-independent core: renders scenarios at several
// block sizes, checks for NaN/inf, bounds and non-silence, writes WAVs for listening.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

static bool presetTests(const char* outDir) {
    bool ok = true;
    for (const auto& pr : infrasonic::kFactoryPresets) {
        DroseraCore core; core.Init();
        for (int i = 0; i < DroseraCore::NumParams; ++i) core.SetParam((DroseraCore::Param)i, pr.knob[i]);
        core.SetEchoWarp(pr.echoHalve);
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

static void applyPreset(DroseraCore& core, const infrasonic::Preset& pr) {
    for (int k = 0; k < DroseraCore::NumParams; ++k) core.SetParam((DroseraCore::Param)k, pr.knob[k]);
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

// The Engine's three voices, driven directly: DroseraCore does not drive voices 1 and 2.
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
    printf(ok ? "ALL OK\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
