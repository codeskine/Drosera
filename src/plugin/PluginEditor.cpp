#include "PluginEditor.h"

namespace {
constexpr int kWidth = 740;
constexpr int kRow1H = 172;  // knobs only
constexpr int kRow2H = 206;  // knobs + echo toggle
constexpr int kHeight = 84 + kRow1H + 12 + kRow2H + 18;
constexpr int kHeader = 84;
constexpr int kMargin = 18;
constexpr int kGap = 12;
constexpr int kSectionTitleH = 26;
constexpr int kKnobW = 88;
constexpr int kKnobH = 128;
} // namespace

// ------------------------------------------------------------------ look & feel

DroseraLookAndFeel::DroseraLookAndFeel() {
    setColour(juce::Slider::textBoxTextColourId, text());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, textDim());
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0x22ffffff));
    setColour(juce::ComboBox::textColourId, text());
    setColour(juce::ComboBox::outlineColourId, accentDim());
    setColour(juce::ComboBox::arrowColourId, accent());
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff16221a));
    setColour(juce::PopupMenu::textColourId, text());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentDim());
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0x22ffffff));
    setColour(juce::TextButton::textColourOffId, accent());
}

juce::Label* DroseraLookAndFeel::createSliderTextBox(juce::Slider& s) {
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(juce::FontOptions(12.5f));
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::textColourId, text());
    l->setJustificationType(juce::Justification::centred);
    return l;
}

void DroseraLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s) {
    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(6.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float trackR = radius - 2.0f;
    const bool active = s.isEnabled();

    // body
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a3a2c), centre.x, centre.y - radius,
                                           juce::Colour(0xff111a13), centre.x, centre.y + radius, false));
    g.fillEllipse(centre.x - radius + 7.0f, centre.y - radius + 7.0f, (radius - 7.0f) * 2.0f, (radius - 7.0f) * 2.0f);

    // track + value arc
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, trackR, trackR, 0.0f, startAngle, endAngle, true);
    g.setColour(accentDim().withAlpha(0.55f));
    g.strokePath(track, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, trackR, trackR, 0.0f, startAngle, angle, true);
    g.setColour(active ? accent() : accentDim());
    g.strokePath(value, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    juce::Path ptr;
    const float inner = radius - 18.0f, outer = radius - 9.0f;
    ptr.startNewSubPath(0.0f, -inner);
    ptr.lineTo(0.0f, -outer);
    g.setColour(text());
    g.strokePath(ptr, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                 juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

void DroseraLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) {
    auto r = b.getLocalBounds().toFloat().reduced(2.0f);
    const bool on = b.getToggleState();
    g.setColour(on ? accent().withAlpha(0.9f) : juce::Colour(0x22ffffff));
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    g.setColour(on ? accent() : (highlighted ? textDim() : accentDim()));
    g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.5f);
    g.setColour(on ? juce::Colour(0xff0b110d) : text());
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(b.getButtonText(), r.toNearestInt(), juce::Justification::centred);
}

// ------------------------------------------------------------------ editor

int DroseraEditor::addKnob(juce::AudioProcessorValueTreeState& apvts, const char* id, const juce::String& caption) {
    auto k = std::make_unique<Knob>();
    k->label.setText(caption, juce::dontSendNotification);
    k->label.setJustificationType(juce::Justification::centred);
    k->label.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    k->label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(k->label);

    k->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kKnobW, 18);
    k->slider.setPopupDisplayEnabled(false, false, nullptr);
    k->slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(k->slider);

    auto* param = apvts.getParameter(id);
    jassert(param != nullptr);
    k->attachment = std::make_unique<juce::SliderParameterAttachment>(*param, k->slider, nullptr);
    knobs_.push_back(std::move(k));
    return (int) knobs_.size() - 1;
}

int DroseraEditor::addToggle(juce::AudioProcessorValueTreeState& apvts, const char* id, const juce::String& caption) {
    auto t = std::make_unique<Toggle>();
    t->button.setButtonText(caption);
    t->button.setClickingTogglesState(true);
    addAndMakeVisible(t->button);
    auto* param = apvts.getParameter(id);
    jassert(param != nullptr);
    t->attachment = std::make_unique<juce::ButtonParameterAttachment>(*param, t->button, nullptr);
    toggles_.push_back(std::move(t));
    return (int) toggles_.size() - 1;
}

