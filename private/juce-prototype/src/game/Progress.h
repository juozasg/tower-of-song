#pragma once

#include "game/Puzzle.h"

#include <map>
#include <string>

namespace tos
{

struct LevelProgress
{
    int best = 0;
    int plays = 0;
    int completions = 0;
    long long totalScore = 0;
};

struct Settings
{
    float volume = 0.8f;
    std::string soundFont; // empty = built-in piano
    int keyboardOctave = 4;
};

// Best scores and settings (progress.json) plus an append-only log of every action
// (events.jsonl), both in the user data folder.
class Progress
{
public:
    explicit Progress(std::string folder);

    void load();
    void save() const;

    LevelProgress level(int id) const;
    void recordStart(int levelId);
    bool recordCompletion(int levelId, int score); // true when it's a new best

    Settings settings;

    void logEvent(int levelId, uint32_t seed, const GameEvent& event);
    const std::string& folder() const { return dir; }

private:
    std::string dir;
    std::map<int, LevelProgress> levels;
};

} // namespace tos
