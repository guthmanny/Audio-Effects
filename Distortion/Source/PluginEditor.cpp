#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
 #include "AudioSettingsPanel.h"
#endif

namespace
{
class SettingsCardRow final : public juce::Component
{
public:
    SettingsCardRow (const juce::String& rowName,
                     const juce::String& title,
                     juce::Component& controlToEmbed,
                     int height)
        : label (rowName + "Label", title),
          control (controlToEmbed),
          rowHeight (height)
    {
        card.setMinPanelHeight (rowHeight);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (AtomLookAndFeel::getUIFont (AtomLookAndFeel::getSystemUIFontHeight(), juce::Font::plain));
        label.setMinimumHorizontalScale (1.0f);
        label.setAutoResizeEnabled (false);
        card.addAndMakeVisible (label);
        card.addAndMakeVisible (control);
        addAndMakeVisible (card);
    }

    void resized() override
    {
        card.setBounds (getLocalBounds());
        auto area = card.getLocalBounds().reduced (12, 8);
        constexpr int labelWidth = 160;
        label.setBounds (area.removeFromLeft (labelWidth));
        area.removeFromLeft (10);
        control.setBounds (area);
    }

private:
    atom::SettingsCard card;
    atom::Label label;
    juce::Component& control;
    int rowHeight;
};

#if JucePlugin_Build_Standalone
void applySystemNativeTitleBarTheme (juce::Component& target)
{
    atom::setNativeTitleBarDarkMode (target, juce::Desktop::getInstance().isDarkModeActive());
}
#endif
} // namespace

