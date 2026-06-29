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

namespace
{
class SettingsCardRow final : public Component
{
public:
    SettingsCardRow(const String &rowName, const String &title, Component &controlToEmbed, int height)
        : label(rowName + "Label", title),
          control(controlToEmbed),
          rowHeight(height)
    {
        card.setMinPanelHeight(rowHeight);
        card.setSize(0, rowHeight);
        label.setJustificationType(Justification::centredLeft);
        label.setFont(AtomLookAndFeel::getUIFont(16.0f, Font::plain));
        label.setMinimumHorizontalScale(1.0f);
        card.addAndMakeVisible(label);
        card.addAndMakeVisible(control);
        addAndMakeVisible(card);
        setSize(0, rowHeight);
    }

    int getRowHeight() const noexcept { return rowHeight; }

    void resized() override
    {
        card.setBounds(getLocalBounds());
        auto area = card.getLocalBounds().reduced(12, 8);
        constexpr int labelWidth = 160;
        label.setBounds(area.removeFromLeft(labelWidth));
        area.removeFromLeft(10);
        control.setBounds(area);
    }

private:
    atom::SettingsCard card;
    atom::Label label;
    Component &control;
    int rowHeight;
};
} // namespace

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
                aSlider->setValueLabelPos(atom::Slider::ValueLabelPos::Right);

                atom::SliderStyleOverride styleOverride;
                styleOverride.colors.labelText = parameter->name;
                atomLookAndFeel.setSliderStyleOverride(*aSlider, styleOverride);

                sliderAttachments.add(new SliderAttachment(processor.parameters.valueTreeState, parameter->paramID, *aSlider));

                aSlider->setName(parameter->name);
                aSlider->setComponentID(parameter->paramID);
                components.add(aSlider);
                addAndMakeVisible(aSlider);
                editorHeight += sliderHeight;
            }
            else if (processor.parameters.parameterTypes[i] == "ToggleButton")
            {
                atom::ToggleButton *aButton;
                toggles.add(aButton = new atom::ToggleButton(parameter->paramID, {}));
                aButton->setToggleState(parameter->getDefaultValue(), dontSendNotification);

                buttonAttachments.add(new ButtonAttachment(processor.parameters.valueTreeState, parameter->paramID, *aButton));

                auto *row = new SettingsCardRow(parameter->paramID + "Row", parameter->name, *aButton, cardRowHeight);
                settingRows.add(row);
                row->setName(parameter->name);
                row->setComponentID(parameter->paramID);
                components.add(row);
                addAndMakeVisible(row);
                editorHeight += cardRowHeight;
            }
            else if (processor.parameters.parameterTypes[i] == "ComboBox")
            {
                atom::ComboBox *aComboBox;
                comboBoxes.add(aComboBox = new atom::ComboBox());
                aComboBox->setEditableText(false);
                aComboBox->setJustificationType(Justification::centredLeft);
                aComboBox->addItemList(processor.parameters.comboBoxItemLists[comboBoxCounter++], 1);

                comboBoxAttachments.add(new ComboBoxAttachment(processor.parameters.valueTreeState, parameter->paramID, *aComboBox));

                auto *row = new SettingsCardRow(parameter->paramID + "Row", parameter->name, *aComboBox, cardRowHeight);
                settingRows.add(row);
                row->setName(parameter->name);
                row->setComponentID(parameter->paramID);
                components.add(row);
                addAndMakeVisible(row);
                editorHeight += cardRowHeight;
            }
        }
    }

    editorHeight += components.size() * editorPadding;
    setSize(editorWidth, editorHeight * 2);
}

ChorusAudioProcessorEditor::~ChorusAudioProcessorEditor()
{
    for (auto *slider : sliders)
        atomLookAndFeel.clearSliderStyleOverride(*slider);

    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

//==============================================================================

void ChorusAudioProcessorEditor::paint(Graphics &g)
{
    g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void ChorusAudioProcessorEditor::resized()
{
    juce::Rectangle<int> r = getLocalBounds().reduced(editorMargin);

#if JucePlugin_Build_Standalone
    const int settingsSize = 28;
    settingsButton.setBounds(r.getRight() - settingsSize, r.getY(), settingsSize, settingsSize);
    settingsButton.toFront(false);
#endif

    for (auto *component : components)
    {
        const int rowHeight = dynamic_cast<atom::Slider *>(component) != nullptr ? sliderHeight : cardRowHeight;
        component->setBounds(r.removeFromTop(rowHeight));
        r.removeFromTop(editorPadding);
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
