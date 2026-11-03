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

/// With POLY from the knobs, a voice's note is held between the engine's limits, so a wide interval can sound narrower
/// than the knob says. A negative note is a voice with nothing to play.
inline Text playedInterval(bool polyRunning, float rootNote, float voiceNote, int knobSemitones, const std::string& knobText) {
    if (!polyRunning || voiceNote < 0.0f) return {knobText, ""};
    const long actual = std::lround(voiceNote - rootNote);
    if (actual == knobSemitones) return {knobText, ""};
    return {format("%+.0f st", (double) actual), "LIMIT"};
}

/// The sounding voices' notes, such as "C3 · G3 · C4". Negative notes are silent voices, left out.
inline std::string chord(const float* notes, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        if (notes[i] < 0.0f) continue;
        if (!out.empty()) out += " \xc2\xb7 ";
        out += noteName(notes[i]);
    }
    return out;
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
