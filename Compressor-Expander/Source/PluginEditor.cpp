/*
  ==============================================================================

    Code by Juan Gil <http://juangil.com/>.
    Copyright (C) 2017 Juan Gil.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================

int CompressorExpanderAudioProcessorEditor::computeTotalHeight() const
{
    int height = 2 * editorMargin + curveHeight + editorPadding;

    for (int i = 0; i < components.size(); ++i)
    {
        if (dynamic_cast<Slider*> (components[i]))
            height += sliderHeight;
        else if (dynamic_cast<ToggleButton*> (components[i]))
            height += buttonHeight;
        else if (dynamic_cast<ComboBox*> (components[i]))
            height += comboBoxHeight;

        height += editorPadding;
    }

    return height;
}

void CompressorExpanderAudioProcessorEditor::applyEditorSize()
{
    totalEditorHeight = computeTotalHeight();
    setSize (editorWidth, totalEditorHeight);
    setResizeLimits (editorWidth, totalEditorHeight, editorWidth, totalEditorHeight);
    resized();
}

CompressorExpanderAudioProcessorEditor::CompressorExpanderAudioProcessorEditor (CompressorExpanderAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&atomLookAndFeel);

    addAndMakeVisible (transferCurve);

    const Array<AudioProcessorParameter*>& parameters = processor.getParameters();
    int comboBoxCounter = 0;

    for (int i = 0; i < parameters.size(); ++i) {
        if (const AudioProcessorParameterWithID* parameter =
                dynamic_cast<AudioProcessorParameterWithID*> (parameters[i])) {

            if (processor.parameters.parameterTypes[i] == "Slider") {
                Slider* aSlider;
                sliders.add (aSlider = new Slider());
                aSlider->setTextValueSuffix (parameter->label);
                aSlider->setTextBoxStyle (Slider::TextBoxLeft,
                                          false,
                                          sliderTextEntryBoxWidth,
                                          sliderTextEntryBoxHeight);

                SliderAttachment* aSliderAttachment;
                sliderAttachments.add (aSliderAttachment =
                    new SliderAttachment (processor.parameters.valueTreeState, parameter->paramID, *aSlider));

                components.add (aSlider);
            }

            //======================================

            else if (processor.parameters.parameterTypes[i] == "ToggleButton") {
                ToggleButton* aButton;
                toggles.add (aButton = new ToggleButton());
                aButton->setToggleState (parameter->getDefaultValue(), dontSendNotification);

                ButtonAttachment* aButtonAttachment;
                buttonAttachments.add (aButtonAttachment =
                    new ButtonAttachment (processor.parameters.valueTreeState, parameter->paramID, *aButton));

                components.add (aButton);
            }

            //======================================

            else if (processor.parameters.parameterTypes[i] == "ComboBox") {
                ComboBox* aComboBox;
                comboBoxes.add (aComboBox = new ComboBox());
                aComboBox->setEditableText (false);
                aComboBox->setJustificationType (Justification::left);
                aComboBox->addItemList (processor.parameters.comboBoxItemLists[comboBoxCounter++], 1);

                ComboBoxAttachment* aComboBoxAttachment;
                comboBoxAttachments.add (aComboBoxAttachment =
                    new ComboBoxAttachment (processor.parameters.valueTreeState, parameter->paramID, *aComboBox));

                components.add (aComboBox);
            }

            //======================================

            Label* aLabel;
            labels.add (aLabel = new Label (parameter->name, parameter->name));
            aLabel->attachToComponent (components.getLast(), true);
            addAndMakeVisible (aLabel);

            components.getLast()->setName (parameter->name);
            components.getLast()->setComponentID (parameter->paramID);
            addAndMakeVisible (components.getLast());
        }
    }

    applyEditorSize();

    // Standalone attaches the editor after construction; re-apply size once on the message thread
    // so the outer window picks up curveHeight changes instead of stretching a stale bounds.
    juce::MessageManager::callAsync ([this]
    {
        applyEditorSize();
    });

    updateTransferCurve();
    startTimerHz (60);
}

CompressorExpanderAudioProcessorEditor::~CompressorExpanderAudioProcessorEditor()
{
    stopTimer();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================

//==============================================================================

float CompressorExpanderAudioProcessorEditor::readParameterValue (const String& paramId, float fallback) const
{
    if (auto* param = processor.parameters.valueTreeState.getParameter (paramId))
        return param->convertFrom0to1 (param->getValue());

    if (auto* value = processor.parameters.valueTreeState.getRawParameterValue (paramId))
        return value->load();

    return fallback;
}

void CompressorExpanderAudioProcessorEditor::updateTransferCurve()
{
    const float thresholdDb = readParameterValue (processor.paramThreshold.paramID, processor.paramThreshold.defaultValue);
    const float ratio = readParameterValue (processor.paramRatio.paramID, processor.paramRatio.defaultValue);
    const float makeupDb = readParameterValue (processor.paramMakeupGain.paramID, processor.paramMakeupGain.defaultValue);
    const bool expanderMode = readParameterValue (processor.paramMode.paramID, 1.0f) >= 0.5f;
    const bool softKnee = readParameterValue (processor.paramKnee.paramID, 0.0f) >= 0.5f;
    const float kneeWidthDb = readParameterValue (processor.paramKneeWidth.paramID, processor.paramKneeWidth.defaultValue);

    transferCurve.setParameters (thresholdDb, ratio, makeupDb, expanderMode, softKnee, kneeWidthDb);
}

void CompressorExpanderAudioProcessorEditor::timerCallback()
{
    const float attackSec = readParameterValue (processor.paramAttack.paramID, processor.paramAttack.defaultValue) * 0.001f;
    const float releaseSec = readParameterValue (processor.paramRelease.paramID, processor.paramRelease.defaultValue) * 0.001f;
    transferCurve.setDynamicMeter (processor.getMeterInputDb(),
                                   processor.getMeterGainReductionDb(),
                                   attackSec,
                                   releaseSec);

    if (++paramUpdateCounter >= 6)
    {
        paramUpdateCounter = 0;
        updateTransferCurve();
    }
}

//==============================================================================

void CompressorExpanderAudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));
}

void CompressorExpanderAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (editorMargin);

    transferCurve.setBounds (bounds.removeFromTop (curveHeight));
    bounds.removeFromTop (editorPadding);

    auto controlArea = bounds;
    controlArea = controlArea.removeFromRight (controlArea.getWidth() - labelWidth);

    for (int i = 0; i < components.size(); ++i) {
        if (dynamic_cast<Slider*> (components[i]))
            components[i]->setBounds (controlArea.removeFromTop (sliderHeight));

        if (dynamic_cast<ToggleButton*> (components[i]))
            components[i]->setBounds (controlArea.removeFromTop (buttonHeight));

        if (dynamic_cast<ComboBox*> (components[i]))
            components[i]->setBounds (controlArea.removeFromTop (comboBoxHeight));

        controlArea.removeFromTop (editorPadding);
    }
}

//==============================================================================
