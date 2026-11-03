#pragma once
#ifndef INFS_VOICE_ALLOCATOR_H
#define INFS_VOICE_ALLOCATOR_H

#include <cstdint>

namespace infrasonic {

// Hands MIDI notes to three voices. Fixed arrays only: safe on the audio thread.
//
// A note takes the lowest never-used voice, then the voice released longest ago, then
// the held voice pressed longest ago; that voice's note waits and comes back, most
// recent first, when a voice is freed. Releasing a voice keeps its note, so a repeated
// note returns to the voice that still carries it.
class VoiceAllocator {

public:

    static constexpr int kVoices = 3, kNoNote = -1, kMaxWaiting = 16;

    VoiceAllocator() { Reset(); }

    void Reset()
    {
        for (int v = 0; v < kVoices; ++v) {
            note_[v] = kNoNote;
            held_[v] = false;
            stamp_[v] = 0;
        }
        waiting_count_ = 0;
        clock_ = 0;
    }

    void NoteOn(const int note)
    {
        // Already on a voice, held or released: that voice plays it
        for (int v = 0; v < kVoices; ++v) {
            if (note_[v] == note) {
                if (!held_[v]) Take(v, note);
                return;
            }
        }
        RemoveWaiting(note);

        int voice = -1;
        for (int v = 0; v < kVoices && voice < 0; ++v) {
            if (note_[v] == kNoNote) voice = v;
        }
        if (voice < 0) voice = Oldest(false);
        if (voice < 0) {
            voice = Oldest(true);
            PushWaiting(note_[voice]);
        }
        Take(voice, note);
    }

    void NoteOff(const int note)
    {
        for (int v = 0; v < kVoices; ++v) {
            if (held_[v] && note_[v] == note) {
                held_[v] = false;
                stamp_[v] = ++clock_;
                if (waiting_count_ > 0) Take(v, waiting_[--waiting_count_]);
                return;
            }
        }
        RemoveWaiting(note);
    }

    // Releases the voices, keeps their notes, drops waiting notes
    void AllNotesOff()
    {
        for (int v = 0; v < kVoices; ++v) {
            if (held_[v]) {
                held_[v] = false;
                stamp_[v] = ++clock_;
            }
        }
        waiting_count_ = 0;
    }

    // Starts over with the held notes, oldest first: the last three stay on the voices
    void Assign(const int* held, const int count)
    {
        Reset();
        for (int i = 0; i < count; ++i) NoteOn(held[i]);
    }

    int Note(const int voice) const { return note_[voice]; }
    bool Held(const int voice) const { return held_[voice]; }

private:

    void Take(const int voice, const int note)
    {
        note_[voice] = note;
        held_[voice] = true;
        stamp_[voice] = ++clock_;
    }

    // Voice with the smallest stamp among the held (or released) ones, -1 if none
    int Oldest(const bool held) const
    {
        int best = -1;
        for (int v = 0; v < kVoices; ++v) {
            if (held_[v] == held && (best < 0 || stamp_[v] < stamp_[best])) best = v;
        }
        return best;
    }

    void PushWaiting(const int note)
    {
        if (waiting_count_ == kMaxWaiting) RemoveWaitingAt(0);
        waiting_[waiting_count_++] = note;
    }

    void RemoveWaiting(const int note)
    {
        for (int i = 0; i < waiting_count_; ++i) {
            if (waiting_[i] == note) {
                RemoveWaitingAt(i);
                return;
            }
        }
    }

    void RemoveWaitingAt(const int index)
    {
        for (int i = index; i + 1 < waiting_count_; ++i) waiting_[i] = waiting_[i + 1];
        --waiting_count_;
    }

    int note_[kVoices];
    bool held_[kVoices];
    uint32_t stamp_[kVoices];
    int waiting_[kMaxWaiting];  // oldest first
    int waiting_count_;
    uint32_t clock_;
};

} // namespace infrasonic

#endif
