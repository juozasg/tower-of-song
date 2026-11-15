#include "engine/AudioEngine.h"

#include "engine/Voices.h"
#include "game/Paths.h"

#include <juce_audio_devices/juce_audio_devices.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>

namespace tos
{

namespace
{

// Single-producer (UI thread) / single-consumer (audio thread) queue.
template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push(const T& item)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 + n2 < 1)
            return false;
        items[(size_t) (n1 > 0 ? s1 : s2)] = item;
        fifo.finishedWrite(1);
        return true;
    }

    bool pop(T& item)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead(1, s1, n1, s2, n2);
        if (n1 + n2 < 1)
            return false;
        item = items[(size_t) (n1 > 0 ? s1 : s2)];
        fifo.finishedRead(1);
        return true;
    }

    int freeSpace() const { return fifo.getFreeSpace(); }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, Capacity> items {};
};

struct Command
{
    enum Type : uint8_t
    {
        BeginPhrase,
        PhraseNote,
        EndPhrase,
        Stop,
        LiveOn,
        LiveOff,
        LiveAllOff
    };

    Type type = Stop;
    int id = 0;
    float startSec = 0.0f, durSec = 0.0f;
    float velocity = 0.0f;
    uint8_t pitch = 0;
    uint8_t channel = 1;
    FxPreset fx = FxPreset::Dry;
};

struct SchedEvent
{
    int64_t sample;
    uint8_t status, data1, data2;
};

// Ping-pong echo.
class StereoDelay
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        bufL.assign((size_t) (sr * 2.0), 0.0f);
        bufR.assign(bufL.size(), 0.0f);
        pos = 0;
    }

    void set(float timeSec, float feedbackAmount, float mixAmount)
    {
        delaySamples = juce::jlimit(1, (int) bufL.size() - 1, (int) (timeSec * sr));
        feedback = feedbackAmount;
        targetMix = mixAmount;
    }

    void process(float* l, float* r, int n)
    {
        if (bufL.empty() || (targetMix == 0.0f && mix < 1.0e-4f))
        {
            mix = targetMix;
            return;
        }
        const int size = (int) bufL.size();
        for (int i = 0; i < n; ++i)
        {
            mix += 0.0005f * (targetMix - mix);
            int read = pos - delaySamples;
            if (read < 0)
                read += size;
            const float dl = bufL[(size_t) read], dr = bufR[(size_t) read];
            bufL[(size_t) pos] = l[i] + feedback * dr;
            bufR[(size_t) pos] = r[i] + feedback * dl;
            l[i] += mix * dl;
            r[i] += mix * dr;
            if (++pos >= size)
                pos = 0;
        }
    }

private:
    double sr = 48000.0;
    std::vector<float> bufL, bufR;
    int pos = 0, delaySamples = 1;
    float feedback = 0.0f, mix = 0.0f, targetMix = 0.0f;
};

// Built-in synth voices plus an optional SoundFont piano on channel 1.
class SoundSource
{
public:
    SoundSource()
    {
        for (int i = 0; i < 24; ++i)
            synth.addVoice(new PianoVoice());
        for (int i = 0; i < 12; ++i)
            synth.addVoice(new PadVoice());
        for (int i = 0; i < 8; ++i)
            synth.addVoice(new BellVoice());
        synth.setNoteStealingEnabled(true);
        setFont(nullptr, 0);
    }

    ~SoundSource() { setFont(nullptr, 0); }

    // Only call while the audio callback is not running. Takes ownership of `f`.
    void setFont(tsf* f, int presetIndex)
    {
        if (font != nullptr)
            tsf_close(font);
        font = f;

        synth.clearSounds();
        if (font == nullptr)
            synth.addSound(new PianoSound());
        synth.addSound(new PadSound());
        synth.addSound(new BellSound());

        if (font != nullptr)
        {
            tsf_set_max_voices(font, 64);
            tsf_channel_set_presetindex(font, 0, presetIndex);
            configureFont();
        }
    }

    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        synth.setCurrentPlaybackSampleRate(sampleRate);
        scratch.assign((size_t) maxBlock * 2, 0.0f);
        configureFont();
    }

    void allNotesOff()
    {
        synth.allNotesOff(0, true);
        if (font != nullptr)
            tsf_channel_note_off_all(font, 0);
    }

    void render(juce::AudioBuffer<float>& buffer, int num, const juce::MidiBuffer& midi)
    {
        buffer.clear(0, num);
        synth.renderNextBlock(buffer, midi, 0, num);
        if (font == nullptr)
            return;

        int pos = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.getChannel() != 1)
                continue;
            renderFont(buffer, pos, meta.samplePosition - pos);
            pos = juce::jmax(pos, meta.samplePosition);
            if (msg.isNoteOn())
                tsf_channel_note_on(font, 0, msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
                tsf_channel_note_off(font, 0, msg.getNoteNumber());
            else if (msg.isController())
                tsf_channel_midi_control(font, 0, msg.getControllerNumber(), msg.getControllerValue());
        }
        renderFont(buffer, pos, num - pos);
    }