DistortionAudioProcessorEditor::DistortionAudioProcessorEditor (DistortionAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&atomLookAndFeel);

    addAndMakeVisible (headerBar);
    addAndMakeVisible (footerBar);
    addAndMakeVisible (bodyViewport);
    addAndMakeVisible (startupOverlay);
    startupOverlay.setAlwaysOnTop (true);
    startupOverlay.setVisible (processor.isModelPreloadPending());
    startupOverlay.setProgress (0.0f, "Loading Distortion...");

    processor.setStartupProgressCallback ([safeThis = juce::Component::SafePointer<DistortionAudioProcessorEditor> (this)]
                                          (float progress, const juce::String& statusMessage)
    {
        if (safeThis == nullptr)
            return;

        safeThis->startupOverlay.setVisible (true);
        safeThis->startupOverlay.toFront (false);
        safeThis->startupOverlay.setProgress (progress, statusMessage);
    });

    bodyViewport.setViewedComponent (&bodyContent, false);
    bodyViewport.setScrollBarsShown (true, false);
    bodyViewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, juce::Colours::grey);

    footerBar.getBtnMidiPort().onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Distortion+", true, processor.getDistortionModel() == DistortionAudioProcessor::kDistortionPlus);
        menu.addItem (2, "TS9", true, processor.getDistortionModel() == DistortionAudioProcessor::kTs9);
        menu.addItem (3, "AC Booster", true, processor.getDistortionModel() == DistortionAudioProcessor::kAcBooster);
        menu.addItem (4, "DS-1", true, processor.getDistortionModel() == DistortionAudioProcessor::kDs1);
        menu.addItem (5, "Rat", true, processor.getDistortionModel() == DistortionAudioProcessor::kRat);
        menu.addItem (6, "Klon", true, processor.getDistortionModel() == DistortionAudioProcessor::kKlon);
        menu.addItem (7, "Guv'nor", true, processor.getDistortionModel() == DistortionAudioProcessor::kGuvnor);

        menu.showMenuAsync (juce::PopupMenu::Options(),
                            [this] (int result)
                            {
                                if (result == 0) return;
                                DistortionAudioProcessor::DistortionModel model;
                                switch (result) {
                                    case 2: model = DistortionAudioProcessor::kTs9; break;
                                    case 3: model = DistortionAudioProcessor::kAcBooster; break;
                                    case 4: model = DistortionAudioProcessor::kDs1; break;
                                    case 5: model = DistortionAudioProcessor::kRat; break;
                                    case 6: model = DistortionAudioProcessor::kKlon; break;
                                    case 7: model = DistortionAudioProcessor::kGuvnor; break;
                                    default: model = DistortionAudioProcessor::kDistortionPlus; break;
                                }
                                processor.setDistortionModel (model);
                                updateModelUI();
                            });
    };

    footerBar.getQualityComboBox().onChange = [this]
    {
        switch (footerBar.getQualityComboBox().getSelectedId())
        {
            case 1:  processor.setOversampleFactor (4); break;
            case 2:  processor.setOversampleFactor (8); break;
            default: break;
        }
    };

    headerBar.getBtnSettings().onClick = [this]
    {
#if JucePlugin_Build_Standalone
        showStandaloneOptionsMenu();
#endif
    };

    footerBar.onZoomChanged = [this] (float scale) { applyZoom (scale); };

    sliderAttachments.add (new SliderAttachment (processor.parameters.valueTreeState, "inputgain", headerBar.getSliderInput()));
    sliderAttachments.add (new SliderAttachment (processor.parameters.valueTreeState, "gatethreshold", headerBar.getSliderGate()));
    sliderAttachments.add (new SliderAttachment (processor.parameters.valueTreeState, "outputgain", headerBar.getSliderOutput()));

    const juce::Array<juce::AudioProcessorParameter*>& parameters = processor.getParameters();
    int comboBoxCounter = 0;
    bodyContentHeight = bodyMargin;

    const juce::StringArray headerParamIds { "inputgain", "gatethreshold", "outputgain" };

    const auto uiFont = AtomLookAndFeel::getUIFont (AtomLookAndFeel::getSystemUIFontHeight(),
                                                    juce::Font::plain);
    const float uiFontHeight = AtomLookAndFeel::getSystemUIFontHeight();
    float maxParamLabelWidth = 0.0f;
    float maxValueTextWidth = 0.0f;

    for (int i = 0; i < parameters.size(); ++i)
    {
        if (const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameters[i]))
        {
            if (headerParamIds.contains (parameter->paramID))
                continue;

            if (processor.parameters.parameterTypes[i] != "Slider")
                continue;

            maxParamLabelWidth = juce::jmax (maxParamLabelWidth, uiFont.getStringWidthFloat (parameter->name));

            if (auto* param = processor.parameters.valueTreeState.getParameter (parameter->paramID))
            {
                maxValueTextWidth = juce::jmax (maxValueTextWidth,
                                                uiFont.getStringWidthFloat (param->getText (0.0f, 0)));
                maxValueTextWidth = juce::jmax (maxValueTextWidth,
                                                uiFont.getStringWidthFloat (param->getText (1.0f, 0)));
            }
        }
    }

    const float labelReserveDlu = maxParamLabelWidth > 0.0f ? maxParamLabelWidth * 8.0f / uiFontHeight : 0.0f;
    const float valueReserveDlu = maxValueTextWidth > 0.0f ? (maxValueTextWidth + 12.0f) * 8.0f / uiFontHeight : 0.0f;
    savedLabelReserveDlu = labelReserveDlu;
    savedValueReserveDlu = valueReserveDlu;

    for (int i = 0; i < parameters.size(); ++i)
    {
        if (const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameters[i]))
        {
            if (headerParamIds.contains (parameter->paramID))
                continue;

            if (processor.parameters.parameterTypes[i] == "Slider")
            {
                auto* aSlider = sliders.add (new atom::Slider());

                if (parameter->paramID == "distortion")
                    distortionSlider = aSlider;
                else if (parameter->paramID == "tone")
                    toneSlider = aSlider;
                else if (parameter->paramID == "bass")
                    bassSlider = aSlider;
                else if (parameter->paramID == "treble")
                    trebleSlider = aSlider;

                aSlider->setTextValueSuffix (parameter->label);
                aSlider->setValueLabelPos (atom::Slider::ValueLabelPos::Right);

                atom::SliderStyleOverride styleOverride;
                styleOverride.colors.labelText = parameter->name;
                styleOverride.metrics.linearHorizontalLabelReserveDlu = labelReserveDlu;
                styleOverride.metrics.linearHorizontalValueLabelReserveDlu = valueReserveDlu;
                atomLookAndFeel.setSliderStyleOverride (*aSlider, styleOverride);

                sliderAttachments.add (new SliderAttachment (processor.parameters.valueTreeState,
                                                               parameter->paramID,
                                                               *aSlider));

                bodyContent.addAndMakeVisible (aSlider);
                bodyComponents.add (aSlider);
                bodyContentHeight += sliderHeight + bodyPadding;
            }
            else if (processor.parameters.parameterTypes[i] == "ToggleButton")
            {
                auto* aButton = toggles.add (new atom::ToggleButton (parameter->paramID, {}));
                aButton->setToggleState (parameter->getDefaultValue(), juce::dontSendNotification);

                buttonAttachments.add (new ButtonAttachment (processor.parameters.valueTreeState,
                                                             parameter->paramID,
                                                             *aButton));

                auto* row = new SettingsCardRow (parameter->paramID + "Row", parameter->name, *aButton, cardRowHeight);
                settingRows.add (row);
                bodyContent.addAndMakeVisible (row);
                bodyComponents.add (row);
                bodyContentHeight += cardRowHeight + bodyPadding;
            }
            else if (processor.parameters.parameterTypes[i] == "ComboBox")
            {
                auto* aComboBox = comboBoxes.add (new atom::ComboBox());
                aComboBox->setEditableText (false);
                aComboBox->setJustificationType (juce::Justification::centredLeft);
                aComboBox->addItemList (processor.parameters.comboBoxItemLists[comboBoxCounter++], 1);

                comboBoxAttachments.add (new ComboBoxAttachment (processor.parameters.valueTreeState,
                                                                 parameter->paramID,
                                                                 *aComboBox));

                auto* row = new SettingsCardRow (parameter->paramID + "Row", parameter->name, *aComboBox, cardRowHeight);
                settingRows.add (row);
                bodyContent.addAndMakeVisible (row);
                bodyComponents.add (row);
                bodyContentHeight += cardRowHeight + bodyPadding;
            }
        }
    }

    bodyContentHeight += bodyMargin;

    applyZoom (1.0f);
    updateModelUI();
    startTimerHz (30);

    if (processor.isModelPreloadPending())
    {
        juce::Component::SafePointer<DistortionAudioProcessorEditor> safeThis (this);
        juce::Timer::callAfterDelay (50, [safeThis]()
        {
            if (safeThis != nullptr)
                safeThis->startModelPreload();
        });
    }

