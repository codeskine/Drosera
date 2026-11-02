// Tests for the interface: the spectrum path to the display, the readouts and the editor's behaviour.
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>
#include "EditorBridge.h"
#include "PluginEditor.h"
#include "pitch_estimator.h"
#include "SpectrumTap.h"
#include "ui/PlantView.h"
#include "ui/Readouts.h"
#include "ui/SpectrumBands.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

using namespace drosera;

// Counts every allocation on this thread while on, malloc included: JUCE's MidiMessage uses malloc, which replacing
// operator new would miss. malloc_logger is the system's hook behind malloc stack logging.
extern "C" void (*malloc_logger)(uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);
static thread_local bool tCountAllocations = false;
static std::atomic<int> gAllocations { 0 };
static void countAllocation(uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t) {
    constexpr uint32_t kAllocate = 2;
    if (tCountAllocations && (type & kAllocate) != 0) ++gAllocations;
}

static int failures = 0;
static void check(bool pass, const char* name) {
    std::printf("%-40s %s\n", name, pass ? "OK" : "FAIL");
    if (!pass) ++failures;
}

// Hann-windowed magnitudes of a sine, as the display computes them.
static std::vector<float> sineMagnitudes(double hz, double amplitude, double sampleRate, int fftSize = 2048) {
    std::vector<std::complex<double>> a((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / fftSize);
        a[(size_t) i] = w * amplitude * std::sin(2.0 * M_PI * hz * i / sampleRate);
    }
    fft(a);
    std::vector<float> m((size_t) fftSize / 2 + 1);
    for (size_t k = 0; k < m.size(); ++k) m[k] = (float) std::abs(a[k]);
    return m;
}

static std::vector<float> bandsOf(const std::vector<float>& magnitudes, double sampleRate, int fftSize = 2048) {
    std::vector<float> out(kNumBands);
    computeBands(magnitudes.data(), fftSize, sampleRate, out.data());
    return out;
}

static int peakBand(const std::vector<float>& bands) {
    return (int) (std::max_element(bands.begin(), bands.end()) - bands.begin());
}

static bool othersBelow(const std::vector<float>& bands, int peak, float dB) {
    for (int i = 0; i < kNumBands; ++i)
        if (std::abs(i - peak) > 1 && bands[(size_t) i] > bands[(size_t) peak] - dB) return false;
    return true;
}

static void tapTests() {
    {
        SpectrumTap tap;
        tap.setActive(true);
        std::thread producer([&tap] {
            float block[64], silence[64] = {};
            for (int value = 0; value < 200000;) {
                for (float& s : block) s = 2.0f * (float) ++value;  // doubled: the tap's mono sum halves L + R
                tap.push(block, silence, 64);
                if (value % 6400 == 0) std::this_thread::yield();
            }
        });
        bool ordered = true;
        float last = 0.0f, got[512];
        int total = 0;
        auto drain = [&] {
            const size_t n = tap.pop(got, 512);
            for (size_t i = 0; i < n; ++i) { ordered &= got[i] > last; last = got[i]; }
            total += (int) n;
            return n;
        };
        for (int spins = 0; spins < 200000 && total < 150000; ++spins)
            if (drain() == 0) std::this_thread::yield();
        producer.join();
        while (drain() > 0) {}
        check(ordered && total > 0, "tapKeepsOrder");
    }
    {
        SpectrumTap tap;
        tap.setActive(true);
        std::vector<float> in(5000), silence(5000, 0.0f), out(6000);
        for (size_t i = 0; i < in.size(); ++i) in[i] = 2.0f * (float) i;
        tap.push(in.data(), silence.data(), in.size());
        const size_t n = tap.pop(out.data(), out.size());
        check(n == 4096 && out[0] == 0.0f && out[4095] == 4095.0f, "tapDropsWhenFull");
    }
    {
        SpectrumTap tap;
        float one[4] = {1, 1, 1, 1}, out[4];
        tap.push(one, one, 4);
        check(tap.pop(out, 4) == 0, "tapInactiveIgnores");
    }
    {
        SpectrumTap tap;
        tap.setActive(true);
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        float l[3] = {nan, inf, 1.0f}, r[3] = {0.0f, 0.0f, nan}, out[3];
        tap.push(l, r, 3);
        check(tap.pop(out, 3) == 3 && out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "tapSanitises");
    }
    {
        SpectrumTap tap;
        tap.setActive(true);
        std::vector<float> zeros(100, 0.0f), out(100);
        tap.push(zeros.data(), zeros.data(), 100);
        tap.requestFlush();
        check(tap.pop(out.data(), 100) == 0, "tapFlush");
    }
    {
        SpectrumTap tap;
        tap.setActive(true);
        float l = 1.0f, r = 0.0f, out = 0.0f;
        tap.push(&l, &r, 1);
        check(tap.pop(&out, 1) == 1 && out == 0.5f, "tapMonoSum");
    }
}

