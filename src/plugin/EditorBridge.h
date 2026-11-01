#pragma once
// What the editor needs from the processor, beyond its parameters.

#include "SpectrumTap.h"

class EditorBridge {
public:
    virtual ~EditorBridge() = default;
    virtual bool midiPitchActive() const = 0;
    virtual float soundingNote() const = 0;
    virtual float bodySeconds() const = 0;
    virtual float echoSeconds() const = 0;
    virtual double hostSampleRate() const = 0;  // 0 until prepared
    virtual drosera::SpectrumTap& spectrumTap() = 0;
};
