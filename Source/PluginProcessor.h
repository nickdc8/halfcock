#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "WahDSP.h"

class HalfcockProcessor final : public juce::AudioProcessor
{
public:
    HalfcockProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Halfcock"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    float getResonanceHz() const { return engine.getResonanceHz(); }
    bool isOn() const { return on->load() > 0.5f; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    wah::Params readParams() const;

    wah::Engine engine;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    std::atomic<float>* frequency = nullptr;
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* pickup = nullptr;
    std::atomic<float>* volume = nullptr;
    std::atomic<float>* on = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HalfcockProcessor)
};
