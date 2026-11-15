#pragma once

#include "music/Types.h"

#include <memory>
#include <string>
#include <vector>

namespace tos
{

struct MidiDeviceStatus
{
    std::string name;
    std::string identifier;
    bool open = false;
    std::string error;
};

// A note played by the user on a hardware MIDI keyboard.
struct InputNote
{
    int pitch = 60;
    float velocity = 0.0f;
    bool on = false;
};

struct AudioStatus
{
    bool running = false;
    std::string deviceType;
    std::string deviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    double outputLatencyMs = 0.0;
    std::string error;
};

// Audio output, MIDI input, synthesis, effects and phrase playback.
// All methods must be called from the UI thread. JUCE stays behind the pimpl so this
// header is plain C++.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    // ---- devices
    bool startAudio();
    void stopAudio();
    AudioStatus audioStatus() const;
    std::vector<std::string> deviceTypes() const;
    std::vector<std::string> outputDevices() const;
    std::vector<int> bufferSizes() const;
    void setDeviceType(const std::string& type);
    void setOutputDevice(const std::string& name);
    void setBufferSize(int samples);

    // Opens new MIDI inputs, closes removed ones and retries busy ones. Call every frame;
    // it only rescans on hot-plug notifications or every two seconds.
    void pollMidiDevices();
    std::vector<MidiDeviceStatus> midiDevices() const;
    // Moves notes received from MIDI keyboards since the last call into `out`.
    void drainInputNotes(std::vector<InputNote>& out);

    // ---- playback
    // Replaces whatever phrase is playing. Returns an id that playingPhraseId() reports.
    int playPhrase(const std::vector<ScheduledNote>& notes, FxPreset fx);
    void stopPlayback();
    bool isPlaying() const;
    int playingPhraseId() const;
    double playbackPositionSec() const;

    // Notes from the mouse or computer keyboard (MIDI keyboards are routed internally).
    void liveNoteOn(int pitch, float velocity);
    void liveNoteOff(int pitch);
    void liveAllNotesOff();

    void setMasterVolume(float gain);
    float masterVolume() const;

    // ---- sounds
    // Lists .sf2 files from the assets and user folders.
    std::vector<std::string> availableSoundFonts() const;
    // Empty path selects the built-in modelled piano.
    bool setPianoSoundFont(const std::string& path, std::string& error);
    std::string pianoName() const;

    // ---- offline rendering (tests, no audio device)
    void prepareOffline(double sampleRate, int blockSize);
    void renderOffline(float* left, float* right, int numSamples);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace tos
