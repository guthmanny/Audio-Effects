#include "ChorusMidiCcSettingsPanel.h"

#include "PluginProcessor.h"

namespace
{
constexpr int kRowHeight = 48;

void fillMidiCcCombo (atom::ComboBox& combo)
{
    combo.clear (juce::dontSendNotification);
    combo.addItem ("Off", 1); // choice index 0 in APVTS
    for (int cc = 0; cc <= 127; ++cc)
        combo.addItem ("CC " + juce::String (cc), cc + 2); // choice index = cc + 1
}
} // namespace

namespace chorus_midi_cc_settings
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
} // namespace chorus_midi_cc_settings

ChorusMidiCcSettingsPanel::ChorusMidiCcSettingsPanel (ChorusAudioProcessor& processorIn,
                                                      AtomLookAndFeel& lookAndFeel)
    : processor (processorIn),
      atomLookAndFeel (lookAndFeel),
      introLabel ("chorusMidiIntro", "MIDI CC")
{
    setLookAndFeel (&atomLookAndFeel);

    introLabel.setHintText (
        "Assign a MIDI Control Change number to each Chorus parameter. "
        "Incoming CC values map across the parameter's full range. Choose Off to disable.");
    introLabel.setFont (AtomLookAndFeel::getUIFont (16.0f, juce::Font::bold));
    introLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (introLabel);

    fillMidiCcCombo (rateCcCombo);
    fillMidiCcCombo (delayCcCombo);
    fillMidiCcCombo (amountCcCombo);
    fillMidiCcCombo (wetCcCombo);
    fillMidiCcCombo (feedbackCcCombo);

    rateRow = std::make_unique<chorus_midi_cc_settings::SettingsCardRow> (
        "rateCcRow", "RATE", rateCcCombo, kRowHeight);
    delayRow = std::make_unique<chorus_midi_cc_settings::SettingsCardRow> (
        "delayCcRow", "DELAY", delayCcCombo, kRowHeight);
    amountRow = std::make_unique<chorus_midi_cc_settings::SettingsCardRow> (
        "amountCcRow", "AMOUNT", amountCcCombo, kRowHeight);
    wetRow = std::make_unique<chorus_midi_cc_settings::SettingsCardRow> (
        "wetCcRow", "WET", wetCcCombo, kRowHeight);
    feedbackRow = std::make_unique<chorus_midi_cc_settings::SettingsCardRow> (
        "feedbackCcRow", "FEEDBACK", feedbackCcCombo, kRowHeight);

    auto midiSection = std::make_unique<chorus_midi_cc_settings::SettingsSection> ("Chorus Parameters");
    midiSection->addRow (static_cast<chorus_midi_cc_settings::SettingsCardRow&> (*rateRow));
    midiSection->addRow (static_cast<chorus_midi_cc_settings::SettingsCardRow&> (*delayRow));
    midiSection->addRow (static_cast<chorus_midi_cc_settings::SettingsCardRow&> (*amountRow));
    midiSection->addRow (static_cast<chorus_midi_cc_settings::SettingsCardRow&> (*wetRow));
    midiSection->addRow (static_cast<chorus_midi_cc_settings::SettingsCardRow&> (*feedbackRow));
    section = std::move (midiSection);
    addAndMakeVisible (*section);

    auto& vts = processor.parameters.valueTreeState;
    rateAttachment = std::make_unique<ComboBoxAttachment> (vts, processor.paramMidiCcChorusRate.paramID, rateCcCombo);
    delayAttachment = std::make_unique<ComboBoxAttachment> (vts, processor.paramMidiCcChorusDelay.paramID, delayCcCombo);
    amountAttachment = std::make_unique<ComboBoxAttachment> (vts, processor.paramMidiCcChorusAmount.paramID, amountCcCombo);
    wetAttachment = std::make_unique<ComboBoxAttachment> (vts, processor.paramMidiCcChorusWet.paramID, wetCcCombo);
    feedbackAttachment =
        std::make_unique<ComboBoxAttachment> (vts, processor.paramMidiCcChorusFeedback.paramID, feedbackCcCombo);
}

ChorusMidiCcSettingsPanel::~ChorusMidiCcSettingsPanel()
{
    rateAttachment.reset();
    delayAttachment.reset();
    amountAttachment.reset();
    wetAttachment.reset();
    feedbackAttachment.reset();
    setLookAndFeel (nullptr);
}

void ChorusMidiCcSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));
}

void ChorusMidiCcSettingsPanel::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    introLabel.setBounds (bounds.removeFromTop (56));
    bounds.removeFromTop (12);

    if (auto* midiSection = dynamic_cast<chorus_midi_cc_settings::SettingsSection*> (section.get()))
    {
        const int h = midiSection->getPreferredHeight();
        midiSection->setBounds (bounds.removeFromTop (h));
    }
}
