#include "HeaderBar.h"
#include "Theme.h"

namespace drosera {

namespace {
// From the mockup, in the window's coordinates.
const juce::Rectangle<int> kPresetField {40, 30, 210, 30}, kSetupField {650, 30, 210, 30};
constexpr int kArrowWidth = 30;

void drawField(juce::Graphics& g, juce::Rectangle<int> field, const juce::String& caption, bool open) {
    g.setFont(theme::captionFont());
    g.setColour(juce::Colour(0xff6e605c));
    g.drawText(caption, field.getX() + 2, field.getY() - 18, field.getWidth(), 14, juce::Justification::centredLeft);
    const auto r = field.toFloat().reduced(0.5f);
    g.setColour(juce::Colour(open ? 0xff1f1719 : 0xff181214));
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour(open ? juce::Colour(0xff5a4436) : theme::hairline());
    g.drawRoundedRectangle(r, 7.0f, 1.0f);
}
} // namespace

HeaderBar::HeaderBar(juce::AudioProcessor& processor) : processor_(processor), program_(processor.getCurrentProgram()) {
    const int count = processor_.getNumPrograms();
    previous_.onClick = [this, count] { choose((processor_.getCurrentProgram() + count - 1) % count); };
    next_.onClick = [this, count] { choose((processor_.getCurrentProgram() + 1) % count); };
    preset_.onClick = [this] {
        presetMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&preset_).withMinimumWidth(kPresetField.getWidth()),
                                   [safe = juce::Component::SafePointer<HeaderBar>(this)](int result) {
                                       if (safe != nullptr && result > 0) safe->choose(result - 1);
                                   });
    };
    setup_.onClick = [this] { if (onSetupClicked) onSetupClicked(); };
    for (auto* area : {&previous_, &next_, &preset_, &setup_}) addAndMakeVisible(*area);
}

void HeaderBar::choose(int index) {
    if (onProgramChosen) onProgramChosen(index);
}

void HeaderBar::setProgram(int index) {
    if (index == program_) return;
    program_ = index;
    repaint();
}

void HeaderBar::setSetupOpen(bool open) {
    if (open == setupOpen_) return;
    setupOpen_ = open;
    repaint();
}

juce::PopupMenu HeaderBar::presetMenu() const {
    juce::PopupMenu menu;
    for (int i = 0; i < processor_.getNumPrograms(); ++i) menu.addItem(i + 1, processor_.getProgramName(i), true, i == program_);
    return menu;
}

void HeaderBar::paint(juce::Graphics& g) {
    g.setFont(theme::titleFont());
    g.setColour(juce::Colour(0xffe6d4c6));
    g.drawText("DROSERA", juce::Rectangle<int>(300, 34).withCentre({450, 46}), juce::Justification::centred);

    const auto p = kPresetField;
    drawField(g, p, "PRESET", false);
    g.setColour(theme::hairline());
    g.drawVerticalLine(p.getX() + kArrowWidth, (float) p.getY() + 6.0f, (float) p.getBottom() - 6.0f);
    g.drawVerticalLine(p.getRight() - kArrowWidth, (float) p.getY() + 6.0f, (float) p.getBottom() - 6.0f);
    g.setFont(juce::FontOptions(16.0f));
    g.setColour(theme::textDim());
    g.drawText(juce::String::fromUTF8("\xe2\x80\xb9"), p.withWidth(kArrowWidth), juce::Justification::centred);
    g.drawText(juce::String::fromUTF8("\xe2\x80\xba"), p.withTrimmedLeft(p.getWidth() - kArrowWidth), juce::Justification::centred);
    const auto name = p.reduced(kArrowWidth + 6, 0);
    g.setFont(theme::largeLabelFont().withHeight(12.5f));
    g.setColour(theme::text());
    g.drawText(processor_.getProgramName(program_), name.withTrimmedRight(14), juce::Justification::centred, true);
    g.setFont(juce::FontOptions(10.0f));
    g.setColour(theme::textDim());
    g.drawText(juce::String::fromUTF8("\xe2\x96\xbe"), name.withTrimmedLeft(name.getWidth() - 12), juce::Justification::centred);

    const auto s = kSetupField;
    drawField(g, s, "SETUP", setupOpen_);
    g.setFont(theme::labelFont().withHeight(12.0f));
    g.setColour(setupOpen_ ? theme::echo() : theme::text());
    g.drawText(juce::String::fromUTF8("Strings \xc2\xb7 Output \xc2\xb7 MIDI"), s.withTrimmedLeft(14), juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(10.0f));
    g.setColour(theme::textDim());
    g.drawText(juce::String::fromUTF8(setupOpen_ ? "\xe2\x96\xb4" : "\xe2\x96\xbe"), s.withTrimmedLeft(s.getWidth() - 30), juce::Justification::centred);
}

void HeaderBar::resized() {
    const auto p = kPresetField;
    previous_.setBounds(p.withWidth(kArrowWidth));
    next_.setBounds(p.withTrimmedLeft(p.getWidth() - kArrowWidth));
    preset_.setBounds(p.reduced(kArrowWidth, 0));
    setup_.setBounds(kSetupField);
}

} // namespace drosera
