// REVIEW EXPERIMENT: the same preset through the VST3 at 44.1, 48 and 96 kHz: level, dominant partial, centroid.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include "pitch_estimator.h"

static std::vector<float> renderMono(juce::AudioPluginInstance& inst, double sr, int block, double seconds, juce::MidiBuffer* first) {
    std::vector<float> out; juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer none;
    for (int b = 0; b < (int) (sr * seconds / block); ++b) {
        buf.clear(); inst.processBlock(buf, first != nullptr && b == 0 ? *first : none);
        for (int i = 0; i < block; ++i) out.push_back(0.5f * (buf.getSample(0, i) + buf.getSample(1, i)));
    }
    return out;
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI init; juce::VST3PluginFormat vst3; juce::OwnedArray<juce::PluginDescription> descs;
    vst3.findAllTypesForFile(descs, argv[1]); if (descs.isEmpty()) { std::printf("no plugin\n"); return 1; }
    const struct { int program; const char* name; bool chord; } cases[] = {
        {0, "Default", false}, {1, "Sub Cellar", false}, {2, "Glass Wind", false}, {4, "Echo Drift", false}, {7, "Razor Feedback", false},
        {12, "Open Fifths", false}, {13, "Sympathetic", false}, {15, "Ember Pong", false}, {14, "Chord Keys (C E G)", true}};
    std::printf("| Preset | 44.1 kHz: RMS / dominante / centroide | 48 kHz | 96 kHz |\n|---|---|---|---|\n");
    for (const auto& k : cases) {
        std::printf("| %s |", k.name);
        for (double sr : {44100.0, 48000.0, 96000.0}) {
            juce::String err; auto inst = vst3.createInstanceFromDescription(*descs[0], sr, 512, err);
            inst->setCurrentProgram(k.program);
            inst->prepareToPlay(sr, 512);
            juce::MidiBuffer chord;
            if (k.chord) { chord.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 100), 0); chord.addEvent(juce::MidiMessage::noteOn(1, 52, (juce::uint8) 100), 0); chord.addEvent(juce::MidiMessage::noteOn(1, 55, (juce::uint8) 100), 0); }
            auto x = renderMono(*inst, sr, 512, 15.0, k.chord ? &chord : nullptr);
            const size_t a = (size_t) (5.0 * sr); double s = 0; for (size_t i = a; i < x.size(); ++i) s += (double) x[i] * x[i];
            const double rms = 20 * std::log10(std::sqrt(s / (double) (x.size() - a)) + 1e-12);
            // centroid and dominant over 2^17 samples from 8 s
            const size_t N = 1 << 17, start = (size_t) (8.0 * sr); std::vector<std::complex<double>> f(N);
            for (size_t i = 0; i < N; ++i) f[i] = (double) x[start + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) N));
            fft(f); const double bin = sr / (double) N; double num = 0, den = 0, mx = 0, dom = 0;
            for (size_t q = 1; q < N / 2; ++q) { const double m = std::abs(f[q]); num += m * q * bin; den += m; if (q * bin > 15 && q * bin < 20000 && m > mx) { mx = m; dom = q * bin; } }
            std::printf(" %.1f dBFS / %.0f Hz / %.0f Hz |", rms, dom, den > 0 ? num / den : 0);
            inst->releaseResources();
        }
        std::printf("\n");
    }
    return 0;
}
