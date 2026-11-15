#pragma once

#include "game/Hints.h"
#include "game/Levels.h"

#include <array>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace tos
{

enum class SlotKind
{
    Melody,
    Chord
};

enum class SlotState
{
    Empty,
    Entered, // filled in, not checked yet
    Wrong,   // checked and wrong; can be re-entered
    Solved
};

// One thing to identify: a melody note or a chord.
struct Slot
{
    SlotKind kind = SlotKind::Melody;
    int section = 0;
    int targetPitch = -1; // melody
    Chord targetChord;    // chord

    SlotState state = SlotState::Empty;
    int enteredPitch = -1;
    uint16_t enteredMask = 0;
    std::string enteredLabel;
    bool revealed = false; // the answer is shown as a ghost
    int wrongCount = 0;
    int points = 0;
};

struct SectionState
{
    float multiplier = 1.0f; // share of this bar's points still available
    int exploreNotes = 0;
    std::array<int, hintCount> uses {};
    bool showContour = false, showIntervals = false, showFirstNote = false, showRoot = false, showQuality = false;
};

struct PuzzleStats
{
    int globalListens = 0;
    int barListens = 0;
    int exploreNotes = 0;
    int answerNotes = 0;
    int hintsUsed = 0;
    int reveals = 0;
    int checks = 0;
    int wrongEntries = 0;
    double seconds = 0.0;
};

struct CheckResult
{
    int correct = 0;
    int wrong = 0;
    int points = 0;
    bool complete = false;
};

// Everything the player does is reported through Puzzle::onEvent for the event log.
struct GameEvent
{
    std::string type;
    int section = -1;
    std::string detail;
    float factor = 1.0f;
    int points = 0;
};

// The rules of one puzzle: entering answers, checking, hint costs and scoring.
// Independent of audio and UI so it can be unit tested.
class Puzzle
{
public:
    // Scoring constants. Points for a slot = basePoints x level difficulty x bar multiplier
    // x chunk bonus. The multiplier only shrinks with help and never drops below the floor,
    // so a correct answer always earns something.
    static constexpr float basePoints = 100.0f;
    static constexpr float minMultiplier = 0.05f;
    static constexpr float wrongCheckFactor = 0.9f;
    static constexpr float exploreFactor = 0.99f;
    static constexpr int freeExploreNotes = 12;
    static constexpr float chunkBonusPerSlot = 0.04f; // solving several slots in one check
    static constexpr float maxChunkBonus = 1.4f;

    Puzzle(const LevelDef& level, Question question);

    const LevelDef& level() const { return levelDef; }
    const Question& question() const { return q; }
    int sectionCount() const { return q.bars; }
    const std::vector<Slot>& slots() const { return slotList; }
    std::vector<int> slotsIn(int section, SlotKind kind) const;
    const SectionState& section(int s) const { return sections[(size_t) s]; }
    bool sectionSolved(int s) const;

    int selectedSection() const { return selected; }
    void selectSection(int s);

    bool answerMode() const { return answering; }
    void setAnswerMode(bool on);

    // Notes from the MIDI keyboard, mouse or computer keyboard. In explore mode they cost
    // a little after the free allowance; in answer mode they fill slots. Three or more
    // notes held together enter a chord.
    void noteOn(int pitch);
    void noteOff(int pitch);
    void enterChord(const Chord& chord); // chord picker
    void undoEntry();
    // Where the next melody note / chord would go (-1 if none or not in answer mode).
    int melodyCursor() const { return answering && levelDef.hasMelody() ? nextMelodySlot() : -1; }
    int chordCursor() const { return answering && levelDef.hasChords() ? chordSlotForEntry() : -1; }
    void clearEntries();
    CheckResult check();

    bool hintAvailable(HintId id, int s) const;
    float hintNextCost(HintId id, int s) const;
    std::optional<PlayOptions> useHint(HintId id, int s);
    std::optional<HintId> nextHint(int s) const;

    bool globalAvailable(GlobalListen id) const;
    float globalNextCost(GlobalListen id) const; // 1.0 = free
    int globalUses(GlobalListen id) const { return globalUseCounts[(size_t) id]; }
    PlayOptions useGlobal(GlobalListen id);

    std::string contourText(int s) const;
    std::string intervalsText(int s) const;
    std::string firstNoteText(int s) const;

    int score() const;
    int potentialRemaining() const;
    int perfectScore() const;
    bool complete() const;
    float slotValue(const Slot& slot) const;

    const PuzzleStats& stats() const { return statistics; }
    void addTime(double seconds) { statistics.seconds += seconds; }

    const std::string& message() const { return lastMessage; }

    std::function<void(const GameEvent&)> onEvent;

private:
    bool needsMet(HintNeeds needs) const;
    void charge(int s, float factor);
    void emit(const std::string& type, int s, const std::string& detail, float factor = 1.0f, int points = 0);
    int nextMelodySlot() const;
    int chordSlotForEntry() const;
    void fillMelody(int slot, int pitch);
    void fillChord(int slot, uint16_t mask, const std::string& label);
    void advanceSelectionIfFull();
    std::vector<int> melodyPitches(int s) const;

    LevelDef levelDef;
    Question q;
    std::vector<Slot> slotList;
    std::vector<SectionState> sections;
    std::array<int, globalListenCount> globalUseCounts {};
    PuzzleStats statistics;
    int selected = 0;
    bool answering = false;
    std::string lastMessage;

    // answer-mode gesture tracking
    std::set<int> held;
    std::set<int> chordNotes;
    bool chordGesture = false;
    std::vector<std::pair<int, int>> tentative; // (slot, pitch) filled during this gesture
    std::vector<int> entryStack;
};

} // namespace tos