static void bandTests() {
    check(std::abs(bandCentreHz(0) - 30.0f) < 0.01f && std::abs(bandCentreHz(53) - 16000.0f) < 1.0f, "bandEnds");

    const auto at1k = bandsOf(sineMagnitudes(1000.0, 1.0, 48000.0), 48000.0);
    const auto at5k = bandsOf(sineMagnitudes(5000.0, 1.0, 48000.0), 48000.0);
    check(peakBand(at1k) == 30 && othersBelow(at1k, 30, 30.0f) && peakBand(at5k) == 43 && othersBelow(at5k, 43, 30.0f),
          "sineLightsItsBand 48 kHz");
    // At 96 kHz bins are 47 Hz wide, and 1 kHz sits near a band edge: one band off is the resolution there.
    const auto at1k96 = bandsOf(sineMagnitudes(1000.0, 1.0, 96000.0), 96000.0);
    const auto at5k96 = bandsOf(sineMagnitudes(5000.0, 1.0, 96000.0), 96000.0);
    check(std::abs(peakBand(at1k96) - 30) <= 1 && othersBelow(at1k96, peakBand(at1k96), 20.0f)
              && peakBand(at5k96) == 43 && othersBelow(at5k96, 43, 20.0f), "sineLightsItsBand 96 kHz");

    // Exactly on a bin: between bins a Hann window reads up to 1.4 dB low.
    const double onBin = 44.0 * 48000.0 / 2048.0;
    const auto loud = bandsOf(sineMagnitudes(onBin, 1.0, 48000.0), 48000.0);
    const auto quiet = bandsOf(sineMagnitudes(onBin, std::pow(10.0, -24.0 / 20.0), 48000.0), 48000.0);
    check(std::abs(loud[30]) < 0.5f && std::abs(quiet[30] + 24.0f) < 1.0f, "calibration");

    std::vector<float> zeros(1025, 0.0f);
    const auto silent = bandsOf(zeros, 48000.0);
    check(std::all_of(silent.begin(), silent.end(), [](float d) { return d == kFloorDb; }), "silenceRests");

    const auto low = bandsOf(sineMagnitudes(1000.0, 1.0, 22050.0), 22050.0);
    bool aboveAtRest = true;
    for (int i = 0; i < kNumBands; ++i) if (bandCentreHz(i) > 11025.0f) aboveAtRest &= low[(size_t) i] == kFloorDb;
    check(aboveAtRest && low[30] > -3.0f, "aboveNyquistRests");

    const auto noRate = bandsOf(sineMagnitudes(1000.0, 1.0, 48000.0), 0.0);
    check(std::all_of(noRate.begin(), noRate.end(), [](float d) { return d == kFloorDb; }), "zeroRateRests");

    const auto at60 = bandsOf(sineMagnitudes(60.0, 1.0, 48000.0), 48000.0);
    int nearest = 0;
    for (int i = 0; i < kNumBands; ++i)
        if (std::abs(std::log(bandCentreHz(i) / 60.0f)) < std::abs(std::log(bandCentreHz(nearest) / 60.0f))) nearest = i;
    check(at60[(size_t) nearest] > -6.0f, "lowBandsHaveNoHoles");

    const float up = smoothLevel(-72.0f, 0.0f, 0.040f), down = smoothLevel(0.0f, -72.0f, 0.300f);
    const float twoSteps = smoothLevel(smoothLevel(-72.0f, 0.0f, 0.020f), 0.0f, 0.020f);
    check(std::abs((up + 72.0f) / 72.0f - 0.632f) < 0.01f && std::abs(-down / 72.0f - 0.632f) < 0.01f
              && std::abs(twoSteps - up) < 0.1f, "ballistics");

    check(bandAmount(-72.0f) == 0.0f && bandAmount(0.0f) == 1.0f && bandAmount(-100.0f) == 0.0f && bandAmount(6.0f) == 1.0f,
          "bandAmountRange");
}

static void readoutTests() {
    using namespace readouts;
    auto is = [](const Text& t, const char* value, const char* tag) { return t.value == value && t.tag == tag; };
    check(is(frequency(true, 69.0f, "x"), "A4 440.0 Hz", "MIDI"), "frequency with a MIDI note");
    check(frequency(true, 60.4f, "x").value.rfind("C4 ", 0) == 0 && frequency(true, 60.4f, "x").value != frequency(true, 60.0f, "x").value,
          "frequency between notes");
    check(is(frequency(false, 69.0f, "82.4 Hz"), "82.4 Hz", ""), "frequency from the knob");
    check(is(body(true, 0.0971f, "x"), "97.1 ms", "SNAP"), "body snapped");
    check(is(body(false, 0.0971f, "30.0 ms"), "30.0 ms", ""), "body from the knob");
    check(echoTime(true, 0.359f, "x") == "0.36 s", "echo time with WARP");
    check(echoTime(false, 0.359f, "1.29 s") == "1.29 s", "echo time from the knob");
    check(is(echoShaping(true, "+6.0 dB"), "+6.0 dB", ""), "echo shaping with PONG on");
    check(is(echoShaping(false, "+6.0 dB"), "+6.0 dB", "PONG OFF"), "echo shaping with PONG off");
    check(is(interval(false, 60.0f, "+7 st"), "+7 st", ""), "interval from the knob");
    check(is(interval(true, 60.0f, "+7 st"), "C4", "MIDI"), "interval with a MIDI note");
    check(is(interval(true, -1.0f, "+7 st"), "-", "MIDI"), "interval with no note");
    check(noteName(40.0f) == "E2" && noteName(60.0f) == "C4" && noteName(69.0f) == "A4", "note names");
}

