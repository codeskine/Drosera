// JUCE wrapper for the Drosera feedback synth (VST3 / AU / Standalone).
// The DSP lives in src/dsp and runs at a fixed 48 kHz; hosts at other
// sample rates are bridged with windowed-sinc resampling (see renderChunk).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <cstring>
#include "DroseraCore.h"
#include "SincResampler.h"
#include "VoiceAllocator.h"
#include "EditorBridge.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace {

using infrasonic::DroseraCore;
using infrasonic::SincResampler;
using infrasonic::VoiceAllocator;

enum class Show { Percent, ValuePercent, LevelDb, ReverbFeedback, NoteHz, Db, Millis, Hz, Seconds };

struct ParamSpec {
    const char* id;
    const char* name;
    float def;           // normalized default
    float lo, hi;        // engine range, used for display only
    daisysp::Mapping map;
    Show show;
};

// Order must match DroseraCore::Param. Ranges/curves mirror DroseraCore::registerParams.
const ParamSpec kSpecs[DroseraCore::NumParams] = {
    {"freq",     "Frequency",     0.43f, 16.0f,  72.0f,    daisysp::Mapping::LINEAR, Show::NoteHz},
    {"fbgain",   "Feedback",      0.75f, -60.0f, 12.0f,    daisysp::Mapping::LINEAR, Show::Db},
    {"body",     "Body",          0.30f, 0.001f, 0.1f,     daisysp::Mapping::EXP,    Show::Millis},
    {"lpf",      "Lowpass",       0.90f, 100.0f, 18000.0f, daisysp::Mapping::LOG,    Show::Hz},
    {"hpf",      "Highpass",      0.20f, 10.0f,  4000.0f,  daisysp::Mapping::LOG,    Show::Hz},
    {"revmix",   "Reverb Mix",    0.30f, 0.0f,   1.0f,     daisysp::Mapping::LINEAR, Show::Percent},
    {"revdecay", "Reverb Decay",  0.50f, 0.0f,   1.0f,     daisysp::Mapping::LINEAR, Show::ReverbFeedback},
    {"echosend", "Echo Send",     0.20f, 0.0f,   1.0f,     daisysp::Mapping::EXP,    Show::ValuePercent},
    {"echotime", "Echo Time",     0.50f, 0.05f,  5.0f,     daisysp::Mapping::EXP,    Show::Seconds},
    {"echofb",   "Echo Feedback", 0.40f, 0.0f,   1.5f,     daisysp::Mapping::LINEAR, Show::ValuePercent},
    {"volume",   "Volume",        0.70f, 0.0f,   1.0f,     daisysp::Mapping::EXP,    Show::LevelDb},
};

juce::String formatValue(const ParamSpec& s, float norm) {
    const float v = daisysp::fmap(norm, s.lo, s.hi, s.map);
    switch (s.show) {
        // The engine's real values, not the knob's position: these curves are far from linear.
        case Show::ValuePercent:   return juce::String(juce::roundToInt(v * 100.0f)) + " %";
        case Show::LevelDb:        return v <= 0.0f ? juce::String("-inf dB") : juce::String(juce::Decibels::gainToDecibels(v), 1) + " dB";
        case Show::ReverbFeedback: return juce::String(juce::roundToInt((0.2f + 0.8f * infrasonic::ftension(norm, -3.0f)) * 100.0f)) + " %";
        case Show::NoteHz:  return juce::String(daisysp::mtof(v), 1) + " Hz";
        case Show::Db:      return juce::String(v, 1) + " dB";
        case Show::Millis:  return juce::String(v * 1000.0f, 1) + " ms";
        case Show::Hz:      return juce::String((int) v) + " Hz";
        case Show::Seconds: return juce::String(v, 2) + " s";
        default:            return juce::String(juce::roundToInt(norm * 100.0f)) + " %";
    }
}

} // namespace

