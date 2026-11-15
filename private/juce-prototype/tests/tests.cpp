// Minimal self-contained test runner: no framework, exits non-zero on failure.

#include "engine/AudioEngine.h"
#include "engine/Platform.h"
#include "game/Levels.h"
#include "game/Puzzle.h"
#include "music/Arrange.h"
#include "music/Generator.h"
#include "music/MidiCatalog.h"
#include "music/Theory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace tos;

namespace
{
int failures = 0, checks = 0;

#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        ++checks;                                                                \
        if (! (cond))                                                            \
        {                                                                        \
            ++failures;                                                          \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                        \
    } while (0)

void run(const char* name, const std::function<void()>& fn)
{
    const int before = failures;
    fn();
    std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", name);
}

void playAnswer(Puzzle& p, const std::vector<int>& pitches)
{
    for (int pitch : pitches)
    {
        p.noteOn(pitch);
        p.noteOff(pitch);
    }
}

void playChord(Puzzle& p, const std::vector<int>& pitches)
{
    for (int pitch : pitches)
        p.noteOn(pitch);
    for (int pitch : pitches)
        p.noteOff(pitch);
}

std::vector<int> targetPitches(const Puzzle& p, int section)
{
    std::vector<int> result;
    for (int i : p.slotsIn(section, SlotKind::Melody))
        result.push_back(p.slots()[(size_t) i].targetPitch);
    return result;
}

// Writes a one-track Standard MIDI File with quarter notes at 120 bpm.
void writeMidi(const std::string& path, const std::vector<int>& pitches)
{
    std::vector<uint8_t> track;
    auto varLen = [&](uint32_t v) {
        uint8_t bytes[4];
        int n = 0;
        do
        {
            bytes[n++] = (uint8_t) (v & 0x7F);
            v >>= 7;
        } while (v != 0);
        while (n-- > 0)
            track.push_back((uint8_t) (bytes[n] | (n > 0 ? 0x80 : 0)));
    };
    track.insert(track.end(), { 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 });       // tempo 120
    track.insert(track.end(), { 0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 }); // 4/4
    for (int p : pitches)
    {
        varLen(0);
        track.insert(track.end(), { 0x90, (uint8_t) p, 100 });
        varLen(470);
        track.insert(track.end(), { 0x80, (uint8_t) p, 0 });
        varLen(10);
        track.insert(track.end(), { 0xB0, 0x07, 100 }); // filler event to carry the gap
    }
    track.insert(track.end(), { 0x00, 0xFF, 0x2F, 0x00 });

    std::ofstream f(path, std::ios::binary);
    const uint8_t header[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0 };
    f.write((const char*) header, sizeof(header));
    const uint32_t len = (uint32_t) track.size();
    const uint8_t trackHeader[] = { 'M', 'T', 'r', 'k', (uint8_t) (len >> 24), (uint8_t) (len >> 16),
                                    (uint8_t) (len >> 8), (uint8_t) len };
    f.write((const char*) trackHeader, sizeof(trackHeader));
    f.write((const char*) track.data(), (std::streamsize) track.size());
}
// Renders a level 3 puzzle plus the synth hint sounds to a 16-bit WAV, to hear the engine.
int renderWav(const std::string& path)
{
    AudioEngine engine;
    const int sr = 48000;
    engine.prepareOffline(sr, 512);
    const auto q = makeQuestion(*findLevel(3), 4, nullptr);
    auto notes = arrange(q, {}).notes;
    PlayOptions build;
    build.melody = false;
    build.chordStyle = ChordStyle::BuildUp;
    build.fromSection = build.toSection = 0;
    const double offset = arrange(q, {}).lengthSec + 1.5;
    for (auto n : arrange(q, build).notes)
    {
        n.startSec += offset;
        notes.push_back(n);
    }
    engine.playPhrase(notes, FxPreset::HallDelay);

    const int total = sr * 16;
    std::vector<float> l((size_t) total), r((size_t) total);
    engine.renderOffline(l.data(), r.data(), total);

    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write((const char*) &v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*) &v, 2); };
    f.write("RIFF", 4);
    u32(36 + (uint32_t) total * 4);
    f.write("WAVEfmt ", 8);
    u32(16), u16(1), u16(2), u32(sr), u32((uint32_t) sr * 4), u16(4), u16(16);
    f.write("data", 4);
    u32((uint32_t) total * 4);
    for (int i = 0; i < total; ++i)
        for (float x : { l[(size_t) i], r[(size_t) i] })
            u16((uint16_t) (int16_t) std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
    std::printf("wrote %s\n", path.c_str());
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    platformInit();
    if (argc == 3 && std::string(argv[1]) == "--wav")
    {
        const int result = renderWav(argv[2]);
        platformShutdown();
        return result;
    }

    run("theory: names and intervals", [] {
        CHECK(noteName(60) == "C4");
        CHECK(noteName(61, true) == "Db4");
        CHECK(intervalName(7) == "P5");
        CHECK(signedIntervalName(-1) == "-m2");
        CHECK(signedIntervalName(4) == "+M3");
        CHECK(majorScalePitch(7, 2, 60) == 71); // G major, 3rd degree = B4
        CHECK(scaleDegreeOf(2, 66) == 2);       // F# is the 3rd of D major
    });

    run("theory: chord identification", [] {
        auto c = identifyChord({ 60, 64, 67 });
        CHECK(c && c->root == 0 && c->quality == ChordQuality::Major);
        c = identifyChord({ 57, 60, 64 });
        CHECK(c && c->name() == "Am");
        c = identifyChord({ 52, 60, 67 }); // first inversion C
        CHECK(c && c->name() == "C");
        c = identifyChord({ 52, 55, 59, 62 }); // Em7, not G6, because E is in the bass
        CHECK(c && c->name() == "Em7");
        c = identifyChord({ 55, 59, 62, 65 });
        CHECK(c && c->name() == "G7");
        CHECK(! identifyChord({ 60, 61, 62 }));
    });

    run("generator: questions for every level stay in bounds", [] {
        for (const auto& level : allLevels())
            for (uint32_t seed = 1; seed <= 300; ++seed)
            {
                const auto q = makeQuestion(level, seed, nullptr);
                CHECK(q.bars == level.bars);
                if (level.hasMelody())
                {
                    CHECK(q.melody.size() >= (size_t) level.bars * 2);
                    for (const auto& n : q.melody)
                    {
                        CHECK(n.pitch >= 60 && n.pitch <= 84);
                        CHECK(scaleDegreeOf(q.keyPc, n.pitch) >= 0);
                        CHECK(n.startBeat >= 0.0 && n.startBeat < q.bars * q.beatsPerBar);
                    }
                    for (int bar = 0; bar < q.bars; ++bar)
                    {
                        int inBar = 0;
                        for (const auto& n : q.melody)
                            inBar += q.barOf(n.startBeat) == bar;
                        CHECK(inBar >= 2);
                    }
                }
                else
                {
                    CHECK(q.melody.empty());
                }
                if (level.hasChords())
                {
                    CHECK(q.chords.size() == (size_t) level.bars);
                    for (const auto& c : q.chords)
                    {
                        CHECK(pitchClassMask(c.voicing) == c.chord.pitchClassMask());
                        CHECK(pitchClass(c.voicing.front()) == c.chord.root);
                        CHECK(std::is_sorted(c.voicing.begin(), c.voicing.end()));
                    }
                }
            }
    });

    run("generator: same seed, same question", [] {
        const auto& level = *findLevel(3);
        const auto a = makeQuestion(level, 42, nullptr), b = makeQuestion(level, 42, nullptr);
        CHECK(a.melody.size() == b.melody.size());
        CHECK(a.keyPc == b.keyPc);
        for (size_t i = 0; i < a.melody.size() && i < b.melody.size(); ++i)
            CHECK(a.melody[i].pitch == b.melody[i].pitch);
    });

    run("arrange: full phrase, single bars and slow styles", [] {
        const auto q = makeQuestion(*findLevel(3), 7, nullptr);
        size_t chordNotes = 0;
        for (const auto& c : q.chords)
            chordNotes += c.voicing.size();

        const auto all = arrange(q, {});
        CHECK(all.notes.size() == q.melody.size() + chordNotes);
        CHECK(all.spans.size() == 4);
        CHECK(all.fx == FxPreset::HallDelay);

        PlayOptions bar2;
        bar2.fromSection = bar2.toSection = 1;
        bar2.chords = false;
        bar2.dry = true;
        const auto one = arrange(q, bar2);
        size_t melodyInBar2 = 0;
        for (const auto& n : q.melody)
            melodyInBar2 += q.barOf(n.startBeat) == 1;
        CHECK(one.notes.size() == melodyInBar2);
        CHECK(one.fx == FxPreset::Dry);
        CHECK(one.notes.front().startSec < 0.1); // starts right away, not at bar 2's time

        auto slow = bar2;
        slow.tempoScale = 0.5;
        CHECK(arrange(q, slow).lengthSec > one.lengthSec * 1.8);

        PlayOptions roots;
        roots.melody = false;
        roots.chordStyle = ChordStyle::RootOnly;
        const auto r = arrange(q, roots);
        CHECK(r.notes.size() == 4);
        for (const auto& n : r.notes)
            CHECK(n.instrument == Instrument::Pad);

        PlayOptions arp;
        arp.melody = false;
        arp.chordStyle = ChordStyle::Arpeggio;
        arp.fromSection = arp.toSection = 0;
        const auto a = arrange(q, arp);
        CHECK(a.notes.size() == q.chords[0].voicing.size());
        CHECK(a.notes[1].startSec > a.notes[0].startSec);
    });

    run("puzzle: melody level solved without help scores the maximum", [] {
        const auto& level = *findLevel(1);
        Puzzle p(level, makeQuestion(level, 11, nullptr));
        p.setAnswerMode(true);
        for (int s = 0; s < p.sectionCount(); ++s)
            playAnswer(p, targetPitches(p, s));
        const auto r = p.check();
        CHECK(r.wrong == 0);
        CHECK(r.complete);
        CHECK(p.complete());
        CHECK(p.score() == p.perfectScore());
    });

    run("puzzle: answers are graded by pitch class, any octave", [] {
        const auto& level = *findLevel(1);
        Puzzle p(level, makeQuestion(level, 12, nullptr));
        p.setAnswerMode(true);
        auto pitches = targetPitches(p, 0);
        for (auto& x : pitches)
            x -= 12;
        playAnswer(p, pitches);
        CHECK(p.check().correct == (int) pitches.size());
    });

    run("puzzle: help costs points but never all of them", [] {
        const auto& level = *findLevel(3);
        Puzzle p(level, makeQuestion(level, 5, nullptr));
        const float start = p.section(0).multiplier;
        CHECK(p.useHint(HintId::HearBar, 0).has_value());
        CHECK(p.section(0).multiplier < start);
        CHECK(p.section(1).multiplier == start); // bar hints only cost their own bar

        for (int round = 0; round < 50; ++round)
            for (const auto& def : hintTable())
                p.useHint(def.id, 0);
        for (int i = 0; i < 200; ++i)
            p.noteOn(60), p.noteOff(60); // exploring
        CHECK(p.section(0).multiplier >= Puzzle::minMultiplier);
        CHECK(p.section(0).multiplier < 0.06f);

        // Revealed answers still have to be entered, and still score.
        p.setAnswerMode(true);
        p.selectSection(0);
        playChord(p, p.question().chords[0].voicing);
        playAnswer(p, targetPitches(p, 0));
        const auto r = p.check();
        CHECK(r.wrong == 0);
        CHECK(r.points > 0);
        CHECK(p.sectionSolved(0));
    });

    run("puzzle: first three plays are free, then each costs every bar", [] {
        const auto& level = *findLevel(2);
        Puzzle p(level, makeQuestion(level, 3, nullptr));
        for (int i = 0; i < 3; ++i)
            p.useGlobal(GlobalListen::PlayAll);
        CHECK(p.section(0).multiplier == 1.0f);
        p.useGlobal(GlobalListen::PlayAll);
        CHECK(p.section(0).multiplier < 1.0f);
        CHECK(p.section(3).multiplier == p.section(0).multiplier);
        CHECK(! p.globalAvailable(GlobalListen::MelodyOnly)); // chords-only level
        CHECK(p.globalAvailable(GlobalListen::RootsOnly));
    });

    run("puzzle: wrong answers cost that bar and can be retried", [] {
        const auto& level = *findLevel(1);
        Puzzle p(level, makeQuestion(level, 21, nullptr));
        p.setAnswerMode(true);
        const auto target = targetPitches(p, 0);
        playAnswer(p, { target[0] + 1 });
        auto r = p.check();
        CHECK(r.wrong == 1 && r.correct == 0);
        CHECK(p.section(0).multiplier < 1.0f);
        CHECK(p.section(1).multiplier == 1.0f);
        CHECK(p.slots()[0].state == SlotState::Wrong);
        playAnswer(p, { target[0] });
        r = p.check();
        CHECK(r.correct == 1);
        CHECK(p.slots()[0].points > 0 && p.slots()[0].points < 100);
    });

    run("puzzle: chords from the keyboard and from the picker", [] {
        const auto& level = *findLevel(2);
        Puzzle p(level, makeQuestion(level, 9, nullptr));
        p.setAnswerMode(true);
        playChord(p, p.question().chords[0].voicing); // fills bar 1, moves on to bar 2
        CHECK(p.selectedSection() == 1);
        p.enterChord(p.question().chords[1].chord);
        auto wrong = p.question().chords[2].chord;
        wrong.root = pitchClass(wrong.root + 1);
        p.enterChord(wrong);
        const auto r = p.check();
        CHECK(r.correct == 2);
        CHECK(r.wrong == 1);

        // A single note on a chords-only level enters nothing.
        p.selectSection(3);
        playAnswer(p, { 60 });
        CHECK(p.slots()[3].state == SlotState::Empty);
    });

    run("puzzle: a chord played on the melody+chords level doesn't fill melody slots", [] {
        const auto& level = *findLevel(3);
        Puzzle p(level, makeQuestion(level, 17, nullptr));
        p.setAnswerMode(true);
        playChord(p, p.question().chords[0].voicing);
        for (int i : p.slotsIn(0, SlotKind::Melody))
            CHECK(p.slots()[(size_t) i].state == SlotState::Empty);
        CHECK(p.slots()[(size_t) p.slotsIn(0, SlotKind::Chord)[0]].state == SlotState::Entered);
        playAnswer(p, targetPitches(p, 0));
        const auto r = p.check();
        CHECK(r.wrong == 0);
        CHECK(p.sectionSolved(0));
        CHECK(p.score() > 0);
        CHECK(p.score() < p.perfectScore());
    });

    run("catalog: melody phrases are cut from MIDI files", [] {
        const auto dir = std::filesystem::temp_directory_path() / "tos_catalog_test";
        std::filesystem::create_directories(dir);
        // Ode to Joy (public domain), quarter notes.
        writeMidi((dir / "ode.mid").string(),
                  { 64, 64, 65, 67, 67, 65, 64, 62, 60, 60, 62, 64, 64, 62, 62, 64, 64, 65, 67, 67, 65, 64, 62,
                    60, 60, 62, 64, 62, 60, 60 });
        MidiCatalog catalog;
        CHECK(catalog.loadFolder(dir.string()) == 1);
        CHECK(catalog.phraseCount(2) >= 3);
        std::mt19937 rng(1);
        const auto phrase = catalog.randomPhrase(rng, 2);
        CHECK(phrase.has_value());
        if (phrase)
        {
            CHECK(phrase->notes.size() == 8);
            CHECK(std::abs(phrase->bpm - 120.0) < 0.5);
        }

        LevelDef level = *findLevel(1);
        int fromCatalog = 0;
        for (uint32_t seed = 0; seed < 40; ++seed)
            fromCatalog += makeQuestion(level, seed, &catalog).source.rfind("catalog", 0) == 0;
        CHECK(fromCatalog > 5);
        CHECK(fromCatalog < 35);
        std::filesystem::remove_all(dir);
    });

    run("engine: offline render of a phrase and live notes", [] {
        AudioEngine engine;
        engine.prepareOffline(48000.0, 512);
        const auto q = makeQuestion(*findLevel(3), 4, nullptr);
        auto arrangement = arrange(q, {});
        arrangement.notes.push_back({ 0.2, 0.5, 48, 0.8f, Instrument::Pad });
        arrangement.notes.push_back({ 0.4, 0.5, 72, 0.8f, Instrument::Bell });
        const int id = engine.playPhrase(arrangement.notes, arrangement.fx);
        CHECK(id > 0);
        CHECK(engine.isPlaying());

        const int n = 48000 * 3;
        std::vector<float> l((size_t) n), r((size_t) n);
        engine.renderOffline(l.data(), r.data(), n);
        double energy = 0.0, peak = 0.0;
        bool finite = true;
        for (int i = 0; i < n; ++i)
        {
            finite = finite && std::isfinite(l[(size_t) i]) && std::isfinite(r[(size_t) i]);
            energy += l[(size_t) i] * l[(size_t) i];
            peak = std::max(peak, (double) std::abs(l[(size_t) i]));
        }
        const double rms = std::sqrt(energy / n);
        std::printf("       phrase rms %.3f peak %.3f\n", rms, peak);
        CHECK(finite);
        CHECK(rms > 0.01);
        CHECK(peak < 1.0);
        CHECK(engine.playbackPositionSec() > 2.5);

        // Render past the end: playback stops by itself.
        std::vector<float> tailL(48000 * 20), tailR(48000 * 20);
        engine.renderOffline(tailL.data(), tailR.data(), (int) tailL.size());
        CHECK(! engine.isPlaying());

        engine.liveNoteOn(60, 0.9f);
        std::vector<float> liveL(4800), liveR(4800);
        engine.renderOffline(liveL.data(), liveR.data(), 4800);
        double liveEnergy = 0.0;
        for (float x : liveL)
            liveEnergy += x * x;
        CHECK(liveEnergy > 0.1);
        engine.liveNoteOff(60);
    });

    platformShutdown();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