private:
    void configureFont()
    {
        if (font != nullptr)
            tsf_set_output(font, TSF_STEREO_UNWEAVED, (int) sr, -4.0f);
    }

    void renderFont(juce::AudioBuffer<float>& buffer, int start, int n)
    {
        if (n <= 0)
            return;
        tsf_render_float(font, scratch.data(), n, 0);
        buffer.addFrom(0, start, scratch.data(), n);
        buffer.addFrom(1, start, scratch.data() + n, n);
    }

    juce::Synthesiser synth;
    tsf* font = nullptr;
    std::vector<float> scratch;
    double sr = 48000.0;
};

int pianoPresetIndex(tsf* f)
{
    const int idx = tsf_get_presetindex(f, 0, 0);
    return idx >= 0 ? idx : 0;
}

} // namespace

// ======================================================================================

struct AudioEngine::Impl : juce::AudioIODeviceCallback, juce::MidiInputCallback
{
    Impl()
    {
        sched.reserve(maxScheduled);
    }

    ~Impl() override
    {
        inputs.clear();
        if (callbackAdded)
            deviceManager.removeAudioCallback(this);
        deviceManager.closeAudioDevice();
        if (fontMaster != nullptr)
        {
            phraseSource.setFont(nullptr, 0);
            liveSource.setFont(nullptr, 0);
            tsf_close(fontMaster);
        }
    }

    // ---------------------------------------------------------------- audio thread

    void prepare(double sampleRate, int blockSize)
    {
        sr = sampleRate;
        maxBlock = juce::jmax(64, blockSize);
        phraseSource.prepare(sr, maxBlock);
        liveSource.prepare(sr, maxBlock);
        phraseBuffer.setSize(2, maxBlock);
        liveBuffer.setSize(2, maxBlock);
        phraseMidi.ensureSize(16384);
        liveMidi.ensureSize(4096);
        phraseReverb.setSampleRate(sr);
        liveReverb.setSampleRate(sr);
        delay.prepare(sr);
        applyFx(FxPreset::Room);

        juce::Reverb::Parameters live;
        live.roomSize = 0.35f;
        live.damping = 0.5f;
        live.wetLevel = 0.06f;
        live.dryLevel = 0.5f;
        live.width = 1.0f;
        liveReverb.setParameters(live);

        collector.reset(sr);
        collectorReady = true;
    }

    void applyFx(FxPreset fx)
    {
        juce::Reverb::Parameters p;
        p.width = 1.0f;
        switch (fx)
        {
            case FxPreset::Dry:
                p.roomSize = 0.3f;
                p.damping = 0.5f;
                p.wetLevel = 0.0f;
                p.dryLevel = 0.5f;
                delay.set(0.3f, 0.0f, 0.0f);
                break;
            case FxPreset::Room:
                p.roomSize = 0.42f;
                p.damping = 0.5f;
                p.wetLevel = 0.09f;
                p.dryLevel = 0.5f;
                delay.set(0.3f, 0.0f, 0.0f);
                break;
            case FxPreset::HallDelay:
                p.roomSize = 0.88f;
                p.damping = 0.3f;
                p.wetLevel = 0.2f;
                p.dryLevel = 0.45f;
                delay.set(0.36f, 0.38f, 0.3f);
                break;
        }
        phraseReverb.setParameters(p);
    }

