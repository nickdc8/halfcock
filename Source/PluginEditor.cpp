#include "PluginEditor.h"

namespace
{
const juce::Colour panelTop    { 0xff2b2d30 };
const juce::Colour panelBottom { 0xff151618 };
const juce::Colour ink         { 0xfff1eee6 };
const juce::Colour inkDim      { 0xffa29e94 };
const juce::Colour ledColour   { 0xffff3b30 };

// The selector's three positions sit this far either side of straight up.
constexpr float modeSpread = 0.72f;

juce::Font labelFont (float size, int style = juce::Font::bold)
{
    return juce::Font (juce::FontOptions (size, style));
}

void drawChickenHead (juce::Graphics& g, juce::Point<float> centre, float radius, float angle)
{
    const auto turn = juce::AffineTransform::rotation (angle).translated (centre);

    // Pointer body: a long taper forward, a short blunt tail behind.
    juce::Path body;
    body.startNewSubPath (0.0f, -radius);
    body.lineTo (radius * 0.30f, -radius * 0.05f);
    body.lineTo (radius * 0.26f, radius * 0.48f);
    body.lineTo (-radius * 0.26f, radius * 0.48f);
    body.lineTo (-radius * 0.30f, -radius * 0.05f);
    body.closeSubPath();
    body = body.createPathWithRoundedCorners (radius * 0.10f);

    const auto hub = juce::Rectangle<float> (radius * 0.92f, radius * 0.92f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (hub.translated (0.0f, 3.0f));
    g.fillPath (body, turn.translated (0.0f, 3.0f));

    g.setGradientFill ({ juce::Colour (0xff3a3a3a), centre.x, centre.y - radius,
                         juce::Colour (0xff080808), centre.x, centre.y + radius, false });
    g.fillEllipse (hub);
    g.fillPath (body, turn);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (body, juce::PathStrokeType (1.0f), turn);

    juce::Path line;
    line.addRoundedRectangle (-1.3f, -radius + 3.0f, 2.6f, radius * 0.62f, 1.3f);
    g.setColour (ink);
    g.fillPath (line, turn);
}
}

void WahLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float startAngle, float endAngle,
                                       juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    if (slider.getName() == "mode")
    {
        drawChickenHead (g, centre, radius, angle);
        return;
    }

    // Scale ticks around the knob.
    g.setColour (ink.withAlpha (0.85f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        g.drawLine ({ centre + dir * (radius + 3.0f), centre + dir * (radius + 8.0f) },
                    i % 5 == 0 ? 1.8f : 1.0f);
    }

    // Skirt, then the black cap.
    const auto skirt = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (skirt.translated (0.0f, 3.0f));
    g.setGradientFill ({ juce::Colour (0xff444444), centre.x, skirt.getY(),
                         juce::Colour (0xff0c0c0c), centre.x, skirt.getBottom(), false });
    g.fillEllipse (skirt);

    const auto cap = skirt.reduced (radius * 0.22f);
    g.setGradientFill ({ juce::Colour (0xff2a2a2a), centre.x, cap.getY(),
                         juce::Colour (0xff050505), centre.x, cap.getBottom(), false });
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (cap, 1.0f);

    // Pointer.
    const float w = juce::jmax (3.0f, radius * 0.06f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-w * 0.5f, -radius + 2.0f, w, radius * 0.55f, w * 0.5f);
    g.setColour (ink);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void StompButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // Hex nut and washer.
    juce::Path nut;
    nut.addPolygon (centre, 6, radius, 0.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillPath (nut, juce::AffineTransform::translation (0.0f, 3.0f));
    g.setGradientFill ({ juce::Colour (0xffd8d8d8), centre.x, bounds.getY(),
                         juce::Colour (0xff6e6e6e), centre.x, bounds.getBottom(), false });
    g.fillPath (nut);

    // The plunger sinks a little while held.
    const float inset = down ? 0.36f : 0.30f;
    const auto plunger = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                             .withCentre (centre).reduced (radius * inset);
    g.setColour (juce::Colour (0xff3c3c3c));
    g.fillEllipse (plunger.expanded (2.0f));
    g.setGradientFill ({ juce::Colour (down ? 0xffbdbdbd : 0xfff4f4f4), centre.x, plunger.getY(),
                         juce::Colour (0xff808080), centre.x, plunger.getBottom(), false });
    g.fillEllipse (plunger);

    if (highlighted && ! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillEllipse (plunger);
    }
}

HalfcockEditor::HalfcockEditor (HalfcockProcessor& p)
    : AudioProcessorEditor (p),
      wahProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    addKnob (frequency, "frequency");
    addKnob (volume, "volume");
    addKnob (pickup, "pickup");

    // Three detents, a short throw either side of straight up.
    mode.slider.setName ("mode");
    addKnob (mode, "mode");
    mode.slider.setPopupDisplayEnabled (false, false, nullptr);
    mode.slider.setRotaryParameters (juce::MathConstants<float>::twoPi - modeSpread,
                                     juce::MathConstants<float>::twoPi + modeSpread, true);
    mode.slider.setMouseDragSensitivity (60);

    addAndMakeVisible (footswitch);
    footswitchAttachment = std::make_unique<ButtonAttachment> (p.apvts, "on", footswitch);

    setSize (460, 520);
    timerCallback();
    startTimerHz (30);
}

HalfcockEditor::~HalfcockEditor()
{
    setLookAndFeel (nullptr);
}

void HalfcockEditor::addKnob (Knob& knob, const juce::String& paramId)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.slider.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (wahProcessor.apvts, paramId, knob.slider);
}

