#pragma once

#include "music/Question.h"

#include <random>

namespace tos
{

struct MelodyOptions
{
    int lowPitch = 64;
    int highPitch = 81;
    bool allowEighths = false;
};

// A diatonic progression in `keyPc`, one chord per bar, voiced with smooth voice leading.
std::vector<ChordEvent> generateProgression(std::mt19937& rng, int keyPc, int bars, int beatsPerBar,
                                            bool allowSevenths);

// A singable melody over `harmony`: chord tones on strong beats, steps in between,
// ending on a chord tone (the tonic when the last chord is I).
std::vector<NoteEvent> generateMelody(std::mt19937& rng, int keyPc, const std::vector<ChordEvent>& harmony,
                                      int beatsPerBar, const MelodyOptions& options);

// Close-position voicing above a bass root, kept near `previous` (empty = centred).
std::vector<int> voiceChord(const Chord& chord, const std::vector<int>& previous);

// Most likely major key for a set of notes (duration-weighted, tie broken by the last note).
int estimateMajorKey(const std::vector<NoteEvent>& notes);

} // namespace tos
