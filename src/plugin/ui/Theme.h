#pragma once
// Rugiada's colours and fonts.

#include <juce_gui_basics/juce_gui_basics.h>

namespace drosera::theme {

inline juce::Colour background() { return juce::Colour(0xff120a10); }
inline juce::Colour panel()      { return juce::Colour(0xff1c0d15); }
inline juce::Colour stalk()      { return juce::Colour(0xffa33244); }
inline juce::Colour dew()        { return juce::Colour(0xffffc27a); }
inline juce::Colour knob()       { return juce::Colour(0xffc98a6a); }
inline juce::Colour track()      { return juce::Colour(0xff3a1a24); }
inline juce::Colour text()       { return juce::Colour(0xffe9d6c8); }
inline juce::Colour textDim()    { return juce::Colour(0xffa98f88); }
inline juce::Colour heart()      { return juce::Colour(0xff4a1320); }

inline juce::Font labelFont()   { return juce::FontOptions(12.0f); }
inline juce::Font valueFont()   { return juce::FontOptions(11.0f); }
inline juce::Font tagFont()     { return juce::FontOptions(9.5f, juce::Font::bold); }
inline juce::Font headFont()    { return juce::FontOptions(11.0f).withKerningFactor(0.2f); }
inline juce::Font titleFont()   { return juce::FontOptions("Georgia", 22.0f, juce::Font::plain).withKerningFactor(0.32f); }

} // namespace drosera::theme
