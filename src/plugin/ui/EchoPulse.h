#pragma once
// The dot beside Echo Time, blinking at the real time between repeats. No JUCE, so it can be tested alone.

#include <cmath>

namespace drosera::pulse {

constexpr double kFlashSeconds = 0.08;

enum class Lit { none, single, left, right };

/// With PONG the repeats bounce, so the dots take turns: left on even repeats, right on odd ones.
inline Lit echoPulse(double seconds, double periodSeconds, bool pong, bool sending) {
    if (!sending) return Lit::none;
    const double period = periodSeconds > 0.05 ? periodSeconds : 0.05;
    const double repeat = std::floor(seconds / period);
    if (seconds - repeat * period >= kFlashSeconds) return Lit::none;
    if (!pong) return Lit::single;
    return std::fmod(repeat, 2.0) == 0.0 ? Lit::left : Lit::right;
}

} // namespace drosera::pulse
