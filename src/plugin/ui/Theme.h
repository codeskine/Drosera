#pragma once
// The interface's colours and fonts. Red marks what sits inside the feedback loop, amber what colours its output.

#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

namespace drosera::theme {

inline juce::Colour backgroundCentre() { return juce::Colour(0xff1e1416); }
inline juce::Colour backgroundEdge()   { return juce::Colour(0xff0f0a0b); }
inline juce::Colour panel()            { return juce::Colour(0xff1b1416); }  // the setup panel and menus
inline juce::Colour loop()             { return juce::Colour(0xffd0465c); }
inline juce::Colour loopDeep()         { return juce::Colour(0xffa33244); }  // tentacle bases
inline juce::Colour echo()             { return juce::Colour(0xffffc27a); }
inline juce::Colour track()            { return juce::Colour(0xff2a1f21); }
inline juce::Colour text()             { return juce::Colour(0xffeadacd); }
inline juce::Colour textDim()          { return juce::Colour(0xff9d8a83); }
inline juce::Colour hairline()         { return juce::Colour(0xff33272a); }
inline juce::Colour cartouche()        { return juce::Colour(0xff3d2f31); }

namespace detail {
inline juce::Typeface::Ptr load(const char* data, int size) { return juce::Typeface::createSystemTypefaceFor(data, (size_t) size); }
inline juce::Typeface::Ptr cormorant() { static auto t = load(BinaryData::CormorantGaramondMedium_otf, BinaryData::CormorantGaramondMedium_otfSize); return t; }
inline juce::Typeface::Ptr interRegular() { static auto t = load(BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize); return t; }
inline juce::Typeface::Ptr interMedium() { static auto t = load(BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize); return t; }

/// A missing typeface falls back to JUCE's default sans-serif at the same size, so the layout holds.
inline juce::Font font(const juce::Typeface::Ptr& typeface, float height, float spacingPx = 0.0f) {
    auto options = typeface != nullptr ? juce::FontOptions(typeface).withHeight(height) : juce::FontOptions(height);
    return juce::Font(options.withKerningFactor(spacingPx / height));
}
} // namespace detail

inline bool fontsEmbedded() {
    return detail::cormorant() != nullptr && detail::interRegular() != nullptr && detail::interMedium() != nullptr;
}

inline juce::Font titleFont()      { return detail::font(detail::cormorant(), 26.0f, 9.0f); }
inline juce::Font largeLabelFont() { return detail::font(detail::interMedium(), 14.0f); }
inline juce::Font labelFont()      { return detail::font(detail::interMedium(), 11.0f); }
inline juce::Font largeValueFont() { return detail::font(detail::interRegular(), 12.0f); }
inline juce::Font valueFont()      { return detail::font(detail::interRegular(), 10.0f); }
inline juce::Font captionFont()    { return detail::font(detail::interMedium(), 9.5f, 3.0f); }
inline juce::Font tagFont()        { return detail::font(detail::interMedium(), 9.0f, 1.0f); }

} // namespace drosera::theme
