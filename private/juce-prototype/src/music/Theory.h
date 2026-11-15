#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tos
{

inline int pitchClass(int midi) { return ((midi % 12) + 12) % 12; }

// Key-aware spelling: flat keys (F, Bb, Eb, ...) spell black keys as flats.
bool keyPrefersFlats(int keyPc);
std::string pitchClassName(int pc, bool flats = false);
std::string noteName(int midi, bool flats = false); // "C#4"

enum class ChordQuality
{
    Major,
    Minor,
    Diminished,
    Augmented,
    Sus2,
    Sus4,
    Dom7,
    Maj7,
    Min7,
    HalfDim7
};

constexpr ChordQuality allQualities[] = { ChordQuality::Major, ChordQuality::Minor, ChordQuality::Diminished,
                                          ChordQuality::Augmented, ChordQuality::Sus2, ChordQuality::Sus4,
                                          ChordQuality::Dom7, ChordQuality::Maj7, ChordQuality::Min7,
                                          ChordQuality::HalfDim7 };

const char* qualitySuffix(ChordQuality q);   // "", "m", "dim", ...
const char* qualityLongName(ChordQuality q); // "major", "minor", ...
const std::vector<int>& qualityIntervals(ChordQuality q);

struct Chord
{
    int root = 0; // pitch class
    ChordQuality quality = ChordQuality::Major;

    std::string name(bool flats = false) const;
    uint16_t pitchClassMask() const;
    bool operator==(const Chord& o) const { return root == o.root && quality == o.quality; }
};

uint16_t pitchClassMask(const std::vector<int>& midiNotes);
int countBits(uint16_t mask);

// Names the chord formed by the notes (any octave, any inversion). Prefers the bass note
// as root when several spellings fit (C6 vs Am7).
std::optional<Chord> identifyChord(const std::vector<int>& midiNotes);

// "m3", "P5", ... for 0..12 semitones; larger intervals are reduced into one octave
// and prefixed with the octave count.
std::string intervalName(int semitones);
// "+M3", "-m2", "=" for a melodic step.
std::string signedIntervalName(int semitones);

// Major-scale helpers. degree 0..6, may be negative or > 6 (wraps octaves).
int majorScalePitch(int keyPc, int degree, int octaveBase);
int scaleDegreeOf(int keyPc, int midi); // -1 if not diatonic

} // namespace tos
