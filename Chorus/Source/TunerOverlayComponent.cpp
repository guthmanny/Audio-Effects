#include "TunerOverlayComponent.h"

#include <cmath>

namespace
{
void setupCentsMeter (atom::MeterBar& meter)
{
    meter.setBarCount (1);
    meter.setOrientation (atom::MeterBar::Orientation::Horizontal);
    meter.setPeakHoldEnabled (false);
    meter.setSegmentCount (0);
    meter.setRefreshRateHz (60);
    meter.setShowValueText (false);
    meter.setValueRange (-50.0, 50.0);
    meter.setValueSuffix (" ct");
    meter.setValueDecimals (0);

    atom::MeterBarStyleOverride style;
    style.colors.peak = juce::Colours::white;
    style.metrics.peakThickness = 0.0f;
    style.metrics.roundness = 4.0f;
    style.metrics.outerPadding = 2.0f;
    style.metrics.barGap = 0.0f;
    style.metrics.segmentGap = 0.0f;
    style.metrics.clipZoneThreshold = 1.0f;
    style.metrics.clipHoldTimeSec = 0.0f;
    meter.setStyleOverride (style);
}
} // namespace

TunerOverlayComponent::TunerOverlayComponent()
{
    titleLabel.setText ("Tuner", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (AtomLookAndFeel::getUIFont (18.0f, juce::Font::bold));

    noteLabel.setText ("--", juce::dontSendNotification);
    noteLabel.setJustificationType (juce::Justification::centred);
    noteLabel.setFont (AtomLookAndFeel::getUIFont (64.0f, juce::Font::bold));

    freqLabel.setText ("-- Hz", juce::dontSendNotification);
    freqLabel.setJustificationType (juce::Justification::centred);
    freqLabel.setFont (AtomLookAndFeel::getUIFont (14.0f, juce::Font::plain));

    centsLabel.setText ("-- cents", juce::dontSendNotification);
    centsLabel.setJustificationType (juce::Justification::centred);
    centsLabel.setFont (AtomLookAndFeel::getUIFont (13.0f, juce::Font::plain));

    periodicityValueLabel.setText ("P --", juce::dontSendNotification);
    periodicityValueLabel.setJustificationType (juce::Justification::centred);
    periodicityValueLabel.setFont (AtomLookAndFeel::getUIFont (12.0f, juce::Font::plain));

    flatLabel.setText ("b", juce::dontSendNotification);
    flatLabel.setJustificationType (juce::Justification::centredRight);
    flatLabel.setFont (AtomLookAndFeel::getUIFont (16.0f, juce::Font::bold));

    sharpLabel.setText ("#", juce::dontSendNotification);
    sharpLabel.setJustificationType (juce::Justification::centredLeft);
    sharpLabel.setFont (AtomLookAndFeel::getUIFont (16.0f, juce::Font::bold));

    setupCentsMeter (centsMeter);
    centsMeter.setLevels ({ 0.5f });

    periodicitySlider.setRange (0.0, 1.0, 0.01);
    periodicitySlider.setValue (0.70, juce::dontSendNotification);
    periodicitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    periodicitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    periodicitySlider.setCustomText ("Periodicity");
    periodicitySlider.setValueLabelPos (atom::Slider::ValueLabelPos::Left);
    periodicitySlider.onValueChange = [this]
    {
        if (onPeriodicityThresholdChanged)
            onPeriodicityThresholdChanged ((float) periodicitySlider.getValue());
    };

    closeButton.onClick = [this]
    {
        if (onCloseRequested)
            onCloseRequested();
    };

    panel.addAndMakeVisible (titleLabel);
    panel.addAndMakeVisible (noteLabel);
    panel.addAndMakeVisible (freqLabel);
    panel.addAndMakeVisible (centsLabel);
    panel.addAndMakeVisible (periodicityValueLabel);
    panel.addAndMakeVisible (flatLabel);
    panel.addAndMakeVisible (sharpLabel);
    panel.addAndMakeVisible (centsMeter);
    panel.addAndMakeVisible (periodicitySlider);
    panel.addAndMakeVisible (closeButton);
    addAndMakeVisible (panel);
}

void TunerOverlayComponent::setPeriodicityThreshold (float threshold)
{
    periodicitySlider.setValue ((double) juce::jlimit (0.0f, 1.0f, threshold), juce::dontSendNotification);
}

float TunerOverlayComponent::getPeriodicityThreshold() const
{
    return (float) periodicitySlider.getValue();
}

void TunerOverlayComponent::clearPitch()
{
    noteLabel.setText ("--", juce::dontSendNotification);
    freqLabel.setText ("-- Hz", juce::dontSendNotification);
    centsLabel.setText ("-- cents", juce::dontSendNotification);
    periodicityValueLabel.setText ("P --", juce::dontSendNotification);
    centsMeter.setLevels ({ 0.5f });
}

void TunerOverlayComponent::updateMeterFromCents (float cents)
{
    // Map [-50, +50] cents -> [0, 1] for MeterBar.
    const float clamped = juce::jlimit (-50.0f, 50.0f, cents);
    const float norm = (clamped + 50.0f) / 100.0f;
    centsMeter.setLevels ({ norm });
}

void TunerOverlayComponent::setPitchResult (const QPitchDetector::Result& result)
{
    if (! result.valid)
    {
        // Keep last measured periodicity visible when gated out by threshold.
        if (result.periodicity > 0.0f)
            periodicityValueLabel.setText ("P " + juce::String (result.periodicity, 2)
                                               + "  <  thr " + juce::String (getPeriodicityThreshold(), 2),
                                           juce::dontSendNotification);
        else
            periodicityValueLabel.setText ("P --", juce::dontSendNotification);

        noteLabel.setText ("--", juce::dontSendNotification);
        freqLabel.setText ("-- Hz", juce::dontSendNotification);
        centsLabel.setText ("-- cents", juce::dontSendNotification);
        centsMeter.setLevels ({ 0.5f });
        return;
    }

    const juce::String noteText = juce::String (QPitchDetector::noteName (result.noteIndex))
                                + juce::String (result.octave);
    noteLabel.setText (noteText, juce::dontSendNotification);
    freqLabel.setText (juce::String (result.frequencyHz, 1) + " Hz", juce::dontSendNotification);

    const juce::String sign = result.cents >= 0.0f ? "+" : "";
    centsLabel.setText (sign + juce::String (result.cents, 1) + " cents", juce::dontSendNotification);
    periodicityValueLabel.setText ("P " + juce::String (result.periodicity, 2), juce::dontSendNotification);
    updateMeterFromCents (result.cents);
}

void TunerOverlayComponent::paint (juce::Graphics&)
{
}

void TunerOverlayComponent::resized()
{
    panel.setBounds (getLocalBounds());
    auto area = panel.getLocalBounds().reduced (20, 16);

    titleLabel.setBounds (area.removeFromTop (28));
    area.removeFromTop (4);
    noteLabel.setBounds (area.removeFromTop (72));
    freqLabel.setBounds (area.removeFromTop (22));
    centsLabel.setBounds (area.removeFromTop (20));
    periodicityValueLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (8);

    auto meterRow = area.removeFromTop (36);
    flatLabel.setBounds (meterRow.removeFromLeft (28));
    sharpLabel.setBounds (meterRow.removeFromRight (28));
    meterRow.reduce (8, 4);
    centsMeter.setBounds (meterRow);

    area.removeFromTop (12);
    periodicitySlider.setBounds (area.removeFromTop (36));

    area.removeFromTop (12);
    closeButton.setBounds (area.removeFromBottom (34).withSizeKeepingCentre (120, 34));
}