class DroseraProcessor final : public juce::AudioProcessor, public EditorBridge {
public:
    DroseraProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), false)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "STATE", createLayout()),
          core(std::make_unique<DroseraCore>()) {
        for (int i = 0; i < DroseraCore::NumParams; ++i)
            raw[i] = apvts.getRawParameterValue(kSpecs[i].id);
        halveRaw = apvts.getRawParameterValue("echohalve");
        inGainRaw = apvts.getRawParameterValue("inputgain");
        midiPitchRaw = apvts.getRawParameterValue("midipitch");
        midiGateRaw = apvts.getRawParameterValue("midigate");
        polyRaw = apvts.getRawParameterValue("poly");
        couplingRaw = apvts.getRawParameterValue("coupling");
        interval2Raw = apvts.getRawParameterValue("interval2");
        interval3Raw = apvts.getRawParameterValue("interval3");
    }

    const juce::String getName() const override { return "Drosera"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return (int) infrasonic::kFactoryPresets.size(); }
    int getCurrentProgram() override { return currentProgram; }
    const juce::String getProgramName(int i) override {
        return juce::isPositiveAndBelow(i, getNumPrograms()) ? juce::String(infrasonic::kFactoryPresets[(size_t) i].name) : juce::String();
    }
    void changeProgramName(int, const juce::String&) override {}

    void setCurrentProgram(int i) override {
        if (!juce::isPositiveAndBelow(i, getNumPrograms())) return;
        currentProgram = i;
        apvts.state.setProperty("program", i, nullptr);
        const auto& pr = infrasonic::kFactoryPresets[(size_t) i];
        auto set = [this](const char* id, float v) {
            if (auto* prm = apvts.getParameter(id)) {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost(v);
                prm->endChangeGesture();
            }
        };
        for (int k = 0; k < DroseraCore::NumParams; ++k) set(kSpecs[k].id, pr.knob[k]);
        set("echohalve", pr.echoHalve ? 1.0f : 0.0f);
        set("inputgain", pr.inputLevel);
        set("midipitch", pr.midiPitch ? 1.0f : 0.0f);
        set("midigate", pr.midiGate ? 1.0f : 0.0f);
        set("poly", pr.poly ? 1.0f : 0.0f);
        set("coupling", pr.coupling);
        set("interval2", apvts.getParameter("interval2")->convertTo0to1((float) pr.interval2));
        set("interval3", apvts.getParameter("interval3")->convertTo0to1((float) pr.interval3));
    }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new DroseraEditor(*this, apvts, *this); }

    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        const auto in = l.getMainInputChannelSet();
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && (in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }

    void prepareToPlay(double sampleRate, int) override {
        core->Init();  // allocates; only ever called off the audio thread
        ratio = (double) DroseraCore::kRate / sampleRate;   // engine samples consumed per host output sample
        inRatio = sampleRate / (double) DroseraCore::kRate; // host samples consumed per engine input sample
        useResampler = std::abs(sampleRate - (double) DroseraCore::kRate) > 0.5;

        const size_t maxRatio = (size_t) std::ceil(std::max(1.0, std::max(ratio, 1.0 / ratio)));
        outL.assign(kMaxChunk * 2 * maxRatio + 4096, 0.0f);
        outR.assign(outL.size(), 0.0f);
        inFifo.assign(kMaxChunk + 512, 0.0f);
        in48.assign(kMaxChunk * 2 * maxRatio + 64, 0.0f);
        inMono.assign(kMaxChunk, 0.0f);
        outCount = inCount = 0;
        resampleL.Init(ratio); resampleR.Init(ratio); resampleIn.Init(inRatio);

        hostRate.store(sampleRate);
        tap.requestFlush();

        heldCount = 0; haveNote = false; bendSemis = 0.0f;
        allocator.Reset();
        lastPoly = polyRaw->load() > 0.5f;
        lastFreqKnob = raw[DroseraCore::Frequency]->load();
        updateParams();
        if (useResampler) {  // prime the output FIFO: absorbs jitter between input-driven and output-driven rates
            core->Render(nullptr, outL.data(), outR.data(), kPrime);
            outCount = kPrime;
        }
        // Input to output delay. Without an input there is nothing to align, so nothing to declare.
        const auto latency = (double) SincResampler::kLatencyInputSamples
                           + ((double) kPrime + SincResampler::kLatencyInputSamples) / ratio;
        setLatencySamples(useResampler && getTotalNumInputChannels() > 0 ? juce::roundToInt(latency) : 0);
        prepared = true;
    }

    void releaseResources() override { prepared = false; }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals noDenormals;
        const int total = buffer.getNumSamples();
        if (!prepared || buffer.getNumChannels() < 2) { buffer.clear(); return; }

        updateParams();
        const int numIn = getTotalNumInputChannels();
        int pos = 0;
        auto renderTo = [&](int end) {
            while (pos < end) {
                const int n = std::min(kMaxChunk, end - pos);
                renderChunk(buffer, pos, n, numIn);
                pos += n;
            }
        };
        // Sample-accurate MIDI: render up to each event, then apply it.
        for (const auto meta : midi) {
            renderTo(std::min(meta.samplePosition, total));
            if (meta.numBytes <= 3) handleMidi(meta.getMessage());  // nothing longer matters here, and a long message allocates
        }
        renderTo(total);

        // Last line of defence for ears and speakers: the feedback loop can get loud.
        for (int c = 0; c < 2; ++c) {
            float* d = buffer.getWritePointer(c);
            for (int i = 0; i < total; ++i) d[i] = juce::jlimit(-1.0f, 1.0f, d[i]);
        }
        tap.push(buffer.getReadPointer(0), buffer.getReadPointer(1), (size_t) total);
    }

    void getStateInformation(juce::MemoryBlock& dest) override {
        auto state = apvts.copyState();
        state.setProperty(kTabProperty, uiTab.load(), nullptr);
        if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
    }

    void setStateInformation(const void* data, int size) override {
        if (auto xml = getXmlFromBinary(data, size))
            if (xml->hasTagName(apvts.state.getType())) {
                uiTab.store(xml->getIntAttribute(kTabProperty, 0));  // an atomic: hosts may call this off the message thread
                apvts.replaceState(juce::ValueTree::fromXml(*xml));
                currentProgram = juce::jlimit(0, getNumPrograms() - 1, (int) apvts.state.getProperty("program", 0));
            }
    }

