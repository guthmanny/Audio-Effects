#include "NoiseGateSettingsPanel.h"

#include <cmath>

#include "CompressorKnee.h"
#include "PluginProcessor.h"

namespace
{
constexpr int kRowHeight = 48;
constexpr float kIntroFontHeight = 16.0f;
constexpr float kThreshAbsMin = -80.0f;
constexpr float kThreshAbsMax = 0.0f;
constexpr float kThreshRangeGap = 0.1f;

void configureSlider (atom::Slider& slider, double minV, double maxV, double interval, const juce::String& suffix)
{
    slider.setRange (minV, maxV, interval);
    slider.setTextValueSuffix (suffix);
    slider.setSliderSnapsToMousePosition (false);
}

float linearToDb (float linear) noexcept
{
    return 20.0f * std::log10 (juce::jmax (linear, 1.0e-6f));
}
} // namespace

namespace noise_gate_settings
{
class SettingsCardRow final : public juce::Component
{
public:
    SettingsCardRow (const juce::String& rowName, const juce::String& title, juce::Component& controlToEmbed, int height)
        : label (rowName + "Label", title), control (controlToEmbed), rowHeight (height)
    {
        card.setMinPanelHeight (rowHeight);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (AtomLookAndFeel::getUIFont (AtomLookAndFeel::getSystemUIFontHeight(), juce::Font::plain));
        label.setMinimumHorizontalScale (1.0f);
        label.setAutoResizeEnabled (false);
        card.addAndMakeVisible (label);
        card.addAndMakeVisible (control);
        addAndMakeVisible (card);
        setSize (0, rowHeight);
    }

    int getRowHeight() const noexcept { return rowHeight; }

    void resized() override
    {
        card.setBounds (getLocalBounds());
        auto area = card.getLocalBounds().reduced (12, 8);
        constexpr int labelWidth = 140;
        label.setBounds (area.removeFromLeft (labelWidth));
        area.removeFromLeft (10);
        control.setBounds (area);
    }

private:
    atom::SettingsCard card;
    atom::Label label;
    juce::Component& control;
    int rowHeight;
};

class SettingsSection final : public juce::Component
{
public:
    explicit SettingsSection (const juce::String& title)
    {
        groupedList.setTitle (title);
        groupedList.setHeaderFont (AtomLookAndFeel::getSystemUIFont (juce::Font::bold));
        addAndMakeVisible (groupedList);
    }

    void addRow (SettingsCardRow& row)
    {
        rows.push_back (&row);
        groupedList.addItem (&row);
    }

    void clearRows()
    {
        groupedList.clearItems();
        rows.clear();
    }

    int getPreferredHeight() const
    {
        const int headerH = juce::roundToInt (AtomLookAndFeel::getSystemUIFontHeight()) + 10;
        int total = headerH + padding * 2;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            total += rows[i]->getRowHeight();
            if (i + 1 < rows.size())
                total += itemGap;
        }
        return total;
    }

    void resized() override { groupedList.setBounds (getLocalBounds()); }

private:
    static constexpr int padding = 12;
    static constexpr int itemGap = 8;

    atom::GroupedList groupedList;
    std::vector<SettingsCardRow*> rows;
};
} // namespace noise_gate_settings

