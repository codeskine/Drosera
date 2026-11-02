#pragma once
// Text for the controls whose knob position is not what sounds. No JUCE, so it can be tested alone.

#include <cmath>
#include <cstdio>
#include <string>

namespace drosera::readouts {

struct Text { std::string value, tag; };

inline std::string format(const char* pattern, double value) {
    char text[32];
    std::snprintf(text, sizeof(text), pattern, value);
    return text;
}

/// Nearest note, with middle C (60) as C4.
inline std::string noteName(float midiNote) {
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int note = (int) std::lround(midiNote);
    return names[((note % 12) + 12) % 12] + std::to_string(note / 12 - 1);
}

inline Text frequency(bool midi, float note, const std::string& knobText) {
    if (!midi) return {knobText, ""};
    return {noteName(note) + " " + format("%.1f Hz", 440.0 * std::pow(2.0, (note - 69.0) / 12.0)), "MIDI"};
}

/// A negative note is a voice with nothing to play.
inline Text interval(bool polyMidi, float note, const std::string& knobText) {
    if (!polyMidi) return {knobText, ""};
    return {note < 0.0f ? "-" : noteName(note), "MIDI"};
}

inline Text body(bool midi, float seconds, const std::string& knobText) {
    if (!midi) return {knobText, ""};
    return {format("%.1f ms", seconds * 1000.0), "SNAP"};
}

inline std::string echoTime(bool warp, float seconds, const std::string& knobText) {
    return warp ? format("%.2f s", seconds) : knobText;
}

/// Drive and Tone only shape the echo while PONG is on.
inline Text echoShaping(bool pong, const std::string& knobText) {
    return {knobText, pong ? "" : "PONG OFF"};
}

} // namespace drosera::readouts