#if JucePlugin_Build_Standalone
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
#endif
}

DistortionAudioProcessorEditor::~DistortionAudioProcessorEditor()
{
    processor.setStartupProgressCallback (nullptr);
    stopTimer();

#if JucePlugin_Build_Standalone
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
#endif

    for (auto* slider : sliders)
        atomLookAndFeel.clearSliderStyleOverride (*slider);

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

int DistortionAudioProcessorEditor::getHeaderHeight() const noexcept
{
    return juce::roundToInt ((float) headerBaseHeight * zoomFactor);
}

int DistortionAudioProcessorEditor::getFooterHeight() const noexcept
{
    return juce::roundToInt ((float) footerBaseHeight * zoomFactor);
}

int DistortionAudioProcessorEditor::getBodyContentHeight() const noexcept
{
    return juce::roundToInt ((float) bodyContentHeight * zoomFactor);
}

int DistortionAudioProcessorEditor::getEditorWidth()
{
    const int headerW = headerBar.getMinimumContentWidth (getHeaderHeight());
    const int footerW = footerBar.getMinimumContentWidth (getFooterHeight());
    return juce::jmax (headerW, footerW);
}

int DistortionAudioProcessorEditor::getNaturalHeight() const noexcept
{
    return getHeaderHeight() + getBodyContentHeight() + getFooterHeight();
}

void DistortionAudioProcessorEditor::applyZoom (float newZoom)
{
    zoomFactor = juce::jlimit (0.75f, 1.25f, newZoom);

    const int width = getEditorWidth();
    const int height = getNaturalHeight();

    setSize (width, height);
    setResizeLimits (width, height, juce::roundToInt (width * 1.5f), juce::roundToInt (height * 1.5f));

    bodyContent.setSize (width, getBodyContentHeight());
    resized();
}

void DistortionAudioProcessorEditor::startModelPreload()
{
    if (! processor.isModelPreloadPending() || modelPreloadActive)
        return;

    modelPreloadActive = true;
    startupOverlay.setVisible (true);
    startupOverlay.toFront (false);
    startupOverlay.setProgress (0.0f, "Initializing distortion engines...");
    startTimerHz (20);
}

void DistortionAudioProcessorEditor::finishModelPreload()
{
    modelPreloadActive = false;
    startupOverlay.setVisible (false);
    startTimerHz (30);
}

void DistortionAudioProcessorEditor::timerCallback()
{
    if (modelPreloadActive)
    {
        const bool moreWork = processor.advanceModelPreloadStep();
        if (! moreWork)
            finishModelPreload();
        return;
    }

    headerBar.setMeterLevels (processor.getMeterLevelMono(),
                              processor.getMeterLevelLeft(),
                              processor.getMeterLevelRight());
    headerBar.setCpuLoad (processor.getCpuLoad());
}

void DistortionAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void DistortionAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    const int headerH = getHeaderHeight();
    const int footerH = getFooterHeight();

    headerBar.setBounds (bounds.removeFromTop (headerH));
    footerBar.setBounds (bounds.removeFromBottom (footerH));
    bodyViewport.setBounds (bounds);

    startupOverlay.setBounds (getLocalBounds());
    if (startupOverlay.isVisible())
        startupOverlay.toFront (false);

    bodyContent.setSize (juce::jmax (getWidth(), getEditorWidth()),
                         juce::jmax (getBodyContentHeight(), bounds.getHeight()));

    auto area = bodyContent.getLocalBounds().reduced (juce::roundToInt ((float) bodyMargin * zoomFactor), 0);
    for (auto* component : bodyComponents)
    {
        if (! component->isVisible())
            continue;
        const int rowHeight = dynamic_cast<atom::Slider*> (component) != nullptr
                                  ? juce::roundToInt ((float) sliderHeight * zoomFactor)
                                  : juce::roundToInt ((float) cardRowHeight * zoomFactor);
        component->setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (juce::roundToInt ((float) bodyPadding * zoomFactor));
    }
}

