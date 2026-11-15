#pragma once

#include "music/MidiCatalog.h"
#include "music/Question.h"

#include <string>
#include <vector>

namespace tos
{

enum class LevelKind
{
    Melody,
    Chords,
    MelodyAndChords
};

struct LevelDef
{
    int id = 1;
    std::string title;       // "The Melody"
    std::string floor;       // "Ground floor"
    std::string description; // shown on the tower
    LevelKind kind = LevelKind::Melody;
    int bars = 2;
    double difficulty = 1.0; // multiplies every slot's points
    FxPreset fx = FxPreset::Room;
    bool useCatalog = false; // draw melodies from the MIDI catalog when it has phrases

    bool hasMelody() const { return kind != LevelKind::Chords; }
    bool hasChords() const { return kind != LevelKind::Melody; }
};

const std::vector<LevelDef>& allLevels();
const LevelDef* findLevel(int id);

Question makeQuestion(const LevelDef& level, uint32_t seed, const MidiCatalog* catalog);

} // namespace tos
