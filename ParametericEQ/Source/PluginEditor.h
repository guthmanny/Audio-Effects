#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <juce_atom_theme/juce_atom_theme.h>

#include "EQCurveComponent.h"
#include "ParametricEQFooterComponent.h"
#include "ParametricEQHeaderComponent.h"
#include "PluginProcessor.h"

class ParametricEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                private juce::Timer
#if JucePlugin_Build_Standalone
                                                , private juce::DarkModeSettingListener
#endif
{
public:
    explicit ParametricEQAudioProcessorEditor (ParametricEQAudioProcessor&);
    ~ParametricEQAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

#if JucePlugin_Build_Standalone
    void showStandaloneOptionsMenu();
    void showAudioSettingsDialog();
#endif

private:
    void timerCallback() override;
#if JucePlugin_Build_Standalone
    void darkModeSettingChanged() override;
    void applyAudioSettingsDialogTitleBarTheme();
#endif
    bool refreshEQCurveIfNeeded();
    void applyZoom (float newZoom);
    int getHeaderHeight() const noexcept;
    int getFooterHeight() const noexcept;

    /** 刷新 EQ 频响曲线数据 */
    void updateEQCurve();

    ParametricEQAudioProcessor& processor;

    static constexpr int headerBaseHeight = 80;
    static constexpr int footerBaseHeight = 32;
    static constexpr int eqCurveHeight = 160;
    static constexpr int sliderHeight = 50;

    float zoomFactor = 1.0f;

    AtomLookAndFeel atomLookAndFeel { atom::ThemeType::Dark };
    ParametricEQHeaderComponent headerBar;
    ParametricEQFooterComponent footerBar;
    EQCurveComponent eqCurve;

    // 所有参数控件（slider, toggle, button）
    juce::OwnedArray<juce::Component> paramRows;
    juce::OwnedArray<atom::Slider> sliders;
    juce::OwnedArray<atom::ToggleButton> toggles;
    juce::OwnedArray<juce::TextButton> controlButtons;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::OwnedArray<SliderAttachment> sliderAttachments;
    juce::OwnedArray<ButtonAttachment> buttonAttachments;

    bool eqCurveCacheValid = false;
    double lastCurveSampleRate = 0.0;
    std::array<float, ParametricEQAudioProcessor::numBands> lastCurveFreqHz {};
    std::array<float, ParametricEQAudioProcessor::numBands> lastCurveQ {};
    std::array<float, ParametricEQAudioProcessor::numBands> lastCurveGainDb {};
    std::array<int, ParametricEQAudioProcessor::numBands> lastCurveType {};

#if JucePlugin_Build_Standalone
    juce::Component::SafePointer<juce::Component> audioSettingsDialog;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParametricEQAudioProcessorEditor)
};
