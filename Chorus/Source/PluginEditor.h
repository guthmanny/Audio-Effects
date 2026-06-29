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

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <juce_atom_theme/juce_atom_theme.h>
#include "PluginProcessor.h"

//==============================================================================

class ChorusAudioProcessorEditor : public AudioProcessorEditor
{
public:
  //==============================================================================

  ChorusAudioProcessorEditor(ChorusAudioProcessor &);
  ~ChorusAudioProcessorEditor();

  //==============================================================================

  void paint(Graphics &) override;
  void resized() override;

#if JucePlugin_Build_Standalone
  void showStandaloneOptionsMenu();
#endif

private:
  //==============================================================================

  ChorusAudioProcessor &processor;

  enum
  {
    editorWidth = 600,
    editorMargin = 10,
    editorPadding = 10,

    sliderHeight = 50,
    cardRowHeight = 54,
  };

  //======================================

  OwnedArray<atom::Slider> sliders;
  OwnedArray<atom::ToggleButton> toggles;
  OwnedArray<atom::ComboBox> comboBoxes;

  OwnedArray<Component> settingRows;
  Array<Component *> components;

  typedef AudioProcessorValueTreeState::SliderAttachment SliderAttachment;
  typedef AudioProcessorValueTreeState::ButtonAttachment ButtonAttachment;
  typedef AudioProcessorValueTreeState::ComboBoxAttachment ComboBoxAttachment;

  OwnedArray<SliderAttachment> sliderAttachments;
  OwnedArray<ButtonAttachment> buttonAttachments;
  OwnedArray<ComboBoxAttachment> comboBoxAttachments;

  //==============================================================================
  AtomLookAndFeel atomLookAndFeel { atom::ThemeType::Dark };

#if JucePlugin_Build_Standalone
  atom::ShapeButton settingsButton { "settingsButton", AtomIconLibrary::Icon::CogWheel };
#endif

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusAudioProcessorEditor)
};
