#pragma once

#include <cstdint>

namespace tos
{

// MIDI channels double as instrument selectors inside the engine.
enum class Instrument : int
{
    Piano = 1, // default voice for questions and for the player's keyboard
    Pad   = 2, // warm synth used for roots and harmonic hints
    Bell  = 3  // clean sine-like tone used for interval hints
};

enum class FxPreset : int
{
    Dry,       // no reverb, no delay
    Room,      // small room
    HallDelay  // big hall plus echo: makes questions harder to hear
};

// A note in musical time, as stored in questions.
struct NoteEvent
{
    double startBeat = 0.0;
    double lengthBeats = 1.0;
    int pitch = 60;
    float velocity = 0.8f;
};

// A note in clock time, as sent to the audio engine.
struct ScheduledNote
{
    double startSec = 0.0;
    double durSec = 0.5;
    int pitch = 60;
    float velocity = 0.8f;
    Instrument instrument = Instrument::Piano;
};

} // namespace tos
