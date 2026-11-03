#include "PluginEditor.h"
#include <daisysp.h>
#include "ui/Readouts.h"
#include "ui/Theme.h"

namespace {
constexpr int kWidth = 900, kHeight = 560;
// The middle band's centre, shared by Frequency, the plant and Body.
constexpr int kBandY = 236;

/// Hz of a 0..1 parameter on the engine's log curve, so the arcs draw what the filters do.
float hz(juce::AudioProcessorValueTreeState& apvts, const char* id, float min, float max) {
    return daisysp::fmap(apvts.getRawParameterValue(id)->load(), min, max, daisysp::Mapping::LOG);
}
} // namespace

DroseraEditor::DroseraEditor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& apvts, EditorBridge& bridge)
    : AudioProcessorEditor(p), apvts_(apvts), bridge_(bridge),
      feedbackAttachment_(*apvts.getParameter("fbgain"), plant_.feedback(), nullptr),
      frequency_(*apvts.getParameter("freq"), "Frequency", drosera::KnobSize::large, drosera::KnobGroup::loop),
      body_(*apvts.getParameter("body"), "Body", drosera::KnobSize::large, drosera::KnobGroup::loop),
      row_(apvts), setup_(apvts), header_(p) {
    setLookAndFeel(&lookAndFeel_);
    addAndMakeVisible(plant_);
    addAndMakeVisible(frequency_);
    addAndMakeVisible(body_);
    addAndMakeVisible(row_);
    scrim_.onClick = [this] { setSetupOpen(false); };
    addChildComponent(scrim_);
    addChildComponent(setup_);

    header_.onProgramChosen = [this](int index) { selectProgram(index); };
    header_.onSetupClicked = [this] { setSetupOpen(!setupOpen()); };
    addAndMakeVisible(header_);

    setWantsKeyboardFocus(true);
    setResizable(false, false);
    setSize(kWidth, kHeight);

    bridge_.spectrumTap().setActive(true);
    refreshDisplay(0.0f);
    lastTick_ = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30);
}

DroseraEditor::~DroseraEditor() {
    stopTimer();
    bridge_.spectrumTap().setActive(false);
    setLookAndFeel(nullptr);
}

void DroseraEditor::selectProgram(int index) {
    if (!juce::isPositiveAndBelow(index, processor.getNumPrograms())) return;
    processor.setCurrentProgram(index);
    processor.updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
    header_.setProgram(index);
}

void DroseraEditor::setSetupOpen(bool open) {
    scrim_.setVisible(open);
    setup_.setVisible(open);
    if (open) setup_.toFront(false);
    header_.setSetupOpen(open);
}

bool DroseraEditor::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && setupOpen()) {
        setSetupOpen(false);
        return true;
    }
    return false;
}

void DroseraEditor::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    refreshDisplay((float) juce::jlimit(0.0, 0.25, (now - lastTick_) * 0.001));
    lastTick_ = now;
}

