#pragma once

#include "PluginProcessor.h"

class WahLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Sliders named "mode" are drawn as a chicken-head selector; the rest as knobs.
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

// The footswitch: a chrome stomp button that toggles.
class StompButton final : public juce::Button
{
public:
    StompButton() : Button ("On/Off") { setClickingTogglesState (true); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class HalfcockEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit HalfcockEditor (HalfcockProcessor&);
    ~HalfcockEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob&, const juce::String& paramId);

    HalfcockProcessor& wahProcessor;
    WahLookAndFeel lookAndFeel;

    Knob frequency, volume, pickup, mode;
    StompButton footswitch;
    std::unique_ptr<ButtonAttachment> footswitchAttachment;

    juce::Rectangle<int> ledArea;
    float led = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HalfcockEditor)
};
