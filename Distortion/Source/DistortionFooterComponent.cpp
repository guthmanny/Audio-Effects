#include "DistortionFooterComponent.h"

DistortionFooterComponent::DistortionFooterComponent()
{
    btnMidiPort.setTooltip ("MIDI Port");
    addAndMakeVisible (btnMidiPort);

    qualityLabel.setJustificationType (juce::Justification::centredLeft);
    qualityLabel.setMinimumHorizontalScale (1.0f);
    qualityLabel.setBorderSize ({});
    qualityLabel.setAutoResizeEnabled (false);
    addAndMakeVisible (qualityLabel);

    qualityComboBox.addItem ("STANDARD", 1);
    qualityComboBox.addItem ("HIGH", 2);
    qualityComboBox.setSelectedId (1, juce::dontSendNotification);
    qualityComboBox.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (qualityComboBox);

    viewLabel.setJustificationType (juce::Justification::centredLeft);
    viewLabel.setMinimumHorizontalScale (1.0f);
    viewLabel.setBorderSize ({});
    viewLabel.setAutoResizeEnabled (false);
    addAndMakeVisible (viewLabel);

    viewComboBox.addItem ("75%", 1);
    viewComboBox.addItem ("100%", 2);
    viewComboBox.addItem ("125%", 3);
    viewComboBox.setSelectedId (2, juce::dontSendNotification);
    viewComboBox.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (viewComboBox);

    viewComboBox.onChange = [this]
    {
        if (! onZoomChanged)
            return;

        switch (viewComboBox.getSelectedId())
        {
            case 1:  onZoomChanged (0.75f); break;
            case 2:  onZoomChanged (1.00f); break;
            case 3:  onZoomChanged (1.25f); break;
            default: break;
        }
    };

    applyFonts();
}

void DistortionFooterComponent::applyFonts()
{
    const auto font = AtomLookAndFeel::getUIFont (AtomLookAndFeel::getSystemUIFontHeight(), juce::Font::plain);
    qualityLabel.setFont (font);
    viewLabel.setFont (font);
}

void DistortionFooterComponent::lookAndFeelChanged()
{
    juce::Component::lookAndFeelChanged();
    applyFonts();
    resized();
    repaint();
}

void DistortionFooterComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void DistortionFooterComponent::resized()
{
    const auto area = getLocalBounds().reduced (16, 0);
    const int h = juce::jmin (area.getHeight() - 2, 64);
    const int y = area.getY() + (area.getHeight() - h) / 2;
    const int btnSize = h;

    btnMidiPort.setBounds (area.getX(), y, btnSize, btnSize);

    qualityComboBox.setSize (qualityComboBox.getIdealWidth(), h);
    viewComboBox.setSize (viewComboBox.getIdealWidth(), h);

    using juce::FlexBox;
    using juce::FlexItem;

    FlexBox rightBox;
    rightBox.flexDirection = FlexBox::Direction::row;
    rightBox.justifyContent = FlexBox::JustifyContent::flexEnd;
    rightBox.alignItems = FlexBox::AlignItems::center;

    const int qualityTextW = juce::roundToInt (qualityLabel.getFont().getStringWidthFloat ("QUALITY:")) + 6;
    const int viewTextW = juce::roundToInt (viewLabel.getFont().getStringWidthFloat ("VIEW:")) + 6;

    auto labelItem = [] (atom::Label& label, int w, int height) -> FlexItem
    {
        FlexItem item ((float) w, (float) height, label);
        item.flexShrink = 0.0f;
        return item;
    };

    auto comboItem = [] (atom::ComboBox& combo, int height) -> FlexItem
    {
        FlexItem item ((float) combo.getWidth(), (float) height, combo);
        item.flexShrink = 0.0f;
        return item;
    };

    FlexItem fixedSpacer (24.0f, 0.0f);
    FlexItem interSpacer (10.0f, 0.0f);
    rightBox.items.addArray ({
        fixedSpacer,
        labelItem (qualityLabel, qualityTextW, h),
        comboItem (qualityComboBox, h),
        interSpacer,
        labelItem (viewLabel, viewTextW, h),
        comboItem (viewComboBox, h),
    });

    rightBox.performLayout (area.withLeft (area.getX() + btnSize));
}

int DistortionFooterComponent::getMinimumContentWidth (int heightHint)
{
    const int h = heightHint > 0 ? heightHint : (getHeight() > 0 ? getHeight() : 32);
    const int btnSize = juce::jmin (h, 64);
    const auto font = AtomLookAndFeel::getUIFont (AtomLookAndFeel::getSystemUIFontHeight(), juce::Font::plain);
    const int qualityTextW = juce::roundToInt (font.getStringWidthFloat ("QUALITY:")) + 6;
    const int viewTextW = juce::roundToInt (font.getStringWidthFloat ("VIEW:")) + 6;
    const int comboW = 100;  // approximate ideal combo width
    const int margin = 16;
    return margin * 2 + btnSize + 24 + qualityTextW + comboW + 10 + viewTextW + comboW;
}
