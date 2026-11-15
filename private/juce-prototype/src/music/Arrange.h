#pragma once

#include "music/Question.h"

namespace tos
{

enum class ChordStyle
{
    Block,    // as written
    RootOnly, // just the root, synth bass, in rhythm
    Arpeggio, // slowly, one note at a time, then together
    BuildUp   // synth: root, root+3rd, root+3rd+5th(+7th)
};

enum class MelodyStyle
{
    Normal,
    NoteByNote,   // even spacing with gaps, rhythm removed
    FirstNoteOnly // the first note of each bar, held
};

struct PlayOptions
{
    bool melody = true;
    bool chords = true;
    int fromSection = 0;
    int toSection = -1; // inclusive; -1 = last
    double tempoScale = 1.0;
    bool dry = false;
    ChordStyle chordStyle = ChordStyle::Block;
    MelodyStyle melodyStyle = MelodyStyle::Normal;
};

// When each section sounds inside an arrangement (used to highlight the bar being played).
struct SectionSpan
{
    int section;
    double startSec, endSec;
};

struct Arrangement
{
    std::vector<ScheduledNote> notes;
    FxPreset fx = FxPreset::Room;
    std::vector<SectionSpan> spans;
    double lengthSec = 0.0;
};

Arrangement arrange(const Question& q, const PlayOptions& options);

} // namespace tos
