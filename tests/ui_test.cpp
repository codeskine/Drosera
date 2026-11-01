// Tests for the interface: the spectrum path to the display, the readouts and the editor's behaviour.
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
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

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    tapTests();
    bandTests();
    readoutTests();
    plantTests();
    editorTests();
    std::printf(failures == 0 ? "ALL OK\n" : "SOME FAILED\n");
    return failures == 0 ? 0 : 1;
}
