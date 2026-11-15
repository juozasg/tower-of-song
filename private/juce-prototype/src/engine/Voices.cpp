#include "engine/Voices.h"

#include <cmath>

namespace tos
{

namespace
{
constexpr float twoPi = 6.28318530718f;

float decayPerSample(float t60Seconds, double sampleRate)
{
    return std::exp(-6.9078f / (t60Seconds * (float) sampleRate));
}

void panGains(int note, float& l, float& r)
{
    const float pan = juce::jlimit(-0.6f, 0.6f, (note - 60) / 48.0f);
    const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    l = std::cos(angle);
    r = std::sin(angle);
}

float polyBlep(double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return (float) (t + t - t * t - 1.0);
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return (float) (t * t + t + t + 1.0);
    }
    return 0.0f;
}
} // namespace

// ------------------------------------------------------------------ piano

void PianoVoice::startNote(int note, float velocity, juce::SynthesiserSound*, int)
{
    const double sr = getSampleRate();
    const double f0 = juce::MidiMessage::getMidiNoteInHertz(note);
    const float vel = juce::jlimit(0.05f, 1.0f, velocity);

    // String stiffness grows with pitch, which stretches upper partials.
    const double inharm = juce::jlimit(0.00005, 0.003, 0.00025 * std::pow(2.0, (note - 60) / 18.0));
    // Low strings ring much longer than high ones.
    const float baseT60 = juce::jlimit(0.7f, 14.0f, 12.0f * std::pow(2.0f, -(note - 36) / 14.0f));
    // Harder hits are brighter.
    const float rolloff = 1.9f - 1.0f * vel;
    const float detuneCents = 0.35f + 0.25f * random.nextFloat();

    numPartials = 0;
    float ampSum = 0.0f;
    for (int n = 1; n <= maxPartials; ++n)
    {
        const double fn = n * f0 * std::sqrt(1.0 + inharm * n * n);
        if (fn > sr * 0.45)
            break;

        auto& p = partials[(size_t) numPartials++];
        const float hammerComb = 0.15f + 0.85f * std::abs(std::sin(juce::MathConstants<float>::pi * n * 0.12f));
        p.amp = hammerComb / std::pow((float) n, rolloff);
        ampSum += p.amp;

        for (int s = 0; s < 2; ++s)
        {
            const double cents = (s == 0 ? -detuneCents : detuneCents);
            const double w = twoPi * fn * std::pow(2.0, cents / 1200.0) / sr;
            p.cs[s] = (float) std::cos(w);
            p.sn[s] = (float) std::sin(w);
            const float startPhase = random.nextFloat() * 0.3f;
            p.re[s] = std::cos(startPhase);
            p.im[s] = std::sin(startPhase);
        }

        const float t60 = baseT60 / (1.0f + 0.35f * (n - 1) + 0.0004f * (float) fn);
        p.fast = 0.55f;
        p.slow = 0.45f;
        p.fastMul = decayPerSample(t60 * 0.18f, sr);
        p.slowMul = decayPerSample(t60, sr);
    }

    const float loudness = 0.3f * std::pow(vel, 1.4f) / juce::jmax(0.001f, ampSum);
    for (int i = 0; i < numPartials; ++i)
        partials[(size_t) i].amp *= loudness;

    panGains(note, gainL, gainR);
    attack = 0.0f;
    attackStep = 1.0f / (float) (0.0015 * sr);
    release = 1.0f;
    releasing = false;
    releaseMul = decayPerSample(note < 48 ? 0.45f : 0.28f, sr);

    noiseEnv = 1.0f;
    noiseMul = decayPerSample(0.03f, sr);
    noiseCoef = juce::jlimit(0.02f, 0.6f, (float) (f0 * 6.0 / sr));
    noiseAmp = 0.05f * vel * vel;
    noiseLp = 0.0f;
    samplesSinceNormalise = 0;
}

void PianoVoice::stopNote(float, bool allowTailOff)
{
    if (! allowTailOff)
    {
        clearCurrentNote();
        return;
    }
    releasing = true;
}

