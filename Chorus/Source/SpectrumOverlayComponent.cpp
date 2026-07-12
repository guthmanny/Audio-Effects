#include "SpectrumOverlayComponent.h"

#include "SpectrumAnalyzer.h"

#include <cmath>

namespace
{
constexpr int kFftSizeChoices[] = { 1024, 2048, 4096, 8192, 16384, 32768, 65536 };

/** Vertical display spans: top is always 0 dB, bottom is -span. */
constexpr float kRangeSpanDb[] = { 40.0f, 60.0f, 80.0f, 90.0f, 100.0f, 120.0f, 150.0f, 180.0f, 200.0f };
constexpr int kDefaultRangeIndex = 3; // 90 dB
} // namespace

SpectrumOverlayComponent::SpectrumOverlayComponent()
{
    titleLabel.setText ("Spectrum", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (AtomLookAndFeel::getUIFont (18.0f, juce::Font::bold));

    fftSizeLabel.setText ("FFT Size", juce::dontSendNotification);
    fftSizeLabel.setJustificationType (juce::Justification::centredLeft);
    fftSizeLabel.setFont (AtomLookAndFeel::getUIFont (13.0f, juce::Font::plain));

    fftSizeCombo.setEditableText (false);
    fftSizeCombo.setJustificationType (juce::Justification::centredLeft);
    for (int i = 0; i < (int) std::size (kFftSizeChoices); ++i)
        fftSizeCombo.addItem (juce::String (kFftSizeChoices[i]), i + 1);
    fftSizeCombo.setSelectedId (2, juce::dontSendNotification); // 2048

    fftSizeCombo.onChange = [this]
    {
        const int id = fftSizeCombo.getSelectedId();
        if (id <= 0 || onFftSizeChanged == nullptr)
            return;

        onFftSizeChanged (kFftSizeChoices[id - 1]);
    };

    rangeLabel.setText ("Range", juce::dontSendNotification);
    rangeLabel.setJustificationType (juce::Justification::centredLeft);
    rangeLabel.setFont (AtomLookAndFeel::getUIFont (13.0f, juce::Font::plain));

    rangeCombo.setEditableText (false);
    rangeCombo.setJustificationType (juce::Justification::centredLeft);
    for (int i = 0; i < (int) std::size (kRangeSpanDb); ++i)
        rangeCombo.addItem ("0 / -" + juce::String ((int) kRangeSpanDb[i]) + " dB", i + 1);
    rangeCombo.setSelectedId (kDefaultRangeIndex + 1, juce::dontSendNotification);

    rangeCombo.onChange = [this]
    {
        const int id = rangeCombo.getSelectedId();
        if (id <= 0)
            return;

        const float span = kRangeSpanDb[id - 1];
        spectrumCurve.setVerticalRangeDb (-span, 0.0f);
    };

    scaleLabel.setText ("Scale", juce::dontSendNotification);
    scaleLabel.setJustificationType (juce::Justification::centredLeft);
    scaleLabel.setFont (AtomLookAndFeel::getUIFont (13.0f, juce::Font::plain));

    scaleCombo.setEditableText (false);
    scaleCombo.setJustificationType (juce::Justification::centredLeft);
    scaleCombo.addItem ("Log", 1);
    scaleCombo.addItem ("Linear", 2);
    scaleCombo.setSelectedId (1, juce::dontSendNotification);

    scaleCombo.onChange = [this]
    {
        const int id = scaleCombo.getSelectedId();
        if (id <= 0)
            return;

        spectrumCurve.setFrequencyScale (id == 1 ? SpectrumCurveComponent::FrequencyScale::Logarithmic
                                                 : SpectrumCurveComponent::FrequencyScale::Linear);
    };

    interpLabel.setText ("Interp", juce::dontSendNotification);
    interpLabel.setJustificationType (juce::Justification::centredLeft);
    interpLabel.setFont (AtomLookAndFeel::getUIFont (13.0f, juce::Font::plain));

    interpCombo.setEditableText (false);
    interpCombo.setJustificationType (juce::Justification::centredLeft);
    interpCombo.addItem ("Nearest", 1);
    interpCombo.addItem ("Linear", 2);
    interpCombo.addItem ("Cubic", 3);
    interpCombo.setSelectedId (2, juce::dontSendNotification);

    interpCombo.onChange = [this]
    {
        const int id = interpCombo.getSelectedId();
        if (id <= 0)
            return;

        using Method = SpectrumCurveComponent::InterpolationMethod;
        Method method = Method::Linear;
        if (id == 1)
            method = Method::Nearest;
        else if (id == 3)
            method = Method::Cubic;

        spectrumCurve.setInterpolationMethod (method);
    };

    spectrumCurve.setVerticalRangeDb (-kRangeSpanDb[kDefaultRangeIndex], 0.0f);
    spectrumCurve.setFrequencyScale (SpectrumCurveComponent::FrequencyScale::Logarithmic);
    spectrumCurve.setInterpolationMethod (SpectrumCurveComponent::InterpolationMethod::Linear);

    closeButton.onClick = [this]
    {
        if (onCloseRequested)
            onCloseRequested();
    };

    panel.addAndMakeVisible (titleLabel);
    panel.addAndMakeVisible (fftSizeLabel);
    panel.addAndMakeVisible (fftSizeCombo);
    panel.addAndMakeVisible (rangeLabel);
    panel.addAndMakeVisible (rangeCombo);
    panel.addAndMakeVisible (scaleLabel);
    panel.addAndMakeVisible (scaleCombo);
    panel.addAndMakeVisible (interpLabel);
    panel.addAndMakeVisible (interpCombo);
    panel.addAndMakeVisible (spectrumCurve);
    panel.addAndMakeVisible (closeButton);
    addAndMakeVisible (panel);
}

void SpectrumOverlayComponent::setSpectrumMagnitudes (const std::vector<float>& magnitudesDb,
                                                      double sampleRate,
                                                      int fftSize)
{
    spectrumCurve.setSpectrumMagnitudes (magnitudesDb, sampleRate, fftSize);
}

void SpectrumOverlayComponent::setSpectrumMagnitudes (std::vector<float>& magnitudesDb,
                                                      double sampleRate,
                                                      int fftSize)
{
    spectrumCurve.setSpectrumMagnitudes (magnitudesDb, sampleRate, fftSize);
}

void SpectrumOverlayComponent::clearSpectrum()
{
    spectrumCurve.clearSpectrum();
}

void SpectrumOverlayComponent::setFftSize (int fftSize)
{
    const int clamped = SpectrumAnalyzer::clampFftSize (fftSize);
    for (int i = 0; i < (int) std::size (kFftSizeChoices); ++i)
    {
        if (kFftSizeChoices[i] == clamped)
        {
            fftSizeCombo.setSelectedId (i + 1, juce::dontSendNotification);
            return;
        }
    }
}

int SpectrumOverlayComponent::getFftSize() const
{
    const int id = fftSizeCombo.getSelectedId();
    if (id <= 0)
        return 1 << SpectrumAnalyzer::defaultFftOrder;
    return kFftSizeChoices[id - 1];
}

void SpectrumOverlayComponent::setVerticalRangeDb (float minDb, float maxDb)
{
    spectrumCurve.setVerticalRangeDb (minDb, maxDb);

    const float span = maxDb - minDb;
    for (int i = 0; i < (int) std::size (kRangeSpanDb); ++i)
    {
        if (std::abs (kRangeSpanDb[i] - span) < 0.1f && std::abs (maxDb) < 0.1f)
        {
            rangeCombo.setSelectedId (i + 1, juce::dontSendNotification);
            return;
        }
    }
}

float SpectrumOverlayComponent::getMagMinDb() const
{
    return spectrumCurve.getMagMinDb();
}

float SpectrumOverlayComponent::getMagMaxDb() const
{
    return spectrumCurve.getMagMaxDb();
}

void SpectrumOverlayComponent::setFrequencyScale (SpectrumCurveComponent::FrequencyScale scale)
{
    spectrumCurve.setFrequencyScale (scale);
    scaleCombo.setSelectedId (scale == SpectrumCurveComponent::FrequencyScale::Logarithmic ? 1 : 2,
                              juce::dontSendNotification);
}

SpectrumCurveComponent::FrequencyScale SpectrumOverlayComponent::getFrequencyScale() const
{
    return spectrumCurve.getFrequencyScale();
}

void SpectrumOverlayComponent::setInterpolationMethod (SpectrumCurveComponent::InterpolationMethod method)
{
    spectrumCurve.setInterpolationMethod (method);

    int id = 2;
    if (method == SpectrumCurveComponent::InterpolationMethod::Nearest)
        id = 1;
    else if (method == SpectrumCurveComponent::InterpolationMethod::Cubic)
        id = 3;

    interpCombo.setSelectedId (id, juce::dontSendNotification);
}

SpectrumCurveComponent::InterpolationMethod SpectrumOverlayComponent::getInterpolationMethod() const
{
    return spectrumCurve.getInterpolationMethod();
}

void SpectrumOverlayComponent::paint (juce::Graphics&)
{
}

void SpectrumOverlayComponent::resized()
{
    panel.setBounds (getLocalBounds());
    auto area = panel.getLocalBounds().reduced (20, 16);

    titleLabel.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);

    auto sizeRow = area.removeFromTop (32);
    fftSizeLabel.setBounds (sizeRow.removeFromLeft (64));
    sizeRow.removeFromLeft (6);
    fftSizeCombo.setBounds (sizeRow.removeFromLeft (100));
    sizeRow.removeFromLeft (12);
    rangeLabel.setBounds (sizeRow.removeFromLeft (48));
    sizeRow.removeFromLeft (6);
    rangeCombo.setBounds (sizeRow.removeFromLeft (130));
    sizeRow.removeFromLeft (12);
    scaleLabel.setBounds (sizeRow.removeFromLeft (44));
    sizeRow.removeFromLeft (6);
    scaleCombo.setBounds (sizeRow.removeFromLeft (90));
    area.removeFromTop (8);

    auto interpRow = area.removeFromTop (32);
    interpLabel.setBounds (interpRow.removeFromLeft (48));
    interpRow.removeFromLeft (6);
    interpCombo.setBounds (interpRow.removeFromLeft (110));
    area.removeFromTop (10);

    closeButton.setBounds (area.removeFromBottom (34).withSizeKeepingCentre (120, 34));
    area.removeFromBottom (12);

    spectrumCurve.setBounds (area);
}
