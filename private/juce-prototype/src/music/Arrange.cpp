#include "music/Arrange.h"

#include <algorithm>

namespace tos
{

namespace
{

constexpr double preRoll = 0.05;

int padRoot(const Chord& c)
{
    int p = 36 + c.root;
    while (p < 43)
        p += 12;
    return p;
}

bool inBar(const Question& q, double beat, int bar)
{
    return beat >= q.barStart(bar) - 1.0e-6 && beat < q.barStart(bar + 1) - 1.0e-6;
}

} // namespace

Arrangement arrange(const Question& q, const PlayOptions& o)
{
    Arrangement a;
    a.fx = o.dry ? FxPreset::Dry : q.fx;

    const int from = std::clamp(o.fromSection, 0, q.bars - 1);
    const int to = o.toSection < 0 ? q.bars - 1 : std::clamp(o.toSection, from, q.bars - 1);

    const bool expandMelody = o.melody && o.melodyStyle != MelodyStyle::Normal && ! q.melody.empty();
    const bool expandChords = o.chords && (o.chordStyle == ChordStyle::Arpeggio || o.chordStyle == ChordStyle::BuildUp)
                              && ! q.chords.empty();

    auto add = [&](double start, double dur, int pitch, float vel, Instrument inst) {
        a.notes.push_back({ start, dur, pitch, vel, inst });
        a.lengthSec = std::max(a.lengthSec, start + dur);
    };

    if (! expandMelody && ! expandChords)
    {
        // Real time: everything in rhythm, optionally slowed down.
        const double spb = 60.0 / q.bpm / o.tempoScale;
        const double offsetBeats = q.barStart(from);
        auto toSec = [&](double beat) { return preRoll + (beat - offsetBeats) * spb; };

        for (int bar = from; bar <= to; ++bar)
            a.spans.push_back({ bar, toSec(q.barStart(bar)), toSec(q.barStart(bar + 1)) });

        if (o.melody)
            for (const auto& n : q.melody)
                for (int bar = from; bar <= to; ++bar)
                    if (inBar(q, n.startBeat, bar))
                        add(toSec(n.startBeat), n.lengthBeats * spb * 0.95, n.pitch, n.velocity, Instrument::Piano);

        if (o.chords)
            for (const auto& c : q.chords)
                for (int bar = from; bar <= to; ++bar)
                {
                    if (! inBar(q, c.startBeat, bar))
                        continue;
                    const double start = toSec(c.startBeat), dur = c.lengthBeats * spb * 0.98;
                    if (o.chordStyle == ChordStyle::RootOnly)
                        add(start, dur, padRoot(c.chord), 0.8f, Instrument::Pad);
                    else
                        for (size_t i = 0; i < c.voicing.size(); ++i)
                            add(start, dur, c.voicing[i], i == 0 ? 0.55f : 0.48f, Instrument::Piano);
                }

        a.lengthSec = std::max(a.lengthSec, a.spans.empty() ? 0.0 : a.spans.back().endSec);
        return a;
    }

    // Expanded: each section becomes a slow, rhythm-free demonstration.
    const double slow = 1.0 / std::max(0.25, o.tempoScale);
    double t = preRoll;
    for (int bar = from; bar <= to; ++bar)
    {
        const double sectionStart = t;

        if (expandChords)
            for (const auto& c : q.chords)
            {
                if (! inBar(q, c.startBeat, bar))
                    continue;
                if (o.chordStyle == ChordStyle::Arpeggio)
                {
                    auto notes = c.voicing;
                    std::sort(notes.begin(), notes.end());
                    const double step = 0.45 * slow;
                    const double holdStart = t + step * (double) notes.size();
                    for (size_t i = 0; i < notes.size(); ++i)
                        add(t + step * (double) i, holdStart - (t + step * (double) i) + 1.2 * slow, notes[i], 0.55f,
                            Instrument::Piano);
                    t = holdStart + 1.6 * slow;
                }
                else
                {
                    const int root = padRoot(c.chord);
                    const auto& ivs = qualityIntervals(c.chord.quality);
                    for (size_t stage = 0; stage < ivs.size(); ++stage)
                    {
                        const bool last = stage + 1 == ivs.size();
                        const double dur = (last ? 1.5 : 0.85) * slow;
                        for (size_t k = 0; k <= stage; ++k)
                            add(t, dur, root + ivs[k], 0.75f, Instrument::Pad);
                        t += dur + 0.15 * slow;
                    }
                    t += 0.3 * slow;
                }
            }

        if (expandMelody)
        {
            bool firstInBar = true;
            for (const auto& n : q.melody)
            {
                if (! inBar(q, n.startBeat, bar))
                    continue;
                if (o.melodyStyle == MelodyStyle::FirstNoteOnly)
                {
                    if (! firstInBar)
                        break;
                    add(t, 1.3 * slow, n.pitch, 0.8f, Instrument::Piano);
                    t += 1.6 * slow;
                }
                else
                {
                    add(t, 0.45 * slow, n.pitch, 0.8f, Instrument::Piano);
                    t += 0.65 * slow;
                }
                firstInBar = false;
            }
            t += 0.3 * slow;
        }

        a.spans.push_back({ bar, sectionStart, t });
    }
    a.lengthSec = std::max(a.lengthSec, t);
    return a;
}

} // namespace tos
