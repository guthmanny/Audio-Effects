#pragma once

#include <juce_atom_theme/juce_atom_theme.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "SpectrumCurveComponent.h"

/** Full-editor dimmed overlay with live STFT spectrum (CurveControl styled). */
class SpectrumOverlayComponent final : public juce::Component
{
public:
    static constexpr int preferredWidth = 640;
    static constexpr int preferredHeight = 460;

    SpectrumOverlayComponent();

    void setSpectrumMagnitudes (const std::vector<float>& magnitudesDb,
                                double sampleRate,
                                int fftSize);
    void setSpectrumMagnitudes (std::vector<float>& magnitudesDb,
                                double sampleRate,
                                int fftSize);
    void clearSpectrum();

    void setFftSize (int fftSize);
    int getFftSize() const;

    void setVerticalRangeDb (float minDb, float maxDb);
    float getMagMinDb() const;
    float getMagMaxDb() const;

    void setFrequencyScale (SpectrumCurveComponent::FrequencyScale scale);
    SpectrumCurveComponent::FrequencyScale getFrequencyScale() const;

    void setInterpolationMethod (SpectrumCurveComponent::InterpolationMethod method);
    SpectrumCurveComponent::InterpolationMethod getInterpolationMethod() const;

    std::function<void()> onCloseRequested;
    std::function<void (int)> onFftSizeChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    atom::SettingsCard panel;
    juce::Label titleLabel;
    juce::Label fftSizeLabel;
    atom::ComboBox fftSizeCombo;
    juce::Label rangeLabel;
    atom::ComboBox rangeCombo;
    juce::Label scaleLabel;
    atom::ComboBox scaleCombo;
    juce::Label interpLabel;
    atom::ComboBox interpCombo;
    SpectrumCurveComponent spectrumCurve;
    juce::TextButton closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumOverlayComponent)
};

/** Dimmed backdrop that hosts SpectrumOverlayComponent centered. */
class SpectrumOverlay final : public juce::Component
{
public:
    SpectrumOverlay()
    {
        addAndMakeVisible (content);
        setInterceptsMouseClicks (true, true);
        setVisible (false);
    }

    SpectrumOverlayComponent& getContent() noexcept { return content; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xcc000000));
    }

    void resized() override
    {
        content.setBounds (getLocalBounds().withSizeKeepingCentre (SpectrumOverlayComponent::preferredWidth,
                                                                   SpectrumOverlayComponent::preferredHeight));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! content.getBounds().contains (e.getPosition()) && content.onCloseRequested)
            content.onCloseRequested();
    }

private:
    SpectrumOverlayComponent content;
};
