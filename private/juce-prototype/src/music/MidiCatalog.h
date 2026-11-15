#pragma once

#include "music/Types.h"

#include <optional>
#include <random>
#include <string>
#include <vector>

namespace tos
{

struct CatalogPhrase
{
    std::string source;
    std::vector<NoteEvent> notes; // beats from the start of the phrase
    double bpm = 100.0;
    int beatsPerBar = 4;
    int bars = 2;
};

// Loads MIDI files, picks the melody line of each song (highest, mostly monophonic
// track, reduced to its top voice) and cuts it into bar-aligned phrases.
class MidiCatalog
{
public:
    int loadFolder(const std::string& dir);
    bool loadFile(const std::string& path, std::string& error);

    size_t songCount() const { return songs.size(); }
    size_t phraseCount(int bars) const;
    std::optional<CatalogPhrase> randomPhrase(std::mt19937& rng, int bars) const;

private:
    struct Song
    {
        std::string name;
        std::vector<NoteEvent> melody;
        double bpm = 120.0;
        int beatsPerBar = 4;
    };

    std::vector<CatalogPhrase> phrasesOf(const Song& song, int bars) const;

    std::vector<Song> songs;
};

} // namespace tos
