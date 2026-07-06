#include "DelayHeaderComponent.h"

#include <cmath>

namespace
{
void setupRotarySlider (atom::Slider& slider, const juce::String& label, double min, double max, double value)
{
    slider.setRange (min, max, (max - min) > 100.0 ? 0.1 : 0.01);
    slider.setValue (value, juce::dontSendNotification);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f,
                                true);
    slider.setValueLabelPos (atom::Slider::ValueLabelPos::Below);
    slider.setCustomText (label);
    slider.setRequiredWidthMode (atom::Slider::RequiredWidthMode::Content);
    slider.setValueLabelGap (0);
}
} // namespace

DelayHeaderComponent::DelayHeaderComponent()
{
    setupMeter (meterLeft, 1);
    setupMeter (meterRight, 2);
    addAndMakeVisible (meterLeft);
    addAndMakeVisible (meterRight);

    setupRotarySlider (sliderInput, "INPUT", -12.0, 12.0, 0.0);
    setupRotarySlider (sliderGate, "GATE", -80.0, 0.0, -40.0);
    setupRotarySlider (sliderOutput, "OUTPUT", -100.0, 0.0, 0.0);
    addAndMakeVisible (sliderInput);
    addAndMakeVisible (sliderGate);
    addAndMakeVisible (sliderOutput);

    btnSettings.setTooltip ("Settings");
    addAndMakeVisible (btnSettings);

    atom::TapCircle::TapConfig tapConfig;
    tapConfig.initialBpm = 30.0;
    tapConfig.minTapBpm = 1.0;
    tapConfig.maxTapBpm = 1200.0;
    tapTempo.setTapConfig (tapConfig);
    addAndMakeVisible (tapTempo);

    startTimerHz (30);
}

DelayHeaderComponent::~DelayHeaderComponent()
{
    stopTimer();
}

void DelayHeaderComponent::setupMeter (atom::MeterBar& meter, int barCount)
{
    meter.setBarCount (barCount);
    meter.setOrientation (atom::MeterBar::Orientation::Vertical);
    meter.setPeakHoldEnabled (true);
    meter.setSegmentCount (0);
    meter.setRefreshRateHz (60);
    meter.setPeakHoldTimeMs (0);
    meter.setPeakReleaseRate (0.015f);

    atom::MeterBarStyleOverride style;
    style.colors.peak = juce::Colours::white;
    style.metrics.peakThickness = 0.0f;
    style.metrics.roundness = 0.0f;
    style.metrics.outerPadding = 2.0f;
    style.metrics.barGap = barCount > 1 ? 1.0f : 0.0f;
    style.metrics.segmentGap = 0.0f;
    style.metrics.clipZoneThreshold = 0.95f;
    style.metrics.clipHoldTimeSec = 3.0f;
    meter.setStyleOverride (style);
}

void DelayHeaderComponent::setMeterLevels (float left, float rightL, float rightR)
{
    meterLeftLevel = left;
    meterRightLLevel = rightL;
    meterRightRLevel = rightR;
}

void DelayHeaderComponent::timerCallback()
{
    meterLeft.setLevels ({ meterLeftLevel });
    meterRight.setLevels ({ meterRightLLevel, meterRightRLevel });
}

void DelayHeaderComponent::lookAndFeelChanged()
{
    juce::Component::lookAndFeelChanged();
    resized();
    repaint();
}

void DelayHeaderComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void DelayHeaderComponent::resized()
{
    using juce::FlexBox;
    using juce::FlexItem;

    const int h = getHeight();
    const int knobSize = (int) (h * 0.75f);

    sliderInput.setContentSize (knobSize);
    sliderGate.setContentSize (knobSize);
    sliderOutput.setContentSize (knobSize);

    const int inW = sliderInput.getRequiredWidth (knobSize);
    const int inH = sliderInput.getRequiredHeight (knobSize);
    const int gateW = sliderGate.getRequiredWidth (knobSize);
    const int gateH = sliderGate.getRequiredHeight (knobSize);
    const int outW = sliderOutput.getRequiredWidth (knobSize);
    const int outH = sliderOutput.getRequiredHeight (knobSize);

    const int margin = h / 6;
    auto area = getLocalBounds().reduced (margin, 0);

    const int meterH = (int) (h * 0.85f);
    const int meterW = 14;
    const int cogSize = juce::jmin (knobSize, 28);
    const int tapSize = juce::jmin (knobSize, 48);
    const auto tapStyle = tapTempo.getResolvedStyle();
    const auto tapFont = AtomLookAndFeel::getUIFont (tapStyle.textFontHeight, juce::Font::plain);
    const int tapLabelWidth = juce::roundToInt (tapFont.getStringWidthFloat ("120 BPM")) + 8;
    const int tapWidth = juce::jmax (tapSize, tapLabelWidth);

    FlexBox flexBox;
    flexBox.flexDirection = FlexBox::Direction::row;
    flexBox.justifyContent = FlexBox::JustifyContent::flexStart;
    flexBox.alignItems = FlexBox::AlignItems::center;

    auto makeItem = [] (float w, float itemH, juce::Component& c, int ml, int mr) -> FlexItem
    {
        FlexItem item (w, itemH, c);
        item.margin = FlexItem::Margin (0, (float) mr, 0, (float) ml);
        item.flexShrink = 0.0f;
        return item;
    };

    FlexItem spacer (0, 0);
    spacer.flexGrow = 1.0f;

    flexBox.items.addArray ({
        makeItem ((float) meterW, (float) meterH, meterLeft, 6, 10),
        makeItem ((float) inW, (float) inH, sliderInput, 6, 6),
        makeItem ((float) gateW, (float) gateH, sliderGate, 6, 6),
        makeItem ((float) cogSize, (float) cogSize, btnSettings, 6, 6),
        makeItem ((float) tapWidth, (float) tapSize, tapTempo, 6, 6),
        spacer,
        makeItem ((float) outW, (float) outH, sliderOutput, 6, 6),
        makeItem ((float) meterW * 2.0f, (float) meterH, meterRight, 10, 6),
    });

    flexBox.performLayout (area);
}

int DelayHeaderComponent::getMinimumContentWidth (int heightHint)
{
    const int h = heightHint > 0 ? heightHint : (getHeight() > 0 ? getHeight() : 80);
    const int knobSize = (int) (h * 0.75f);
    const int margin = h / 6;
    const int meterW = 14;
    const int cogSize = juce::jmin (knobSize, 28);
    const int tapSize = juce::jmin (knobSize, 48);
    const auto tapStyle = tapTempo.getResolvedStyle();
    const auto tapFont = AtomLookAndFeel::getUIFont (tapStyle.textFontHeight, juce::Font::plain);
    const int tapWidth = juce::jmax (tapSize, juce::roundToInt (tapFont.getStringWidthFloat ("120 BPM")) + 8);

    const int inW = sliderInput.getRequiredWidth (knobSize);
    const int gateW = sliderGate.getRequiredWidth (knobSize);
    const int outW = sliderOutput.getRequiredWidth (knobSize);

    const int content = meterW + 16 + inW + 12 + gateW + 12 + cogSize + 12 + tapWidth + 12 + outW + 12 + meterW * 2 + 16;
    return content + margin * 2;
}
