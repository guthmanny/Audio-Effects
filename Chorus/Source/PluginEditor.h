#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <juce_atom_theme/juce_atom_theme.h>

#include "ChorusFooterComponent.h"
#include "ChorusHeaderComponent.h"
#include "PluginProcessor.h"
#include "SpectrumOverlayComponent.h"
#include "TunerOverlayComponent.h"

#if JucePlugin_Build_Standalone
#include "AppSettingsPanel.h"
#endif

class ChorusBodyContent final : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
    }
};

class ChorusAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
#if JucePlugin_Build_Standalone
                                         , private juce::DarkModeSettingListener
#endif
{
public:
    explicit ChorusAudioProcessorEditor (ChorusAudioProcessor&);
    ~ChorusAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

#if JucePlugin_Build_Standalone
    void showStandaloneOptionsMenu();
    void showAppSettingsDialog (AppSettingsPanel::Page initialPage = AppSettingsPanel::Page::AudioSettings);
#endif

    void updateModelUI();
    void setTunerVisible (bool shouldShow);
    void setSpectrumVisible (bool shouldShow);

private:
    void timerCallback() override;
#if JucePlugin_Build_Standalone
    void darkModeSettingChanged() override;
    void applyAppSettingsDialogTitleBarTheme();
#endif
    void applyZoom (float newZoom);
    int getEditorWidth();
    int getNaturalHeight() const noexcept;
    int getHeaderHeight() const noexcept;
    int getFooterHeight() const noexcept;
    int getBodyContentHeight() const noexcept;

    ChorusAudioProcessor& processor;

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

    AtomLookAndFeel atomLookAndFeel { atom::ThemeType::Dark };
    ChorusHeaderComponent headerBar;
    ChorusFooterComponent footerBar;
    ChorusBodyContent bodyContent;
    juce::Viewport bodyViewport;
    TunerOverlay tunerOverlay;
    SpectrumOverlay spectrumOverlay;
    std::vector<float> spectrumScratch;
    uint32_t lastSpectrumFrameId = 0;

    // Chorus model slider pointers
    atom::Slider* chorusRateSlider = nullptr;
    atom::Slider* chorusDelaySlider = nullptr;
    atom::Slider* chorusAmountSlider = nullptr;
    atom::Slider* chorusWetSlider = nullptr;
    atom::Slider* chorusFeedbackSlider = nullptr;

    // Phase90 model slider pointers
    atom::Slider* phase90RateSlider = nullptr;
    atom::Slider* centerSlider = nullptr;
    atom::Slider* phase90AmountSlider = nullptr;
    atom::Slider* phase90FeedbackSlider = nullptr;
    atom::Slider* mixSlider = nullptr;

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
    juce::Component::SafePointer<juce::DialogWindow> appSettingsDialog;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusAudioProcessorEditor)
};
