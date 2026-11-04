// Sound-design measurements on the engine (host independent, 48 kHz). Prints Markdown tables.
//  A) every preset: RMS, peak, crest, time to reach -3 dB of its steady level, loudness spread over 2 s windows
//  B) pitch map under MIDI: cents error of the dominant partial per note and Body, silent notes, and with reverb
//  C) repeatability: the same preset reached from Init at once or after gliding from the defaults
//  D) mono compatibility: mono sum against the stereo average, and L/R correlation
//  E) long runs: 10 minutes at the extremes, level and centroid per minute
//  F) extreme combinations
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>
#include "DroseraCore.h"
#include "Presets.h"
#include "pitch_estimator.h"

using infrasonic::DroseraCore;
using infrasonic::Preset;
static constexpr double kSr = 48000.0;

struct Stereo { std::vector<float> L, R; };

static void apply(DroseraCore& c, const Preset& p) {
    for (int i = 0; i < DroseraCore::NumParams; ++i) c.SetParam((DroseraCore::Param) i, p.knob[i]);
    c.SetEchoWarp(p.echoHalve); c.SetPolyphony(p.poly); c.SetCoupling(p.coupling);
    c.SetIntervals(p.interval2, p.interval3); c.SetStringLock(p.stringLock);
    c.SetEchoPong(p.echoPong); c.SetEchoDrive(p.echoDrive); c.SetEchoTone(p.echoTone);
    c.SetEchoWidth(p.echoWidth); c.SetEchoLevel(p.echoLevel);
}
// MIDI presets need a note; the resonator needs an input.
static void play(DroseraCore& c, const Preset& p) {
    if (!p.midiPitch && !p.midiGate) return;
    c.SetMidiPitch(true, 48.0f);
    if (p.poly) { c.SetVoiceNote(1, 52.0f); c.SetVoiceNote(2, 55.0f); for (size_t v = 0; v < 3; ++v) c.SetVoiceGate(v, true, true); }
    else c.SetGate(p.midiGate, true);
}
static Stereo render(DroseraCore& c, double seconds, const float* in = nullptr) {
    const size_t n = (size_t) (seconds * kSr);
    Stereo s; s.L.assign(n, 0.0f); s.R.assign(n, 0.0f);
    for (size_t p = 0; p < n; p += 256) c.Render(in ? in + p : nullptr, &s.L[p], &s.R[p], std::min<size_t>(256, n - p));
    return s;
}
static double rmsDb(const std::vector<float>& v, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return 20.0 * std::log10(std::sqrt(s / (double) (b - a)) + 1e-12);
}
static double rmsDb(const Stereo& s, double a, double b) {
    return 10.0 * std::log10(0.5 * (std::pow(10.0, rmsDb(s.L, (size_t) (a * kSr), (size_t) (b * kSr)) / 10.0) + std::pow(10.0, rmsDb(s.R, (size_t) (a * kSr), (size_t) (b * kSr)) / 10.0)));
}
static double peakOf(const Stereo& s, double a, double b) {
    double p = 0; for (size_t i = (size_t) (a * kSr); i < (size_t) (b * kSr); ++i) p = std::max({p, (double) std::fabs(s.L[i]), (double) std::fabs(s.R[i])});
    return p;
}
static std::vector<float> mono(const Stereo& s) { std::vector<float> m(s.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (s.L[i] + s.R[i]); return m; }
static std::vector<float> side(const Stereo& s) { std::vector<float> m(s.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (s.L[i] - s.R[i]); return m; }

// Spectrum of 2^17 samples from `start`: dominant partial and centroid
struct Spec { double dominantHz, centroidHz; std::vector<std::pair<double, double>> top; };
static Spec spectrum(const std::vector<float>& x, size_t start) {
    const size_t N = 1 << 17;
    std::vector<std::complex<double>> a(N);
    for (size_t i = 0; i < N; ++i) a[i] = (double) x[start + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) N));
    fft(a);
    const double bin = kSr / (double) N;
    double num = 0, den = 0; std::vector<std::pair<double, double>> peaks;
    for (size_t k = 1; k < N / 2; ++k) {
        const double m = std::abs(a[k]); num += m * k * bin; den += m;
        if (k > 2 && m > std::abs(a[k - 1]) && m >= std::abs(a[k + 1]) && k * bin > 15.0) peaks.push_back({k * bin, m});
    }
    std::sort(peaks.begin(), peaks.end(), [](auto& p, auto& q) { return p.second > q.second; });
    Spec s; s.centroidHz = den > 0 ? num / den : 0; s.dominantHz = peaks.empty() ? 0 : peaks[0].first;
    for (size_t i = 0; i < std::min<size_t>(3, peaks.size()); ++i) s.top.push_back({peaks[i].first, 20 * std::log10(peaks[i].second / (peaks[0].second + 1e-30))});
    return s;
}

