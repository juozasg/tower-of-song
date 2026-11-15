#include "game/Puzzle.h"

#include <algorithm>
#include <cmath>

namespace tos
{

Puzzle::Puzzle(const LevelDef& level, Question question) : levelDef(level), q(std::move(question))
{
    sections.resize((size_t) q.bars);
    for (int bar = 0; bar < q.bars; ++bar)
    {
        if (levelDef.hasChords())
            for (const auto& c : q.chords)
                if (q.barOf(c.startBeat) == bar)
                {
                    Slot s;
                    s.kind = SlotKind::Chord;
                    s.section = bar;
                    s.targetChord = c.chord;
                    slotList.push_back(s);
                }
        if (levelDef.hasMelody())
            for (const auto& n : q.melody)
                if (q.barOf(n.startBeat) == bar)
                {
                    Slot s;
                    s.kind = SlotKind::Melody;
                    s.section = bar;
                    s.targetPitch = n.pitch;
                    slotList.push_back(s);
                }
    }
}

std::vector<int> Puzzle::slotsIn(int s, SlotKind kind) const
{
    std::vector<int> result;
    for (size_t i = 0; i < slotList.size(); ++i)
        if (slotList[i].section == s && slotList[i].kind == kind)
            result.push_back((int) i);
    return result;
}

bool Puzzle::sectionSolved(int s) const
{
    for (const auto& slot : slotList)
        if (slot.section == s && slot.state != SlotState::Solved)
            return false;
    return true;
}

void Puzzle::selectSection(int s)
{
    selected = std::clamp(s, 0, q.bars - 1);
}

void Puzzle::setAnswerMode(bool on)
{
    if (answering == on)
        return;
    answering = on;
    chordGesture = false;
    chordNotes.clear();
    tentative.clear();
    emit("mode", -1, on ? "answer" : "explore");
}

// ------------------------------------------------------------------------- entering

void Puzzle::noteOn(int pitch)
{
    held.insert(pitch);

    if (! answering)
    {
        auto& sec = sections[(size_t) selected];
        ++sec.exploreNotes;
        ++statistics.exploreNotes;
        float factor = 1.0f;
        if (sec.exploreNotes > freeExploreNotes && ! sectionSolved(selected))
        {
            factor = exploreFactor;
            charge(selected, factor);
        }
        emit("explore", selected, noteName(pitch, q.flats()), factor);
        return;
    }

    ++statistics.answerNotes;
    if (held.size() == 1)
    {
        chordGesture = false;
        chordNotes.clear();
        tentative.clear();
    }

    if (levelDef.hasChords() && held.size() >= 3)
    {
        if (! chordGesture)
        {
            chordGesture = true;
            // Notes entered as melody that turned out to be part of this chord are taken back.
            for (const auto& [slot, p] : tentative)
                if (held.count(p) != 0 && slotList[(size_t) slot].state == SlotState::Entered)
                {
                    auto& s = slotList[(size_t) slot];
                    s.state = SlotState::Empty;
                    s.enteredLabel.clear();
                    s.enteredPitch = -1;
                    entryStack.erase(std::remove(entryStack.begin(), entryStack.end(), slot), entryStack.end());
                }
            tentative.clear();
        }
        chordNotes.insert(held.begin(), held.end());
        return;
    }

    if (chordGesture)
    {
        chordNotes.insert(pitch);
        return;
    }

    if (levelDef.hasMelody())
    {
        const int slot = nextMelodySlot();
        if (slot >= 0)
        {
            fillMelody(slot, pitch);
            tentative.emplace_back(slot, pitch);
        }
        else
        {
            lastMessage = "Every melody note is filled in. Press Check (Enter).";
        }
    }
}

void Puzzle::noteOff(int pitch)
{
    held.erase(pitch);
    if (! answering || ! held.empty())
        return;

    if (chordGesture)
    {
        const std::vector<int> notes(chordNotes.begin(), chordNotes.end());
        const uint16_t mask = pitchClassMask(notes);
        const int slot = chordSlotForEntry();
        if (countBits(mask) < 3)
            lastMessage = "A chord needs at least three different notes.";
        else if (slot < 0)
            lastMessage = "Every chord is filled in. Press Check (Enter).";
        else
        {
            const auto chord = identifyChord(notes);
            fillChord(slot, mask, chord ? chord->name(q.flats()) : "?");
        }
    }
    else if (! levelDef.hasMelody())
    {
        lastMessage = "Hold three or more notes together to enter a chord, or use the chord picker.";
    }

    chordGesture = false;
    chordNotes.clear();
    tentative.clear();
}

void Puzzle::enterChord(const Chord& chord)
{
    const int slot = chordSlotForEntry();
    if (slot < 0)
    {
        lastMessage = "Every chord is filled in. Press Check (Enter).";
        return;
    }
    fillChord(slot, chord.pitchClassMask(), chord.name(q.flats()));
}

void Puzzle::undoEntry()
{
    while (! entryStack.empty())
    {
        const int idx = entryStack.back();
        entryStack.pop_back();
        auto& s = slotList[(size_t) idx];
        if (s.state != SlotState::Entered)
            continue;
        s.state = SlotState::Empty;
        s.enteredLabel.clear();
        s.enteredPitch = -1;
        s.enteredMask = 0;
        selected = s.section;
        emit("undo", s.section, "");
        return;
    }
}

void Puzzle::clearEntries()
{
    for (auto& s : slotList)
        if (s.state == SlotState::Entered)
        {
            s.state = SlotState::Empty;
            s.enteredLabel.clear();
            s.enteredPitch = -1;
            s.enteredMask = 0;
        }
    entryStack.clear();
}

void Puzzle::fillMelody(int idx, int pitch)
{
    auto& s = slotList[(size_t) idx];
    s.state = SlotState::Entered;
    s.enteredPitch = pitch;
    s.enteredLabel = pitchClassName(pitch, q.flats());
    entryStack.push_back(idx);
    emit("enter", s.section, s.enteredLabel);
    advanceSelectionIfFull();
}

void Puzzle::fillChord(int idx, uint16_t mask, const std::string& label)
{
    auto& s = slotList[(size_t) idx];
    s.state = SlotState::Entered;
    s.enteredMask = mask;
    s.enteredLabel = label;
    entryStack.push_back(idx);
    emit("enter", s.section, label);
    advanceSelectionIfFull();
}

int Puzzle::nextMelodySlot() const
{
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < slotList.size(); ++i)
        {
            const auto& s = slotList[i];
            const bool inRange = pass == 0 ? s.section >= selected : s.section < selected;
            if (inRange && s.kind == SlotKind::Melody && (s.state == SlotState::Empty || s.state == SlotState::Wrong))
                return (int) i;
        }
    return -1;
}

