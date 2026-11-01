// Loads the built VST3 bundle through JUCE's plugin host, renders audio at several
// sample rates / block sizes and checks level, finiteness, parameter set and state round trip.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include "pitch_estimator.h"


static void setParam(juce::AudioProcessor& p, const char* name, float norm) {
    for (auto* prm : p.getParameters())
        if (prm->getName(64) == name) { prm->setValue(norm); return; }
    std::printf("missing parameter %s\n", name);
    std::exit(1);
}

struct Stats { double rms = 0; bool finite = true; };

static Stats rmsOf(const juce::AudioBuffer<float>& b, Stats acc, double& sumSq, long& n) {
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) {
            float v = b.getSample(c, i);
            if (!std::isfinite(v)) acc.finite = false;
            sumSq += (double) v * v; ++n;
        }
    return acc;
}

// Audio input and MIDI behaviour through the real VST3 bundle.
static bool featureTests(juce::VST3PluginFormat& vst3, const juce::PluginDescription& d) {
    bool ok = true;
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        const int block = 480;
        // ---- audio input: feedback closed, sine in -> sound; silence in -> silence
        for (int withSignal = 0; withSignal < 2; ++withSignal) {
            juce::String err;
            auto inst = vst3.createInstanceFromDescription(d, sr, block, err);
            inst->enableAllBuses();
            if (inst->getTotalNumInputChannels() != 2) { std::printf("FAIL: input bus not enabled\n"); return false; }
            setParam(*inst, "Feedback", 0.0f); setParam(*inst, "Reverb Mix", 0.0f); setParam(*inst, "Echo Send", 0.0f);
            inst->prepareToPlay(sr, block);
            juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
            double sumSq = 0; long n = 0; bool finite = true; double phase = 0;
            const int blocks = (int) (sr * 4.0 / block);
            for (int b = 0; b < blocks; ++b) {
                for (int i = 0; i < block; ++i) {
                    float s = withSignal ? 0.5f * (float) std::sin(phase) : 0.0f; phase += 2.0 * M_PI * 220.0 / sr;
                    buf.setSample(0, i, s); buf.setSample(1, i, s);
                }
                inst->processBlock(buf, midi);
                if (b * block > sr * 2.0) finite &= rmsOf(buf, {}, sumSq, n).finite;
            }
            double r = std::sqrt(sumSq / (double) n);
            bool pass = finite && (withSignal ? r > 1e-3 : r < 1e-4);
            std::printf("sr=%-7.0f audio input %-7s rms=%.5f %s\n", sr, withSignal ? "sine" : "silence", r, pass ? "OK" : "FAIL");
            ok &= pass;
            inst->releaseResources();
        }
        // ---- MIDI gate: silent until note-on, sound while held, dies after note-off
        {
            juce::String err;
            auto inst = vst3.createInstanceFromDescription(d, sr, block, err);
            setParam(*inst, "MIDI Gate", 1.0f); setParam(*inst, "Reverb Mix", 0.0f); setParam(*inst, "Echo Send", 0.0f);
            inst->prepareToPlay(sr, block);
            juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
            auto run = [&](double seconds, double skip) {
                double sumSq = 0; long n = 0; bool fin = true;
                const int blocks = (int) (sr * seconds / block);
                for (int b = 0; b < blocks; ++b) {
                    buf.clear(); inst->processBlock(buf, midi); midi.clear();
                    if (b * block >= sr * skip) fin &= rmsOf(buf, {}, sumSq, n).finite;
                }
                return std::make_pair(n ? std::sqrt(sumSq / (double) n) : 0.0, fin);
            };
            auto before = run(1.0, 0.0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 100), 0);
            auto held = run(3.0, 1.5);
            midi.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
            auto after = run(4.0, 3.5);
            bool pass = before.first < 1e-4 && held.first > 1e-2 && after.first < 1e-3 && held.second && after.second;
            std::printf("sr=%-7.0f midi gate: before=%.5f held=%.4f after-release=%.5f %s\n", sr, before.first, held.first, after.first, pass ? "OK" : "FAIL");
            ok &= pass;
            inst->releaseResources();
        }
    }
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: plugin_host_test <path to .vst3>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI init;

    juce::AudioPluginFormatManager fm;
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descs;
    vst3.findAllTypesForFile(descs, argv[1]);
    if (descs.isEmpty()) { std::printf("FAIL: no plugin found in bundle\n"); return 1; }
    std::printf("Found: %s (%s) isInstrument=%d\n", descs[0]->name.toRawUTF8(),
                descs[0]->pluginFormatName.toRawUTF8(), (int) descs[0]->isInstrument);

    bool ok = true;
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        for (int block : {64, 512, 1000}) {
            juce::String err;
            auto inst = vst3.createInstanceFromDescription(*descs[0], sr, block, err);
            if (!inst) { std::printf("FAIL: create: %s\n", err.toRawUTF8()); return 1; }
            inst->setPlayConfigDetails(0, 2, sr, block);
            inst->prepareToPlay(sr, block);

            if (sr == 44100.0 && block == 64) {
                std::printf("Parameters: %d\n", inst->getParameters().size());
                for (auto* p : inst->getParameters())
                    std::printf("  %-16s default=%-8s now=%s\n", p->getName(32).toRawUTF8(),
                                p->getText(p->getDefaultValue(), 32).toRawUTF8(), p->getText(p->getValue(), 32).toRawUTF8());
            }

            juce::AudioBuffer<float> buf(2, block);
            juce::MidiBuffer midi;
            double rms = 0, peak = 0; long count = 0; bool finite = true;
            const int blocks = (int) (sr * 6.0 / block);
            for (int b = 0; b < blocks; ++b) {
                buf.clear();
                inst->processBlock(buf, midi);
                if (b * block < sr) continue;  // skip first second
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i) {
                        float v = buf.getSample(c, i);
                        if (!std::isfinite(v)) finite = false;
                        peak = std::max(peak, (double) std::abs(v)); rms += (double) v * v; ++count;
                    }
            }
            rms = std::sqrt(rms / (double) count);
            bool pass = finite && peak <= 1.0 && rms > 1e-3;
            ok &= pass;
            std::printf("sr=%-7.0f block=%-5d peak=%.3f rms=%.4f finite=%d %s\n", sr, block, peak, rms, finite, pass ? "OK" : "FAIL");

            if (sr == 44100.0 && block == 1000) {  // state round trip
                juce::MemoryBlock state; inst->getStateInformation(state);
                auto* p0 = inst->getParameters()[0]; float before = p0->getValue();
                p0->setValue(0.1f);
                inst->setStateInformation(state.getData(), (int) state.getSize());
                bool rt = std::abs(inst->getParameters()[0]->getValue() - before) < 1e-4f;
                std::printf("state round trip (%zu bytes): %s\n", state.getSize(), rt ? "OK" : "FAIL");
                ok &= rt;
            }
            inst->releaseResources();
        }
    }
    ok &= featureTests(vst3, *descs[0]);

    // Tape echo through the bundle: toggling it must change the sound, stay bounded and audible.
    for (double sr : {44100.0, 48000.0}) {
        juce::String err;
        auto inst = vst3.createInstanceFromDescription(*descs[0], sr, 512, err);
        setParam(*inst, "Echo Send", 0.8f); setParam(*inst, "Echo Time", 0.2f); setParam(*inst, "Echo Tape", 1.0f);
        inst->prepareToPlay(sr, 512);
        juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m;
        double sum = 0, pk = 0; long n = 0; bool fin = true;
        for (int b = 0; b < (int) (sr * 6 / 512); ++b) {
            buf.clear(); inst->processBlock(buf, m);
            if (b * 512 < sr * 2) continue;
            for (int i = 0; i < 512; ++i) { float v = buf.getSample(0, i); fin &= std::isfinite(v); sum += (double) v * v; pk = std::max(pk, (double) std::abs(v)); ++n; }
        }
        const double rms = std::sqrt(sum / (double) n);
        const bool pass = fin && pk <= 1.0 && rms > 1e-3;
        std::printf("sr=%-7.0f tape echo on: rms %.4f peak %.3f %s\n", sr, rms, pk, pass ? "OK" : "FAIL");
        ok &= pass;
    }

    // MIDI pitch through the bundle: note in tune at every rate; the knob takes back over.
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        const int block = 480;
        juce::String err;
        auto render = [&](juce::AudioPluginInstance& inst, juce::MidiBuffer& midi, double seconds) {
            std::vector<float> out;
            juce::AudioBuffer<float> buf(2, block);
            for (int b = 0; b < (int) (sr * seconds / block); ++b) {
                buf.clear(); inst.processBlock(buf, midi); midi.clear();
                out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + block);
            }
            return out;
        };
        auto inst = vst3.createInstanceFromDescription(*descs[0], sr, block, err);
        setParam(*inst, "MIDI Pitch", 1.0f); setParam(*inst, "Reverb Mix", 0.0f); setParam(*inst, "Echo Send", 0.0f);
        inst->prepareToPlay(sr, block);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        auto a = render(*inst, midi, 6.0);
        const double hz = dominantHz(a, (size_t) (sr * 2.0), sr);
        const double cents = 1200.0 * std::log2(hz / 261.6256);
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        render(*inst, midi, 0.1);
        setParam(*inst, "Frequency", 0.2f);  // knob must win once the key is up
        auto b = render(*inst, midi, 6.0);
        const double after = dominantHz(b, (size_t) (sr * 2.0), sr);

        auto ref = vst3.createInstanceFromDescription(*descs[0], sr, block, err);
        setParam(*ref, "MIDI Pitch", 1.0f); setParam(*ref, "Reverb Mix", 0.0f); setParam(*ref, "Echo Send", 0.0f); setParam(*ref, "Frequency", 0.2f);
        ref->prepareToPlay(sr, block);
        juce::MidiBuffer none;
        auto c = render(*ref, none, 6.0);
        const double knobOnly = dominantHz(c, (size_t) (sr * 2.0), sr);
        const bool pass = std::abs(cents) < 25.0 && std::abs(1200.0 * std::log2(after / knobOnly)) < 100.0;
        std::printf("sr=%-7.0f midi note 60: %.1f Hz (%+.1f c); knob after release %.1f Hz, knob alone %.1f Hz %s\n", sr, hz, cents, after, knobOnly, pass ? "OK" : "FAIL");
        ok &= pass;
    }

    {   // factory presets exposed as programs
        juce::String err;
        auto inst = vst3.createInstanceFromDescription(*descs[0], 48000.0, 512, err);
        const int n = inst->getNumPrograms();
        std::printf("programs: %d\n", n);
        bool pass = n >= 10;
        const juce::String name3 = inst->getProgramName(3);
        auto* freq = inst->getParameters()[0];
        const float before = freq->getValue();
        inst->setCurrentProgram(2);  // Glass Wind: freq 0.80
        pass &= std::abs(freq->getValue() - 0.80f) < 1e-3f && inst->getCurrentProgram() == 2;
        juce::MemoryBlock st; inst->getStateInformation(st);
        inst->setCurrentProgram(0);
        inst->setStateInformation(st.getData(), (int) st.getSize());
        pass &= inst->getCurrentProgram() == 2;  // program index survives a state round trip
        std::printf("presets: program 3 = \"%s\", freq %.2f -> %.2f, state keeps program: %s\n", name3.toRawUTF8(), before,
                    inst->getParameters()[0]->getValue(), pass ? "OK" : "FAIL");
        ok &= pass;
    }

    std::printf(ok ? "ALL OK\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
