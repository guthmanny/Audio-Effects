#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_atom_theme/juce_atom_theme.h>

class ChorusFooterComponent final : public juce::Component
{
public:
    ChorusFooterComponent();

    atom::ShapeButton& getBtnMidiPort() { return btnMidiPort; }
    atom::ComboBox& getQualityComboBox() { return qualityComboBox; }
    atom::ComboBox& getViewComboBox() { return viewComboBox; }

    std::function<void (float)> onZoomChanged;

    void lookAndFeelChanged() override;
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void applyFonts();

    atom::ShapeButton btnMidiPort { "btnMidiPort", AtomIconLibrary::Icon::MidiPort };
    atom::Label qualityLabel { "qualityLabel", "QUALITY:" };
    atom::ComboBox qualityComboBox;
    atom::Label viewLabel { "viewLabel", "VIEW:" };
    atom::ComboBox viewComboBox;
};
