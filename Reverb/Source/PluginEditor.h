#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <juce_atom_theme/juce_atom_theme.h>

#include "ReverbFooterComponent.h"
#include "ReverbHeaderComponent.h"
#include "PluginProcessor.h"

class ReverbBodyContent final : public juce::Component
{
public:
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
};

class ReverbAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
#if JucePlugin_Build_Standalone
                                         ,
                                         private juce::DarkModeSettingListener
#endif
{
public:
    explicit ReverbAudioProcessorEditor(ReverbAudioProcessor&);
    ~ReverbAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

#if JucePlugin_Build_Standalone
    void showStandaloneOptionsMenu();
    void showAudioSettingsDialog();
#endif

    void updateModelUI();

private:
    void timerCallback() override;
#if JucePlugin_Build_Standalone
    void darkModeSettingChanged() override;
    void applyAudioSettingsDialogTitleBarTheme();
#endif
    void applyZoom(float newZoom);
    int getEditorWidth();
    int getNaturalHeight() const noexcept;
    int getHeaderHeight() const noexcept;
    int getFooterHeight() const noexcept;
    int getBodyContentHeight() const noexcept;

    ReverbAudioProcessor& processor;

    static constexpr int headerBaseHeight = 80;
    static constexpr int footerBaseHeight = 32;
    static constexpr int sliderHeight = 50;
    static constexpr int cardRowHeight = 48;
    static constexpr int bodyPadding = 10;
    static constexpr int bodyMargin = 20;

    float zoomFactor = 1.0f;
    int bodyContentHeight = 0;
    float savedLabelReserveDlu = 0.0f;
    float savedValueReserveDlu = 0.0f;

    AtomLookAndFeel atomLookAndFeel{atom::ThemeType::Dark};
    ReverbHeaderComponent headerBar;
    ReverbFooterComponent footerBar;
    ReverbBodyContent bodyContent;
    juce::Viewport bodyViewport;

    // Freeverb slider pointers
    atom::Slider* roomsizeSlider = nullptr;
    atom::Slider* dampSlider = nullptr;
    atom::Slider* widthSlider = nullptr;
    atom::Slider* freeverbWetSlider = nullptr;
    atom::Slider* freeverbDrySlider = nullptr;
    atom::Slider* freezeSlider = nullptr;

    // Plate slider pointers
    atom::Slider* predelaySlider = nullptr;
    atom::Slider* bandwidthSlider = nullptr;
    atom::Slider* dampingSlider = nullptr;
    atom::Slider* decaySlider = nullptr;
    atom::Slider* plateWetSlider = nullptr;

    juce::OwnedArray<atom::Slider> sliders;
    juce::OwnedArray<atom::ToggleButton> toggles;
    juce::OwnedArray<atom::ComboBox> comboBoxes;
    juce::OwnedArray<juce::Component> settingRows;
    juce::Array<juce::Component*> bodyComponents;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::OwnedArray<SliderAttachment> sliderAttachments;
    juce::OwnedArray<ButtonAttachment> buttonAttachments;
    juce::OwnedArray<ComboBoxAttachment> comboBoxAttachments;

#if JucePlugin_Build_Standalone
    juce::Component::SafePointer<juce::Component> audioSettingsDialog;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbAudioProcessorEditor)
};