DroseraEditor::DroseraEditor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& apvts,
                           std::function<bool()> isMidiPitchActive)
    : AudioProcessorEditor(p), isMidiPitchActive_(std::move(isMidiPitchActive)) {
    setLookAndFeel(&laf_);

    // Row 1: the instrument core
    sections_.push_back({"STRING", {addKnob(apvts, "freq", "Frequency"), addKnob(apvts, "fbgain", "Feedback"),
                                    addKnob(apvts, "body", "Body")}, {}, {}});
    sections_.push_back({"FILTER", {addKnob(apvts, "lpf", "Lowpass"), addKnob(apvts, "hpf", "Highpass")}, {}, {}});
    sections_.push_back({"REVERB", {addKnob(apvts, "revmix", "Mix"), addKnob(apvts, "revdecay", "Decay")}, {}, {}});

    // Row 2: echo, output and the controls that only exist in the plugin
    const int echoSend = addKnob(apvts, "echosend", "Send"), echoTime = addKnob(apvts, "echotime", "Time"),
              echoFb = addKnob(apvts, "echofb", "Feedback");
    sections_.push_back({"ECHO", {echoSend, echoTime, echoFb}, {addToggle(apvts, "echohalve", "WARP")}, {}});
    sections_.push_back({"OUTPUT", {addKnob(apvts, "volume", "Volume"), addKnob(apvts, "inputgain", "Input")}, {}, {}});
    sections_.push_back({"MIDI", {}, {addToggle(apvts, "midipitch", "PITCH"), addToggle(apvts, "midigate", "GATE")}, {}});

    // Preset selector (factory presets are exposed to the host as programs too)
    for (int i = 0; i < p.getNumPrograms(); ++i) presetBox_.addItem(p.getProgramName(i), i + 1);
    presetBox_.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox_.setTextWhenNothingSelected("Preset");
    presetBox_.onChange = [this] { selectProgram(presetBox_.getSelectedId() - 1); };
    addAndMakeVisible(presetBox_);
    prevButton_.onClick = [this] { selectProgram((processor.getCurrentProgram() + processor.getNumPrograms() - 1) % processor.getNumPrograms()); };
    nextButton_.onClick = [this] { selectProgram((processor.getCurrentProgram() + 1) % processor.getNumPrograms()); };
    addAndMakeVisible(prevButton_);
    addAndMakeVisible(nextButton_);

    setSize(kWidth, kHeight);
    timerCallback();
    startTimerHz(5);  // follow program changes made by the host and the MIDI pitch state
}

DroseraEditor::~DroseraEditor() { stopTimer(); setLookAndFeel(nullptr); }

void DroseraEditor::selectProgram(int index) {
    if (index < 0 || index >= processor.getNumPrograms()) return;
    processor.setCurrentProgram(index);
    presetBox_.setSelectedId(index + 1, juce::dontSendNotification);
}

void DroseraEditor::timerCallback() {
    const int id = processor.getCurrentProgram() + 1;
    if (presetBox_.getSelectedId() != id) presetBox_.setSelectedId(id, juce::dontSendNotification);

    // While a MIDI note sets the pitch, the Frequency and Body readouts are not what sounds.
    const bool midi = isMidiPitchActive_ && isMidiPitchActive_();
    if (midi != showingMidiPitch_) {
        showingMidiPitch_ = midi;
        auto& freq = *knobs_[0];
        auto& body = *knobs_[2];
        freq.label.setText(midi ? "Frequency: MIDI" : "Frequency", juce::dontSendNotification);
        body.label.setText(midi ? "Body: snapped" : "Body", juce::dontSendNotification);
        for (auto* k : {&freq, &body}) {
            k->label.setColour(juce::Label::textColourId, midi ? DroseraLookAndFeel::accent() : DroseraLookAndFeel::textDim());
            k->slider.setAlpha(midi ? 0.45f : 1.0f);
        }
    }
}