static void plantTests() {
    constexpr float dt = 1.0f / 30.0f;
    constexpr int blockSize = 1600;  // 48 kHz at 30 updates per second
    PlantView plant;
    SpectrumTap tap;
    tap.setActive(true);
    std::vector<float> sine(blockSize), zeros(blockSize, 0.0f);
    double phase = 0.0;
    auto pushSine = [&] {
        for (float& v : sine) { v = 0.25f * (float) std::sin(phase); phase += 2.0 * M_PI * 1000.0 / 48000.0; }
        tap.push(sine.data(), sine.data(), blockSize);
    };
    auto allAtFloor = [&] {
        for (int i = 0; i < kNumBands; ++i) if (plant.level(i) != kFloorDb) return false;
        return true;
    };

    for (int i = 0; i < 30; ++i) plant.update(tap, 48000.0, dt);
    check(allAtFloor(), "plantRestsWithNoAudio");

    for (int i = 0; i < 30; ++i) { pushSine(); plant.update(tap, 48000.0, dt); }
    check(plant.level(30) > -20.0f, "plantRespondsToSound");

    for (int i = 0; i < 90; ++i) { tap.push(zeros.data(), zeros.data(), blockSize); plant.update(tap, 48000.0, dt); }
    check(plant.level(30) < kFloorDb + 1.0f, "plantFallsWithSilence");

    for (int i = 0; i < 30; ++i) { pushSine(); plant.update(tap, 48000.0, dt); }
    for (int i = 0; i < 90; ++i) plant.update(tap, 48000.0, dt);
    check(plant.level(30) < kFloorDb + 1.0f, "plantFallsWhenHostStops");

    {   // A steady sound stays still however long the host's blocks are: a tick with no new audio is not silence.
        PlantView steady;
        SpectrumTap steadyTap;
        steadyTap.setActive(true);
        const int hostBlock = 2048;
        std::vector<float> block((size_t) hostBlock);
        double owed = 0.0, swing = 0.0, lowest = 0.0, highest = -100.0;
        for (int tick = 0; tick < 300; ++tick) {
            for (owed += blockSize; owed >= hostBlock; owed -= hostBlock) {
                for (float& v : block) { v = 0.25f * (float) std::sin(phase); phase += 2.0 * M_PI * 1000.0 / 48000.0; }
                steadyTap.push(block.data(), block.data(), (size_t) hostBlock);
            }
            steady.update(steadyTap, 48000.0, dt);
            if (tick >= 150) { lowest = std::min(lowest, (double) steady.level(30)); highest = std::max(highest, (double) steady.level(30)); }
        }
        swing = highest - lowest;
        std::printf("  level swing with 2048-sample blocks: %.2f dB\n", swing);
        check(swing < 1.0, "plantStillWithLongBlocks");
    }

    PlantView unprepared;
    for (int i = 0; i < 30; ++i) { pushSine(); unprepared.update(tap, 0.0, dt); }
    check(unprepared.level(30) == kFloorDb, "plantRestsWithNoSampleRate");
}