static void loudnessTable() {
    std::printf("\n## A. Presets: level, dynamics and onset time (20 s from Init)\n\n");
    std::printf("| Preset | RMS 5-20 s | Peak | Crest | -3 dB after | Spread 2 s | Dominant | Centroid |\n|---|---|---|---|---|---|---|---|\n");
    for (const auto& p : infrasonic::kFactoryPresets) {
        if (p.needsInput) { std::printf("| %s | (needs input: see F) | | | | | | |\n", p.name); continue; }
        DroseraCore c; c.Init(); apply(c, p); play(c, p);
        const Stereo s = render(c, 20.0);
        const double steady = rmsDb(s, 5.0, 20.0), peak = peakOf(s, 5.0, 20.0);
        double born = -1; for (double t = 0.0; t < 20.0; t += 0.05) if (rmsDb(s, t, t + 0.1) >= steady - 3.0) { born = t; break; }
        double lo = 1e9, hi = -1e9; for (double t = 5.0; t + 2.0 <= 20.0; t += 1.0) { const double r = rmsDb(s, t, t + 2.0); lo = std::min(lo, r); hi = std::max(hi, r); }
        const Spec sp = spectrum(mono(s), (size_t) (10.0 * kSr));
        std::printf("| %s | %.1f dBFS | %.2f | %.1f dB | %.2f s | %.1f dB | %.0f Hz | %.0f Hz |\n", p.name, steady, peak, 20 * std::log10(peak + 1e-12) - steady, born, hi - lo, sp.dominantHz, sp.centroidHz);
    }
}

static void pitchMap() {
    std::printf("\n## B. Note range under MIDI Pitch (error in cents of the dominant partial; `mute` = below -40 dBFS; `*` = over 25 cents)\n");
    const float base[] = {0.43f, 0.75f, 0.30f, 0.80f, 0.20f, 0.00f, 0.50f, 0.00f, 0.50f, 0.00f, 0.70f};  // Keys preset, dry
    const float bodies[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
    const int notes[] = {16, 18, 20, 24, 28, 31, 36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 80, 84};
    for (float revmix : {0.0f, 0.35f}) {
        std::printf("\n### Reverb Mix %.2f\n\n| Body \\ note |", revmix);
        for (int n : notes) std::printf(" %d |", n);
        std::printf("\n|---|"); for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); ++i) std::printf("---|"); std::printf("\n");
        for (float body : bodies) {
            std::printf("| %.1f |", body);
            for (int n : notes) {
                DroseraCore c; c.Init();
                for (int i = 0; i < DroseraCore::NumParams; ++i) c.SetParam((DroseraCore::Param) i, base[i]);
                c.SetParam(DroseraCore::FeedbackBody, body); c.SetParam(DroseraCore::ReverbMix, revmix);
                c.SetMidiPitch(true, (float) n); c.SetGate(true, true);
                const Stereo s = render(c, 5.0);
                const double level = rmsDb(s, 2.0, 5.0);
                if (level < -40.0) { std::printf(" mute |"); continue; }
                const double hz = dominantHz(mono(s), (size_t) (2.0 * kSr), kSr);
                const double cents = 1200.0 * std::log2(hz / (440.0 * std::pow(2.0, (n - 69) / 12.0)));
                std::printf(" %+.0f%s |", cents, std::fabs(cents) > 25 ? "*" : "");
            }
            std::printf("\n");
        }
    }
}