void PianoVoice::renderNextBlock(juce::AudioBuffer<float>& out, int start, int num)
{
    if (! isVoiceActive())
        return;

    auto* left = out.getWritePointer(0);
    auto* right = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;

    for (int i = start; i < start + num; ++i)
    {
        float sum = 0.0f;
        for (int k = 0; k < numPartials; ++k)
        {
            auto& p = partials[(size_t) k];
            float s = 0.0f;
            for (int str = 0; str < 2; ++str)
            {
                const float re = p.re[str] * p.cs[str] - p.im[str] * p.sn[str];
                const float im = p.re[str] * p.sn[str] + p.im[str] * p.cs[str];
                p.re[str] = re;
                p.im[str] = im;
                s += im;
            }
            p.fast *= p.fastMul;
            p.slow *= p.slowMul;
            sum += s * p.amp * (p.fast + p.slow);
        }

        noiseLp += noiseCoef * ((random.nextFloat() * 2.0f - 1.0f) - noiseLp);
        sum += noiseLp * noiseAmp * noiseEnv;
        noiseEnv *= noiseMul;

        if (attack < 1.0f)
            attack = juce::jmin(1.0f, attack + attackStep);
        if (releasing)
            release *= releaseMul;

        const float v = sum * attack * release;
        left[i] += v * gainL;
        if (right != nullptr)
            right[i] += v * gainR;
    }

    // Keep the phasors on the unit circle despite float rounding.
    samplesSinceNormalise += num;
    if (samplesSinceNormalise > 4096)
    {
        samplesSinceNormalise = 0;
        for (int k = 0; k < numPartials; ++k)
            for (int str = 0; str < 2; ++str)
            {
                auto& p = partials[(size_t) k];
                const float mag = std::sqrt(p.re[str] * p.re[str] + p.im[str] * p.im[str]);
                p.re[str] /= mag;
                p.im[str] /= mag;
            }
    }

    const float level = numPartials > 0 ? (partials[0].fast + partials[0].slow) * release : 0.0f;
    if (level < 0.0005f || (releasing && release < 0.0005f))
        clearCurrentNote();
}

// ------------------------------------------------------------------ pad

void PadVoice::startNote(int note, float velocity, juce::SynthesiserSound*, int)
{
    const double sr = getSampleRate();
    const double f0 = juce::MidiMessage::getMidiNoteInHertz(note);
    inc[0] = f0 * std::pow(2.0, -7.0 / 1200.0) / sr;
    inc[1] = f0 * std::pow(2.0, 7.0 / 1200.0) / sr;
    inc[2] = f0 * 0.5 / sr;
    phase[0] = 0.0;
    phase[1] = 0.37;
    phase[2] = 0.0;
    const double cutoff = juce::jlimit(600.0, 5000.0, f0 * 6.0);
    lpCoef = (float) (1.0 - std::exp(-twoPi * cutoff / sr));
    level = 0.16f * juce::jlimit(0.1f, 1.0f, velocity);

    adsr.setSampleRate(sr);
    adsr.setParameters({ 0.02f, 0.3f, 0.75f, 0.35f });
    adsr.noteOn();
}

void PadVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
        adsr.noteOff();
    else
    {
        adsr.reset();
        clearCurrentNote();
    }
}

void PadVoice::renderNextBlock(juce::AudioBuffer<float>& out, int start, int num)
{
    if (! isVoiceActive())
        return;

    auto* left = out.getWritePointer(0);
    auto* right = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;

    for (int i = start; i < start + num; ++i)
    {
        float s = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            s += (float) (2.0 * phase[k] - 1.0) - polyBlep(phase[k], inc[k]);
            phase[k] += inc[k];
            if (phase[k] >= 1.0)
                phase[k] -= 1.0;
        }
        s = s * 0.5f + 0.3f * std::sin(twoPi * (float) phase[2]);
        phase[2] += inc[2];
        if (phase[2] >= 1.0)
            phase[2] -= 1.0;

        lp1 += lpCoef * (s - lp1);
        lp2 += lpCoef * (lp1 - lp2);

        const float v = lp2 * level * adsr.getNextSample();
        left[i] += v * 0.7f;
        if (right != nullptr)
            right[i] += v * 0.7f;
    }

    if (! adsr.isActive())
        clearCurrentNote();
}

// ------------------------------------------------------------------ bell

void BellVoice::startNote(int note, float velocity, juce::SynthesiserSound*, int)
{
    inc = juce::MidiMessage::getMidiNoteInHertz(note) / getSampleRate();
    phase = 0.0;
    level = 0.2f * juce::jlimit(0.1f, 1.0f, velocity);
    adsr.setSampleRate(getSampleRate());
    adsr.setParameters({ 0.005f, 1.2f, 0.35f, 0.4f });
    adsr.noteOn();
}

void BellVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
        adsr.noteOff();
    else
    {
        adsr.reset();
        clearCurrentNote();
    }
}

void BellVoice::renderNextBlock(juce::AudioBuffer<float>& out, int start, int num)
{
    if (! isVoiceActive())
        return;

    auto* left = out.getWritePointer(0);
    auto* right = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;

    for (int i = start; i < start + num; ++i)
    {
        const float ph = twoPi * (float) phase;
        const float s = std::sin(ph) + 0.2f * std::sin(2.0f * ph) + 0.05f * std::sin(3.0f * ph);
        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;

        const float v = s * level * adsr.getNextSample();
        left[i] += v * 0.7f;
        if (right != nullptr)
            right[i] += v * 0.7f;
    }

    if (! adsr.isActive())
        clearCurrentNote();
}

} // namespace tos