int Puzzle::chordSlotForEntry() const
{
    for (int idx : slotsIn(selected, SlotKind::Chord))
        if (slotList[(size_t) idx].state != SlotState::Solved)
            return idx; // re-entering the selected bar replaces its chord

    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < slotList.size(); ++i)
        {
            const auto& s = slotList[i];
            const bool inRange = pass == 0 ? s.section > selected : s.section < selected;
            if (inRange && s.kind == SlotKind::Chord && (s.state == SlotState::Empty || s.state == SlotState::Wrong))
                return (int) i;
        }
    return -1;
}

void Puzzle::advanceSelectionIfFull()
{
    auto hasOpen = [&](int sec) {
        for (const auto& s : slotList)
            if (s.section == sec && (s.state == SlotState::Empty || s.state == SlotState::Wrong))
                return true;
        return false;
    };
    if (hasOpen(selected))
        return;
    for (int k = 1; k < q.bars; ++k)
    {
        const int sec = (selected + k) % q.bars;
        if (hasOpen(sec))
        {
            selected = sec;
            return;
        }
    }
}

// ------------------------------------------------------------------------- checking

CheckResult Puzzle::check()
{
    CheckResult r;
    std::vector<int> correct;
    std::set<int> wrongSections;

    for (size_t i = 0; i < slotList.size(); ++i)
    {
        auto& s = slotList[i];
        if (s.state != SlotState::Entered)
            continue;
        const bool ok = s.kind == SlotKind::Melody ? pitchClass(s.enteredPitch) == pitchClass(s.targetPitch)
                                                   : s.enteredMask == s.targetChord.pitchClassMask();
        if (ok)
            correct.push_back((int) i);
        else
        {
            s.state = SlotState::Wrong;
            ++s.wrongCount;
            ++r.wrong;
            ++statistics.wrongEntries;
            wrongSections.insert(s.section);
        }
    }

    if (correct.empty() && r.wrong == 0)
    {
        lastMessage = answering ? "Nothing to check yet. Play your answer first."
                                : "Nothing to check yet. Switch to Answer mode (Tab) and play your answer.";
        return r;
    }

    ++statistics.checks;
    const float bonus = std::min(maxChunkBonus, 1.0f + chunkBonusPerSlot * (float) (correct.size() - (correct.empty() ? 0 : 1)));
    for (int idx : correct)
    {
        auto& s = slotList[(size_t) idx];
        s.points = (int) std::lround(slotValue(s) * bonus);
        s.state = SlotState::Solved;
        r.points += s.points;
        ++r.correct;
    }
    for (int sec : wrongSections)
        charge(sec, wrongCheckFactor);

    entryStack.erase(std::remove_if(entryStack.begin(), entryStack.end(),
                                    [&](int idx) { return slotList[(size_t) idx].state != SlotState::Entered; }),
                     entryStack.end());

    r.complete = complete();
    if (r.complete)
        lastMessage = "Solved! +" + std::to_string(r.points);
    else if (r.wrong == 0)
        lastMessage = std::to_string(r.correct) + " correct, +" + std::to_string(r.points);
    else if (r.correct == 0)
        lastMessage = std::to_string(r.wrong) + " not right yet. Try again or take a hint.";
    else
        lastMessage = std::to_string(r.correct) + " correct (+" + std::to_string(r.points) + "), "
                      + std::to_string(r.wrong) + " not right yet.";

    emit("check", -1, "correct=" + std::to_string(r.correct) + " wrong=" + std::to_string(r.wrong), 1.0f, r.points);
    if (r.complete)
        emit("complete", -1, "score=" + std::to_string(score()), 1.0f, score());
    advanceSelectionIfFull();
    return r;
}