    void handleCommands()
    {
        Command c;
        while (commands.pop(c))
        {
            switch (c.type)
            {
                case Command::BeginPhrase:
                    phraseSource.allNotesOff();
                    sched.clear();
                    schedPos = 0;
                    phraseStart = clock + (int64_t) (0.03 * sr);
                    phraseEnd = phraseStart;
                    activeId = c.id;
                    building = true;
                    applyFx(c.fx);
                    break;

                case Command::PhraseNote:
                {
                    if (sched.size() + 2 > sched.capacity())
                        break;
                    const auto on = phraseStart + (int64_t) (c.startSec * sr);
                    const auto off = on + juce::jmax((int64_t) 1, (int64_t) (c.durSec * sr));
                    const auto vel = (uint8_t) juce::jlimit(1, 127, (int) (c.velocity * 127.0f));
                    sched.push_back({ on, (uint8_t) (0x90 | (c.channel - 1)), c.pitch, vel });
                    sched.push_back({ off, (uint8_t) (0x80 | (c.channel - 1)), c.pitch, 0 });
                    phraseEnd = juce::jmax(phraseEnd, off);
                    break;
                }

                case Command::EndPhrase:
                    std::sort(sched.begin(), sched.end(), [](const SchedEvent& a, const SchedEvent& b) {
                        if (a.sample != b.sample)
                            return a.sample < b.sample;
                        return (a.status & 0xF0) < (b.status & 0xF0); // note-offs first
                    });
                    building = false;
                    playingId = activeId;
                    playing = true;
                    break;

                case Command::Stop:
                    sched.clear();
                    schedPos = 0;
                    building = false;
                    activeId = 0;
                    playingId = 0;
                    playing = false;
                    phraseSource.allNotesOff();
                    break;

                case Command::LiveOn:
                    if (numLiveUi < (int) liveUi.size())
                        liveUi[(size_t) numLiveUi++] = juce::MidiMessage::noteOn(1, c.pitch, c.velocity);
                    break;

                case Command::LiveOff:
                    if (numLiveUi < (int) liveUi.size())
                        liveUi[(size_t) numLiveUi++] = juce::MidiMessage::noteOff(1, c.pitch);
                    break;

                case Command::LiveAllOff:
                    liveSource.allNotesOff();
                    break;
            }
        }
    }

    void process(int n)
    {
        handleCommands();

        phraseMidi.clear();
        if (! building)
        {
            while (schedPos < sched.size() && sched[schedPos].sample < clock + n)
            {
                const auto& e = sched[schedPos++];
                const int offset = (int) juce::jmax((int64_t) 0, e.sample - clock);
                phraseMidi.addEvent(juce::MidiMessage(e.status, e.data1, e.data2), offset);
            }
        }

        liveMidi.clear();
        if (collectorReady && ! offline)
            collector.removeNextBlockOfMessages(liveMidi, n);
        for (int i = 0; i < numLiveUi; ++i)
            liveMidi.addEvent(liveUi[(size_t) i], 0);
        numLiveUi = 0;

        phraseSource.render(phraseBuffer, n, phraseMidi);
        liveSource.render(liveBuffer, n, liveMidi);

        auto* pl = phraseBuffer.getWritePointer(0);
        auto* pr = phraseBuffer.getWritePointer(1);
        delay.process(pl, pr, n);
        phraseReverb.processStereo(pl, pr, n);
        liveReverb.processStereo(liveBuffer.getWritePointer(0), liveBuffer.getWritePointer(1), n);

        const float gain = volume.load();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* p = phraseBuffer.getWritePointer(ch);
            const auto* l = liveBuffer.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                p[i] = std::tanh((p[i] + l[i]) * gain);
        }