static void setParameter(juce::AudioProcessor& p, const char* id, float value) {
    for (auto* prm : p.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
            if (withId->paramID == id) { withId->setValueNotifyingHost(value); return; }
    std::printf("missing parameter %s\n", id);
    ++failures;
}

static float parameter(juce::AudioProcessor& p, const char* id) {
    for (auto* prm : p.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
            if (withId->paramID == id) return prm->getValue();
    return -1.0f;
}

static std::unique_ptr<DroseraEditor> openEditor(juce::AudioProcessor& p) {
    return std::unique_ptr<DroseraEditor>(dynamic_cast<DroseraEditor*>(p.createEditor()));
}

static void render(juce::AudioProcessor& p, double seconds, juce::MidiBuffer* midi = nullptr) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer none;
    for (int b = 0; b < (int) (seconds * 48000.0 / 512.0); ++b) {
        buffer.clear();
        p.processBlock(buffer, midi != nullptr && b == 0 ? *midi : none);
    }
}

static void editorTests() {
    constexpr float dt = 1.0f / 30.0f;

    {   // The open tab lives in the plugin's state, and a bad or missing value opens Filters.
        std::unique_ptr<juce::AudioProcessor> first(createPluginFilter());
        juce::MemoryBlock state;
        {
            auto editor = openEditor(*first);
            editor->setTab(3);
            first->getStateInformation(state);
        }
        std::unique_ptr<juce::AudioProcessor> second(createPluginFilter());
        second->setStateInformation(state.getData(), (int) state.getSize());
        check(openEditor(*second)->tab() == 3, "tabPersists");

        setParameter(*first, "lpf", 0.33f);
        first->getStateInformation(state);
        auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
        xml->setAttribute("uiTab", 99);
        juce::MemoryBlock bad;
        juce::AudioProcessor::copyXmlToBinary(*xml, bad);
        xml->removeAttribute("uiTab");
        juce::MemoryBlock missing;
        juce::AudioProcessor::copyXmlToBinary(*xml, missing);
        std::unique_ptr<juce::AudioProcessor> third(createPluginFilter()), fourth(createPluginFilter());
        third->setStateInformation(bad.getData(), (int) bad.getSize());
        fourth->setStateInformation(missing.getData(), (int) missing.getSize());
        check(openEditor(*third)->tab() == 0 && std::abs(parameter(*third, "lpf") - 0.33f) < 0.001f, "tabSurvivesBadState");
        check(openEditor(*fourth)->tab() == 0 && std::abs(parameter(*fourth, "lpf") - 0.33f) < 0.001f, "tabSurvivesMissingState");
    }

    {   // A host may restore state off the message thread while the window reads the open tab.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto editor = openEditor(*p);
        std::atomic<bool> done { false };
        std::thread host([&] { while (!done) p->setStateInformation(state.getData(), (int) state.getSize()); });
        for (int i = 0; i < 400000; ++i) editor->refreshDisplay(0.0f);
        done = true;
        host.join();
        check(true, "stateRestoredOffThreadWithWindowOpen");
    }

    {   // Only the open tab's controls show; the main ones always do.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        auto editor = openEditor(*p);
        check(editor->getWidth() == 760 && editor->getHeight() == 500, "windowSize");
        editor->setVisible(true);
        editor->setTab(2);
        auto shown = [&](const char* id) { return editor->slider(id)->getParentComponent()->isVisible(); };
        check(shown("echosend") && !shown("lpf") && shown("freq") && shown("body") && shown("fbgain"), "tabsShowTheirControls");
        check(editor->slider("tapewow") == nullptr, "noTapeControls");

        // Changes made while a control is hidden show when it returns.
        editor->setTab(1);
        setParameter(*p, "lpf", 0.8f);
        editor->setTab(0);
        check(std::abs((float) editor->slider("lpf")->getValue() - 0.8f) < 0.001f, "hiddenValuesFollow");
        p->setCurrentProgram(2);
        check(std::abs((float) editor->slider("lpf")->getValue() - parameter(*p, "lpf")) < 0.001f, "presetReachesHiddenControls");
    }

    {   // MIDI: the readouts say what sounds, and the dimmed knobs stay usable.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        p->prepareToPlay(48000.0, 512);
        auto editor = openEditor(*p);
        editor->refreshDisplay(dt);
        check(editor->tag("freq").isEmpty() && editor->tag("body").isEmpty(), "noTagsWithoutMidi");
        setParameter(*p, "midipitch", 1.0f);
        juce::MidiBuffer on, off;
        on.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        render(*p, 2.0, &on);
        editor->refreshDisplay(dt);
        check(editor->tag("freq") == "MIDI" && editor->tag("body") == "SNAP" && editor->readout("freq").startsWith("C4"), "midiTagsAndNote");
        check(editor->slider("freq")->isEnabled() && editor->slider("freq")->getAlpha() < 1.0f, "midiDimsButDoesNotDisable");
        render(*p, 0.1, &off);
        setParameter(*p, "freq", 0.2f);
        render(*p, 0.1);
        editor->refreshDisplay(dt);
        check(editor->tag("freq").isEmpty() && editor->slider("freq")->getAlpha() == 1.0f, "knobTakesPitchBack");
    }

    {   // LOCK: from the knobs Body is snapped, and the readout says so. It means nothing with POLY off.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        p->prepareToPlay(48000.0, 512);
        auto editor = openEditor(*p);
        check(parameter(*p, "polylock") == 1.0f, "lockOnByDefault");
        render(*p, 0.1);
        editor->refreshDisplay(dt);
        check(editor->tag("body").isEmpty(), "lockMeansNothingWithPolyOff");
        setParameter(*p, "poly", 1.0f);
        render(*p, 0.1);
        editor->refreshDisplay(dt);
        check(editor->tag("body") == "SNAP" && editor->tag("freq").isEmpty() && editor->slider("body")->getAlpha() < 1.0f, "lockSnapsBody");
        setParameter(*p, "polylock", 0.0f);  // with no audio running, as a host may be stopped
        editor->refreshDisplay(dt);
        check(editor->tag("body").isEmpty(), "freeBodyHasNoTag");

        juce::MemoryBlock state;
        p->getStateInformation(state);
        std::unique_ptr<juce::AudioProcessor> second(createPluginFilter());
        second->setStateInformation(state.getData(), (int) state.getSize());
        check(parameter(*second, "polylock") == 0.0f, "lockStateRoundTrip");

        p->setCurrentProgram(12);
        const float openFifths = parameter(*p, "polylock");
        p->setCurrentProgram(13);
        check(openFifths == 1.0f && parameter(*p, "polylock") == 0.0f, "presetsSetLock");
    }

    {   // WARP: Echo Time shows the shortened time.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        p->prepareToPlay(48000.0, 512);
        auto editor = openEditor(*p);
        setParameter(*p, "echotime", 0.5f);
        render(*p, 1.0);
        editor->refreshDisplay(dt);
        const auto normal = editor->readout("echotime");
        setParameter(*p, "echohalve", 1.0f);
        render(*p, 2.0);
        editor->refreshDisplay(dt);
        auto* bridge = dynamic_cast<EditorBridge*>(p.get());
        check(editor->readout("echotime") == juce::String(readouts::echoTime(true, bridge->echoSeconds(), "")) && editor->readout("echotime") != normal,
              "warpShowsRealTime");
    }

    {   // With no audio running, Echo Time's readout still follows the knob and WARP.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        auto editor = openEditor(*p);
        setParameter(*p, "echohalve", 1.0f);
        setParameter(*p, "echotime", 0.9f);
        editor->refreshDisplay(dt);
        const auto high = editor->readout("echotime");
        setParameter(*p, "echotime", 0.2f);
        editor->refreshDisplay(dt);
        const auto low = editor->readout("echotime");
        std::printf("  WARP with no audio: 0.9 reads %s, 0.2 reads %s\n", high.toRawUTF8(), low.toRawUTF8());
        check(high == "1.05 s" && low == "0.10 s", "warpReadoutWithoutAudio");
    }

    {   // Nothing on the audio path allocates, with a window open and a long sysex arriving.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        p->prepareToPlay(48000.0, 512);
        auto editor = openEditor(*p);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        const std::vector<juce::uint8> sysex(200, 0x11);
        midi.addEvent(juce::MidiMessage::createSysExMessage(sysex.data(), (int) sysex.size()), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 10);
        gAllocations = 0;
        malloc_logger = countAllocation;
        tCountAllocations = true;
        for (int b = 0; b < 100; ++b) p->processBlock(buffer, midi);
        tCountAllocations = false;
        malloc_logger = nullptr;
        std::printf("  allocations in 100 blocks: %d\n", gAllocations.load());
        check(gAllocations.load() == 0, "audioPathDoesNotAllocate");
    }

    {   // Polyphony: parameters, presets and the voices MIDI reaches.
        auto text = [](juce::AudioProcessor& p, const char* id) {
            for (auto* prm : p.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
                    if (withId->paramID == id) return prm->getText(prm->getValue(), 32);
            return juce::String("?");
        };
        auto notes = [](std::initializer_list<int> on, std::initializer_list<int> off = {}) {
            juce::MidiBuffer m;
            for (int n : on) m.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8) 100), 0);
            for (int n : off) m.addEvent(juce::MidiMessage::noteOff(1, n), 0);
            return m;
        };
        // Renders and returns the RMS of the last `tail` seconds, whatever the sample rate.
        auto renderRms = [](juce::AudioProcessor& p, double rate, double seconds, double tail, juce::MidiBuffer* midi = nullptr) {
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer none;
            const int blocks = (int) (seconds * rate / 512.0);
            double sumSq = 0; long n = 0; bool finite = true;
            for (int b = 0; b < blocks; ++b) {
                buffer.clear();
                p.processBlock(buffer, midi != nullptr && b == 0 ? *midi : none);
                if (b * 512 < (seconds - tail) * rate) continue;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i) {
                        const float v = buffer.getSample(c, i);
                        finite &= std::isfinite(v);
                        sumSq += (double) v * v; ++n;
                    }
            }
            return finite && n > 0 ? std::sqrt(sumSq / (double) n) : -1.0;
        };
        auto fresh = [](double rate = 48000.0) {
            std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
            p->prepareToPlay(rate, 512);
            return p;
        };
        auto setPolyMidi = [](juce::AudioProcessor& p) { setParameter(p, "midipitch", 1.0f); setParameter(p, "poly", 1.0f); };

        {
            auto p = fresh();
            check(parameter(*p, "poly") == 0.0f && parameter(*p, "coupling") == 0.0f
                  && text(*p, "interval2") == "+7 st" && text(*p, "interval3") == "+12 st", "defaultsOff");
        }
        {
            auto first = fresh();
            setParameter(*first, "poly", 1.0f);
            setParameter(*first, "coupling", 0.4f);
            setParameter(*first, "interval2", (-5.0f + 24.0f) / 48.0f);
            juce::MemoryBlock state;
            first->getStateInformation(state);
            auto second = fresh();
            second->setStateInformation(state.getData(), (int) state.getSize());
            check(parameter(*second, "poly") == 1.0f && std::abs(parameter(*second, "coupling") - 0.4f) < 0.001f
                  && text(*second, "interval2") == "-5 st" && text(*second, "interval3") == "+12 st", "polyStateRoundTrip");
        }
        {   // Audio Units order parameters by this hint, and hosts like Logic store automation by index: what is new since 0.1.1 needs a higher one.
            auto p = fresh();
            std::map<juce::String, int> hints;
            for (auto* parameter : p->getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter)) hints[withId->paramID] = withId->getVersionHint();
            bool ok = true;
            for (const char* added : { "poly", "coupling", "interval2", "interval3", "polylock" })
                ok = ok && hints.at(added) == 2;
            for (const char* released : { "freq", "fbgain", "body", "lpf", "hpf", "revmix", "revdecay", "echosend", "echotime", "echofb", "volume", "echohalve", "inputgain", "midipitch", "midigate" })
                ok = ok && hints.at(released) == 1;
            check(ok, "polyphonyParametersKeepAudioUnitOrder");
        }
        {
            auto p = fresh();
            p->setCurrentProgram(13);
            const float on = parameter(*p, "poly");
            p->setCurrentProgram(2);
            check(on == 1.0f && parameter(*p, "poly") == 0.0f, "oldPresetsTurnPolyOff");
        }
        {
            auto p = fresh();
            const juce::String old[] = { "Default", "Sub Cellar", "Glass Wind", "Cathedral Bloom", "Echo Drift", "Doppler Void", "Dark Tide",
                                         "Razor Feedback", "Quiet Room", "Keys (play notes)", "Resonator (needs input)", "Init (silent)" };
            bool names = p->getNumPrograms() == 16;
            for (int i = 0; i < 12 && names; ++i) names = p->getProgramName(i) == old[i];
            names = names && p->getProgramName(12) == "Open Fifths" && p->getProgramName(13) == "Sympathetic" && p->getProgramName(14) == "Chord Keys" && p->getProgramName(15) == "Ember Pong";
            check(names, "presetCountAndOrder");
        }
        {
            auto p = fresh();
            auto* bridge = dynamic_cast<EditorBridge*>(p.get());
            setPolyMidi(*p);
            auto chord = notes({ 48, 55, 64 });
            render(*p, 0.1, &chord);
            check(bridge->polyMidiActive() && bridge->voiceNote(0) == 48 && bridge->voiceNote(1) == 55 && bridge->voiceNote(2) == 64, "threeNotesThreeVoices");
        }
        {
            auto p = fresh();
            auto* bridge = dynamic_cast<EditorBridge*>(p.get());
            setPolyMidi(*p);
            auto one = notes({ 48 });
            render(*p, 0.1, &one);
            check(bridge->voiceNote(0) == 48 && bridge->voiceNote(1) < 0 && bridge->voiceNote(2) < 0, "singleNoteLeavesOthersClosed");
        }
        {
            auto p = fresh();
            auto* bridge = dynamic_cast<EditorBridge*>(p.get());
            setPolyMidi(*p);
            auto four = notes({ 48, 55, 64, 71 });
            render(*p, 0.1, &four);
            const bool stole = bridge->voiceNote(0) == 71;
            auto off = notes({}, { 71 });
            render(*p, 0.1, &off);
            check(stole && bridge->voiceNote(0) == 48, "fourthNoteSteals");
        }
        {
            auto p = fresh();
            auto* bridge = dynamic_cast<EditorBridge*>(p.get());
            setPolyMidi(*p);
            auto on = notes({ 48 }), off = notes({}, { 48 });
            render(*p, 0.1, &on);
            render(*p, 0.1, &off);
            check(bridge->voiceNote(0) == 48 && bridge->polyMidiActive(), "releaseKeepsDroneWithoutGate");
        }
        {
            auto p = fresh();
            auto* bridge = dynamic_cast<EditorBridge*>(p.get());
            setPolyMidi(*p);
            auto chord = notes({ 48, 55, 64 }), off = notes({}, { 48, 55, 64 });
            render(*p, 0.1, &chord);
            render(*p, 0.1, &off);
            setParameter(*p, "freq", 0.2f);
            render(*p, 0.1);
            const bool taken = !bridge->polyMidiActive() && bridge->voiceNote(0) < 0 && bridge->voiceNote(1) < 0;
            auto again = notes({ 60 });
            render(*p, 0.1, &again);
            check(taken && bridge->polyMidiActive() && bridge->voiceNote(0) == 60 && bridge->voiceNote(1) < 0, "knobTakesAllVoicesBack");
        }
        {
            auto p = fresh();
            auto* bridge = dynamic_cast<EditorBridge*>(p.get());
            setParameter(*p, "midipitch", 1.0f);
            auto on = notes({ 48 }), off = notes({}, { 48 });
            render(*p, 0.1, &on);
            render(*p, 0.1, &off);
            setParameter(*p, "poly", 1.0f);
            const double rms = renderRms(*p, 48000.0, 3.0, 1.0);
            std::printf("  latched note, polyphony on: rms %.4f\n", rms);
            check(bridge->voiceNote(0) == 48 && rms > 1e-3, "polyOnWithLatchedNote");
        }
        {
            auto p = fresh();
            auto* bridge = dynamic_cast<EditorBridge*>(p.get());
            setParameter(*p, "midipitch", 1.0f);
            auto four = notes({ 48, 55, 64, 71 });
            render(*p, 0.1, &four);
            setParameter(*p, "poly", 1.0f);
            render(*p, 0.1);
            check(bridge->voiceNote(0) == 71 && bridge->voiceNote(1) == 55 && bridge->voiceNote(2) == 64, "polyOnWithHeldKeys");
        }
        {
            auto p = fresh();
            auto* bridge = dynamic_cast<EditorBridge*>(p.get());
            setPolyMidi(*p);
            auto chord = notes({ 48, 55, 64 });
            render(*p, 0.1, &chord);
            setParameter(*p, "poly", 0.0f);
            render(*p, 1.0);
            check(!bridge->polyMidiActive() && bridge->midiPitchActive() && std::abs(bridge->soundingNote() - 64.0f) < 0.5f, "polyOffAfterChord");
        }
        {
            auto p = fresh(44100.0);
            setPolyMidi(*p);
            auto chord = notes({ 48, 55, 64 });
            const double rms = renderRms(*p, 44100.0, 3.0, 1.0, &chord);
            std::printf("  chord at 44.1 kHz: rms %.4f\n", rms);
            check(rms > 1e-3, "chordAt44100");
        }
        {
            auto p = fresh();
            auto* bridge = dynamic_cast<EditorBridge*>(p.get());
            setPolyMidi(*p);
            auto chord = notes({ 48, 55, 64 });
            chord.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);  // +2 semitones, less one step of 16384
            render(*p, 0.1, &chord);
            const float bend = 2.0f * 8191.0f / 8192.0f;
            check(std::abs(bridge->voiceNote(0) - (48.0f + bend)) < 0.01f && std::abs(bridge->voiceNote(1) - (55.0f + bend)) < 0.01f
                  && std::abs(bridge->voiceNote(2) - (64.0f + bend)) < 0.01f, "voiceNoteIncludesBend");
        }
        {   // What sounds, judged at each note's fundamental: a released voice closes, and the bend moves every voice.
            // 2 s of output, as the strongest Hann-windowed bin within 40 cents of the note: the drone's pitch wanders a little
            auto level = [](juce::AudioProcessor& p, double note) {
                juce::AudioBuffer<float> buffer(2, 512);
                juce::MidiBuffer none;
                const int blocks = (int) (2.0 * 48000.0 / 512.0);
                const int n = blocks * 512;
                std::vector<float> x((size_t) n);
                for (int b = 0; b < blocks; ++b) {
                    buffer.clear();
                    p.processBlock(buffer, none);
                    for (int i = 0; i < 512; ++i) x[(size_t) (b * 512 + i)] = buffer.getSample(0, i);
                }
                double best = 0;
                for (int cents = -40; cents <= 40; cents += 10) {
                    const double hz = 440.0 * std::pow(2.0, (note + cents / 100.0 - 69.0) / 12.0);
                    double re = 0, im = 0;
                    for (int k = 0; k < n; ++k) {
                        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * k / n), a = 2.0 * M_PI * hz * k / 48000.0;
                        re += w * x[(size_t) k] * std::cos(a);
                        im += w * x[(size_t) k] * std::sin(a);
                    }
                    best = std::max(best, re * re + im * im);
                }
                return 10.0 * std::log10(best + 1e-20);
            };
            auto keys = [&] {
                auto p = fresh();
                setPolyMidi(*p);
                setParameter(*p, "midigate", 1.0f);
                return p;
            };
            {
                auto p = keys();
                auto chord = notes({ 48, 55, 64 }), off = notes({}, { 55 });
                render(*p, 2.0, &chord);
                const double before55 = level(*p, 55), before48 = level(*p, 48);
                render(*p, 0.1, &off);
                const double after55 = level(*p, 55), after48 = level(*p, 48);
                std::printf("  released 55: its level %+.1f dB, 48's %+.1f dB\n", after55 - before55, after48 - before48);
                check(after55 < before55 - 20.0 && after48 > before48 - 6.0, "releasedNoteCloses");
            }
            {   // Voices 1 and 2 sound at their bent notes about as strongly as voice 0 does at its own, and no longer at the unbent ones
                auto p = keys();
                auto chord = notes({ 48, 55, 64 });
                chord.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
                render(*p, 3.0, &chord);
                const double v0 = level(*p, 50), v1 = level(*p, 57), v2 = level(*p, 66), old1 = level(*p, 55), old2 = level(*p, 64);
                std::printf("  bend +2: voice 0 at 50 %+.1f dB, voice 1 at 57 %+.1f dB (55: %+.1f), voice 2 at 66 %+.1f dB (64: %+.1f)\n",
                            v0, v1, old1, v2, old2);
                check(v1 > v0 - 15.0 && v2 > v0 - 15.0 && v1 > old1 + 10.0 && v2 > old2 + 10.0, "bendMovesAllVoices");
            }
        }
        {   // Nothing allocates with polyphony on and notes arriving past the three voices.
            auto p = fresh();
            setPolyMidi(*p);
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;
            for (int i = 0; i < 6; ++i) midi.addEvent(juce::MidiMessage::noteOn(1, 40 + 7 * i, (juce::uint8) 100), 10 + i);
            midi.addEvent(juce::MidiMessage::noteOff(1, 47), 200);
            midi.addEvent(juce::MidiMessage::allNotesOff(1), 300);
            gAllocations = 0;
            malloc_logger = countAllocation;
            tCountAllocations = true;
            for (int b = 0; b < 100; ++b) p->processBlock(buffer, midi);
            tCountAllocations = false;
            malloc_logger = nullptr;
            std::printf("  allocations in 100 polyphonic blocks: %d\n", gAllocations.load());
            check(gAllocations.load() == 0, "polyAudioPathDoesNotAllocate");
        }
    }

    {   // Screen readers can name every knob.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        check(openEditor(*p)->slider("fbgain")->getTitle() == "Feedback", "feedbackHasATitle");
    }

    {   // STRINGS is the sixth tab: it shows its controls, persists, and fits the window.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        juce::MemoryBlock state;
        {
            auto editor = openEditor(*p);
            editor->setVisible(true);
            editor->setTab(5);
            auto shown = [&](const char* id) { return editor->slider(id)->getParentComponent()->isVisible(); };
            check(shown("coupling") && shown("interval2") && shown("interval3") && !shown("lpf"), "stringsTabShowsItsControls");
            check(editor->tabBounds(5).getRight() <= 760 - 26, "tabsFitTheWindow");
            const auto knobBounds = [&](const char* id) { return editor->slider(id)->getParentComponent()->getBounds(); };
            check(knobBounds("interval2").getX() - knobBounds("coupling").getX() == 112, "otherTabsKeepTheirPlaces");

            editor->setTab(2);
            check(shown("echosend") && shown("echotime") && shown("echofb") && shown("echodrive") && shown("echotone"), "echoTabShowsItsControls");
            std::vector<juce::Rectangle<int>> cells;
            for (const char* id : { "echosend", "echotime", "echofb", "echodrive", "echotone" }) cells.push_back(knobBounds(id));
            cells.push_back(editor->toggleBounds("echohalve"));
            cells.push_back(editor->toggleBounds("echopong"));
            bool fit = true;
            for (size_t i = 0; i < cells.size(); ++i) {
                fit = fit && !cells[i].isEmpty() && cells[i].getX() >= 26 && cells[i].getRight() <= 760 - 26;
                for (size_t j = i + 1; j < cells.size(); ++j) fit = fit && !cells[i].intersects(cells[j]);
            }
            check(fit, "echoControlsFit");

            setParameter(*p, "echopong", 0.0f);
            editor->refreshDisplay(0.0f);
            check(editor->tag("echodrive") == "PONG OFF" && editor->tag("echotone") == "PONG OFF" && editor->slider("echodrive")->getAlpha() < 1.0f
                      && editor->tag("echofb").isEmpty(), "shapingTagWithPongOff");
            editor->slider("echodrive")->setValue(0.8, juce::sendNotificationSync);
            check(std::abs(parameter(*p, "echodrive") - 0.8f) < 0.001f, "shapingStaysEditableWithPongOff");
            setParameter(*p, "echopong", 1.0f);
            editor->refreshDisplay(0.0f);
            check(editor->tag("echodrive").isEmpty() && editor->tag("echotone").isEmpty() && editor->slider("echotone")->getAlpha() == 1.0f, "shapingTagWithPongOn");
            editor->setTab(5);
            p->getStateInformation(state);
        }
        std::unique_ptr<juce::AudioProcessor> second(createPluginFilter());
        second->setStateInformation(state.getData(), (int) state.getSize());
        check(openEditor(*second)->tab() == 5, "sixthTabPersists");
    }

    {   // The interval knobs show the notes the voices play when MIDI Pitch controls them.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        p->prepareToPlay(48000.0, 512);
        auto editor = openEditor(*p);
        editor->refreshDisplay(dt);
        check(editor->readout("interval2") == "+7 st" && editor->tag("interval2").isEmpty(), "intervalFromKnobWithoutMidi");
        setParameter(*p, "midipitch", 1.0f);
        setParameter(*p, "poly", 1.0f);
        juce::MidiBuffer chord;
        for (int n : { 48, 55 }) chord.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8) 100), 0);
        render(*p, 0.1, &chord);
        editor->refreshDisplay(dt);
        check(editor->readout("interval2") == "G3" && editor->tag("interval2") == "MIDI"
              && editor->slider("interval2")->isEnabled() && editor->slider("interval2")->getAlpha() < 1.0f, "intervalReadoutFollowsMidi");
        check(editor->readout("interval3") == "-" && editor->tag("interval3") == "MIDI", "intervalWithoutANote");
    }

    {   // A preset chosen in the window reaches the host's own display.
        struct Listener final : juce::AudioProcessorListener {
            bool programChanged = false;
            void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
            void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override { programChanged |= details.programChanged; }
        } listener;
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        auto editor = openEditor(*p);
        p->addListener(&listener);
        editor->selectProgram(2);
        p->removeListener(&listener);
        check(listener.programChanged && p->getCurrentProgram() == 2, "presetNotifiesHost");
    }

    {   // The tap runs only while a window is open, and reopening shows no stale burst.
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        p->prepareToPlay(48000.0, 512);
        auto* bridge = dynamic_cast<EditorBridge*>(p.get());
        bool openWhileEditor, closedAfter;
        {
            auto editor = openEditor(*p);
            openWhileEditor = bridge->spectrumTap().isActive();
            render(*p, 1.0);
        }
        closedAfter = !bridge->spectrumTap().isActive();
        check(openWhileEditor && closedAfter, "closingStopsTap");
        render(*p, 1.0);
        auto editor = openEditor(*p);
        editor->refreshDisplay(dt);
        float highest = kFloorDb;
        for (int i = 0; i < kNumBands; ++i) highest = std::max(highest, editor->plant().level(i));
        check(highest <= kFloorDb + 1.0f, "reopenDoesNotBurst");
    }
}