// ------------------------------------------------------------------------- hints

bool Puzzle::needsMet(HintNeeds needs) const
{
    const bool melody = levelDef.hasMelody() && ! q.melody.empty();
    const bool chords = levelDef.hasChords() && ! q.chords.empty();
    switch (needs)
    {
        case HintNeeds::Any: return true;
        case HintNeeds::Melody: return melody;
        case HintNeeds::Chord: return chords;
        case HintNeeds::Both: return melody && chords;
        case HintNeeds::Fx: return q.fx != FxPreset::Dry;
    }
    return false;
}

bool Puzzle::hintAvailable(HintId id, int s) const
{
    const auto& def = hintDef(id);
    if (! needsMet(def.needs) || s < 0 || s >= q.bars)
        return false;

    const auto& sec = sections[(size_t) s];
    const auto melodySlots = slotsIn(s, SlotKind::Melody);
    const auto chordSlots = slotsIn(s, SlotKind::Chord);
    auto anyUnsolved = [&](const std::vector<int>& idxs, bool unrevealed) {
        for (int i : idxs)
            if (slotList[(size_t) i].state != SlotState::Solved && ! (unrevealed && slotList[(size_t) i].revealed))
                return true;
        return false;
    };

    switch (id)
    {
        case HintId::HearBarMelody:
        case HintId::MelodyNoteByNote:
        case HintId::FirstNoteAlone:
            return ! melodySlots.empty();
        case HintId::HearBarChord:
        case HintId::ChordArpeggio:
        case HintId::ChordRoot:
        case HintId::ChordBuildUp:
            return ! chordSlots.empty();
        case HintId::ShowContour: return melodySlots.size() >= 2 && ! sec.showContour && anyUnsolved(melodySlots, false);
        case HintId::ShowIntervals: return melodySlots.size() >= 2 && ! sec.showIntervals && anyUnsolved(melodySlots, false);
        case HintId::ShowFirstNote: return ! melodySlots.empty() && ! sec.showFirstNote && anyUnsolved(melodySlots, false);
        case HintId::ShowRoot: return ! sec.showRoot && anyUnsolved(chordSlots, true);
        case HintId::ShowQuality: return ! sec.showQuality && anyUnsolved(chordSlots, true);
        case HintId::RevealNextNote: return anyUnsolved(melodySlots, true);
        case HintId::RevealChord: return anyUnsolved(chordSlots, true);
        default: return true;
    }
}

float Puzzle::hintNextCost(HintId id, int s) const
{
    if (sectionSolved(s))
        return 1.0f;
    const auto& def = hintDef(id);
    return sections[(size_t) s].uses[(size_t) id] == 0 ? def.firstCost : def.repeatCost;
}

std::optional<PlayOptions> Puzzle::useHint(HintId id, int s)
{
    if (! hintAvailable(id, s))
        return std::nullopt;

    const auto& def = hintDef(id);
    auto& sec = sections[(size_t) s];
    const float factor = hintNextCost(id, s);
    if (sec.uses[(size_t) id]++ == 0)
        ++statistics.hintsUsed;
    charge(s, factor);

    switch (id)
    {
        case HintId::ShowContour: sec.showContour = true; break;
        case HintId::ShowIntervals: sec.showIntervals = true; break;
        case HintId::ShowFirstNote: sec.showFirstNote = true; break;
        case HintId::ShowRoot: sec.showRoot = true; break;
        case HintId::ShowQuality: sec.showQuality = true; break;
        case HintId::RevealNextNote:
        {
            // Prefer a slot the player hasn't filled in; fall back to unchecked entries.
            int target = -1;
            for (int i : slotsIn(s, SlotKind::Melody))
            {
                const auto& slot = slotList[(size_t) i];
                if (slot.state == SlotState::Solved || slot.revealed)
                    continue;
                if (target < 0 || (slotList[(size_t) target].state == SlotState::Entered && slot.state != SlotState::Entered))
                    target = i;
                if (slot.state != SlotState::Entered)
                    break;
            }
            if (target >= 0)
                slotList[(size_t) target].revealed = true;
            break;
        }
        case HintId::RevealChord:
            for (int i : slotsIn(s, SlotKind::Chord))
                slotList[(size_t) i].revealed = true;
            break;
        default: break;
    }

    if (def.reveal)
        ++statistics.reveals;
    else
        ++statistics.barListens;
    emit("hint", s, def.label, factor);
    return hintPlayback(id, s);
}

