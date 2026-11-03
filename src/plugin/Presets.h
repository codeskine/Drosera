#pragma once
// Factory presets. All values are normalized (0..1) plugin parameter values, in the order of
// DroseraCore::Param, plus the plugin-only extras. Plain C++ so the offline tests can use it.

#include <array>
#include <cstddef>

namespace infrasonic {

struct Preset {
    const char* name;
    //          freq  fbgain body  lpf   hpf   revmix revdec esend etime efb   volume
    float knob[11];
    bool  echoHalve;
    float inputLevel;   // 0.5 = unity
    bool  midiPitch;
    bool  midiGate;
    bool  needsInput;   // silent without an audio input (feedback gain is nearly closed)
    bool  poly = false;
    float coupling = 0.0f;
    int   interval2 = 7;    // semitones above voice 0, when MIDI is not in control
    int   interval3 = 12;
    bool  stringLock = true;  // the knobs' voices sound the notes they show
    bool  echoPong = false;   // the echo's repeats bounce between left and right and saturate
    float echoDrive = 0.5f;
    float echoTone = 0.5f;
    float echoWidth = 1.0f;          // fully left and right
    float echoLevel = 30.0f / 36.0f; // 0 dB
};

inline constexpr std::array<Preset, 16> kFactoryPresets {{
    {"Default",
     {0.43f, 0.75f, 0.30f, 0.90f, 0.20f, 0.30f, 0.50f, 0.20f, 0.50f, 0.40f, 0.70f}, false, 0.5f, false, false, false},
    {"Sub Cellar",
     {0.08f, 0.80f, 0.75f, 0.45f, 0.10f, 0.35f, 0.70f, 0.15f, 0.60f, 0.35f, 0.75f}, false, 0.5f, false, false, false},
    {"Glass Wind",
     {0.80f, 0.70f, 0.15f, 1.00f, 0.35f, 0.45f, 0.65f, 0.30f, 0.40f, 0.45f, 0.65f}, false, 0.5f, false, false, false},
    {"Cathedral Bloom",
     {0.45f, 0.78f, 0.55f, 0.80f, 0.15f, 0.85f, 0.90f, 0.20f, 0.65f, 0.35f, 0.65f}, false, 0.5f, false, false, false},
    {"Echo Drift",
     {0.50f, 0.70f, 0.35f, 0.65f, 0.25f, 0.15f, 0.50f, 0.80f, 0.55f, 0.85f, 0.65f}, false, 0.5f, false, false, false},
    {"Doppler Void",
     {0.35f, 0.75f, 0.30f, 0.60f, 0.20f, 0.30f, 0.60f, 0.75f, 0.70f, 0.80f, 0.65f}, true, 0.5f, false, false, false},
    {"Dark Tide",
     {0.20f, 0.85f, 0.85f, 0.30f, 0.10f, 0.50f, 0.75f, 0.40f, 0.75f, 0.55f, 0.70f}, false, 0.5f, false, false, false},
    {"Razor Feedback",
     {0.65f, 0.95f, 0.10f, 1.00f, 0.30f, 0.10f, 0.40f, 0.10f, 0.30f, 0.30f, 0.80f}, false, 0.5f, false, false, false},
    {"Quiet Room",
     {0.30f, 0.62f, 0.50f, 0.60f, 0.30f, 0.50f, 0.50f, 0.00f, 0.50f, 0.00f, 0.60f}, false, 0.5f, false, false, false},
    {"Keys (play notes)",
     {0.40f, 0.75f, 0.30f, 0.80f, 0.20f, 0.35f, 0.60f, 0.20f, 0.50f, 0.30f, 0.70f}, false, 0.5f, true, true, false},
    {"Resonator (needs input)",
     {0.50f, 0.40f, 0.45f, 0.85f, 0.20f, 0.35f, 0.60f, 0.20f, 0.50f, 0.30f, 0.70f}, false, 0.6f, true, false, true},
    {"Init (silent)",
     {0.43f, 0.00f, 0.30f, 0.90f, 0.20f, 0.00f, 0.50f, 0.00f, 0.50f, 0.00f, 0.70f}, false, 0.5f, false, false, true},
    {"Open Fifths",
     {0.40f, 0.75f, 0.30f, 0.85f, 0.15f, 0.35f, 0.60f, 0.20f, 0.50f, 0.35f, 0.70f}, false, 0.5f, false, false, false, true, 0.0f, 7, 12},
    {"Sympathetic",
     {0.20f, 0.78f, 0.40f, 0.70f, 0.12f, 0.45f, 0.70f, 0.15f, 0.60f, 0.30f, 0.70f}, false, 0.5f, false, false, false, true, 0.4f, 7, 16, false},
    {"Chord Keys",
     {0.40f, 0.75f, 0.30f, 0.80f, 0.20f, 0.10f, 0.60f, 0.20f, 0.50f, 0.30f, 0.70f}, false, 0.5f, true, true, false, true, 0.8f},
    {"Ember Pong",
     {0.35f, 0.75f, 0.35f, 0.75f, 0.15f, 0.20f, 0.55f, 0.55f, 0.45f, 0.60f, 0.65f}, false, 0.5f, false, false, false, false, 0.0f, 7, 12, true, true, 0.70f, 0.60f},
}};

} // namespace infrasonic
