/*
  ==============================================================================

    Code by Juan Gil <http://juangil.com/>.
    Copyright (C) 2017 Juan Gil.

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <juce_atom_theme/juce_atom_theme.h>

#include "CompressorCurveComponent.h"
#include "PluginProcessor.h"

//==============================================================================

class CompressorExpanderAudioProcessorEditor final : public AudioProcessorEditor,
                                                     private juce::Timer
{
public:
    //==============================================================================

    CompressorExpanderAudioProcessorEditor (CompressorExpanderAudioProcessor&);
    ~CompressorExpanderAudioProcessorEditor() override;

    //==============================================================================

    void paint (Graphics&) override;
    void resized() override;

    /** Adjust curveHeight here — must rebuild after changing. */
    static constexpr int curveHeight = 520;

private:
    //==============================================================================

    void timerCallback() override;
    void updateTransferCurve();
    float readParameterValue (const String& paramId, float fallback) const;
    int computeTotalHeight() const;
    void applyEditorSize();

    CompressorExpanderAudioProcessor& processor;

    static constexpr int editorWidth = 520;
    static constexpr int editorMargin = 10;
    static constexpr int editorPadding = 10;

    static constexpr int sliderTextEntryBoxWidth = 100;
    static constexpr int sliderTextEntryBoxHeight = 25;
    static constexpr int sliderHeight = 25;
    static constexpr int buttonHeight = 25;
    static constexpr int comboBoxHeight = 25;
    static constexpr int labelWidth = 100;

    int totalEditorHeight = 0;
    int paramUpdateCounter = 0;

    //======================================

    AtomLookAndFeel atomLookAndFeel { atom::ThemeType::Dark };
    CompressorCurveComponent transferCurve;

    OwnedArray<Slider> sliders;
    OwnedArray<ToggleButton> toggles;
    OwnedArray<ComboBox> comboBoxes;

    OwnedArray<Label> labels;
    Array<Component*> components;

    typedef AudioProcessorValueTreeState::SliderAttachment SliderAttachment;
    typedef AudioProcessorValueTreeState::ButtonAttachment ButtonAttachment;
    typedef AudioProcessorValueTreeState::ComboBoxAttachment ComboBoxAttachment;

    OwnedArray<SliderAttachment> sliderAttachments;
    OwnedArray<ButtonAttachment> buttonAttachments;
    OwnedArray<ComboBoxAttachment> comboBoxAttachments;

    //==============================================================================

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorExpanderAudioProcessorEditor)
};
