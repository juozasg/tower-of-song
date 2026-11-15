#include "game/Hints.h"

namespace tos
{

const std::vector<HintDef>& hintTable()
{
    // clang-format off
    static const std::vector<HintDef> table {
        { HintId::HearBar,          "Hear this bar",               "Just this bar, as written.",                       0.96f, 0.995f, HintNeeds::Any,    false, false },
        { HintId::HearBarDry,       "Hear it dry",                 "This bar without reverb or echo.",                 0.97f, 0.995f, HintNeeds::Fx,     false, false },
        { HintId::HearBarMelody,    "Melody only",                 "This bar's melody without the chord.",             0.94f, 0.995f, HintNeeds::Both,   false, false },
        { HintId::HearBarChord,     "Chord only",                  "This bar's chord without the melody.",             0.94f, 0.995f, HintNeeds::Both,   false, false },
        { HintId::HearBarSlow,      "Slowly",                      "This bar at half speed.",                          0.92f, 0.995f, HintNeeds::Any,    false, false },
        { HintId::MelodyNoteByNote, "Note by note",                "Melody notes evenly spaced, rhythm removed.",      0.88f, 0.99f,  HintNeeds::Melody, false, false },
        { HintId::ChordArpeggio,    "Chord, one note at a time",   "The chord played slowly from the bottom up.",      0.88f, 0.99f,  HintNeeds::Chord,  false, false },
        { HintId::ChordRoot,        "Just the root",               "The chord's root on a synth bass.",                0.82f, 0.99f,  HintNeeds::Chord,  false, false },
        { HintId::FirstNoteAlone,   "First note alone",            "The first melody note of the bar, held.",          0.82f, 0.99f,  HintNeeds::Melody, false, false },
        { HintId::ChordBuildUp,     "Build the chord",             "Synth: root, then +3rd, then +5th (and 7th).",     0.78f, 0.99f,  HintNeeds::Chord,  false, false },
        { HintId::ShowContour,      "Show contour",                "Whether each note goes up, down or repeats.",      0.80f, 1.0f,   HintNeeds::Melody, true,  false },
        { HintId::ShowIntervals,    "Show intervals",              "The interval between each pair of notes.",         0.70f, 1.0f,   HintNeeds::Melody, true,  false },
        { HintId::ShowFirstNote,    "Show first note",             "The name of the bar's first note.",                0.70f, 1.0f,   HintNeeds::Melody, true,  false },
        { HintId::ShowRoot,         "Show chord root",             "The name of the chord's root.",                    0.65f, 1.0f,   HintNeeds::Chord,  true,  false },
        { HintId::ShowQuality,      "Show chord quality",          "Major, minor, diminished...",                      0.65f, 1.0f,   HintNeeds::Chord,  true,  false },
        { HintId::RevealNextNote,   "Reveal next note",            "Shows the next unsolved note. You still play it.", 0.60f, 0.60f,  HintNeeds::Melody, true,  true  },
        { HintId::RevealChord,      "Reveal the chord",            "Shows the chord name. You still enter it.",        0.45f, 1.0f,   HintNeeds::Chord,  true,  false },
    };
    // clang-format on
    return table;
}

const HintDef& hintDef(HintId id)
{
    return hintTable()[(size_t) id];
}

std::optional<PlayOptions> hintPlayback(HintId id, int section)
{
    PlayOptions o;
    o.fromSection = o.toSection = section;
    switch (id)
    {
        case HintId::HearBar: return o;
        case HintId::HearBarDry: o.dry = true; return o;
        case HintId::HearBarMelody: o.chords = false; return o;
        case HintId::HearBarChord: o.melody = false; return o;
        case HintId::HearBarSlow: o.tempoScale = 0.5; return o;
        case HintId::MelodyNoteByNote: o.chords = false; o.melodyStyle = MelodyStyle::NoteByNote; return o;
        case HintId::ChordArpeggio: o.melody = false; o.chordStyle = ChordStyle::Arpeggio; return o;
        case HintId::ChordRoot: o.melody = false; o.chordStyle = ChordStyle::RootOnly; return o;
        case HintId::FirstNoteAlone: o.chords = false; o.melodyStyle = MelodyStyle::FirstNoteOnly; return o;
        case HintId::ChordBuildUp: o.melody = false; o.chordStyle = ChordStyle::BuildUp; return o;
        default: return std::nullopt;
    }
}

const std::vector<GlobalListenDef>& globalListenTable()
{
    // clang-format off
    static const std::vector<GlobalListenDef> table {
        { GlobalListen::PlayAll,    "Play",        "The whole phrase. The first three plays are free.", 3, 0.98f, 0.98f,  HintNeeds::Any },
        { GlobalListen::MelodyOnly, "Melody only", "The whole melody without chords.",                 0, 0.97f, 0.99f,  HintNeeds::Both },
        { GlobalListen::ChordsOnly, "Chords only", "The whole progression without melody.",            0, 0.97f, 0.99f,  HintNeeds::Both },
        { GlobalListen::RootsOnly,  "Roots only",  "Just the root of each chord, on a synth bass.",    0, 0.93f, 0.99f,  HintNeeds::Chord },
        { GlobalListen::Slow,       "Slow",        "The whole phrase at half speed.",                  0, 0.95f, 0.99f,  HintNeeds::Any },
    };
    // clang-format on
    return table;
}

const GlobalListenDef& globalListenDef(GlobalListen id)
{
    return globalListenTable()[(size_t) id];
}

PlayOptions globalPlayback(GlobalListen id)
{
    PlayOptions o;
    switch (id)
    {
        case GlobalListen::PlayAll: break;
        case GlobalListen::MelodyOnly: o.chords = false; break;
        case GlobalListen::ChordsOnly: o.melody = false; break;
        case GlobalListen::RootsOnly: o.melody = false; o.chordStyle = ChordStyle::RootOnly; break;
        case GlobalListen::Slow: o.tempoScale = 0.5; break;
        case GlobalListen::Count: break;
    }
    return o;
}

} // namespace tos