        clock += n;
        if (activeId != 0 && ! building)
        {
            position = (double) (clock - phraseStart) / sr;
            const auto tail = (int64_t) (0.25 * sr);
            if (clock > phraseEnd + tail)
            {
                activeId = 0;
                playingId = 0;
                playing = false;
            }
        }
    }

    void renderInto(float* left, float* right, int numSamples)
    {
        int done = 0;
        while (done < numSamples)
        {
            const int chunk = juce::jmin(maxBlock, numSamples - done);
            process(chunk);
            std::memcpy(left + done, phraseBuffer.getReadPointer(0), sizeof(float) * (size_t) chunk);
            std::memcpy(right + done, phraseBuffer.getReadPointer(1), sizeof(float) * (size_t) chunk);
            done += chunk;
        }
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs, int numOutputs,
                                          int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        int done = 0;
        while (done < numSamples)
        {
            const int chunk = juce::jmin(maxBlock, numSamples - done);
            process(chunk);
            for (int ch = 0; ch < numOutputs; ++ch)
                if (outputs[ch] != nullptr)
                    std::memcpy(outputs[ch] + done, phraseBuffer.getReadPointer(juce::jmin(ch, 1)),
                                sizeof(float) * (size_t) chunk);
            done += chunk;
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        prepare(device->getCurrentSampleRate(), 512);
    }

    void audioDeviceStopped() override {}

    // ---------------------------------------------------------------- MIDI threads

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isActiveSense() || m.isMidiClock() || m.isSysEx() || m.isMidiStart() || m.isMidiStop()
            || m.isMidiContinue() || m.isSongPositionPointer() || m.isQuarterFrame())
            return;

        juce::MidiMessage out;
        if (m.isNoteOn())
        {
            out = juce::MidiMessage::noteOn(1, m.getNoteNumber(), m.getVelocity());
            pushInputNote({ m.getNoteNumber(), m.getFloatVelocity(), true });
        }
        else if (m.isNoteOff()) // includes note-on with velocity 0
        {
            out = juce::MidiMessage::noteOff(1, m.getNoteNumber());
            pushInputNote({ m.getNoteNumber(), 0.0f, false });
        }
        else if (m.isSustainPedalOn() || m.isSustainPedalOff())
            out = juce::MidiMessage::controllerEvent(1, 64, m.getControllerValue());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            out = juce::MidiMessage::allNotesOff(1);
        else
            return;

        if (collectorReady)
        {
            out.setTimeStamp(m.getTimeStamp() > 0.0 ? m.getTimeStamp()
                                                    : juce::Time::getMillisecondCounterHiRes() * 0.001);
            collector.addMessageToQueue(out);
        }
    }

    void pushInputNote(InputNote n)
    {
        std::lock_guard<std::mutex> lock(inputNoteMutex);
        if (pendingInputNotes.size() < 1024)
            pendingInputNotes.push_back(n);
    }

    // ---------------------------------------------------------------- UI thread helpers

    bool push(const Command& c) { return commands.push(c); }

    void reloadFonts(const std::function<void()>& swap)
    {
        const bool wasRunning = callbackAdded;
        if (wasRunning)
            deviceManager.removeAudioCallback(this);
        swap();
        phraseSource.prepare(sr, maxBlock);
        liveSource.prepare(sr, maxBlock);
        if (wasRunning)
            deviceManager.addAudioCallback(this);
    }

    struct Input
    {
        juce::String identifier, name;
        std::unique_ptr<juce::MidiInput> device;
        juce::String error;
    };

    static constexpr size_t maxScheduled = 16384;

    juce::AudioDeviceManager deviceManager;
    bool callbackAdded = false;
    juce::String audioError;

    SoundSource phraseSource, liveSource;
    juce::Reverb phraseReverb, liveReverb;
    StereoDelay delay;
    juce::MidiMessageCollector collector;
    std::atomic<bool> collectorReady { false };
    bool offline = false;

    SpscQueue<Command, 8192> commands;

    // audio-thread state
    std::vector<SchedEvent> sched;
    size_t schedPos = 0;
    int64_t clock = 0, phraseStart = 0, phraseEnd = 0;
    int activeId = 0;
    bool building = false;
    std::array<juce::MidiMessage, 256> liveUi;
    int numLiveUi = 0;
    juce::MidiBuffer phraseMidi, liveMidi;
    juce::AudioBuffer<float> phraseBuffer, liveBuffer;
    double sr = 48000.0;
    int maxBlock = 512;

    // shared with UI
    std::atomic<int> playingId { 0 };
    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 };
    std::atomic<float> volume { 0.8f };

    // UI-thread state
    int nextPhraseId = 1;
    tsf* fontMaster = nullptr;
    std::string fontPath, fontName;

    std::vector<Input> inputs;
    std::optional<juce::MidiDeviceListConnection> deviceListConnection;
    std::atomic<bool> devicesChanged { true };
    double lastScan = -100.0;

    std::mutex inputNoteMutex;
    std::vector<InputNote> pendingInputNotes;
};

// ======================================================================================

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() = default;

bool AudioEngine::startAudio()
{
    impl->offline = false;
    impl->audioError = impl->deviceManager.initialise(0, 2, nullptr, true);
    if (impl->audioError.isNotEmpty())
        return false;

    // Prefer a small buffer: playing along needs low latency more than CPU headroom.
    auto setup = impl->deviceManager.getAudioDeviceSetup();
    if (auto* device = impl->deviceManager.getCurrentAudioDevice(); device != nullptr && setup.bufferSize > 256)
        for (int size : device->getAvailableBufferSizes())
            if (size >= 128 && size <= 256)
            {
                setup.bufferSize = size;
                impl->deviceManager.setAudioDeviceSetup(setup, true);
                break;
            }

    if (! impl->callbackAdded)
    {
        impl->deviceManager.addAudioCallback(impl.get());
        impl->callbackAdded = true;
    }
    return impl->deviceManager.getCurrentAudioDevice() != nullptr;
}

