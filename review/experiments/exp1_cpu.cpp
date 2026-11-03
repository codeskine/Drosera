// CPU cost of DroseraCore per mode: renders N seconds at 48 kHz in 512-sample blocks and reports
// wall time as a percentage of real time (one core). Also reports the limiter's overshoot above 1.0.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
#include "DroseraCore.h"
#include "Presets.h"

using infrasonic::DroseraCore;

static void apply(DroseraCore& c, const infrasonic::Preset& p) {
    for (int i = 0; i < DroseraCore::NumParams; ++i) c.SetParam((DroseraCore::Param) i, p.knob[i]);
    c.SetEchoWarp(p.echoHalve); c.SetPolyphony(p.poly); c.SetCoupling(p.coupling);
    c.SetIntervals(p.interval2, p.interval3); c.SetStringLock(p.stringLock);
    c.SetEchoPong(p.echoPong); c.SetEchoDrive(p.echoDrive); c.SetEchoTone(p.echoTone);
    c.SetEchoWidth(p.echoWidth); c.SetEchoLevel(p.echoLevel);
}

static void bench(const char* name, DroseraCore& core, double seconds) {
    const size_t block = 512, total = (size_t) (seconds * 48000);
    std::vector<float> L(block), R(block);
    size_t over = 0; double peak = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < total; pos += block) {
        core.Render(nullptr, L.data(), R.data(), block);
        for (size_t i = 0; i < block; ++i) { const double a = std::fabs(L[i]); if (a > 1.0) ++over; if (a > peak) peak = a; }
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%-28s %6.2f%% of one core   peak %.3f   samples > 1.0: %zu   recoveries %d\n",
                name, 100.0 * wall / seconds, peak, over, core.RecoveryCount());
}

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? std::atof(argv[1]) : 20.0;
    using infrasonic::kFactoryPresets;
    { DroseraCore c; c.Init(); apply(c, kFactoryPresets[0]); bench("Default (mono knob)", c, seconds); }
    { DroseraCore c; c.Init(); apply(c, kFactoryPresets[9]); c.SetMidiPitch(true, 48.0f); c.SetGate(true, true); bench("Keys (mono MIDI tracking)", c, seconds); }
    { DroseraCore c; c.Init(); apply(c, kFactoryPresets[12]); bench("Open Fifths (poly, lock)", c, seconds); }
    { DroseraCore c; c.Init(); apply(c, kFactoryPresets[13]); bench("Sympathetic (poly, free)", c, seconds); }
    { DroseraCore c; c.Init(); apply(c, kFactoryPresets[14]); c.SetMidiPitch(true, 48.0f); c.SetVoiceNote(1, 52.0f); c.SetVoiceNote(2, 55.0f);
      for (size_t v = 0; v < 3; ++v) c.SetVoiceGate(v, true, true); bench("Chord Keys (poly MIDI)", c, seconds); }
    { DroseraCore c; c.Init(); apply(c, kFactoryPresets[15]); bench("Ember Pong (pong on)", c, seconds); }
    { DroseraCore c; c.Init(); apply(c, kFactoryPresets[7]); bench("Razor Feedback (gain 0.95)", c, seconds); }
    return 0;
}