#if JucePlugin_Build_Standalone
void DistortionAudioProcessorEditor::applyAudioSettingsDialogTitleBarTheme()
{
    if (audioSettingsDialog == nullptr)
        return;

    applySystemNativeTitleBarTheme (*audioSettingsDialog);
}

void DistortionAudioProcessorEditor::darkModeSettingChanged()
{
    applyAudioSettingsDialogTitleBarTheme();
}

void DistortionAudioProcessorEditor::showAudioSettingsDialog()
{
    if (audioSettingsDialog != nullptr)
    {
        audioSettingsDialog->toFront (true);
        audioSettingsDialog->grabKeyboardFocus();
        return;
    }

    auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window == nullptr)
        return;

    auto* panel = new AudioSettingsPanel (window->getDeviceManager(), atomLookAndFeel);

    // 初始大小使用固定值，与反向约束的最小值解耦
    panel->setSize (560, 420);

    // 计算反向约束的最小值，用于限制对话框缩小
    const int minPanelW = panel->getMinimumPanelWidth();
    const int minPanelH = panel->getMinimumPanelHeight();
    // 最小值上限 520px，避免因设备名过长导致对话框无法缩小到合理尺寸
    constexpr int kMaxMinW = 520;
    constexpr int kMaxMinH = 600;
    const int clampedMinW = juce::jmin (minPanelW, kMaxMinW);
    const int clampedMinH = juce::jmin (minPanelH, kMaxMinH);
    // 对话框最小尺寸（内容 + 窗口装饰预留）
    const int minDialogW = juce::jmax (400, clampedMinW + 20);
    const int minDialogH = juce::jmax (300, clampedMinH + 40);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.useBottomRightCornerResizer = false;
    options.content.setOwned (panel);
    options.componentToCentreAround = window;

    auto* dialog = options.create();
    audioSettingsDialog = dialog;

    if (dialog != nullptr)
    {
        dialog->setResizeLimits (minDialogW, minDialogH, 1600, 1200);
        dialog->setAlwaysOnTop (true);
        applyAudioSettingsDialogTitleBarTheme();

        juce::Component::SafePointer<juce::Component> safeDialog (dialog);
        juce::Timer::callAfterDelay (0, [safeDialog]()
        {
            if (safeDialog != nullptr)
                applySystemNativeTitleBarTheme (*safeDialog);
        });

        juce::Component::SafePointer<DistortionAudioProcessorEditor> safeEditor (this);
        dialog->enterModalState (true,
                                 juce::ModalCallbackFunction::create ([safeEditor] (int)
                                 {
                                     if (safeEditor != nullptr)
                                         safeEditor->audioSettingsDialog = nullptr;
                                 }),
                                 true);
    }
}