// A project from before polyphony, opened over an instance already in use: what it does not hold returns to its default.
static void stateTests() {
    // The fixture was saved by JUCE's VST3 host, which wraps the plugin's own state. Calling the plugin with the
    // wrapper is ignored, as its root tag is not the plugin's, so unwrap it.
    juce::MemoryBlock file, state;
    const bool read = juce::File(DROSERA_STATE_FIXTURE).loadFileAsData(file);
    const auto wrapper = read ? juce::AudioProcessor::getXmlFromBinary(file.getData(), (int) file.getSize()) : nullptr;
    const auto* component = wrapper != nullptr ? wrapper->getChildByName("IComponent") : nullptr;
    const bool unwrapped = component != nullptr && state.fromBase64Encoding(component->getAllSubText());
    const auto plugin = unwrapped ? juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize()) : nullptr;
    check(plugin != nullptr, "old state: fixture holds the plugin's");
    if (plugin == nullptr) return;
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    const auto param = [&](const char* id) -> juce::RangedAudioParameter* {
        for (auto* candidate : p->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(candidate))
                if (ranged->paramID == id) return ranged;
        return nullptr;
    };
    const auto isDefault = [&](const char* id) { return std::abs(param(id)->getValue() - param(id)->getDefaultValue()) < 0.001f; };
    param("poly")->setValueNotifyingHost(1.0f);
    param("coupling")->setValueNotifyingHost(0.4f);
    param("interval2")->setValueNotifyingHost(param("interval2")->convertTo0to1(3.0f));
    param("interval3")->setValueNotifyingHost(param("interval3")->convertTo0to1(-12.0f));
    param("polylock")->setValueNotifyingHost(0.0f);
    param("freq")->setValueNotifyingHost(0.1f);
    param("fbgain")->setValueNotifyingHost(0.1f);
    param("lpf")->setValueNotifyingHost(0.1f);
    check(! isDefault("poly") && ! isDefault("coupling"), "old state: instance modified first");
    p->setStateInformation(state.getData(), (int) state.getSize());
    check(isDefault("poly"), "old state: POLY returns to its default");
    check(isDefault("coupling"), "old state: Coupling returns to its default");
    check(isDefault("interval2"), "old state: Interval 2 returns to default");
    check(isDefault("interval3"), "old state: Interval 3 returns to default");
    check(isDefault("polylock"), "old state: LOCK returns to default");
    // kSavedValues in plugin_host_test
    check(std::abs(param("freq")->getValue() - 0.61f) < 0.001f, "old state: Frequency loads");
    check(std::abs(param("fbgain")->getValue() - 0.83f) < 0.001f, "old state: Feedback loads");
    check(std::abs(param("lpf")->getValue() - 0.37f) < 0.001f, "old state: Lowpass loads");
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    tapTests();
    bandTests();
    readoutTests();
    plantTests();
    editorTests();
    stateTests();
    std::printf(failures == 0 ? "ALL OK\n" : "SOME FAILED\n");
    return failures == 0 ? 0 : 1;
}
