#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

class DistortionStartupProgressOverlay final : public juce::Component
{
public:
    static constexpr int preferredWidth = 420;
    static constexpr int preferredHeight = 140;

    DistortionStartupProgressOverlay();

    void setProgress (float progress, const juce::String& statusMessage);
    void paint (juce::Graphics& g) override;

private:
    juce::String titleText { "Loading Distortion" };
    juce::String statusText { "Initializing distortion engines..." };
    float progress = 0.0f;
};
