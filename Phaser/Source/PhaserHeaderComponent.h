#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_atom_theme/juce_atom_theme.h>

class PhaserHeaderComponent final : public juce::Component, private juce::Timer
{
public:
    PhaserHeaderComponent();
    ~PhaserHeaderComponent() override;

    atom::Slider& getSliderInput() { return sliderInput; }
    atom::Slider& getSliderGate() { return sliderGate; }
    atom::Slider& getSliderOutput() { return sliderOutput; }
    atom::ShapeButton& getBtnSettings() { return btnSettings; }
    atom::TapCircle& getTapTempo() { return tapTempo; }
    atom::MeterBar& getMeterLeft() { return meterLeft; }
    atom::MeterBar& getMeterRight() { return meterRight; }
    atom::Label& getModelLabel() { return modelLabel; }

    void setModelName (const juce::String& name) { modelLabel.setText (name, juce::dontSendNotification); }
    void setMeterLevels (float left, float rightL, float rightR);

    void lookAndFeelChanged() override;
    void paint (juce::Graphics& g) override;
    void resized() override;

    int getMinimumContentWidth (int heightHint = 0);

private:
    void timerCallback() override;
    void setupMeter (atom::MeterBar& meter, int barCount);

    atom::MeterBar meterLeft;
    atom::MeterBar meterRight;
    atom::Label modelLabel { "modelLabel", "Phaser" };
    atom::ShapeButton btnSettings { "btnSettings", AtomIconLibrary::Icon::CogWheel };
    atom::TapCircle tapTempo;
    atom::Slider sliderInput { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    atom::Slider sliderGate { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    atom::Slider sliderOutput { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

    float meterLeftLevel = 0.0f;
    float meterRightLLevel = 0.0f;
    float meterRightRLevel = 0.0f;
};
