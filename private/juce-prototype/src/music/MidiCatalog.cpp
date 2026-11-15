#include "music/MidiCatalog.h"

#include "game/Paths.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>

namespace tos
{

namespace
{

std::vector<NoteEvent> notesOfTrack(const juce::MidiMessageSequence& track, double ticksPerQuarter)
{
    std::vector<NoteEvent> notes;
    for (int i = 0; i < track.getNumEvents(); ++i)
    {
        const auto* ev = track.getEventPointer(i);
        if (! ev->message.isNoteOn() || ev->message.getChannel() == 10)
            continue;
        NoteEvent n;
        n.startBeat = ev->message.getTimeStamp() / ticksPerQuarter;
        const double end = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() / ticksPerQuarter
                                                        : n.startBeat + 0.5;
        n.lengthBeats = std::max(0.05, end - n.startBeat);
        n.pitch = ev->message.getNoteNumber();
        n.velocity = ev->message.getFloatVelocity();
        notes.push_back(n);
    }
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.pitch > b.pitch;
    });
    return notes;
}

// Fraction of notes that start while another note is still sounding.
double polyphony(const std::vector<NoteEvent>& notes)
{
    if (notes.size() < 2)
        return 0.0;
    int overlapping = 0;
    double latestEnd = -1.0;
    for (const auto& n : notes)
    {
        if (n.startBeat < latestEnd - 0.05)
            ++overlapping;
        latestEnd = std::max(latestEnd, n.startBeat + n.lengthBeats);
    }
    return overlapping / (double) notes.size();
}

// Keeps the highest note of each onset and stops each note where the next one starts.
std::vector<NoteEvent> skyline(const std::vector<NoteEvent>& notes)
{
    std::vector<NoteEvent> top;
    for (const auto& n : notes)
    {
        if (! top.empty() && std::abs(n.startBeat - top.back().startBeat) < 0.03)
        {
            if (n.pitch > top.back().pitch)
                top.back() = n;
            continue;
        }
        top.push_back(n);
    }
    for (size_t i = 0; i + 1 < top.size(); ++i)
        top[i].lengthBeats = std::min(top[i].lengthBeats, top[i + 1].startBeat - top[i].startBeat);
    return top;
}

} // namespace

int MidiCatalog::loadFolder(const std::string& dir)
{
    int loaded = 0;
    for (const auto& path : paths::listFiles(dir, { ".mid", ".midi" }))
    {
        std::string error;
        if (loadFile(path, error))
            ++loaded;
    }
    return loaded;
}

bool MidiCatalog::loadFile(const std::string& path, std::string& error)
{
    juce::File file(path);
    juce::FileInputStream stream(file);
    if (! stream.openedOk())
    {
        error = "cannot open";
        return false;
    }

    juce::MidiFile midi;
    if (! midi.readFrom(stream, true))
    {
        error = "not a MIDI file";
        return false;
    }
    if (midi.getTimeFormat() <= 0)
    {
        error = "SMPTE timing is not supported";
        return false;
    }
    const double tpq = midi.getTimeFormat();

    Song song;
    song.name = file.getFileNameWithoutExtension().toStdString();

    bool haveTempo = false, haveSignature = false;
    double bestScore = -1.0e9;
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence track(*midi.getTrack(t));
        track.updateMatchedPairs();

        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto& m = track.getEventPointer(i)->message;
            if (! haveTempo && m.isTempoMetaEvent())
            {
                song.bpm = 60.0 / m.getTempoSecondsPerQuarterNote();
                haveTempo = true;
            }
            if (! haveSignature && m.isTimeSignatureMetaEvent())
            {
                int num = 4, den = 4;
                m.getTimeSignatureInfo(num, den);
                song.beatsPerBar = std::max(1, (int) std::lround(num * 4.0 / den));
                haveSignature = true;
            }
        }

        auto notes = notesOfTrack(track, tpq);
        if (notes.size() < 8)
            continue;
        double meanPitch = 0.0;
        for (const auto& n : notes)
            meanPitch += n.pitch;
        meanPitch /= (double) notes.size();
        const double score = meanPitch - 30.0 * polyphony(notes) + std::min<double>((double) notes.size(), 200.0) * 0.02;
        if (score > bestScore)
        {
            bestScore = score;
            song.melody = skyline(notes);
        }
    }

    if (song.melody.size() < 4)
    {
        error = "no melody track found";
        return false;
    }
    song.bpm = std::clamp(song.bpm, 50.0, 160.0);
    songs.push_back(std::move(song));
    return true;
}

std::vector<CatalogPhrase> MidiCatalog::phrasesOf(const Song& song, int bars) const
{
    std::vector<CatalogPhrase> result;
    const double barLength = song.beatsPerBar;
    const double songEnd = song.melody.back().startBeat + song.melody.back().lengthBeats;

    for (int startBar = 0; startBar * barLength < songEnd; ++startBar)
    {
        const double from = startBar * barLength, to = from + bars * barLength;
        CatalogPhrase phrase;
        phrase.source = song.name + " (bar " + std::to_string(startBar + 1) + ")";
        phrase.bpm = song.bpm;
        phrase.beatsPerBar = song.beatsPerBar;
        phrase.bars = bars;

        int low = 127, high = 0;
        for (const auto& n : song.melody)
        {
            if (n.startBeat < from - 1.0e-6 || n.startBeat >= to - 1.0e-6)
                continue;
            NoteEvent rebased = n;
            rebased.startBeat -= from;
            rebased.lengthBeats = std::min(rebased.lengthBeats, bars * barLength - rebased.startBeat);
            phrase.notes.push_back(rebased);
            low = std::min(low, n.pitch);
            high = std::max(high, n.pitch);
        }

        const auto count = (int) phrase.notes.size();
        if (count < 3 || count > bars * 8 || high - low > 19 || phrase.notes.front().startBeat > 1.0)
            continue;
        result.push_back(std::move(phrase));
    }
    return result;
}

size_t MidiCatalog::phraseCount(int bars) const
{
    size_t total = 0;
    for (const auto& s : songs)
        total += phrasesOf(s, bars).size();
    return total;
}

std::optional<CatalogPhrase> MidiCatalog::randomPhrase(std::mt19937& rng, int bars) const
{
    std::vector<CatalogPhrase> all;
    for (const auto& s : songs)
    {
        auto p = phrasesOf(s, bars);
        all.insert(all.end(), std::make_move_iterator(p.begin()), std::make_move_iterator(p.end()));
    }
    if (all.empty())
        return std::nullopt;
    std::uniform_int_distribution<size_t> d(0, all.size() - 1);
    return all[d(rng)];
}

} // namespace tos