void AudioEngine::stopAudio()
{
    if (impl->callbackAdded)
        impl->deviceManager.removeAudioCallback(impl.get());
    impl->callbackAdded = false;
    impl->deviceManager.closeAudioDevice();
}

AudioStatus AudioEngine::audioStatus() const
{
    AudioStatus s;
    s.error = impl->audioError.toStdString();
    s.deviceType = impl->deviceManager.getCurrentAudioDeviceType().toStdString();
    if (auto* d = impl->deviceManager.getCurrentAudioDevice())
    {
        s.running = d->isPlaying();
        s.deviceName = d->getName().toStdString();
        s.sampleRate = d->getCurrentSampleRate();
        s.bufferSize = d->getCurrentBufferSizeSamples();
        if (s.sampleRate > 0)
            s.outputLatencyMs = 1000.0 * (d->getOutputLatencyInSamples() + s.bufferSize) / s.sampleRate;
    }
    return s;
}

std::vector<std::string> AudioEngine::deviceTypes() const
{
    std::vector<std::string> result;
    for (auto* type : impl->deviceManager.getAvailableDeviceTypes())
        result.push_back(type->getTypeName().toStdString());
    return result;
}

std::vector<std::string> AudioEngine::outputDevices() const
{
    std::vector<std::string> result;
    if (auto* type = impl->deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        for (const auto& name : type->getDeviceNames(false))
            result.push_back(name.toStdString());
    }
    return result;
}

std::vector<int> AudioEngine::bufferSizes() const
{
    std::vector<int> result;
    if (auto* d = impl->deviceManager.getCurrentAudioDevice())
        for (int size : d->getAvailableBufferSizes())
            result.push_back(size);
    return result;
}

void AudioEngine::setDeviceType(const std::string& type)
{
    impl->deviceManager.setCurrentAudioDeviceType(type, true);
}

void AudioEngine::setOutputDevice(const std::string& name)
{
    auto setup = impl->deviceManager.getAudioDeviceSetup();
    setup.outputDeviceName = name;
    setup.useDefaultOutputChannels = true;
    impl->audioError = impl->deviceManager.setAudioDeviceSetup(setup, true);
}

void AudioEngine::setBufferSize(int samples)
{
    auto setup = impl->deviceManager.getAudioDeviceSetup();
    setup.bufferSize = samples;
    impl->audioError = impl->deviceManager.setAudioDeviceSetup(setup, true);
}