void HalfcockEditor::timerCallback()
{
    // The LED is lit when the pedal is on, and a touch brighter as the
    // resonance climbs.
    float next = 0.0f;
    if (wahProcessor.isOn())
    {
        const float octaves = std::log2 (juce::jmax (100.0f, wahProcessor.getResonanceHz()) / 300.0f);
        next = juce::jlimit (0.55f, 1.0f, 0.55f + 0.14f * octaves);
    }

    if (std::abs (next - led) > 0.004f)
    {
        led = next;
        repaint (ledArea.expanded (36));
    }
}

void HalfcockEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill ({ panelTop, 0.0f, 0.0f, panelBottom, 0.0f, bounds.getBottom(), false });
    g.fillAll();

    // Hammertone-ish speckle, seeded so it doesn't shimmer on repaint.
    juce::Random random (20);
    for (int i = 0; i < 1800; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (random.nextFloat() * 0.035f));
        g.fillEllipse (random.nextFloat() * bounds.getWidth(), random.nextFloat() * bounds.getHeight(),
                       2.5f, 1.5f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (6.0f), 10.0f, 1.5f);

    // LED.
    const float lit = juce::jmax (0.0f, led);
    const auto ledCentre = ledArea.toFloat().getCentre();
    const float glow = 18.0f + 16.0f * lit;
    g.setGradientFill ({ ledColour.withAlpha (0.55f * lit), ledCentre.x, ledCentre.y,
                         ledColour.withAlpha (0.0f), ledCentre.x + glow, ledCentre.y, true });
    g.fillEllipse (juce::Rectangle<float> (glow * 2.0f, glow * 2.0f).withCentre (ledCentre));
    g.setColour (juce::Colour (0xffb9b9b9));
    g.fillEllipse (ledArea.toFloat().expanded (3.0f));
    g.setColour (juce::Colour (0xff3a0d0b).interpolatedWith (juce::Colour (0xffff6a5e), lit));
    g.fillEllipse (ledArea.toFloat());

    // Mode legends, fanned around the selector.
    {
        const auto area = mode.slider.getBounds().toFloat();
        const auto centre = area.getCentre();
        const float reach = area.getWidth() * 0.5f + 6.0f;
        const char* const names[3] = { "Wacked", "Jimi", "Shaft" };
        const juce::Justification sides[3] = { juce::Justification::centredRight,
                                               juce::Justification::centred,
                                               juce::Justification::centredLeft };
        g.setColour (ink);
        g.setFont (labelFont (14.0f));
        for (int i = 0; i < 3; ++i)
        {
            const float a = (float) (i - 1) * modeSpread;
            const auto at = centre + juce::Point<float> (std::sin (a), -std::cos (a)) * reach;
            auto box = juce::Rectangle<float> (80.0f, 18.0f).withCentre (at);
            if (i == 0) box = box.withRightX (at.x + 4.0f);
            if (i == 2) box = box.withX (at.x - 4.0f);
            g.drawText (names[i], box, sides[i]);
        }
    }

    // Knob captions.
    g.setColour (ink);
    g.setFont (labelFont (14.0f));
    {
        const auto v = volume.slider.getBounds();
        g.drawText ("Volume", v.getX() - 20, v.getY() - 16, v.getWidth() + 40, 18, juce::Justification::centred);

        const auto f = frequency.slider.getBounds();
        g.drawText ("Frequency", f.getX() - 96, f.getBottom() - 40, 100, 18, juce::Justification::centredRight);

        const auto s = footswitch.getBounds();
        g.drawText ("On/Off", s.getRight() + 6, s.getBottom() - 26, 70, 18, juce::Justification::centredLeft);
    }

    g.setFont (labelFont (10.5f));
    {
        const auto c = pickup.slider.getBounds();
        g.drawText ("Pickup", c.getX() - 20, c.getY() - 13, c.getWidth() + 40, 14, juce::Justification::centred);
        g.drawText ("Compensator", c.getX() - 20, c.getBottom() - 1, c.getWidth() + 40, 14, juce::Justification::centred);
    }

    // Name plate.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.setFont (labelFont (54.0f, juce::Font::bold | juce::Font::italic));
    g.drawText ("Halfcock", 0, 243, getWidth(), 64, juce::Justification::centred);
    g.setColour (ink);
    g.drawText ("Halfcock", 0, 240, getWidth(), 64, juce::Justification::centred);
    g.setColour (inkDim);
    g.setFont (labelFont (11.5f, juce::Font::plain));
    g.drawText ("P A R K E D   W A H", 0, 302, getWidth(), 16, juce::Justification::centred);
}

void HalfcockEditor::resized()
{
    ledArea = juce::Rectangle<int> (14, 14).withCentre ({ getWidth() / 2, 42 });

    mode.slider.setBounds (juce::Rectangle<int> (112, 112).withCentre ({ 108, 150 }));
    pickup.slider.setBounds (juce::Rectangle<int> (64, 64).withCentre ({ getWidth() / 2, 158 }));
    volume.slider.setBounds (juce::Rectangle<int> (124, 124).withCentre ({ 352, 142 }));

    footswitch.setBounds (juce::Rectangle<int> (76, 76).withCentre ({ 84, 430 }));
    frequency.slider.setBounds (juce::Rectangle<int> (176, 176).withCentre ({ 346, 410 }));
}
