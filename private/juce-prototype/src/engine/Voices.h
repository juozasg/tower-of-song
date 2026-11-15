#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace tos
{

// Sounds are bound to MIDI channels: 1 = piano, 2 = pad, 3 = bell (see Instrument).
template <int Channel>
struct ChannelSound : juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int ch) override { return ch == Channel; }
};

using PianoSound = ChannelSound<1>;
using PadSound = ChannelSound<2>;
using BellSound = ChannelSound<3>;

// Additive piano: inharmonic partials with two-stage decay, a hammer-position comb,
// two slightly detuned "strings" per partial and a short hammer noise burst.
class PianoVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<PianoSound*>(s) != nullptr; }
    void startNote(int note, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>& out, int start, int num) override;

private:
    static constexpr int maxPartials = 14;

    struct Partial
    {
        float re[2], im[2]; // two rotating phasors (detuned strings)
        float cs[2], sn[2]; // per-sample rotation
        float amp;
        float fast, slow;       // envelope stages
        float fastMul, slowMul; // per-sample decay
    };

    std::array<Partial, maxPartials> partials {};
    int numPartials = 0;
    float gainL = 0.5f, gainR = 0.5f;
    float attack = 0.0f, attackStep = 0.0f;
    float release = 1.0f, releaseMul = 1.0f;
    bool releasing = false;
    float noiseEnv = 0.0f, noiseMul = 0.0f, noiseLp = 0.0f, noiseCoef = 0.0f, noiseAmp = 0.0f;
    juce::Random random;
    int samplesSinceNormalise = 0;
};

// Two detuned PolyBLEP saws plus a sub sine through a 2-pole low-pass.
class PadVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<PadSound*>(s) != nullptr; }
    void startNote(int note, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>& out, int start, int num) override;

private:
    juce::ADSR adsr;
    double phase[3] {}, inc[3] {};
    float lp1 = 0.0f, lp2 = 0.0f, lpCoef = 0.1f, level = 0.0f;
};

// Near-sine bell with a soft second partial; good for hearing intervals clearly.
class BellVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<BellSound*>(s) != nullptr; }
    void startNote(int note, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>& out, int start, int num) override;

private:
    juce::ADSR adsr;
    double phase = 0.0, inc = 0.0;
    float level = 0.0f;
};

} // namespace tos