std::optional<HintId> Puzzle::nextHint(int s) const
{
    for (const auto& def : hintTable())
        if (hintAvailable(def.id, s) && sections[(size_t) s].uses[(size_t) def.id] == 0)
            return def.id;
    for (const auto& def : hintTable())
        if (def.repeatable && hintAvailable(def.id, s))
            return def.id;
    return std::nullopt;
}

bool Puzzle::globalAvailable(GlobalListen id) const
{
    return needsMet(globalListenDef(id).needs);
}

float Puzzle::globalNextCost(GlobalListen id) const
{
    const auto& def = globalListenDef(id);
    const int uses = globalUseCounts[(size_t) id];
    if (uses < def.freeUses)
        return 1.0f;
    return uses == def.freeUses ? def.firstCost : def.repeatCost;
}

PlayOptions Puzzle::useGlobal(GlobalListen id)
{
    const float factor = globalNextCost(id);
    ++globalUseCounts[(size_t) id];
    if (factor < 1.0f)
        for (int s = 0; s < q.bars; ++s)
            charge(s, factor);
    ++statistics.globalListens;
    emit("listen", -1, globalListenDef(id).label, factor);
    return globalPlayback(id);
}

void Puzzle::charge(int s, float factor)
{
    if (factor >= 1.0f || sectionSolved(s))
        return;
    auto& m = sections[(size_t) s].multiplier;
    m = std::max(minMultiplier, m * factor);
}

// ------------------------------------------------------------------------- display

std::vector<int> Puzzle::melodyPitches(int s) const
{
    std::vector<int> result;
    for (int i : slotsIn(s, SlotKind::Melody))
        result.push_back(slotList[(size_t) i].targetPitch);
    return result;
}

std::string Puzzle::contourText(int s) const
{
    const auto p = melodyPitches(s);
    std::string text;
    for (size_t i = 1; i < p.size(); ++i)
    {
        if (! text.empty())
            text += ' ';
        text += p[i] > p[i - 1] ? "up" : (p[i] < p[i - 1] ? "down" : "same");
    }
    return text;
}

std::string Puzzle::intervalsText(int s) const
{
    const auto p = melodyPitches(s);
    std::string text;
    for (size_t i = 1; i < p.size(); ++i)
    {
        if (! text.empty())
            text += ' ';
        text += signedIntervalName(p[i] - p[i - 1]);
    }
    return text;
}

std::string Puzzle::firstNoteText(int s) const
{
    const auto p = melodyPitches(s);
    return p.empty() ? std::string() : pitchClassName(p.front(), q.flats());
}

// ------------------------------------------------------------------------- scoring

float Puzzle::slotValue(const Slot& slot) const
{
    return basePoints * (float) levelDef.difficulty * sections[(size_t) slot.section].multiplier;
}

int Puzzle::score() const
{
    int total = 0;
    for (const auto& s : slotList)
        total += s.points;
    return total;
}

int Puzzle::potentialRemaining() const
{
    float total = 0.0f;
    for (const auto& s : slotList)
        if (s.state != SlotState::Solved)
            total += slotValue(s);
    return (int) std::lround(total);
}

int Puzzle::perfectScore() const
{
    const auto n = (float) slotList.size();
    const float bonus = std::min(maxChunkBonus, 1.0f + chunkBonusPerSlot * std::max(0.0f, n - 1.0f));
    return (int) std::lround(basePoints * (float) levelDef.difficulty * n * bonus);
}

bool Puzzle::complete() const
{
    if (slotList.empty())
        return false;
    for (const auto& s : slotList)
        if (s.state != SlotState::Solved)
            return false;
    return true;
}

void Puzzle::emit(const std::string& type, int s, const std::string& detail, float factor, int points)
{
    if (onEvent)
        onEvent({ type, s, detail, factor, points });
}

} // namespace tos
