#pragma once
// The sundew seen from above. Each tentacle is a frequency band: at rest it is short and curled, and the real sound
// unfurls it and swells its drop of dew. There is no invented motion, so a silent plugin shows a still, closed plant.
// Holds the Feedback knob at its heart.

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
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

    /// Neighbouring bands are spread around the leaf, so one partial lights tentacles on every side.
    static constexpr int tentacleBand(int tentacle) { return (tentacle * 23) % kNumBands; }
    /// The bands holding the sounding strings' notes get a ring on their drop. -1 is no string.
    void setVoiceBands(const std::array<int, 3>&);
    bool isMarked(int band) const { return std::find(voiceBands_.begin(), voiceBands_.end(), band) != voiceBands_.end(); }

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
    float pendingSeconds_ = 0.0f;  // time since the levels last moved

    /// Each tentacle's shape, fixed so the plant is the same every time.
    struct Tentacle { float angle, base, reach, curl, sway, width, drop; };
    std::array<Tentacle, kNumBands> tentacles_ {};
    std::array<int, 3> voiceBands_ {-1, -1, -1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlantView)
};

} // namespace drosera