static void repeatability() {
    std::printf("\n## C. Repeatability: the same preset reached straight from Init, or after 1 / 10 / 100 blocks at the default values (like a host that loads the state after prepareToPlay)\n\n");
    std::printf("| Preset | Dominant at once | after 1 block | after 10 | after 100 | RMS at once / 1 / 10 / 100 |\n|---|---|---|---|---|---|\n");
    for (const auto& p : infrasonic::kFactoryPresets) {
        if (p.needsInput) continue;
        std::printf("| %s |", p.name);
        std::string rms;
        for (int delayBlocks : {0, 1, 10, 100}) {
            DroseraCore c; c.Init(); apply(c, infrasonic::kFactoryPresets[0]);  // the defaults first
            float l[256], r[256];
            for (int b = 0; b < delayBlocks; ++b) c.Render(nullptr, l, r, 256);
            apply(c, p); play(c, p);
            const Stereo s = render(c, 15.0);
            const Spec sp = spectrum(mono(s), (size_t) (10.0 * kSr));
            std::printf(" %.0f Hz |", sp.dominantHz);
            rms += (rms.empty() ? "" : " / ") + std::to_string((int) std::lround(rmsDb(s, 5.0, 15.0)));
        }
        std::printf(" %s |\n", rms.c_str());
    }
}

static void monoCompat() {
    std::printf("\n## D. Mono compatibility (5-20 s): mono sum against the channel average, side energy, L/R correlation\n\n");
    std::printf("| Preset | Mono - stereo | Side - Mid | Corr. L/R |\n|---|---|---|---|\n");
    for (const auto& p : infrasonic::kFactoryPresets) {
        if (p.needsInput) continue;
        DroseraCore c; c.Init(); apply(c, p); play(c, p);
        const Stereo s = render(c, 20.0);
        const auto m = mono(s), sd = side(s);
        const size_t a = (size_t) (5.0 * kSr), b = s.L.size();
        const double mid = rmsDb(m, a, b), sideDb = rmsDb(sd, a, b), stereo = rmsDb(s, 5.0, 20.0);
        double num = 0, dl = 0, dr = 0; for (size_t i = a; i < b; ++i) { num += (double) s.L[i] * s.R[i]; dl += (double) s.L[i] * s.L[i]; dr += (double) s.R[i] * s.R[i]; }
        std::printf("| %s | %+.1f dB | %+.1f dB | %+.2f |\n", p.name, mid - stereo, sideDb - mid, num / std::sqrt(dl * dr + 1e-30));
    }
}

static void longRuns() {
    std::printf("\n## E. Ten minutes at extreme values: level and centroid per minute\n\n");
    struct Case { const char* name; float knobs[11]; bool poly; float coupling; bool pong; float drive; };
    const Case cases[] = {
        {"Reverb Decay 1.0, Feedback max", {0.43f, 1.0f, 0.3f, 0.9f, 0.2f, 0.5f, 1.0f, 0.2f, 0.5f, 0.4f, 0.7f}, false, 0, false, 0},
        {"Echo feedback 150 %, PONG drive max", {0.43f, 0.75f, 0.3f, 0.9f, 0.2f, 0.3f, 0.5f, 1.0f, 0.5f, 1.0f, 0.7f}, false, 0, true, 1},
        {"Poly, Coupling max, Feedback max", {0.43f, 1.0f, 0.3f, 0.9f, 0.2f, 0.3f, 0.5f, 0.2f, 0.5f, 0.4f, 0.7f}, true, 1, false, 0},
    };
    std::printf("| Case | RMS per minute (dBFS) | Centroid per minute (Hz) | Peak | DC | Recoveries |\n|---|---|---|---|---|---|\n");
    for (const auto& k : cases) {
        DroseraCore c; c.Init();
        for (int i = 0; i < 11; ++i) c.SetParam((DroseraCore::Param) i, k.knobs[i]);
        c.SetPolyphony(k.poly); c.SetCoupling(k.coupling); c.SetEchoPong(k.pong); c.SetEchoDrive(k.drive);
        std::string rms, cen; double peak = 0, dc = 0; size_t count = 0;
        for (int minute = 0; minute < 10; ++minute) {
            const Stereo s = render(c, 60.0);
            rms += (minute ? " " : "") + std::to_string((int) std::lround(rmsDb(s, 0.0, 60.0)));
            cen += (minute ? " " : "") + std::to_string((int) std::lround(spectrum(mono(s), (size_t) (30.0 * kSr)).centroidHz));
            peak = std::max(peak, peakOf(s, 0.0, 60.0));
            for (float v : s.L) { dc += v; ++count; }
        }
        std::printf("| %s | %s | %s | %.2f | %+.5f | %d |\n", k.name, rms.c_str(), cen.c_str(), peak, dc / (double) count, c.RecoveryCount());
    }
}

