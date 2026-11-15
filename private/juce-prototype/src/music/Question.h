#pragma once

#include "music/Theory.h"
#include "music/Types.h"

#include <string>
#include <vector>

namespace tos
{

struct ChordEvent
{
    Chord chord;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    std::vector<int> voicing; // bass first, then upper notes, ascending
    std::string roman;        // "vi", "V7", ...
};

// One transcription puzzle: a melody and/or a chord progression, one section per bar.
struct Question
{
    uint32_t seed = 0;
    int keyPc = 0;
    double bpm = 90.0;
    int beatsPerBar = 4;
    int bars = 4;
    std::vector<NoteEvent> melody;
    std::vector<ChordEvent> chords;
    FxPreset fx = FxPreset::Room;
    std::string source = "generated";

    bool flats() const { return keyPrefersFlats(keyPc); }
    std::string keyName() const { return pitchClassName(keyPc, flats()) + " major"; }
    double barStart(int bar) const { return bar * (double) beatsPerBar; }
    int barOf(double beat) const { return (int) (beat / beatsPerBar + 1.0e-6); }
};

} // namespace tos