void DroseraEditor::refreshDisplay(float dtSeconds) {
    plant_.update(bridge_.spectrumTap(), bridge_.hostSampleRate(), dtSeconds);
    plant_.setFeedbackText(apvts_.getParameter("fbgain")->getCurrentValueAsText());

    const bool midi = bridge_.midiPitchActive();
    const bool polyMidi = bridge_.polyMidiActive();
    const auto frequency = drosera::readouts::frequency(midi, bridge_.soundingNote(), frequency_.parameter().getCurrentValueAsText().toStdString());
    const auto body = drosera::readouts::body(bridge_.bodySnapped(), bridge_.bodySeconds(), body_.parameter().getCurrentValueAsText().toStdString());
    if (bridge_.polyRunning()) {
        float notes[3];
        for (int v = 0; v < 3; ++v) notes[v] = polyMidi ? bridge_.voiceNote(v) : bridge_.publishedNote(v);
        frequency_.show(juce::String::fromUTF8(drosera::readouts::chord(notes, 3).c_str()), frequency.tag);
    } else {
        frequency_.show(frequency.value, frequency.tag);
    }
    body_.show(body.value, body.tag);

    const bool warp = apvts_.getRawParameterValue("echohalve")->load() > 0.5f;
    const bool pong = apvts_.getRawParameterValue("echopong")->load() > 0.5f;
    for (const auto* knobs : { &row_.knobs(), &setup_.knobs() })
        for (const auto& [id, knob] : *knobs) {
            const auto text = knob->parameter().getCurrentValueAsText();
            if (id == "interval2" || id == "interval3") {
                const int voice = id == "interval2" ? 1 : 2;
                const int semitones = juce::roundToInt(knob->parameter().convertFrom0to1(knob->parameter().getValue()));
                const auto interval = polyMidi ? drosera::readouts::interval(true, bridge_.voiceNote(voice), text.toStdString())
                                               : drosera::readouts::playedInterval(bridge_.polyRunning(), bridge_.publishedNote(0),
                                                                                   bridge_.publishedNote(voice), semitones, text.toStdString());
                knob->show(interval.value, interval.tag);
                continue;
            }
            knob->show(id == "echotime" ? juce::String(drosera::readouts::echoTime(warp, bridge_.echoSeconds(), text.toStdString())) : text, {});
        }

    pulseSeconds_ += dtSeconds;
    const bool sending = apvts_.getRawParameterValue("echosend")->load() > 0.0f;
    row_.refresh(hz(apvts_, "hpf", 10.0f, 4000.0f), hz(apvts_, "lpf", 100.0f, 18000.0f), hz(apvts_, "echotone", 200.0f, 3200.0f), pong,
                 drosera::pulse::echoPulse(pulseSeconds_, bridge_.echoSeconds(), pong, sending));
    setup_.refresh(bridge_.polyRunning());

    // Follow what the host changes.
    header_.setProgram(processor.getCurrentProgram());
}

const drosera::LabeledKnob* DroseraEditor::findKnob(const juce::String& paramId) const {
    if (paramId == "freq") return &frequency_;
    if (paramId == "body") return &body_;
    for (const auto* knobs : { &row_.knobs(), &setup_.knobs() }) {
        const auto found = knobs->find(paramId);
        if (found != knobs->end()) return found->second.get();
    }
    return nullptr;
}

juce::Slider* DroseraEditor::slider(const juce::String& paramId) {
    if (paramId == "fbgain") return &plant_.feedback();
    auto* knob = const_cast<drosera::LabeledKnob*>(findKnob(paramId));
    return knob != nullptr ? &knob->slider() : nullptr;
}

juce::Button* DroseraEditor::toggle(const juce::String& paramId) {
    if (auto* button = row_.toggle(paramId)) return button;
    return setup_.toggle(paramId);
}

juce::String DroseraEditor::readout(const juce::String& paramId) const {
    const auto* knob = findKnob(paramId);
    return knob != nullptr ? knob->readout() : juce::String();
}

juce::String DroseraEditor::tag(const juce::String& paramId) const {
    const auto* knob = findKnob(paramId);
    return knob != nullptr ? knob->tag() : juce::String();
}

void DroseraEditor::paint(juce::Graphics& g) {
    g.setGradientFill(juce::ColourGradient(drosera::theme::backgroundCentre(), {450.0f, 240.0f},
                                           drosera::theme::backgroundEdge(), {450.0f, 240.0f + 560.0f}, true));
    g.fillAll();
}

void DroseraEditor::resized() {
    plant_.setBounds(450 - 380, kBandY - 180, 760, 314);  // until the plant is redrawn around its own centre
    auto placeLarge = [](drosera::LabeledKnob& knob, int x) {
        const int fromTop = drosera::LabeledKnob::radius(drosera::KnobSize::large) + drosera::LabeledKnob::margin(drosera::KnobSize::large);
        knob.setBounds(x - 80, kBandY - fromTop, 160, 190);
    };
    placeLarge(frequency_, 150);
    placeLarge(body_, 750);
    row_.setBounds(0, drosera::ControlRow::kTop, kWidth, kHeight - drosera::ControlRow::kTop);
    header_.setBounds(0, 0, kWidth, drosera::HeaderBar::kHeight);
    scrim_.setBounds(0, drosera::HeaderBar::kHeight, kWidth, drosera::SetupPanel::kTop - drosera::HeaderBar::kHeight);
    setup_.setBounds(0, drosera::SetupPanel::kTop, kWidth, kHeight - drosera::SetupPanel::kTop);
}
