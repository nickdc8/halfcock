#include "PluginProcessor.h"
#include "PluginEditor.h"

HalfcockProcessor::HalfcockProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    frequency = apvts.getRawParameterValue ("frequency");
    mode      = apvts.getRawParameterValue ("mode");
    pickup    = apvts.getRawParameterValue ("pickup");
    volume    = apvts.getRawParameterValue ("volume");
    on        = apvts.getRawParameterValue ("on");
}

juce::AudioProcessorValueTreeState::ParameterLayout HalfcockProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The knob that replaces the treadle. Automate this (or map an expression
    // pedal to it) for a moving wah.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "frequency", 1 }, "Frequency", Range (0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 }, "Mode", juce::StringArray { "Wacked", "Jimi", "Shaft" }, 1));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "pickup", 1 }, "Pickup Compensator", Range (0.0f, 1.0f, 0.001f), 0.5f));

    // Unity at noon, about 10 dB either way, as on the pedal.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "volume", 1 }, "Volume", Range (-10.0f, 10.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "on", 1 }, "On", true));

    return layout;
}

wah::Params HalfcockProcessor::readParams() const
{
    wah::Params p;
    p.frequency = frequency->load();
    p.mode      = juce::roundToInt (mode->load());
    p.pickup    = pickup->load();
    p.volume    = juce::Decibels::decibelsToGain (volume->load());
    p.on        = on->load() > 0.5f;
    return p;
}

bool HalfcockProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == out;
}

void HalfcockProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 2x oversampling: Q1 clips, and the circuit solver wants the headroom.
    // The dry (bypassed) signal goes through the same path, so it stays
    // time-aligned with the effect.
    const auto channels = (size_t) juce::jmax (1, getTotalNumOutputChannels());
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        channels, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples (juce::roundToInt (oversampling->getLatencyInSamples()));

    engine.setParams (readParams());
    engine.prepare (sampleRate * 2.0);
}

void HalfcockProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setParams (readParams());

    juce::dsp::AudioBlock<float> block (buffer);
    auto up = oversampling->processSamplesUp (block);

    float* channels[wah::Engine::kMaxChannels] = {};
    const int numChannels = juce::jmin ((int) up.getNumChannels(), wah::Engine::kMaxChannels);
    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = up.getChannelPointer ((size_t) ch);

    engine.process (channels, numChannels, (int) up.getNumSamples());

    oversampling->processSamplesDown (block);
}

juce::AudioProcessorEditor* HalfcockProcessor::createEditor()
{
    return new HalfcockEditor (*this);
}

void HalfcockProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HalfcockProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HalfcockProcessor();
}
