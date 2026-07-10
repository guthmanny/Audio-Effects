#include "DistortionStartupProgressOverlay.h"

DistortionStartupProgressOverlay::DistortionStartupProgressOverlay()
{
    setSize (preferredWidth, preferredHeight);
    setOpaque (true);
}

void DistortionStartupProgressOverlay::setProgress (float newProgress, const juce::String& statusMessage)
{
    progress = juce::jlimit (0.0f, 1.0f, newProgress);
    statusText = statusMessage;
    repaint();
}

void DistortionStartupProgressOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    auto area = getLocalBounds().reduced (24, 20);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (16.0f, juce::Font::bold));
    g.drawFittedText (titleText, area.removeFromTop (28), juce::Justification::centred, 1);

    area.removeFromTop (8);

    g.setFont (juce::Font (13.0f, juce::Font::plain));
    g.drawFittedText (statusText, area.removeFromTop (24), juce::Justification::centred, 2);

    area.removeFromTop (12);

    const auto barArea = area.removeFromTop (10).toFloat();
    g.setColour (juce::Colour (0xff303030));
    g.fillRoundedRectangle (barArea, 2.0f);

    if (progress > 0.0f)
    {
        auto filled = barArea;
        filled.setWidth (barArea.getWidth() * progress);
        g.setColour (juce::Colour (0xff22cc55));
        g.fillRoundedRectangle (filled, 2.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::Font (11.0f, juce::Font::plain));
    g.drawText (juce::String (juce::roundToInt (progress * 100.0f)) + "%",
                area.removeFromTop (18),
                juce::Justification::centred);
}
