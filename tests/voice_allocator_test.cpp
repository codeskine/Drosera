// Checks the MIDI note allocator for the three-voice mode.
#include <cstdio>
#include <initializer_list>
#include "VoiceAllocator.h"

using infrasonic::VoiceAllocator;

static bool gOk = true;

static bool voices(const VoiceAllocator& a, int n0, int n1, int n2) {
    return a.Note(0) == n0 && a.Note(1) == n1 && a.Note(2) == n2;
}

static void report(const char* name, bool pass) {
    printf("%-24s %s\n", name, pass ? "OK" : "FAIL");
    gOk &= pass;
}

static void on(VoiceAllocator& a, std::initializer_list<int> notes) { for (int n : notes) a.NoteOn(n); }
static void off(VoiceAllocator& a, std::initializer_list<int> notes) { for (int n : notes) a.NoteOff(n); }

int main() {
    { VoiceAllocator a; on(a, {60});
      report("firstNotes", voices(a, 60, -1, -1) && a.Held(0)); }
    { VoiceAllocator a; on(a, {60, 64, 67});
      report("freeVoiceFirst", voices(a, 60, 64, 67)); }
    { VoiceAllocator a; on(a, {60, 64, 67, 71});
      report("stealsOldestHeld", voices(a, 71, 64, 67)); }
    { VoiceAllocator a; on(a, {60, 64, 67}); off(a, {64}); on(a, {71});
      report("releasedBeforeHeld", voices(a, 60, 71, 67)); }
    { VoiceAllocator a; on(a, {60, 64, 67}); off(a, {67, 60}); on(a, {71});
      report("longestReleased", voices(a, 60, 64, 71)); }
    { VoiceAllocator a; on(a, {60, 64, 67, 71}); off(a, {67});
      report("waitingResumes", voices(a, 71, 64, 60) && a.Held(2)); }
    { VoiceAllocator a; on(a, {60, 62, 64, 65, 67}); off(a, {65});
      report("mostRecentWaitingFirst", voices(a, 62, 67, 64) && a.Held(0)); }
    { VoiceAllocator a; on(a, {60, 64, 60});
      report("repeatedNoteKeepsVoice", voices(a, 60, 64, -1)); }
    { VoiceAllocator a; on(a, {60, 64}); off(a, {60}); on(a, {60});
      report("releasedNoteReturns", voices(a, 60, 64, -1) && a.Held(0)); }
    { VoiceAllocator a; off(a, {60});
      report("offWithoutOn", voices(a, -1, -1, -1)); }
    { VoiceAllocator a; on(a, {60, 64, 67, 71}); off(a, {60});
      bool pass = voices(a, 71, 64, 67) && a.Held(0);
      off(a, {71});
      report("offOfWaiting", pass && voices(a, 71, 64, 67) && !a.Held(0) && a.Held(1)); }
    { VoiceAllocator a; on(a, {60, 64}); a.AllNotesOff();
      report("allNotesOff", voices(a, 60, 64, -1) && !a.Held(0) && !a.Held(1)); }
    { VoiceAllocator a; for (int n = 20; n < 40; ++n) a.NoteOn(n);
      for (int n = 20; n < 40; ++n) a.NoteOff(n);
      report("overflow", !a.Held(0) && !a.Held(1) && !a.Held(2)); }
    { VoiceAllocator a; const int held[] = {60, 62, 64, 65}; a.Assign(held, 4);
      bool pass = voices(a, 65, 62, 64) && a.Held(0) && a.Held(1) && a.Held(2);
      off(a, {65});
      report("assign", pass && a.Note(0) == 60 && a.Held(0)); }

    printf("%s\n", gOk ? "voice allocator: OK" : "voice allocator: FAIL");
    return gOk ? 0 : 1;
}
