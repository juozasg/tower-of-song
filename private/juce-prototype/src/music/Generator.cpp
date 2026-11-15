#include "music/Generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace tos
{

namespace
{

struct Degree
{
    int step;
    ChordQuality quality;
    const char* roman;
};

constexpr std::array<Degree, 7> diatonic { {
    { 0, ChordQuality::Major, "I" },
    { 1, ChordQuality::Minor, "ii" },
    { 2, ChordQuality::Minor, "iii" },
    { 3, ChordQuality::Major, "IV" },
    { 4, ChordQuality::Major, "V" },
    { 5, ChordQuality::Minor, "vi" },
    { 6, ChordQuality::Diminished, "vii\xC2\xB0" },
} };

const std::vector<std::vector<int>>& progressionsFor(int bars)
{
    static const std::vector<std::vector<int>> four {
        { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 5, 3, 0, 4 }, { 0, 3, 4, 0 }, { 1, 4, 0, 5 },
        { 0, 3, 5, 4 }, { 0, 2, 3, 4 }, { 3, 4, 0, 0 }, { 0, 3, 0, 4 }, { 5, 1, 4, 0 },
    };
    static const std::vector<std::vector<int>> two { { 0, 4 }, { 0, 3 }, { 4, 0 }, { 3, 0 }, { 0, 0 } };
    return bars <= 2 ? two : four;
}

template <typename T>
const T& pick(std::mt19937& rng, const std::vector<T>& items)
{
    std::uniform_int_distribution<size_t> d(0, items.size() - 1);
    return items[d(rng)];
}

int weightedPick(std::mt19937& rng, const std::vector<double>& weights)
{
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    if (total <= 0.0)
        return 0;
    std::uniform_real_distribution<double> d(0.0, total);
    double r = d(rng);
    for (size_t i = 0; i < weights.size(); ++i)
    {
        r -= weights[i];
        if (r <= 0.0)
            return (int) i;
    }
    return (int) weights.size() - 1;
}

const ChordEvent& chordAt(const std::vector<ChordEvent>& harmony, double beat)
{
    for (const auto& c : harmony)
        if (beat >= c.startBeat - 1.0e-6 && beat < c.startBeat + c.lengthBeats - 1.0e-6)
            return c;
    return harmony.back();
}

bool isChordTone(const Chord& c, int pitch)
{
    return (c.pitchClassMask() >> pitchClass(pitch)) & 1u;
}

} // namespace

std::vector<int> voiceChord(const Chord& chord, const std::vector<int>& previous)
{
    int bass = 36 + chord.root;
    if (bass < 40)
        bass += 12;

    const auto& ivs = qualityIntervals(chord.quality);
    const double target = previous.size() > 1
                              ? std::accumulate(previous.begin() + 1, previous.end(), 0.0) / (double) (previous.size() - 1)
                              : 60.0;

    std::vector<int> best;
    double bestDistance = 1.0e9;
    for (size_t inversion = 0; inversion < ivs.size(); ++inversion)
    {
        for (int start = 52; start < 64; ++start)
        {
            if (pitchClass(start) != pitchClass(chord.root + ivs[inversion]))
                continue;
            std::vector<int> upper { start };
            for (size_t k = 1; k < ivs.size(); ++k)
            {
                const int pc = pitchClass(chord.root + ivs[(inversion + k) % ivs.size()]);
                int p = upper.back() + 1;
                while (pitchClass(p) != pc)
                    ++p;
                upper.push_back(p);
            }
            if (upper.back() > 70)
                continue;
            const double mean = std::accumulate(upper.begin(), upper.end(), 0.0) / (double) upper.size();
            const double distance = std::abs(mean - target);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = upper;
            }
        }
    }

    std::vector<int> voicing { bass };
    voicing.insert(voicing.end(), best.begin(), best.end());
    return voicing;
}

std::vector<ChordEvent> generateProgression(std::mt19937& rng, int keyPc, int bars, int beatsPerBar,
                                            bool allowSevenths)
{
    const auto& steps = pick(rng, progressionsFor(bars));
    std::vector<ChordEvent> result;
    std::vector<int> previous;
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& deg = diatonic[(size_t) steps[(size_t) bar % steps.size()]];
        ChordEvent ev;
        ev.chord.root = pitchClass(keyPc + majorScalePitch(0, deg.step, 0));
        ev.chord.quality = deg.quality;
        ev.roman = deg.roman;
        if (allowSevenths && deg.step == 4 && coin(rng) < 0.35)
        {
            ev.chord.quality = ChordQuality::Dom7;
            ev.roman = "V7";
        }
        ev.startBeat = bar * (double) beatsPerBar;
        ev.lengthBeats = beatsPerBar;
        ev.voicing = voiceChord(ev.chord, previous);
        previous = ev.voicing;
        result.push_back(ev);
    }
    return result;
}