private:
    bool midiPitchActive() const override { return midiPitchActive_.load(); }
    float soundingNote() const override { return core->PublishedNote(); }
    bool polyMidiActive() const override { return polyMidiActive_.load(); }
    float voiceNote(int voice) const override { return voiceNotes_[voice].load(); }
    float bodySeconds() const override { return core->PublishedBodySeconds(); }
    float echoSeconds() const override { return DroseraCore::EchoTimeSeconds(raw[DroseraCore::EchoTime]->load(), halveRaw->load() > 0.5f); }
    double hostSampleRate() const override { return hostRate.load(); }
    drosera::SpectrumTap& spectrumTap() override { return tap; }
    int openTab() const override { return uiTab.load(); }
    void setOpenTab(int index) override { uiTab.store(index); }

    static constexpr int kMaxChunk = 1024;
    static constexpr const char* kTabProperty = "uiTab";
    static constexpr size_t kPrime = 256;
    static constexpr int kMaxHeld = 16;

    void updateParams() {
        // Moving the Frequency knob (or automation, or a preset) takes pitch back from the last
        // note, unless a key is still down: MIDI pitch must not leave the knob dead.
        const float freqKnob = raw[DroseraCore::Frequency]->load();
        if (freqKnob != lastFreqKnob) {
            lastFreqKnob = freqKnob;
            if (heldCount == 0) { haveNote = false; allocator.Reset(); }
        }
        const bool poly = polyRaw->load() > 0.5f;
        if (poly != lastPoly) {
            lastPoly = poly;
            allocator.Assign(held, heldCount);
            if (heldCount == 0 && haveNote) { allocator.NoteOn(lastNote); allocator.NoteOff(lastNote); }  // a latched drone keeps sounding
        }
        for (int i = 0; i < DroseraCore::NumParams; ++i)
            core->SetParam((DroseraCore::Param) i, raw[i]->load());
        core->SetEchoWarp(halveRaw->load() > 0.5f);
        core->SetPolyphony(poly);
        core->SetCoupling(couplingRaw->load());
        core->SetIntervals((int) interval2Raw->load(), (int) interval3Raw->load());
        inGain = inGainRaw->load() * 2.0f;
        applyMidiState();
    }

    void applyMidiState() {
        const bool pitchOn = midiPitchRaw->load() > 0.5f && haveNote;
        const bool gateOn = midiGateRaw->load() > 0.5f;
        const bool polyMidi = lastPoly && pitchOn;
        midiPitchActive_.store(pitchOn);
        polyMidiActive_.store(polyMidi);
        for (int v = 0; v < VoiceAllocator::kVoices; ++v)
            voiceNotes_[v].store(polyMidi && allocator.Note(v) != VoiceAllocator::kNoNote ? (float) allocator.Note(v) : -1.0f);

        if (!polyMidi) {
            core->SetMidiPitch(pitchOn, (float) lastNote + bendSemis);
            core->SetGate(gateOn, heldCount > 0);
            for (int v = 1; v < VoiceAllocator::kVoices; ++v) core->SetVoiceGate((size_t) v, gateOn, heldCount > 0);
            return;
        }
        const int first = allocator.Note(0);
        const float firstNote = (first != VoiceAllocator::kNoNote ? (float) first : (float) lastNote) + bendSemis;
        core->SetMidiPitch(true, firstNote);
        for (int v = 0; v < VoiceAllocator::kVoices; ++v) {
            const bool assigned = allocator.Note(v) != VoiceAllocator::kNoNote;
            if (v > 0) core->SetVoiceNote((size_t) v, assigned ? (float) allocator.Note(v) + bendSemis : firstNote);  // a closed voice's pitch does not matter
            core->SetVoiceGate((size_t) v, true, gateOn ? assigned && allocator.Held(v) : assigned);
        }
    }

    void handleMidi(const juce::MidiMessage& m) {
        if (m.isNoteOn()) {
            removeNote(m.getNoteNumber());
            allocator.NoteOn(m.getNoteNumber());
            if (heldCount < kMaxHeld) held[heldCount++] = m.getNoteNumber();
            lastNote = m.getNoteNumber();
            haveNote = true;
        } else if (m.isNoteOff()) {
            removeNote(m.getNoteNumber());
            allocator.NoteOff(m.getNoteNumber());
            if (heldCount > 0) lastNote = held[heldCount - 1];  // fall back to the previous held note
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            heldCount = 0;
            allocator.AllNotesOff();
        } else if (m.isPitchWheel()) {
            bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        } else {
            return;
        }
        applyMidiState();
    }

    void removeNote(int note) {
        int w = 0;
        for (int i = 0; i < heldCount; ++i) if (held[i] != note) held[w++] = held[i];
        heldCount = w;
    }

    // Renders n host samples starting at `start` into the buffer's output channels.
    void renderChunk(juce::AudioBuffer<float>& buffer, int start, int n, int numIn) {
        float* dstL = buffer.getWritePointer(0) + start;
        float* dstR = buffer.getWritePointer(1) + start;

        const float* in = nullptr;  // mono-summed host input, copied first: in/out share the buffer
        if (numIn > 0) {
            const float* l = buffer.getReadPointer(0) + start;
            const float* r = numIn > 1 ? buffer.getReadPointer(1) + start : l;
            for (int i = 0; i < n; ++i) inMono[(size_t) i] = 0.5f * (l[i] + r[i]) * inGain;
            in = inMono.data();
        }

        if (!useResampler) {
            core->Render(in, dstL, dstR, (size_t) n);
            return;
        }

        // 1) Input side: resample host input to 48 kHz (input-driven) and run the engine on it.
        if (in) {
            std::memcpy(inFifo.data() + inCount, in, (size_t) n * sizeof(float));
            inCount += (size_t) n;
            const int k = (int) std::floor(((double) inCount - 8.0) / inRatio);
            if (k > 0) {
                const size_t used = (size_t) resampleIn.Process(inFifo.data(), in48.data(), k);
                inCount -= used;
                std::memmove(inFifo.data(), inFifo.data() + used, inCount * sizeof(float));
                core->Render(in48.data(), outL.data() + outCount, outR.data() + outCount, (size_t) k);
                outCount += (size_t) k;
            }
        } else {
            inCount = 0;
        }

        // 2) Output side: top up with engine output if needed, then resample to the host rate.
        const size_t need = (size_t) std::ceil(n * ratio) + 8;
        while (outCount < need) {
            core->Render(nullptr, outL.data() + outCount, outR.data() + outCount, 64);
            outCount += 64;
        }
        const size_t used = (size_t) resampleL.Process(outL.data(), dstL, n);
        resampleR.Process(outR.data(), dstR, n);
        outCount -= used;
        std::memmove(outL.data(), outL.data() + used, outCount * sizeof(float));
        std::memmove(outR.data(), outR.data() + used, outCount * sizeof(float));
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (const auto& s : kSpecs) {
            auto attrs = juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction([s](float v, int) { return formatValue(s, v); });
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{s.id, 1}, s.name, juce::NormalisableRange<float>(0.0f, 1.0f), s.def, attrs));
        }
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"echohalve", 1}, "Echo Warp", false));
        // Input gain 0..2x (0.5 = unity); only audible when the host connects the input bus.
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"inputgain", 1}, "Input Level", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                return v <= 0.0f ? juce::String("-inf dB") : juce::String(juce::Decibels::gainToDecibels(v * 2.0f), 1) + " dB";
            })));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"midipitch", 1}, "MIDI Pitch", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"midigate", 1}, "MIDI Gate", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"poly", 1}, "Polyphony", false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"coupling", 1}, "Coupling", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; })));
        // Semitones above voice 0 when MIDI is not in control.
        auto semitones = juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) {
            return juce::String(v > 0 ? "+" : "") + juce::String(v) + " st";
        });
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"interval2", 1}, "Interval 2", -24, 24, 7, semitones));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"interval3", 1}, "Interval 3", -24, 24, 12, semitones));
        return layout;
    }

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<DroseraCore> core;
    std::atomic<float>* raw[DroseraCore::NumParams] {};
    std::atomic<float>* halveRaw = nullptr;
    std::atomic<float>* inGainRaw = nullptr;
    std::atomic<float>* midiPitchRaw = nullptr;
    std::atomic<float>* midiGateRaw = nullptr;
    std::atomic<float>* polyRaw = nullptr;
    std::atomic<float>* couplingRaw = nullptr;
    std::atomic<float>* interval2Raw = nullptr;
    std::atomic<float>* interval3Raw = nullptr;

    // MIDI state (audio thread only)
    int held[kMaxHeld] {};
    int heldCount = 0, lastNote = 40;
    std::atomic<int> currentProgram { 0 };  // read by the editor's timer
    bool haveNote = false, lastPoly = false;
    infrasonic::VoiceAllocator allocator;
    float bendSemis = 0.0f, inGain = 1.0f, lastFreqKnob = 0.0f;
    std::atomic<bool> midiPitchActive_ { false };  // read by the editor
    std::atomic<bool> polyMidiActive_ { false };
    std::atomic<float> voiceNotes_[VoiceAllocator::kVoices] { -1.0f, -1.0f, -1.0f };

    drosera::SpectrumTap tap;
    std::atomic<double> hostRate { 0.0 };
    std::atomic<int> uiTab { 0 };

    // Sample-rate bridging state
    double ratio = 1.0, inRatio = 1.0;
    bool useResampler = false, prepared = false;
    SincResampler resampleL, resampleR, resampleIn;
    std::vector<float> outL, outR, inFifo, in48, inMono;
    size_t outCount = 0, inCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DroseraProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DroseraProcessor(); }