static void extremes() {
    std::printf("\n## F. Extreme combinations and input (20 s)\n\n| Case | RMS 5-20 s | Peak | Dominant | Centroid | Notes |\n|---|---|---|---|---|---|\n");
    auto row = [](const char* name, DroseraCore& c, const float* in, const char* note) {
        const Stereo s = render(c, 20.0, in);
        const Spec sp = spectrum(mono(s), (size_t) (10.0 * kSr));
        std::printf("| %s | %.1f dBFS | %.2f | %.0f Hz | %.0f Hz | %s |\n", name, rmsDb(s, 5.0, 20.0), peakOf(s, 5.0, 20.0), sp.dominantHz, sp.centroidHz, note);
    };
    { DroseraCore c; c.Init(); apply(c, infrasonic::kFactoryPresets[0]); c.SetParam(DroseraCore::FeedbackLPF, 0.0f); c.SetParam(DroseraCore::FeedbackHPF, 1.0f);
      row("Highpass 4 kHz above Lowpass 100 Hz", c, nullptr, "the filters cancel out"); }
    { DroseraCore c; c.Init(); apply(c, infrasonic::kFactoryPresets[13]); c.SetStringLock(false); c.SetCoupling(1.0f);
      row("Sympathetic, LOCK off, Coupling 100 %", c, nullptr, "free voices, coupled"); }
    { DroseraCore c; c.Init(); apply(c, infrasonic::kFactoryPresets[15]); c.SetParam(DroseraCore::EchoFeedback, 1.0f); c.SetEchoDrive(1.0f); c.SetEchoTone(1.0f);
      row("Ember Pong, Echo Feedback 150 %, Drive max, Tone 3.2 kHz", c, nullptr, "saturated runaway"); }
    { DroseraCore c; c.Init(); apply(c, infrasonic::kFactoryPresets[0]); c.SetParam(DroseraCore::FeedbackGain, 1.0f); c.SetParam(DroseraCore::FeedbackBody, 0.0f);
      row("Default, Feedback +12 dB, Body minimum", c, nullptr, "shortest loop at full gain"); }
    {   // the resonator preset with a 220 Hz sine, then with pink-ish noise bursts
        const size_t n = (size_t) (20.0 * kSr); std::vector<float> sine(n), bursts(n);
        for (size_t i = 0; i < n; ++i) { sine[i] = 0.3f * (float) std::sin(2.0 * M_PI * 220.0 * i / kSr); bursts[i] = ((i / 24000) % 2 == 0) ? 0.3f * (float) std::sin(2.0 * M_PI * 110.0 * i / kSr) : 0.0f; }
        DroseraCore c; c.Init(); apply(c, infrasonic::kFactoryPresets[10]); c.SetMidiPitch(true, 57.0f);
        row("Resonator, 220 Hz sine at the input, MIDI note 57", c, sine.data(), "continuous input");
        DroseraCore d; d.Init(); apply(d, infrasonic::kFactoryPresets[10]); d.SetMidiPitch(true, 45.0f);
        row("Resonator, 110 Hz in 0.5 s bursts, note 45", d, bursts.data(), "intermittent input");
    }
}

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    if (which == "all" || which == "A") loudnessTable();
    if (which == "all" || which == "B") pitchMap();
    if (which == "all" || which == "C") repeatability();
    if (which == "all" || which == "D") monoCompat();
    if (which == "all" || which == "F") extremes();
    if (which == "all" || which == "E") longRuns();
    return 0;
}
