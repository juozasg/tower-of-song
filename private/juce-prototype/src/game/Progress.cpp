#include "game/Progress.h"

#include <juce_core/juce_core.h>

namespace tos
{

Progress::Progress(std::string folder) : dir(std::move(folder)) {}

void Progress::load()
{
    const auto file = juce::File(dir).getChildFile("progress.json");
    if (! file.existsAsFile())
        return;

    const auto json = juce::JSON::parse(file);
    if (auto* levelsObj = json["levels"].getDynamicObject())
        for (const auto& prop : levelsObj->getProperties())
        {
            LevelProgress p;
            p.best = prop.value["best"];
            p.plays = prop.value["plays"];
            p.completions = prop.value["completions"];
            p.totalScore = (juce::int64) prop.value["totalScore"];
            levels[prop.name.toString().getIntValue()] = p;
        }

    const auto s = json["settings"];
    if (s.isObject())
    {
        settings.volume = (float) (double) s.getProperty("volume", settings.volume);
        settings.soundFont = s.getProperty("soundFont", "").toString().toStdString();
        settings.keyboardOctave = s.getProperty("keyboardOctave", settings.keyboardOctave);
    }
}

void Progress::save() const
{
    auto* root = new juce::DynamicObject();
    auto* levelsObj = new juce::DynamicObject();
    for (const auto& [id, p] : levels)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("best", p.best);
        o->setProperty("plays", p.plays);
        o->setProperty("completions", p.completions);
        o->setProperty("totalScore", (juce::int64) p.totalScore);
        levelsObj->setProperty(juce::String(id), juce::var(o));
    }
    root->setProperty("version", 1);
    root->setProperty("levels", juce::var(levelsObj));

    auto* s = new juce::DynamicObject();
    s->setProperty("volume", settings.volume);
    s->setProperty("soundFont", juce::String(settings.soundFont));
    s->setProperty("keyboardOctave", settings.keyboardOctave);
    root->setProperty("settings", juce::var(s));

    const auto file = juce::File(dir).getChildFile("progress.json");
    file.replaceWithText(juce::JSON::toString(juce::var(root)));
}

LevelProgress Progress::level(int id) const
{
    const auto it = levels.find(id);
    return it != levels.end() ? it->second : LevelProgress {};
}

void Progress::recordStart(int levelId)
{
    ++levels[levelId].plays;
    save();
}

bool Progress::recordCompletion(int levelId, int score)
{
    auto& p = levels[levelId];
    ++p.completions;
    p.totalScore += score;
    const bool newBest = score > p.best;
    if (newBest)
        p.best = score;
    save();
    return newBest;
}

void Progress::logEvent(int levelId, uint32_t seed, const GameEvent& event)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("t", juce::Time::getCurrentTime().toISO8601(true));
    o->setProperty("level", levelId);
    o->setProperty("seed", (juce::int64) seed);
    o->setProperty("type", juce::String(event.type));
    if (event.section >= 0)
        o->setProperty("bar", event.section + 1);
    if (! event.detail.empty())
        o->setProperty("detail", juce::String(event.detail));
    if (event.factor < 1.0f)
        o->setProperty("factor", event.factor);
    if (event.points != 0)
        o->setProperty("points", event.points);

    const auto line = juce::JSON::toString(juce::var(o), juce::JSON::FormatOptions {}.withSpacing(juce::JSON::Spacing::none));
    juce::File(dir).getChildFile("events.jsonl").appendText(line + "\n");
}

} // namespace tos