void DroseraEditor::paint(juce::Graphics& g) {
    g.setGradientFill(juce::ColourGradient(DroseraLookAndFeel::bgTop(), 0.0f, 0.0f,
                                           DroseraLookAndFeel::bgBottom(), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // header
    g.setColour(DroseraLookAndFeel::accent());
    g.setFont(juce::FontOptions(34.0f, juce::Font::bold));
    g.drawText("DROSERA", kMargin, 12, 360, 40, juce::Justification::centredLeft);
    g.setColour(DroseraLookAndFeel::textDim());
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("feedback drone synth", kMargin + 2, 50, 360, 20, juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("Based on open-source firmware by Nick Donaldson (MIT)",
               getWidth() - 440 - kMargin, 54, 440, 18, juce::Justification::centredRight);

    for (const auto& s : sections_) {
        auto b = s.bounds.toFloat();
        g.setColour(DroseraLookAndFeel::panel());
        g.fillRoundedRectangle(b, 10.0f);
        g.setColour(juce::Colour(0x30ffffff));
        g.drawRoundedRectangle(b, 10.0f, 1.0f);
        g.setColour(DroseraLookAndFeel::accent());
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(s.title, s.bounds.withHeight(kSectionTitleH).reduced(12, 0), juce::Justification::centredLeft);
    }
}

void DroseraEditor::resized() {
    const int presetW = 330, bx = getWidth() - kMargin - presetW;
    prevButton_.setBounds(bx, 16, 30, 28);
    presetBox_.setBounds(bx + 36, 16, presetW - 72, 28);
    nextButton_.setBounds(bx + presetW - 30, 16, 30, 28);

    // Two rows; each section takes as many knob-sized slots as it needs.
    const int usableW = getWidth() - 2 * kMargin;
    const int rowH[2] = {kRow1H, kRow2H};
    const int rows[2][3] = {{0, 1, 2}, {3, 4, 5}};
    const int slots[6] = {3, 2, 2, 3, 2, 1};

    for (int r = 0; r < 2; ++r) {
        int x = kMargin;
        const int slotW = usableW / (slots[rows[r][0]] + slots[rows[r][1]] + slots[rows[r][2]]);
        for (int i : rows[r]) {
            auto& s = sections_[(size_t) i];
            const int w = slots[i] * slotW;
            s.bounds = {x, kHeader + (r == 0 ? 0 : kRow1H + kGap), w - (i == rows[r][2] ? 0 : kGap), rowH[r]};
            x += w;

            auto inner = s.bounds.withTrimmedTop(kSectionTitleH);
            const int n = (int) s.knobs.size();
            const int slot = n > 0 ? inner.getWidth() / std::max(n, 1) : 0;
            for (int k = 0; k < n; ++k) {
                auto cell = juce::Rectangle<int>(inner.getX() + k * slot, inner.getY(), slot, kKnobH)
                                .withSizeKeepingCentre(kKnobW, kKnobH);
                auto& kn = *knobs_[(size_t) s.knobs[(size_t) k]];
                kn.label.setBounds(cell.removeFromTop(18));
                kn.slider.setBounds(cell);
            }
            // Toggles: under the knobs if there are any, otherwise centred in the panel.
            const int t = (int) s.toggles.size();
            if (t > 0) {
                const int bh = 28;
                const int bw = std::min(112, inner.getWidth() - 12);
                if (n > 0) {  // side by side, under the knobs
                    const int total = t * bw + (t - 1) * 8;
                    for (int k = 0; k < t; ++k)
                        toggles_[(size_t) s.toggles[(size_t) k]]->button.setBounds(
                            inner.getCentreX() - total / 2 + k * (bw + 8), inner.getBottom() - bh - 6, bw, bh);
                } else {  // stacked in the panel
                    for (int k = 0; k < t; ++k)
                        toggles_[(size_t) s.toggles[(size_t) k]]->button.setBounds(
                            inner.getCentreX() - bw / 2, inner.getY() + 14 + k * (bh + 14), bw, bh);
                }
            }
        }
    }
}
