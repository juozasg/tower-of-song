#include "music/Theory.h"

#include <algorithm>
#include <array>

namespace tos
{

namespace
{
constexpr std::array<const char*, 12> sharpNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr std::array<const char*, 12> flatNames { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
constexpr std::array<int, 7> majorSteps { 0, 2, 4, 5, 7, 9, 11 };
} // namespace

bool keyPrefersFlats(int keyPc)
{
    switch (pitchClass(keyPc))
    {
        case 5: case 10: case 3: case 8: case 1: case 6:
            return true;
        default:
            return false;
    }
}

std::string pitchClassName(int pc, bool flats)
{
    return (flats ? flatNames : sharpNames)[(size_t) pitchClass(pc)];
}

std::string noteName(int midi, bool flats)
{
    return pitchClassName(midi, flats) + std::to_string(midi / 12 - 1);
}

const char* qualitySuffix(ChordQuality q)
{
    switch (q)
    {
        case ChordQuality::Major: return "";
        case ChordQuality::Minor: return "m";
        case ChordQuality::Diminished: return "dim";
        case ChordQuality::Augmented: return "aug";
        case ChordQuality::Sus2: return "sus2";
        case ChordQuality::Sus4: return "sus4";
        case ChordQuality::Dom7: return "7";
        case ChordQuality::Maj7: return "maj7";
        case ChordQuality::Min7: return "m7";
        case ChordQuality::HalfDim7: return "m7b5";
    }
    return "";
}

const char* qualityLongName(ChordQuality q)
{
    switch (q)
    {
        case ChordQuality::Major: return "major";
        case ChordQuality::Minor: return "minor";
        case ChordQuality::Diminished: return "diminished";
        case ChordQuality::Augmented: return "augmented";
        case ChordQuality::Sus2: return "suspended 2nd";
        case ChordQuality::Sus4: return "suspended 4th";
        case ChordQuality::Dom7: return "dominant 7th";
        case ChordQuality::Maj7: return "major 7th";
        case ChordQuality::Min7: return "minor 7th";
        case ChordQuality::HalfDim7: return "half-diminished";
    }
    return "";
}

const std::vector<int>& qualityIntervals(ChordQuality q)
{
    static const std::vector<int> major { 0, 4, 7 }, minor { 0, 3, 7 }, dim { 0, 3, 6 }, aug { 0, 4, 8 },
        sus2 { 0, 2, 7 }, sus4 { 0, 5, 7 }, dom7 { 0, 4, 7, 10 }, maj7 { 0, 4, 7, 11 }, min7 { 0, 3, 7, 10 },
        halfDim { 0, 3, 6, 10 };
    switch (q)
    {
        case ChordQuality::Major: return major;
        case ChordQuality::Minor: return minor;
        case ChordQuality::Diminished: return dim;
        case ChordQuality::Augmented: return aug;
        case ChordQuality::Sus2: return sus2;
        case ChordQuality::Sus4: return sus4;
        case ChordQuality::Dom7: return dom7;
        case ChordQuality::Maj7: return maj7;
        case ChordQuality::Min7: return min7;
        case ChordQuality::HalfDim7: return halfDim;
    }
    return major;
}

std::string Chord::name(bool flats) const
{
    return pitchClassName(root, flats) + qualitySuffix(quality);
}

uint16_t Chord::pitchClassMask() const
{
    uint16_t mask = 0;
    for (int iv : qualityIntervals(quality))
        mask |= (uint16_t) (1u << pitchClass(root + iv));
    return mask;
}

uint16_t pitchClassMask(const std::vector<int>& midiNotes)
{
    uint16_t mask = 0;
    for (int n : midiNotes)
        mask |= (uint16_t) (1u << pitchClass(n));
    return mask;
}

int countBits(uint16_t mask)
{
    int c = 0;
    for (; mask != 0; mask &= (uint16_t) (mask - 1))
        ++c;
    return c;
}

std::optional<Chord> identifyChord(const std::vector<int>& midiNotes)
{
    if (midiNotes.empty())
        return std::nullopt;
    const uint16_t mask = pitchClassMask(midiNotes);
    const int bass = pitchClass(*std::min_element(midiNotes.begin(), midiNotes.end()));

    std::optional<Chord> best;
    for (int root = 0; root < 12; ++root)
        for (auto q : allQualities)
        {
            const Chord c { root, q };
            if (c.pitchClassMask() != mask)
                continue;
            if (! best || root == bass)
                best = c;
        }
    return best;
}

std::string intervalName(int semitones)
{
    static constexpr std::array<const char*, 13> names { "P1", "m2", "M2", "m3", "M3", "P4", "TT",
                                                         "P5", "m6", "M6", "m7", "M7", "P8" };
    int s = std::abs(semitones);
    if (s <= 12)
        return names[(size_t) s];
    const int octaves = s / 12;
    s %= 12;
    return std::to_string(octaves) + "oct+" + names[(size_t) s];
}

std::string signedIntervalName(int semitones)
{
    if (semitones == 0)
        return "=";
    return (semitones > 0 ? "+" : "-") + intervalName(semitones);
}

int majorScalePitch(int keyPc, int degree, int octaveBase)
{
    const int octave = degree >= 0 ? degree / 7 : -((6 - degree) / 7);
    const int idx = degree - octave * 7;
    return octaveBase + pitchClass(keyPc) + majorSteps[(size_t) idx] + 12 * octave;
}

int scaleDegreeOf(int keyPc, int midi)
{
    const int rel = pitchClass(midi - keyPc);
    for (int i = 0; i < 7; ++i)
        if (majorSteps[(size_t) i] == rel)
            return i;
    return -1;
}

} // namespace tos
