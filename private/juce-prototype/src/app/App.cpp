#include "app/App.h"

#include "game/Paths.h"
#include "ui/Screens.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <random>

namespace tos
{

namespace
{
// Computer keyboard as a piano, laid out like most DAWs.
int qwertySemitone(SDL_Scancode sc)
{
    switch (sc)
    {
        case SDL_SCANCODE_A: return 0;
        case SDL_SCANCODE_W: return 1;
        case SDL_SCANCODE_S: return 2;
        case SDL_SCANCODE_E: return 3;
        case SDL_SCANCODE_D: return 4;
        case SDL_SCANCODE_F: return 5;
        case SDL_SCANCODE_T: return 6;
        case SDL_SCANCODE_G: return 7;
        case SDL_SCANCODE_Y: return 8;
        case SDL_SCANCODE_H: return 9;
        case SDL_SCANCODE_U: return 10;
        case SDL_SCANCODE_J: return 11;
        case SDL_SCANCODE_K: return 12;
        case SDL_SCANCODE_O: return 13;
        case SDL_SCANCODE_L: return 14;
        case SDL_SCANCODE_P: return 15;
        case SDL_SCANCODE_SEMICOLON: return 16;
        case SDL_SCANCODE_APOSTROPHE: return 17;
        default: return -1;
    }
}

uint32_t randomSeed()
{
    std::random_device rd;
    return rd() | 1u;
}
} // namespace

App::App(const AppOptions& options)
    : progress(options.dataDir.empty() ? paths::userDataDir() : options.dataDir), opts(options)
{
    qwertyNotes.fill(-1);
    progress.load();
    keyboardOctave = std::clamp(progress.settings.keyboardOctave, 2, 6);

    catalog.loadFolder(paths::join(paths::assetsDir(), "catalog"));
    catalog.loadFolder(paths::join(paths::userDataDir(), "catalog"));

    if (opts.audio)
    {
        if (! audio.startAudio())
            statusMessage = "No audio output: " + audio.audioStatus().error;
        audio.setMasterVolume(progress.settings.volume);
        if (! progress.settings.soundFont.empty() && std::filesystem::exists(progress.settings.soundFont))
            setSoundFont(progress.settings.soundFont);
    }

    if (opts.startLevel > 0)
        startLevel(opts.startLevel);
}

App::~App()
{
    audio.stopAudio();
    progress.save();
}

// ------------------------------------------------------------------------- flow

void App::startLevel(int levelId)
{
    const auto* level = findLevel(levelId);
    if (level == nullptr)
        return;

    const uint32_t seed = opts.seed != 0 ? opts.seed : randomSeed();
    opts.seed = 0; // only the first puzzle uses a fixed seed

    stopPlayback();
    puzzle = std::make_unique<Puzzle>(*level, makeQuestion(*level, seed, &catalog));
    puzzle->onEvent = [this, levelId, seed](const GameEvent& e) { progress.logEvent(levelId, seed, e); };
    const auto& q = puzzle->question();
    progress.logEvent(levelId, seed, { "start", -1, q.source + ", " + q.keyName() });
    progress.recordStart(levelId);

    showCompletion = false;
    screen = Screen::Play;
    autoPlayIn = 0.5;

    if (opts.demo)
    {
        opts.demo = false;
        applyDemoState();
    }
}

void App::backToTower()
{
    stopPlayback();
    if (puzzle && ! puzzle->complete())
        progress.logEvent(puzzle->level().id, puzzle->question().seed, { "quit", -1, "", 1.0f, puzzle->score() });
    puzzle.reset();
    screen = Screen::Tower;
}

void App::play(const PlayOptions& options)
{
    if (! puzzle)
        return;
    arrangement = arrange(puzzle->question(), options);
    phraseId = audio.playPhrase(arrangement.notes, arrangement.fx);
}

void App::stopPlayback()
{
    audio.stopPlayback();
    phraseId = 0;
}

int App::playingSection() const
{
    if (phraseId == 0 || audio.playingPhraseId() != phraseId)
        return -1;
    const double t = audio.playbackPositionSec();
    for (const auto& span : arrangement.spans)
        if (t >= span.startSec && t < span.endSec)
            return span.section;
    return -1;
}

void App::check()
{
    if (! puzzle || puzzle->complete())
        return;
    const auto result = puzzle->check();
    if (result.complete)
    {
        completionWasBest = progress.recordCompletion(puzzle->level().id, puzzle->score());
        showCompletion = true;
    }
}

void App::toggleAnswerMode()
{
    if (puzzle)
        puzzle->setAnswerMode(! puzzle->answerMode());
}

// ------------------------------------------------------------------------- notes

void App::userNoteOn(int pitch, float velocity)
{
    if (pitch < 0 || pitch > 127)
        return;
    audio.liveNoteOn(pitch, velocity);
    heldByUser[(size_t) pitch] = true;
    if (screen == Screen::Play && puzzle && ! showCompletion)
        puzzle->noteOn(pitch);
}

void App::userNoteOff(int pitch)
{
    if (pitch < 0 || pitch > 127)
        return;
    audio.liveNoteOff(pitch);
    heldByUser[(size_t) pitch] = false;
    if (screen == Screen::Play && puzzle && ! showCompletion)
        puzzle->noteOff(pitch);
}

void App::handleEvent(const SDL_Event& e)
{
    if (e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_KEY_UP)
        return;
    if (ImGui::GetIO().WantTextInput)
        return;

    const auto sc = e.key.scancode;
    if (e.type == SDL_EVENT_KEY_UP)
    {
        if (qwertyNotes[(size_t) sc] >= 0)
        {
            userNoteOff(qwertyNotes[(size_t) sc]);
            qwertyNotes[(size_t) sc] = -1;
        }
        return;
    }
    if (e.key.repeat)
        return;

    const int semitone = qwertySemitone(sc);
    if (semitone >= 0 && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT)) == 0)
    {
        const int pitch = qwertyBase() + semitone;
        qwertyNotes[(size_t) sc] = pitch;
        userNoteOn(pitch, 0.75f);
        return;
    }

    switch (sc)
    {
        case SDL_SCANCODE_Z:
            keyboardOctave = std::max(2, keyboardOctave - 1);
            saveSettings();
            break;
        case SDL_SCANCODE_X:
            keyboardOctave = std::min(6, keyboardOctave + 1);
            saveSettings();
            break;
        case SDL_SCANCODE_SPACE:
            if (screen == Screen::Play && puzzle)
            {
                if (audio.isPlaying())
                    stopPlayback();
                else
                    play(puzzle->useGlobal(GlobalListen::PlayAll));
            }
            break;
        case SDL_SCANCODE_TAB:
            toggleAnswerMode();
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (showCompletion)
                startLevel(puzzle->level().id);
            else
                check();
            break;
        case SDL_SCANCODE_BACKSPACE:
            if (puzzle)
                puzzle->undoEntry();
            break;
        case SDL_SCANCODE_ESCAPE:
            if (audio.isPlaying())
                stopPlayback();
            else if (screen == Screen::Play)
                backToTower();
            break;
        case SDL_SCANCODE_1:
        case SDL_SCANCODE_2:
        case SDL_SCANCODE_3:
        case SDL_SCANCODE_4:
            if (puzzle)
                puzzle->selectSection((int) sc - (int) SDL_SCANCODE_1);
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------------------- frame

void App::frame(double dt)
{
    audio.pollMidiDevices();

    midiNotes.clear();
    audio.drainInputNotes(midiNotes);
    for (const auto& n : midiNotes)
    {
        heldByMidi[(size_t) n.pitch] = n.on;
        if (screen == Screen::Play && puzzle && ! showCompletion)
        {
            if (n.on)
                puzzle->noteOn(n.pitch);
            else
                puzzle->noteOff(n.pitch);
        }
    }
    for (size_t i = 0; i < 128; ++i)
        heldDisplay[i] = heldByMidi[i] || heldByUser[i];

    if (puzzle && ! puzzle->complete())
        puzzle->addTime(dt);
    if (autoPlayIn > 0.0)
    {
        autoPlayIn -= dt;
        if (autoPlayIn <= 0.0 && puzzle)
            play(puzzle->useGlobal(GlobalListen::PlayAll));
    }

    if (screen == Screen::Tower)
        ui::drawTowerScreen(*this);
    else
        ui::drawPlayScreen(*this);
}

void App::setSoundFont(const std::string& path)
{
    std::string error;
    if (audio.setPianoSoundFont(path, error))
    {
        progress.settings.soundFont = path;
        saveSettings();
    }
    else
    {
        statusMessage = error;
    }
}

void App::saveSettings()
{
    progress.settings.volume = audio.masterVolume();
    progress.settings.keyboardOctave = keyboardOctave;
    progress.save();
}

// A believable mid-game state, used for screenshots: some bars solved, hints shown,
// a wrong answer and a pending entry.
void App::applyDemoState()
{
    auto& p = *puzzle;
    autoPlayIn = -1.0;
    auto pitchesOf = [&](int s) {
        std::vector<int> v;
        for (int i : p.slotsIn(s, SlotKind::Melody))
            v.push_back(p.slots()[(size_t) i].targetPitch);
        return v;
    };

    p.useGlobal(GlobalListen::PlayAll);
    p.useHint(HintId::HearBar, 0);
    p.setAnswerMode(true);
    p.selectSection(0);
    if (p.level().hasChords())
        p.enterChord(p.question().chords[0].chord);
    for (int pitch : pitchesOf(0))
        p.noteOn(pitch), p.noteOff(pitch);
    p.check();

    if (p.sectionCount() > 1)
    {
        p.selectSection(1);
        p.useHint(HintId::ShowContour, 1);
        if (p.level().hasChords())
        {
            p.useHint(HintId::ShowRoot, 1);
            auto wrong = p.question().chords[1].chord;
            wrong.quality = wrong.quality == ChordQuality::Major ? ChordQuality::Minor : ChordQuality::Major;
            p.enterChord(wrong);
        }
        const auto bar2 = pitchesOf(1);
        if (! bar2.empty())
            p.noteOn(bar2[0] + 2), p.noteOff(bar2[0] + 2);
        p.check();
        if (bar2.size() > 1)
            p.noteOn(bar2[0]), p.noteOff(bar2[0]);
        p.useHint(HintId::RevealNextNote, 1);
    }
}

} // namespace tos
