#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <juce_atom_theme/juce_atom_theme.h>

#include "DistortionFooterComponent.h"
#include "DistortionHeaderComponent.h"
#include "DistortionStartupProgressOverlay.h"
#include "PluginProcessor.h"

class DistortionBodyContent final : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
    }
};

class DistortionAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
#if JucePlugin_Build_Standalone
                                             , private juce::DarkModeSettingListener
#endif
{
public:
    explicit DistortionAudioProcessorEditor (DistortionAudioProcessor&);
    ~DistortionAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

#if JucePlugin_Build_Standalone
    void showStandaloneOptionsMenu();
    void showAudioSettingsDialog();
#endif

    void updateModelUI();

private:
    class StartupOverlay final : public juce::Component
    {
    public:
        StartupOverlay()
        {
            addAndMakeVisible (content);
            setInterceptsMouseClicks (true, true);
        }

        void setProgress (float progress, const juce::String& statusMessage)
        {
            content.setProgress (progress, statusMessage);
            content.repaint();
            repaint();
        }

        void parentHierarchyChanged() override
        {
            resized();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xcc000000));
        }

        void resized() override
        {
            auto bounds = getLocalBounds();
            content.setBounds (bounds.withSizeKeepingCentre (DistortionStartupProgressOverlay::preferredWidth,
                                                             DistortionStartupProgressOverlay::preferredHeight));
        }

    private:
        DistortionStartupProgressOverlay content;
    };

    void timerCallback() override;
    void startModelPreload();
    void finishModelPreload();
#if JucePlugin_Build_Standalone
    void darkModeSettingChanged() override;
    void applyAudioSettingsDialogTitleBarTheme();
#endif
    void applyZoom (float newZoom);
    int getEditorWidth();
    int getNaturalHeight() const noexcept;
    int getHeaderHeight() const noexcept;
    int getFooterHeight() const noexcept;
    int getBodyContentHeight() const noexcept;

    DistortionAudioProcessor& processor;
    bool modelPreloadActive = false;

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
    StartupOverlay startupOverlay;
    DistortionHeaderComponent headerBar;
    DistortionFooterComponent footerBar;
    DistortionBodyContent bodyContent;
    juce::Viewport bodyViewport;

    atom::Slider* distortionSlider = nullptr;
    atom::Slider* toneSlider = nullptr;
    atom::Slider* bassSlider = nullptr;
    atom::Slider* trebleSlider = nullptr;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistortionAudioProcessorEditor)
};
