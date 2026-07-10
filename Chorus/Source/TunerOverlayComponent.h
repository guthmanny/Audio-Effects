#pragma once

#include <juce_atom_theme/juce_atom_theme.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "QPitchDetector.h"

/** Full-editor dimmed overlay with 12-TET note name and cents MeterBar. */
class TunerOverlayComponent final : public juce::Component
{
public:
    static constexpr int preferredWidth = 420;
    static constexpr int preferredHeight = 340;

    TunerOverlayComponent();

    void setPitchResult (const QPitchDetector::Result& result);
    void clearPitch();

    void setPeriodicityThreshold (float threshold);
    float getPeriodicityThreshold() const;

    std::function<void()> onCloseRequested;
    std::function<void (float)> onPeriodicityThresholdChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void updateMeterFromCents (float cents);

    atom::SettingsCard panel;
    juce::Label titleLabel;
    juce::Label noteLabel;
    juce::Label freqLabel;
    juce::Label centsLabel;
    juce::Label periodicityValueLabel;
    juce::Label flatLabel;
    juce::Label sharpLabel;
    atom::MeterBar centsMeter;
    atom::Slider periodicitySlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerOverlayComponent)
};

/** Dimmed backdrop that hosts TunerOverlayComponent centered. */
class TunerOverlay final : public juce::Component
{
public:
    TunerOverlay()
    {
        addAndMakeVisible (content);
        setInterceptsMouseClicks (true, true);
        setVisible (false);
    }

    TunerOverlayComponent& getContent() noexcept { return content; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xcc000000));
    }

    void resized() override
    {
        content.setBounds (getLocalBounds().withSizeKeepingCentre (TunerOverlayComponent::preferredWidth,
                                                                   TunerOverlayComponent::preferredHeight));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! content.getBounds().contains (e.getPosition()) && content.onCloseRequested)
            content.onCloseRequested();
    }

private:
    TunerOverlayComponent content;
};