std::vector<NoteEvent> generateMelody(std::mt19937& rng, int keyPc, const std::vector<ChordEvent>& harmony,
                                      int beatsPerBar, const MelodyOptions& options)
{
    using Pattern = std::vector<double>;
    static const std::vector<Pattern> easy { { 1, 1, 2 }, { 2, 1, 1 }, { 1, 1, 1, 1 }, { 1, 2, 1 } };
    static const std::vector<Pattern> easyEnd { { 1, 1, 2 }, { 2, 2 }, { 1, 1, 2 } };
    static const std::vector<Pattern> medium { { 1, 1, 1, 1 }, { 2, 1, 1 }, { 1, 1, 2 }, { 0.5, 0.5, 1, 2 },
                                               { 1, 0.5, 0.5, 2 }, { 1.5, 0.5, 2 }, { 1, 1, 0.5, 0.5, 1 } };
    static const std::vector<Pattern> mediumEnd { { 1, 1, 2 }, { 2, 2 }, { 1.5, 0.5, 2 } };

    const int bars = harmony.empty() ? 1 : (int) harmony.size();
    std::vector<NoteEvent> melody;
    std::uniform_real_distribution<double> jitter(-0.04, 0.04);

    // Work in scale degrees relative to the tonic of octave 4 (MIDI 60 + key).
    const int tonicBase = 60 + pitchClass(keyPc) - (pitchClass(keyPc) > 6 ? 12 : 0);
    auto pitchOf = [&](int degree) { return majorScalePitch(0, degree, tonicBase); };

    int lowDeg = -14, highDeg = 21;
    while (pitchOf(lowDeg) < options.lowPitch)
        ++lowDeg;
    while (pitchOf(highDeg) > options.highPitch)
        --highDeg;

    // Start on a chord tone of the first chord near the middle of the range.
    const int middle = (options.lowPitch + options.highPitch) / 2;
    int current = lowDeg;
    {
        int bestDistance = 1000;
        for (int d = lowDeg; d <= highDeg; ++d)
            if (isChordTone(harmony.front().chord, pitchOf(d)) && std::abs(pitchOf(d) - middle) < bestDistance)
            {
                bestDistance = std::abs(pitchOf(d) - middle);
                current = d;
            }
    }

    bool first = true;
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool lastBar = bar == bars - 1;
        const auto& patterns = options.allowEighths ? (lastBar ? mediumEnd : medium) : (lastBar ? easyEnd : easy);
        const auto& pattern = pick(rng, patterns);

        double beatInBar = 0.0;
        for (size_t i = 0; i < pattern.size(); ++i)
        {
            const double beat = bar * (double) beatsPerBar + beatInBar;
            const auto& chord = chordAt(harmony, beat).chord;
            const bool strong = std::fmod(beatInBar, 2.0) < 1.0e-6;
            const bool lastNote = lastBar && i == pattern.size() - 1;

            if (! first)
            {
                std::vector<int> candidates;
                std::vector<double> weights;
                for (int d = std::max(lowDeg, current - 4); d <= std::min(highDeg, current + 4); ++d)
                {
                    const int pitch = pitchOf(d);
                    const int dist = std::abs(d - current);
                    double w = 0.0;
                    if (strong || lastNote)
                    {
                        if (! isChordTone(chord, pitch))
                            continue;
                        w = std::exp(-0.6 * dist) * (dist == 0 ? 0.5 : 1.0);
                        if (lastNote && pitchClass(pitch) == pitchClass(keyPc))
                            w *= 4.0;
                    }
                    else
                    {
                        static constexpr double stepWeights[] = { 0.2, 1.0, 0.4, 0.15, 0.05 };
                        w = stepWeights[dist];
                    }
                    candidates.push_back(d);
                    weights.push_back(w);
                }
                if (! candidates.empty())
                    current = candidates[(size_t) weightedPick(rng, weights)];
            }
            first = false;

            NoteEvent n;
            n.startBeat = beat;
            n.lengthBeats = pattern[i];
            n.pitch = pitchOf(current);
            n.velocity = (float) (0.76 + (strong ? 0.06 : 0.0) + jitter(rng));
            melody.push_back(n);
            beatInBar += pattern[i];
        }
    }
    return melody;
}

int estimateMajorKey(const std::vector<NoteEvent>& notes)
{
    if (notes.empty())
        return 0;
    std::array<double, 12> weight {};
    for (const auto& n : notes)
        weight[(size_t) pitchClass(n.pitch)] += n.lengthBeats;

    int bestKey = 0;
    double bestScore = -1.0;
    for (int key = 0; key < 12; ++key)
    {
        double score = 0.0;
        for (int pc = 0; pc < 12; ++pc)
            if (scaleDegreeOf(key, pc) >= 0)
                score += weight[(size_t) pc];
        if (pitchClass(notes.back().pitch) == key)
            score += 0.5;
        if (pitchClass(notes.front().pitch) == key)
            score += 0.25;
        if (score > bestScore)
        {
            bestScore = score;
            bestKey = key;
        }
    }
    return bestKey;
}

} // namespace tos
