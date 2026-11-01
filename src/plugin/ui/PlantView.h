#pragma once
// The sundew seen from above. Each tentacle is a frequency band, and its drop of dew swells with the real sound: there is
// no invented motion, so a silent plugin shows a still plant at rest. Holds the Feedback knob at its heart.

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "SpectrumBands.h"
#include "SpectrumTap.h"

namespace drosera {

class PlantView final : public juce::Component {
public:
    PlantView();

    juce::Slider& feedback() { return feedback_; }
    void setFeedbackText(const juce::String&);

    /// Message thread: drains the tap and moves the levels on by dtSeconds. Repaints only if something changed.
    void update(SpectrumTap&, double sampleRate, float dtSeconds);

    float level(int band) const { return levels_[(size_t) band]; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kFftOrder = 11, kFftSize = 1 << kFftOrder;

    juce::Slider feedback_;
    juce::String feedbackText_;
    juce::dsp::FFT fft_ { kFftOrder };
    juce::dsp::WindowingFunction<float> window_ { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, kFftSize> history_ {};
    std::array<float, 2 * kFftSize> fftData_ {};
    std::array<float, kNumBands> levels_ {}, targets_ {};
    std::array<float, kNumBands> drawn_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlantView)
};

} // namespace drosera
