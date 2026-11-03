#pragma once
// What the editor needs from the processor, beyond its parameters.

#include "SpectrumTap.h"

class EditorBridge {
public:
    virtual ~EditorBridge() = default;
    virtual bool midiPitchActive() const = 0;
    virtual bool bodySnapped() const = 0;          // Body picks a multiple of the note's period
    virtual float soundingNote() const = 0;
    virtual bool polyMidiActive() const = 0;       // polyphony on and MIDI Pitch in control
    virtual float voiceNote(int voice) const = 0;  // negative when the voice has no note
    virtual float publishedNote(int voice) const = 0;  // the note the voice's loop is tuned to, MIDI or knobs
    virtual bool polyRunning() const = 0;          // POLY is on
    virtual float bodySeconds() const = 0;
    virtual float echoSeconds() const = 0;
    virtual double hostSampleRate() const = 0;  // 0 until prepared
    virtual drosera::SpectrumTap& spectrumTap() = 0;
    virtual float uiScale() const = 0;    // the window's scale, 0.75 to 1.5, as saved
    virtual void setUiScale(float) = 0;
};
