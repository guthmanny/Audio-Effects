/*
  ==============================================================================

    Code by Juan Gil <http://juangil.com/>.
    Copyright (C) 2017 Juan Gil.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

//==============================================================================

ChorusAudioProcessorEditor::ChorusAudioProcessorEditor(ChorusAudioProcessor &p)
    : AudioProcessorEditor(&p), processor(p)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&atomLookAndFeel);

#if JucePlugin_Build_Standalone
    settingsButton.setTooltip("Settings");
    settingsButton.onClick = [this]() { showStandaloneOptionsMenu(); };
    addAndMakeVisible(settingsButton);
#endif

    const Array<AudioProcessorParameter *> &parameters = processor.getParameters();
    int comboBoxCounter = 0;

    int editorHeight = 2 * editorMargin;
    for (int i = 0; i < parameters.size(); ++i)
    {
        if (const AudioProcessorParameterWithID *parameter =
                dynamic_cast<AudioProcessorParameterWithID *>(parameters[i]))
        {

            if (processor.parameters.parameterTypes[i] == "Slider")
            {
                atom::Slider *aSlider;
                sliders.add(aSlider = new atom::Slider());
                aSlider->setTextValueSuffix(parameter->label);
                aSlider->setValueLabelPos(atom::Slider::ValueLabelPos::Left);

                SliderAttachment *aSliderAttachment;
                sliderAttachments.add(aSliderAttachment =
                                          new SliderAttachment(processor.parameters.valueTreeState, parameter->paramID, *aSlider));

                components.add(aSlider);
                editorHeight += sliderHeight;
                        }

            //======================================

            else if (processor.parameters.parameterTypes[i] == "ToggleButton")
            {
                atom::ToggleButton *aButton;
                toggles.add(aButton = new atom::ToggleButton(parameter->paramID, {}));
                aButton->setToggleState(parameter->getDefaultValue(), dontSendNotification);

                ButtonAttachment *aButtonAttachment;
                buttonAttachments.add(aButtonAttachment =
                                          new ButtonAttachment(processor.parameters.valueTreeState, parameter->paramID, *aButton));

                components.add(aButton);
                editorHeight += buttonHeight;
            }

            //======================================

            else if (processor.parameters.parameterTypes[i] == "ComboBox")
            {
                atom::ComboBox *aComboBox;
                comboBoxes.add(aComboBox = new atom::ComboBox());
                aComboBox->setEditableText(false);
                aComboBox->setJustificationType(Justification::left);
                aComboBox->addItemList(processor.parameters.comboBoxItemLists[comboBoxCounter++], 1);

                ComboBoxAttachment *aComboBoxAttachment;
                comboBoxAttachments.add(aComboBoxAttachment =
                                            new ComboBoxAttachment(processor.parameters.valueTreeState, parameter->paramID, *aComboBox));

                components.add(aComboBox);
                editorHeight += comboBoxHeight;
            }

            //======================================

            Label *aLabel;
            labels.add(aLabel = new Label(parameter->name, parameter->name));
            aLabel->attachToComponent(components.getLast(), true);
            aLabel->setFont(Font(16.0f));
            aLabel->setMinimumHorizontalScale(1.0f);
            addAndMakeVisible(aLabel);

            components.getLast()->setName(parameter->name);
            components.getLast()->setComponentID(parameter->paramID);
            addAndMakeVisible(components.getLast());
        }
    }

    //======================================

    editorHeight += components.size() * editorPadding;
    setSize(editorWidth, editorHeight * 2);
}

ChorusAudioProcessorEditor::~ChorusAudioProcessorEditor()
{
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

//==============================================================================

void ChorusAudioProcessorEditor::paint(Graphics &g)
{
    g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void ChorusAudioProcessorEditor::resized()
{
    Rectangle<int> r = getLocalBounds().reduced(editorMargin);

#if JucePlugin_Build_Standalone
    const int settingsSize = 28;
    settingsButton.setBounds(r.getRight() - settingsSize, r.getY(), settingsSize, settingsSize);
    settingsButton.toFront(false);
#endif

    r.removeFromLeft(labelWidth);

    for (int i = 0; i < components.size(); ++i)
    {
        if (atom::Slider *aSlider = dynamic_cast<atom::Slider *>(components[i]))
            components[i]->setBounds(r.removeFromTop(sliderHeight));

        if (atom::ToggleButton *aButton = dynamic_cast<atom::ToggleButton *>(components[i]))
            components[i]->setBounds(r.removeFromTop(buttonHeight));

        if (atom::ComboBox *aComboBox = dynamic_cast<atom::ComboBox *>(components[i]))
            components[i]->setBounds(r.removeFromTop(comboBoxHeight));

        r = r.removeFromBottom(r.getHeight() - editorPadding);
    }
}

#if JucePlugin_Build_Standalone
void ChorusAudioProcessorEditor::showStandaloneOptionsMenu()
{
    auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, TRANS("Audio/MIDI Settings..."));
    menu.addSeparator();
    menu.addItem(2, TRANS("Save current state..."));
    menu.addItem(3, TRANS("Load a saved state..."));
    menu.addSeparator();
    menu.addItem(4, TRANS("Reset to default state"));

    juce::Component::SafePointer<juce::StandaloneFilterWindow> safeWindow(window);
    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [safeWindow](int result)
                       {
                           if (result != 0 && safeWindow != nullptr)
                               safeWindow->handleMenuResult(result);
                       });
}
#endif

//==============================================================================
