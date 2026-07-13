#pragma once

#include <juce_atom_theme/juce_atom_theme.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

class ChorusAudioProcessor;

/** Settings page: assign MIDI CC numbers to all Chorus model parameters. */
class ChorusMidiCcSettingsPanel final : public juce::Component
{
public:
    ChorusMidiCcSettingsPanel (ChorusAudioProcessor& processor, AtomLookAndFeel& lookAndFeel);
    ~ChorusMidiCcSettingsPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    ChorusAudioProcessor& processor;
    AtomLookAndFeel& atomLookAndFeel;

    atom::Label introLabel;

    atom::ComboBox rateCcCombo;
    atom::ComboBox delayCcCombo;
    atom::ComboBox amountCcCombo;
    atom::ComboBox wetCcCombo;
    atom::ComboBox feedbackCcCombo;

    std::unique_ptr<juce::Component> rateRow;
    std::unique_ptr<juce::Component> delayRow;
    std::unique_ptr<juce::Component> amountRow;
    std::unique_ptr<juce::Component> wetRow;
    std::unique_ptr<juce::Component> feedbackRow;
    std::unique_ptr<juce::Component> section;

    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboBoxAttachment> rateAttachment;
    std::unique_ptr<ComboBoxAttachment> delayAttachment;
    std::unique_ptr<ComboBoxAttachment> amountAttachment;
    std::unique_ptr<ComboBoxAttachment> wetAttachment;
    std::unique_ptr<ComboBoxAttachment> feedbackAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusMidiCcSettingsPanel)
};