void AudioEngine::pollMidiDevices()
{
    if (! impl->deviceListConnection && juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        impl->deviceListConnection = juce::MidiDeviceListConnection::make([this] { impl->devicesChanged = true; });

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const bool notified = impl->devicesChanged.exchange(false);
    if (! notified && now - impl->lastScan < 2.0)
        return;
    impl->lastScan = now;

    const auto available = juce::MidiInput::getAvailableDevices();
    auto& inputs = impl->inputs;

    const auto sizeBefore = inputs.size();
    inputs.erase(std::remove_if(inputs.begin(), inputs.end(),
                                [&](const Impl::Input& in) {
                                    for (const auto& info : available)
                                        if (info.identifier == in.identifier)
                                            return false;
                                    return true;
                                }),
                 inputs.end());
    if (inputs.size() != sizeBefore)
        liveAllNotesOff(); // an unplugged keyboard can't send its note-offs

    for (const auto& info : available)
    {
        auto it = std::find_if(inputs.begin(), inputs.end(),
                               [&](const Impl::Input& in) { return in.identifier == info.identifier; });
        if (it == inputs.end())
        {
            inputs.push_back({ info.identifier, info.name, nullptr, {} });
            it = inputs.end() - 1;
        }
        if (it->device == nullptr)
        {
            if (auto device = juce::MidiInput::openDevice(info.identifier, impl.get()))
            {
                device->start();
                it->device = std::move(device);
                it->error.clear();
            }
            else
            {
                it->error = "Unavailable - another app may be using it";
            }
        }
    }
}

std::vector<MidiDeviceStatus> AudioEngine::midiDevices() const
{
    std::vector<MidiDeviceStatus> result;
    for (const auto& in : impl->inputs)
        result.push_back({ in.name.toStdString(), in.identifier.toStdString(), in.device != nullptr,
                           in.error.toStdString() });
    return result;
}

void AudioEngine::drainInputNotes(std::vector<InputNote>& out)
{
    std::lock_guard<std::mutex> lock(impl->inputNoteMutex);
    out.insert(out.end(), impl->pendingInputNotes.begin(), impl->pendingInputNotes.end());
    impl->pendingInputNotes.clear();
}

int AudioEngine::playPhrase(const std::vector<ScheduledNote>& notes, FxPreset fx)
{
    if (impl->commands.freeSpace() < (int) notes.size() + 2)
        return 0;

    const int id = impl->nextPhraseId++;
    Command begin;
    begin.type = Command::BeginPhrase;
    begin.id = id;
    begin.fx = fx;
    impl->push(begin);

    for (const auto& n : notes)
    {
        Command c;
        c.type = Command::PhraseNote;
        c.startSec = (float) n.startSec;
        c.durSec = (float) n.durSec;
        c.pitch = (uint8_t) juce::jlimit(0, 127, n.pitch);
        c.velocity = n.velocity;
        c.channel = (uint8_t) n.instrument;
        impl->push(c);
    }

    Command end;
    end.type = Command::EndPhrase;
    impl->push(end);

    // Report "playing" immediately so the UI doesn't flicker before the audio thread starts.
    impl->playingId = id;
    impl->playing = true;
    impl->position = 0.0;
    return id;
}

void AudioEngine::stopPlayback()
{
    Command c;
    c.type = Command::Stop;
    impl->push(c);
    impl->playing = false;
    impl->playingId = 0;
}

bool AudioEngine::isPlaying() const { return impl->playing.load(); }
int AudioEngine::playingPhraseId() const { return impl->playingId.load(); }
double AudioEngine::playbackPositionSec() const { return impl->position.load(); }

void AudioEngine::liveNoteOn(int pitch, float velocity)
{
    Command c;
    c.type = Command::LiveOn;
    c.pitch = (uint8_t) juce::jlimit(0, 127, pitch);
    c.velocity = velocity;
    impl->push(c);
}

void AudioEngine::liveNoteOff(int pitch)
{
    Command c;
    c.type = Command::LiveOff;
    c.pitch = (uint8_t) juce::jlimit(0, 127, pitch);
    impl->push(c);
}

void AudioEngine::liveAllNotesOff()
{
    Command c;
    c.type = Command::LiveAllOff;
    impl->push(c);
}

void AudioEngine::setMasterVolume(float gain) { impl->volume = juce::jlimit(0.0f, 2.0f, gain); }
float AudioEngine::masterVolume() const { return impl->volume.load(); }

std::vector<std::string> AudioEngine::availableSoundFonts() const
{
    auto fonts = paths::listFiles(paths::join(paths::assetsDir(), "soundfonts"), { ".sf2" });
    const auto user = paths::listFiles(paths::join(paths::userDataDir(), "soundfonts"), { ".sf2" });
    fonts.insert(fonts.end(), user.begin(), user.end());
    return fonts;
}

bool AudioEngine::setPianoSoundFont(const std::string& path, std::string& error)
{
    tsf* master = nullptr;
    if (! path.empty())
    {
        master = tsf_load_filename(path.c_str());
        if (master == nullptr)
        {
            error = "Could not load " + path;
            return false;
        }
    }

    impl->reloadFonts([&] {
        if (master != nullptr)
        {
            const int preset = pianoPresetIndex(master);
            impl->phraseSource.setFont(tsf_copy(master), preset);
            impl->liveSource.setFont(tsf_copy(master), preset);
        }
        else
        {
            impl->phraseSource.setFont(nullptr, 0);
            impl->liveSource.setFont(nullptr, 0);
        }
        if (impl->fontMaster != nullptr)
            tsf_close(impl->fontMaster);
        impl->fontMaster = master;
    });

    impl->fontPath = path;
    if (master != nullptr)
    {
        const char* presetName = tsf_get_presetname(master, pianoPresetIndex(master));
        impl->fontName = paths::fileStem(path) + (presetName != nullptr ? std::string(" - ") + presetName : "");
    }
    else
    {
        impl->fontName.clear();
    }
    return true;
}

std::string AudioEngine::pianoName() const
{
    return impl->fontName.empty() ? "Built-in modelled piano" : impl->fontName;
}

void AudioEngine::prepareOffline(double sampleRate, int blockSize)
{
    impl->offline = true;
    impl->prepare(sampleRate, blockSize);
}

void AudioEngine::renderOffline(float* left, float* right, int numSamples)
{
    impl->renderInto(left, right, numSamples);
}

} // namespace tos