NoiseGateSettingsPanel::NoiseGateSettingsPanel (ChorusAudioProcessor& processorIn, AtomLookAndFeel& lookAndFeel)
    : processor (processorIn),
      atomLookAndFeel (lookAndFeel),
      introLabel ("noiseGateIntro", "Noise Gate")
{
    setLookAndFeel (&atomLookAndFeel);

    introLabel.setHintText ("Advanced gate controls. Threshold remains on the main header; OFF AT MIN bypasses the gate at THRESH MIN.");
    introLabel.setFont (AtomLookAndFeel::getUIFont (kIntroFontHeight, juce::Font::bold));
    addAndMakeVisible (introLabel);

    transferCurve.setCurveTitle ("Noise Gate");
    transferCurve.setPlotRange (-80.0f, 0.0f, -80.0f, 0.0f);
    addAndMakeVisible (transferCurve);

    configureSlider (threshMinSlider, kThreshAbsMin, kThreshAbsMax, 0.1, " dB");
    configureSlider (threshMaxSlider, kThreshAbsMin, kThreshAbsMax, 0.1, " dB");

    offAtMinCombo.setEditableText (false);
    offAtMinCombo.setJustificationType (juce::Justification::centredLeft);
    offAtMinCombo.addItemList (processor.paramGateOffAtMin.items, 1);

    kneeCombo.setEditableText (false);
    kneeCombo.setJustificationType (juce::Justification::centredLeft);
    kneeCombo.addItemList (processor.paramGateKnee.items, 1);
    kneeCombo.onChange = [this] { updateKneeWidthEnabled(); };

    configureSlider (kneeWidthSlider, 0.1, 24.0, 0.1, " dB");
    configureSlider (ratioSlider, 1.0, 10.0, 0.1, "");
    configureSlider (attackSlider, 5.0, 100.0, 0.1, " ms");
    configureSlider (releaseSlider, 100.0, 10000.0, 1.0, " ms");

    threshMinRow = std::make_unique<noise_gate_settings::SettingsCardRow> ("threshMinRow", "THRESH MIN", threshMinSlider, kRowHeight);
    threshMaxRow = std::make_unique<noise_gate_settings::SettingsCardRow> ("threshMaxRow", "THRESH MAX", threshMaxSlider, kRowHeight);
    offAtMinRow = std::make_unique<noise_gate_settings::SettingsCardRow> ("offAtMinRow", "OFF AT MIN", offAtMinCombo, kRowHeight);
    kneeRow = std::make_unique<noise_gate_settings::SettingsCardRow> ("kneeRow", "KNEE", kneeCombo, kRowHeight);
    kneeWidthRow = std::make_unique<noise_gate_settings::SettingsCardRow> ("kneeWidthRow", "KNEE WIDTH", kneeWidthSlider, kRowHeight);
    ratioRow = std::make_unique<noise_gate_settings::SettingsCardRow> ("ratioRow", "RATIO", ratioSlider, kRowHeight);
    attackRow = std::make_unique<noise_gate_settings::SettingsCardRow> ("attackRow", "ATTACK", attackSlider, kRowHeight);
    releaseRow = std::make_unique<noise_gate_settings::SettingsCardRow> ("releaseRow", "RELEASE", releaseSlider, kRowHeight);

    auto gateSection = std::make_unique<noise_gate_settings::SettingsSection> ("Hidden Settings");
    gateSection->addRow (static_cast<noise_gate_settings::SettingsCardRow&> (*threshMinRow));
    gateSection->addRow (static_cast<noise_gate_settings::SettingsCardRow&> (*threshMaxRow));
    gateSection->addRow (static_cast<noise_gate_settings::SettingsCardRow&> (*offAtMinRow));
    gateSection->addRow (static_cast<noise_gate_settings::SettingsCardRow&> (*kneeRow));
    gateSection->addRow (static_cast<noise_gate_settings::SettingsCardRow&> (*kneeWidthRow));
    gateSection->addRow (static_cast<noise_gate_settings::SettingsCardRow&> (*ratioRow));
    gateSection->addRow (static_cast<noise_gate_settings::SettingsCardRow&> (*attackRow));
    gateSection->addRow (static_cast<noise_gate_settings::SettingsCardRow&> (*releaseRow));
    section = std::move (gateSection);
    addAndMakeVisible (*section);

    auto& vts = processor.parameters.valueTreeState;
    threshMinAttachment = std::make_unique<SliderAttachment> (vts, processor.paramGateThreshMin.paramID, threshMinSlider);
    threshMaxAttachment = std::make_unique<SliderAttachment> (vts, processor.paramGateThreshMax.paramID, threshMaxSlider);
    offAtMinAttachment = std::make_unique<ComboBoxAttachment> (vts, processor.paramGateOffAtMin.paramID, offAtMinCombo);
    kneeAttachment = std::make_unique<ComboBoxAttachment> (vts, processor.paramGateKnee.paramID, kneeCombo);
    kneeWidthAttachment = std::make_unique<SliderAttachment> (vts, processor.paramGateKneeWidth.paramID, kneeWidthSlider);
    ratioAttachment = std::make_unique<SliderAttachment> (vts, processor.paramGateRatio.paramID, ratioSlider);
    attackAttachment = std::make_unique<SliderAttachment> (vts, processor.paramGateAttack.paramID, attackSlider);
    releaseAttachment = std::make_unique<SliderAttachment> (vts, processor.paramGateRelease.paramID, releaseSlider);

    updateKneeWidthEnabled();
    enforceThresholdRange();
    updateTransferCurve();
    startTimerHz (60);
}

NoiseGateSettingsPanel::~NoiseGateSettingsPanel()
{
    stopTimer();
    threshMinAttachment.reset();
    threshMaxAttachment.reset();
    offAtMinAttachment.reset();
    kneeAttachment.reset();
    kneeWidthAttachment.reset();
    ratioAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();

    if (auto* gateSection = dynamic_cast<noise_gate_settings::SettingsSection*> (section.get()))
        gateSection->clearRows();

    section.reset();
    threshMinRow.reset();
    threshMaxRow.reset();
    offAtMinRow.reset();
    kneeRow.reset();
    kneeWidthRow.reset();
    ratioRow.reset();
    attackRow.reset();
    releaseRow.reset();
    setLookAndFeel (nullptr);
}

float NoiseGateSettingsPanel::readParam (const juce::String& paramId, float fallback) const
{
    if (auto* param = processor.parameters.valueTreeState.getParameter (paramId))
        return param->convertFrom0to1 (param->getValue());

    if (auto* value = processor.parameters.valueTreeState.getRawParameterValue (paramId))
        return value->load();

    return fallback;
}

