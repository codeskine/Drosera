// Measures the pitch the engine actually produces for MIDI notes.
// For each note: renders and reports the strongest partial of each channel, in cents against the note.
// usage: pitch_probe [body_norm=0.30] [first_note=28] [last_note=76] [step=4]
#include <cstdio>
#include <cstdlib>
#include "DroseraCore.h"
#include "pitch_estimator.h"

using infrasonic::DroseraCore;

int main(int argc, char** argv) {
    const float body = argc > 1 ? (float)std::atof(argv[1]) : 0.30f;
    const int first = argc > 2 ? std::atoi(argv[2]) : 28, last = argc > 3 ? std::atoi(argv[3]) : 76, step = argc > 4 ? std::atoi(argv[4]) : 4;
    const float p[DroseraCore::NumParams] = {0.43f, 0.75f, body, 0.90f, 0.20f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.7f};
    double worst = 0, sumAbs = 0, sumSigned = 0; int n = 0;
    std::printf("body=%.2f\n note  nominal    L Hz   L cents   R Hz   R cents\n", body);
    for (int note = first; note <= last; note += step) {
        DroseraCore core; core.Init();
        for (int i = 0; i < DroseraCore::NumParams; ++i) core.SetParam((DroseraCore::Param)i, p[i]);
        core.SetMidiPitch(true, (float)note);
        const size_t total = 48000 * 5;
        std::vector<float> L(total), R(total);
        for (size_t pos = 0; pos < total; pos += 128) core.Render(nullptr, &L[pos], &R[pos], 128);
        const double nominal = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const double hl = dominantHz(L, 48000 * 2), hr = dominantHz(R, 48000 * 2);
        const double cl = hl > 0 ? 1200.0 * std::log2(hl / nominal) : 9999, cr = hr > 0 ? 1200.0 * std::log2(hr / nominal) : 9999;
        std::printf(" %3d  %7.1f  %7.1f  %+7.1f  %7.1f  %+7.1f\n", note, nominal, hl, cl, hr, cr);
        worst = std::max(worst, std::fabs(cl)); sumAbs += std::fabs(cl); sumSigned += cl; ++n;
    }
    std::printf("left channel: mean error %+.1f cents, mean |error| %.1f cents, worst %.1f cents\n", sumSigned / n, sumAbs / n, worst);
    return 0;
}
