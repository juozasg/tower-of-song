#pragma once

#include "music/Arrange.h"

#include <optional>
#include <vector>

namespace tos
{

// Per-bar help, ordered from least to most helpful. Each costs a share of that bar's
// points; the cost table lives in Hints.cpp so it can be tuned in one place.
enum class HintId
{
    HearBar,
    HearBarDry,
    HearBarMelody,
    HearBarChord,
    HearBarSlow,
    MelodyNoteByNote,
    ChordArpeggio,
    ChordRoot,
    FirstNoteAlone,
    ChordBuildUp,
    ShowContour,
    ShowIntervals,
    ShowFirstNote,
    ShowRoot,
    ShowQuality,
    RevealNextNote,
    RevealChord,
    Count
};

constexpr int hintCount = (int) HintId::Count;

enum class HintNeeds
{
    Any,
    Melody,
    Chord,
    Both,
    Fx
};

struct HintDef
{
    HintId id;
    const char* label;
    const char* tip;
    float firstCost;  // multiplier applied to the bar's points on first use
    float repeatCost; // multiplier on each later use
    HintNeeds needs;
    bool reveal;      // shows information instead of playing something
    bool repeatable;  // reveals that can be used more than once (one more note each time)
};

const std::vector<HintDef>& hintTable();
const HintDef& hintDef(HintId id);
// Playback for a listening hint on one bar. Empty for reveals.
std::optional<PlayOptions> hintPlayback(HintId id, int section);

// Whole-phrase listening, available from the transport bar.
enum class GlobalListen
{
    PlayAll,
    MelodyOnly,
    ChordsOnly,
    RootsOnly,
    Slow,
    Count
};

constexpr int globalListenCount = (int) GlobalListen::Count;

struct GlobalListenDef
{
    GlobalListen id;
    const char* label;
    const char* tip;
    int freeUses;
    float firstCost; // applied to every unsolved bar
    float repeatCost;
    HintNeeds needs;
};

const std::vector<GlobalListenDef>& globalListenTable();
const GlobalListenDef& globalListenDef(GlobalListen id);
PlayOptions globalPlayback(GlobalListen id);

} // namespace tos
