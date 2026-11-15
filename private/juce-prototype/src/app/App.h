#pragma once

#include "engine/AudioEngine.h"
#include "game/Progress.h"
#include "game/Puzzle.h"
#include "music/Arrange.h"
#include "music/MidiCatalog.h"
#include "ui/Theme.h"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <string>

namespace tos
{

struct AppOptions
{
    bool audio = true;
    int startLevel = 0; // jump straight into a level
    uint32_t seed = 0;  // 0 = random
    bool demo = false;  // pre-fill some answers and hints (for screenshots)
    std::string dataDir; // empty = the per-user data folder
};

enum class Screen
{
    Tower,
    Play
};

// Owns the engine, the save data and the current puzzle, and routes input between them.
class App
{
public:
    explicit App(const AppOptions& options);
    ~App();

    void handleEvent(const SDL_Event& e);
    void frame(double dt);

    // ---- used by the screens
    void startLevel(int levelId);
    void backToTower();
    void play(const PlayOptions& options);
    void stopPlayback();
    int playingSection() const; // -1 when nothing of the current puzzle is playing
    void check();
    void toggleAnswerMode();

    void userNoteOn(int pitch, float velocity);
    void userNoteOff(int pitch);

    void setSoundFont(const std::string& path);
    void saveSettings();

    AudioEngine audio;
    Progress progress;
    MidiCatalog catalog;
    ui::Fonts fonts;

    Screen screen = Screen::Tower;
    std::unique_ptr<Puzzle> puzzle;
    bool showCompletion = false;
    bool completionWasBest = false;
    int pickerRoot = 0;
    int pickerQuality = 0;
    std::string statusMessage;

    int keyboardOctave = 4;
    int qwertyBase() const { return 12 * (keyboardOctave + 1); }
    std::array<bool, 128> heldDisplay {};

private:
    void applyDemoState();

    AppOptions opts;
    Arrangement arrangement;
    int phraseId = 0;
    double autoPlayIn = -1.0;
    std::vector<InputNote> midiNotes;
    std::array<int, SDL_SCANCODE_COUNT> qwertyNotes {};
    std::array<bool, 128> heldByMidi {}, heldByUser {};
};

} // namespace tos
