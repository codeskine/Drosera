// REVIEW EXPERIMENT (sandbox only, not part of the project): drives the built VST3 through cases the
// project's plugin_host_test does not cover: unusual sample rates, very small, very large and empty
// blocks, a long run with input at a non-48k rate, bypass, garbage state, and velocity-0 note-ons.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>

static void setParam(juce::AudioProcessor& p, const char* name, float norm) {
    for (auto* prm : p.getParameters())
        if (prm->getName(64) == name) { prm->setValue(norm); return; }
    std::printf("missing parameter %s\n", name); std::exit(1);
}

struct Stats { double peak = 0, sumSq = 0; long n = 0; bool finite = true; };
static void accumulate(Stats& s, const juce::AudioBuffer<float>& b) {
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const float v = b.getSample(c, i);
            if (!std::isfinite(v)) s.finite = false;
            s.peak = std::max(s.peak, (double) std::abs(v)); s.sumSq += (double) v * v; ++s.n;
        }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: host_probe <path to .vst3>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descs;
    vst3.findAllTypesForFile(descs, argv[1]);
    if (descs.isEmpty()) { std::printf("FAIL: no plugin found\n"); return 1; }
    bool ok = true;
    juce::String err;

    // 1) Unusual rates and block sizes, with input, feedback open: finite, bounded, audible, latency as declared.
    for (double sr : {22050.0, 44100.0, 88200.0, 176400.0, 192000.0}) {
        for (int block : {1, 7, 2048, 8192}) {
            auto inst = vst3.createInstanceFromDescription(*descs[0], sr, block, err);
            if (!inst) { std::printf("FAIL create %s\n", err.toRawUTF8()); return 1; }
            inst->enableAllBuses();
            inst->prepareToPlay(sr, block);
            juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
            Stats s; double phase = 0;
            const long total = (long) (sr * 3.0);
            for (long pos = 0; pos < total; pos += block) {
                for (int i = 0; i < block; ++i) { const float v = 0.2f * (float) std::sin(phase); phase += 2.0 * M_PI * 220.0 / sr; buf.setSample(0, i, v); buf.setSample(1, i, v); }
                inst->processBlock(buf, midi);
                if (pos > sr) accumulate(s, buf);
            }
            const double rms = std::sqrt(s.sumSq / (double) s.n);
            const bool pass = s.finite && s.peak <= 1.0 && rms > 1e-3;
            std::printf("sr=%-7.0f block=%-5d peak=%.3f rms=%.4f finite=%d latency=%d %s\n", sr, block, s.peak, rms, s.finite, inst->getLatencySamples(), pass ? "OK" : "FAIL");
            ok &= pass;
            inst->releaseResources();
        }
    }

    // 2) Latency at the extra rates: impulse with feedback closed.
    for (double sr : {22050.0, 88200.0, 192000.0}) {
        auto inst = vst3.createInstanceFromDescription(*descs[0], sr, 512, err);
        inst->enableAllBuses();
        setParam(*inst, "Feedback", 0.0f); setParam(*inst, "Reverb Mix", 0.0f); setParam(*inst, "Echo Send", 0.0f);
        inst->prepareToPlay(sr, 512);
        juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m;
        const long at = (long) sr; long peakAt = -1; double peak = 0;
        for (long b = 0; b < (long) (sr * 2 / 512); ++b) {
            buf.clear();
            if (at >= b * 512 && at < (b + 1) * 512) { buf.setSample(0, (int) (at - b * 512), 0.9f); buf.setSample(1, (int) (at - b * 512), 0.9f); }
            inst->processBlock(buf, m);
            for (int i = 0; i < 512; ++i) if (b * 512 + i >= at && std::abs(buf.getSample(0, i)) > peak) { peak = std::abs(buf.getSample(0, i)); peakAt = b * 512 + i; }
        }
        const long measured = peakAt - at; const int declared = inst->getLatencySamples();
        const bool pass = peak > 1e-3 && std::labs(measured - declared) <= 12;
        std::printf("sr=%-7.0f latency: declared %d, measured %ld %s\n", sr, declared, measured, pass ? "OK" : "FAIL");
        ok &= pass;
    }

    // 3) Long run at 44.1 kHz with input and a varying block size: output must stay continuous (no dropouts) for 60 s.
    {
        const double sr = 44100.0;
        auto inst = vst3.createInstanceFromDescription(*descs[0], sr, 4096, err);
        inst->enableAllBuses();
        setParam(*inst, "Feedback", 0.0f); setParam(*inst, "Reverb Mix", 0.0f); setParam(*inst, "Echo Send", 0.0f);  // output is the resampled input
        inst->prepareToPlay(sr, 4096);
        juce::AudioBuffer<float> buf(2, 4096); juce::MidiBuffer midi;
        double phase = 0; long pos = 0; int quiet = 0, blocks = 0; bool finite = true;
        const int sizes[] = {4096, 64, 1000, 3, 2048, 512};
        while (pos < (long) (sr * 60.0)) {
            const int block = sizes[blocks % 6];
            juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, block);
            for (int i = 0; i < block; ++i) { const float v = 0.2f * (float) std::sin(phase); phase += 2.0 * M_PI * 220.0 / sr; view.setSample(0, i, v); view.setSample(1, i, v); }
            inst->processBlock(view, midi);
            Stats s; accumulate(s, view); finite &= s.finite;
            if (pos > sr && block >= 512 && std::sqrt(s.sumSq / (double) s.n) < 1e-4) ++quiet;
            pos += block; ++blocks;
        }
        // After the long run: has the input-to-output delay drifted from what was declared?
        juce::AudioBuffer<float> b512(2, 512); long peakAt = -1, at = 0; double pk = 0;
        for (long b = 0; b < 400; ++b) { b512.clear(); inst->processBlock(b512, midi); }  // let the ring empty
        for (long b = 0; b < 200; ++b) {
            b512.clear();
            if (b == 10) { at = b * 512; b512.setSample(0, 0, 0.9f); b512.setSample(1, 0, 0.9f); }
            inst->processBlock(b512, midi);
            for (int i = 0; i < 512; ++i) if (b * 512 + i >= at && b >= 10 && std::abs(b512.getSample(0, i)) > pk) { pk = std::abs(b512.getSample(0, i)); peakAt = b * 512 + i; }
        }
        const long measured = peakAt - at; const int declared = inst->getLatencySamples();
        const bool pass = finite && quiet == 0 && std::labs(measured - declared) <= 12;
        std::printf("60 s at 44.1k, mixed block sizes: finite=%d quiet blocks=%d, latency after: declared %d measured %ld %s\n", finite, quiet, declared, measured, pass ? "OK" : "FAIL");
        ok &= pass;
    }

    // 4) Empty block, bypass, garbage state, note-on with velocity 0.
    {
        auto inst = vst3.createInstanceFromDescription(*descs[0], 48000.0, 512, err);
        inst->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> empty(2, 0); juce::MidiBuffer midi;
        inst->processBlock(empty, midi);
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear(); inst->processBlockBypassed(buf, midi);
        Stats s; accumulate(s, buf);
        std::printf("empty block and bypass: finite=%d peak=%.3f %s\n", s.finite, s.peak, s.finite ? "OK" : "FAIL"); ok &= s.finite;

        const char garbage[] = "this is not a state";
        inst->setStateInformation(garbage, (int) sizeof(garbage));
        juce::MemoryBlock state; inst->getStateInformation(state);
        inst->setStateInformation(state.getData(), (int) state.getSize() / 2);  // truncated
        buf.clear(); inst->processBlock(buf, midi);
        Stats t; accumulate(t, buf);
        std::printf("garbage and truncated state: still processing, finite=%d %s\n", t.finite, t.finite ? "OK" : "FAIL"); ok &= t.finite;

        setParam(*inst, "MIDI Gate", 1.0f);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        for (int b = 0; b < 100; ++b) { buf.clear(); inst->processBlock(buf, midi); midi.clear(); }
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 0), 0);  // running-status note-off
        Stats held, after;
        for (int b = 0; b < 400; ++b) { buf.clear(); inst->processBlock(buf, midi); midi.clear(); if (b > 300) accumulate(after, buf); }
        const double rmsAfter = std::sqrt(after.sumSq / (double) after.n);
        std::printf("note-on velocity 0 releases the gate: rms after %.5f %s\n", rmsAfter, rmsAfter < 1e-3 ? "OK" : "FAIL"); ok &= rmsAfter < 1e-3;
    }

    std::printf(ok ? "HOST PROBE ALL OK\n" : "HOST PROBE: SOME FAILED\n");
    return ok ? 0 : 1;
}