void NoiseGateSettingsPanel::writeParam (const juce::String& paramId, float value)
{
    if (auto* param = processor.parameters.valueTreeState.getParameter (paramId))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void NoiseGateSettingsPanel::enforceThresholdRange()
{
    float minDb = readParam (processor.paramGateThreshMin.paramID, processor.paramGateThreshMin.defaultValue);
    float maxDb = readParam (processor.paramGateThreshMax.paramID, processor.paramGateThreshMax.defaultValue);

    minDb = juce::jlimit (kThreshAbsMin, kThreshAbsMax, minDb);
    maxDb = juce::jlimit (kThreshAbsMin, kThreshAbsMax, maxDb);

    if (minDb > maxDb - kThreshRangeGap)
    {
        // Prefer keeping the user's last max when range collapses.
        maxDb = juce::jmin (kThreshAbsMax, minDb + kThreshRangeGap);
        if (maxDb - minDb < kThreshRangeGap)
            minDb = juce::jmax (kThreshAbsMin, maxDb - kThreshRangeGap);
    }

    if (std::abs (minDb - readParam (processor.paramGateThreshMin.paramID, minDb)) > 1.0e-3f)
        writeParam (processor.paramGateThreshMin.paramID, minDb);
    if (std::abs (maxDb - readParam (processor.paramGateThreshMax.paramID, maxDb)) > 1.0e-3f)
        writeParam (processor.paramGateThreshMax.paramID, maxDb);

    const float thresholdDb = readParam (processor.paramGateThreshold.paramID, processor.paramGateThreshold.defaultValue);
    const float clamped = juce::jlimit (minDb, maxDb, thresholdDb);
    if (std::abs (clamped - thresholdDb) > 1.0e-3f)
        writeParam (processor.paramGateThreshold.paramID, clamped);
}

void NoiseGateSettingsPanel::updateKneeWidthEnabled()
{
    const bool softKnee = readParam (processor.paramGateKnee.paramID, (float) processor.paramGateKnee.defaultChoice) >= 0.5f;
    kneeWidthSlider.setEnabled (softKnee);
    if (kneeWidthRow != nullptr)
        kneeWidthRow->setAlpha (softKnee ? 1.0f : 0.45f);
}

void NoiseGateSettingsPanel::updateTransferCurve()
{
    const float thresholdDb = readParam (processor.paramGateThreshold.paramID, processor.paramGateThreshold.defaultValue);
    const float ratio = juce::jmax (1.0f, readParam (processor.paramGateRatio.paramID, processor.paramGateRatio.defaultValue));
    const bool softKnee = readParam (processor.paramGateKnee.paramID, (float) processor.paramGateKnee.defaultChoice) >= 0.5f;
    const float kneeWidthDb = readParam (processor.paramGateKneeWidth.paramID, processor.paramGateKneeWidth.defaultValue);

    transferCurve.setParameters (thresholdDb, ratio, 0.0f, true, softKnee, kneeWidthDb);
}

void NoiseGateSettingsPanel::timerCallback()
{
    enforceThresholdRange();
    updateKneeWidthEnabled();
    updateTransferCurve();

    const float thresholdDb = readParam (processor.paramGateThreshold.paramID, processor.paramGateThreshold.defaultValue);
    const float ratio = juce::jmax (1.0f, readParam (processor.paramGateRatio.paramID, processor.paramGateRatio.defaultValue));
    const bool softKnee = readParam (processor.paramGateKnee.paramID, (float) processor.paramGateKnee.defaultChoice) >= 0.5f;
    const float kneeWidthDb = readParam (processor.paramGateKneeWidth.paramID, processor.paramGateKneeWidth.defaultValue);
    const float attackSec = readParam (processor.paramGateAttack.paramID, processor.paramGateAttack.defaultValue) * 0.001f;
    const float releaseSec = readParam (processor.paramGateRelease.paramID, processor.paramGateRelease.defaultValue) * 0.001f;

    const float inputDb = juce::jlimit (-80.0f, 0.0f, linearToDb (processor.getMeterLevelMono()));
    const float outputDb = CompressorKnee::computeOutputDb (inputDb, thresholdDb, ratio, 0.0f, true, softKnee, kneeWidthDb);
    const float gainReductionDb = juce::jmax (0.0f, inputDb - outputDb);

    transferCurve.setDynamicMeter (inputDb, gainReductionDb, attackSec, releaseSec);
}

void NoiseGateSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));
}

void NoiseGateSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    introLabel.setBounds (area.removeFromTop (48));
    area.removeFromTop (8);

    int sectionH = 0;
    if (auto* gateSection = dynamic_cast<noise_gate_settings::SettingsSection*> (section.get()))
        sectionH = gateSection->getPreferredHeight();

    const int curveH = juce::jmax (kCurveMinHeight, area.getHeight() - sectionH - 12);
    transferCurve.setBounds (area.removeFromTop (curveH));
    area.removeFromTop (12);

    if (auto* gateSection = dynamic_cast<noise_gate_settings::SettingsSection*> (section.get()))
        gateSection->setBounds (area.removeFromTop (sectionH));
}
