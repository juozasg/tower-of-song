#include "game/Levels.h"

#include "music/Generator.h"

#include <algorithm>

namespace tos
{

const std::vector<LevelDef>& allLevels()
{
    static const std::vector<LevelDef> levels {
        { 1, "The Melody", "Ground floor",
          "Two bars of melody on piano. Play it back note by note.",
          LevelKind::Melody, 2, 1.0, FxPreset::Room, true },
        { 2, "The Chords", "Second floor",
          "Four chords, one per bar. Name them or play them.",
          LevelKind::Chords, 4, 1.5, FxPreset::Room, false },
        { 3, "The Song", "The top of the tower",
          "Four bars of melody over four chords, in a big echoing hall. Work bar by bar if you need to.",
          LevelKind::MelodyAndChords, 4, 2.0, FxPreset::HallDelay, false },
    };
    return levels;
}

const LevelDef* findLevel(int id)
{
    for (const auto& l : allLevels())
        if (l.id == id)
            return &l;
    return nullptr;
}

namespace
{

bool fromCatalog(Question& q, std::mt19937& rng, const LevelDef& level, const MidiCatalog& catalog)
{
    auto phrase = catalog.randomPhrase(rng, level.bars);
    if (! phrase)
        return false;

    // Transpose into a comfortable range, with a random shift so songs don't repeat in one key.
    int low = 127, high = 0;
    for (const auto& n : phrase->notes)
    {
        low = std::min(low, n.pitch);
        high = std::max(high, n.pitch);
    }
    std::uniform_int_distribution<int> shiftDist(-5, 6);
    int shift = shiftDist(rng);
    while (low + shift < 60)
        shift += 12;
    while (high + shift > 84)
        shift -= 12;

    q.melody = phrase->notes;
    for (auto& n : q.melody)
        n.pitch += shift;
    q.bpm = phrase->bpm;
    q.beatsPerBar = phrase->beatsPerBar;
    q.keyPc = estimateMajorKey(q.melody);
    q.source = "catalog: " + phrase->source;
    return true;
}

} // namespace

Question makeQuestion(const LevelDef& level, uint32_t seed, const MidiCatalog* catalog)
{
    std::mt19937 rng(seed);
    Question q;
    q.seed = seed;
    q.bars = level.bars;
    q.fx = level.fx;

    static const std::vector<int> easyKeys { 0, 7, 5, 2 };
    static const std::vector<int> allKeys { 0, 7, 5, 2, 10, 9, 3 };
    const auto& keys = level.id == 1 ? easyKeys : allKeys;
    q.keyPc = keys[std::uniform_int_distribution<size_t>(0, keys.size() - 1)(rng)];

    if (level.useCatalog && catalog != nullptr && catalog->songCount() > 0
        && std::uniform_real_distribution<double>(0.0, 1.0)(rng) < 0.5 && fromCatalog(q, rng, level, *catalog))
        return q;

    switch (level.kind)
    {
        case LevelKind::Melody:
        {
            q.bpm = std::uniform_real_distribution<double>(84.0, 100.0)(rng);
            // The harmony is only implied: it shapes the melody but isn't played or graded.
            const auto implied = generateProgression(rng, q.keyPc, q.bars, q.beatsPerBar, false);
            q.melody = generateMelody(rng, q.keyPc, implied, q.beatsPerBar, { 64, 79, false });
            break;
        }
        case LevelKind::Chords:
            q.bpm = std::uniform_real_distribution<double>(72.0, 84.0)(rng);
            q.chords = generateProgression(rng, q.keyPc, q.bars, q.beatsPerBar, false);
            break;
        case LevelKind::MelodyAndChords:
            q.bpm = std::uniform_real_distribution<double>(80.0, 96.0)(rng);
            q.chords = generateProgression(rng, q.keyPc, q.bars, q.beatsPerBar, true);
            q.melody = generateMelody(rng, q.keyPc, q.chords, q.beatsPerBar, { 65, 81, true });
            break;
    }
    return q;
}

} // namespace tos