void DistortionAudioProcessorEditor::showStandaloneOptionsMenu()
{
    auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, TRANS ("Audio/MIDI Settings..."));
    menu.addSeparator();
    menu.addItem (2, TRANS ("Save current state..."));
    menu.addItem (3, TRANS ("Load a saved state..."));
    menu.addSeparator();
    menu.addItem (4, TRANS ("Reset to default state"));

    juce::Component::SafePointer<juce::StandaloneFilterWindow> safeWindow (window);
    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safeWindow, safeEditor = juce::Component::SafePointer<DistortionAudioProcessorEditor> (this)] (int result)
                        {
                            if (result == 0)
                                return;

                            if (result == 1)
                            {
                                if (safeEditor != nullptr)
                                    safeEditor->showAudioSettingsDialog();
                                return;
                            }

                            if (safeWindow != nullptr)
                                safeWindow->handleMenuResult (result);
                        });
}
#endif

//==============================================================================

void DistortionAudioProcessorEditor::updateModelUI()
{
    const auto m = processor.getDistortionModel();

    juce::String label = "Distortion";
    juce::String toneLabel = "Tone";
    if (m == DistortionAudioProcessor::kTs9) label = "Drive";
    else if (m == DistortionAudioProcessor::kAcBooster) label = "Gain";
    else if (m == DistortionAudioProcessor::kDs1) label = "Gain";
    else if (m == DistortionAudioProcessor::kRat) { label = "Distortion"; toneLabel = "Filter"; }
    else if (m == DistortionAudioProcessor::kKlon) { label = "Gain"; toneLabel = "Treble"; }
    else if (m == DistortionAudioProcessor::kGuvnor) { label = "Gain"; toneLabel = "Treble"; }

    if (distortionSlider != nullptr)
    {
        atom::SliderStyleOverride styleOverride;
        styleOverride.colors.labelText = label;
        styleOverride.metrics.linearHorizontalLabelReserveDlu = savedLabelReserveDlu;
        styleOverride.metrics.linearHorizontalValueLabelReserveDlu = savedValueReserveDlu;
        atomLookAndFeel.setSliderStyleOverride (*distortionSlider, styleOverride);
        distortionSlider->repaint();
    }

    const bool showTone = (m == DistortionAudioProcessor::kTs9 || m == DistortionAudioProcessor::kDs1 || m == DistortionAudioProcessor::kRat || m == DistortionAudioProcessor::kKlon || m == DistortionAudioProcessor::kGuvnor);
    const bool showBass = (m == DistortionAudioProcessor::kAcBooster || m == DistortionAudioProcessor::kGuvnor);
    const bool showTreble = (m == DistortionAudioProcessor::kAcBooster || m == DistortionAudioProcessor::kGuvnor);

    if (toneSlider != nullptr)
    {
        toneSlider->setVisible (showTone);
        atom::SliderStyleOverride ts;
        ts.colors.labelText = toneLabel;
        ts.metrics.linearHorizontalLabelReserveDlu = savedLabelReserveDlu;
        ts.metrics.linearHorizontalValueLabelReserveDlu = savedValueReserveDlu;
        atomLookAndFeel.setSliderStyleOverride (*toneSlider, ts);
        toneSlider->repaint();
    }
    if (bassSlider != nullptr)
    {
        bassSlider->setVisible (showBass);
        atom::SliderStyleOverride bs;
        bs.colors.labelText = "Bass";
        bs.metrics.linearHorizontalLabelReserveDlu = savedLabelReserveDlu;
        bs.metrics.linearHorizontalValueLabelReserveDlu = savedValueReserveDlu;
        atomLookAndFeel.setSliderStyleOverride (*bassSlider, bs);
        bassSlider->repaint();
    }
    if (trebleSlider != nullptr)
    {
        trebleSlider->setVisible (showTreble);
        const juce::String trebleLabel = (m == DistortionAudioProcessor::kGuvnor) ? "Mid" : "Treble";
        atom::SliderStyleOverride ts2;
        ts2.colors.labelText = trebleLabel;
        ts2.metrics.linearHorizontalLabelReserveDlu = savedLabelReserveDlu;
        ts2.metrics.linearHorizontalValueLabelReserveDlu = savedValueReserveDlu;
        atomLookAndFeel.setSliderStyleOverride (*trebleSlider, ts2);
        trebleSlider->repaint();
    }

    bodyContentHeight = bodyMargin;
    for (auto* component : bodyComponents)
    {
        if (! component->isVisible())
            continue;
        const int rowHeight = dynamic_cast<atom::Slider*> (component) != nullptr ? sliderHeight : cardRowHeight;
        bodyContentHeight += rowHeight + bodyPadding;
    }
    bodyContentHeight += bodyMargin;

    applyZoom (zoomFactor);
}
