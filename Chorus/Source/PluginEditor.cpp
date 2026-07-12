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
  SettingsCardRow(const juce::String& rowName, const juce::String& title, juce::Component& controlToEmbed, int height)
      : label(rowName + "Label", title), control(controlToEmbed), rowHeight(height)
  {
    card.setMinPanelHeight(rowHeight);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setFont(AtomLookAndFeel::getUIFont(AtomLookAndFeel::getSystemUIFontHeight(), juce::Font::plain));
    label.setMinimumHorizontalScale(1.0f);
    label.setAutoResizeEnabled(false);
    card.addAndMakeVisible(label);
    card.addAndMakeVisible(control);
    addAndMakeVisible(card);
  }

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
  juce::Component& control;
  int rowHeight;
};

#if JucePlugin_Build_Standalone
void applySystemNativeTitleBarTheme(juce::Component& target)
{
  atom::setNativeTitleBarDarkMode(target, juce::Desktop::getInstance().isDarkModeActive());
}
#endif
}  // namespace

ChorusAudioProcessorEditor::ChorusAudioProcessorEditor(ChorusAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
  juce::LookAndFeel::setDefaultLookAndFeel(&atomLookAndFeel);

  addAndMakeVisible(headerBar);
  addAndMakeVisible(footerBar);
  addAndMakeVisible(bodyViewport);
  bodyViewport.setViewedComponent(&bodyContent, false);
  bodyViewport.setScrollBarsShown(true, false);
  bodyViewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, juce::Colours::grey);

  // 模型选择：通过 Footer MIDI 端口图标弹出菜单
  footerBar.getBtnMidiPort().onClick = [this]
  {
    juce::PopupMenu menu;
    menu.addItem(1, "Chorus", true, processor.getEffectModel() == ChorusAudioProcessor::kChorus);
    menu.addItem(2, "Phase90", true, processor.getEffectModel() == ChorusAudioProcessor::kPhase90);

    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [this](int result)
                       {
                         if (result == 0) return;
                         ChorusAudioProcessor::EffectModel model;
                         switch (result)
                         {
                           case 2:
                             model = ChorusAudioProcessor::kPhase90;
                             break;
                           default:
                             model = ChorusAudioProcessor::kChorus;
                             break;
                         }
                         processor.setEffectModel(model);
                         updateModelUI();
                       });
  };

  headerBar.getBtnSettings().onClick = [this]
  {
#if JucePlugin_Build_Standalone
    showStandaloneOptionsMenu();
#endif
  };

  headerBar.getBtnTuner().onClick = [this]
  {
    setTunerVisible (! tunerOverlay.isVisible());
  };

  headerBar.getBtnSpectrum().onClick = [this]
  {
    setSpectrumVisible (! spectrumOverlay.isVisible());
  };

  tunerOverlay.getContent().onCloseRequested = [this] { setTunerVisible (false); };
  tunerOverlay.getContent().onPeriodicityThresholdChanged = [this] (float threshold)
  {
    processor.setTunerPeriodicityThreshold (threshold);
  };
  tunerOverlay.getContent().setPeriodicityThreshold (processor.getTunerPeriodicityThreshold());
  addChildComponent (tunerOverlay);
  tunerOverlay.setAlwaysOnTop (true);

  spectrumOverlay.getContent().onCloseRequested = [this] { setSpectrumVisible (false); };
  spectrumOverlay.getContent().onFftSizeChanged = [this] (int fftSize)
  {
    processor.setSpectrumFftSize (fftSize);
    spectrumOverlay.getContent().clearSpectrum();
  };
  spectrumOverlay.getContent().setFftSize (processor.getSpectrumFftSize());
  addChildComponent (spectrumOverlay);
  spectrumOverlay.setAlwaysOnTop (true);

  headerBar.getTapTempo().onBPMChanged = [this](double bpm)
  {
    // 根据当前模型，Tap Tempo 同步到对应的 Rate 参数
    const String rateParamId = (processor.getEffectModel() == ChorusAudioProcessor::kPhase90)
                                   ? processor.paramPhase90Rate.paramID
                                   : processor.paramChorusRate.paramID;
    if (auto* param = processor.parameters.valueTreeState.getParameter(rateParamId))
    {
      const float rateHz = juce::jlimit(0.1f, 20.0f, (float)(bpm / 60.0));
      param->setValueNotifyingHost(param->convertTo0to1(rateHz));
    }
  };

  footerBar.onZoomChanged = [this](float scale) { applyZoom(scale); };

  sliderAttachments.add(
      new SliderAttachment(processor.parameters.valueTreeState, "inputgain", headerBar.getSliderInput()));
  sliderAttachments.add(
      new SliderAttachment(processor.parameters.valueTreeState, "gatethreshold", headerBar.getSliderGate()));
  sliderAttachments.add(
      new SliderAttachment(processor.parameters.valueTreeState, "outputgain", headerBar.getSliderOutput()));

  const juce::Array<juce::AudioProcessorParameter*>& parameters = processor.getParameters();
  int comboBoxCounter = 0;
  bodyContentHeight = bodyMargin;

  const juce::StringArray headerParamIds{"inputgain", "gatethreshold", "outputgain"};

  const auto uiFont = AtomLookAndFeel::getUIFont(AtomLookAndFeel::getSystemUIFontHeight(), juce::Font::plain);
  const float uiFontHeight = AtomLookAndFeel::getSystemUIFontHeight();
  float maxParamLabelWidth = 0.0f;
  float maxValueTextWidth = 0.0f;

  for (int i = 0; i < parameters.size(); ++i)
  {
    if (const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameters[i]))
    {
      if (headerParamIds.contains(parameter->paramID)) continue;

      if (processor.parameters.parameterTypes[i] != "Slider") continue;

      maxParamLabelWidth = juce::jmax(maxParamLabelWidth, uiFont.getStringWidthFloat(parameter->name));

      if (auto* param = processor.parameters.valueTreeState.getParameter(parameter->paramID))
      {
        const float numW0 = uiFont.getStringWidthFloat(param->getText(0.0f, 0));
        const float numW1 = uiFont.getStringWidthFloat(param->getText(1.0f, 0));
        const float suffixW = uiFont.getStringWidthFloat(parameter->label);
        maxValueTextWidth = juce::jmax(maxValueTextWidth, numW0 + suffixW, numW1 + suffixW);
      }
    }
  }

  const float labelReserveDlu = maxParamLabelWidth > 0.0f ? maxParamLabelWidth * 8.0f / uiFontHeight : 0.0f;
  const float valueReserveDlu = maxValueTextWidth > 0.0f ? (maxValueTextWidth + 12.0f) * 8.0f / uiFontHeight : 0.0f;
  savedLabelReserveDlu = labelReserveDlu;
  savedValueReserveDlu = valueReserveDlu;

  for (int i = 0; i < parameters.size(); ++i)
  {
    if (const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameters[i]))
    {
      if (headerParamIds.contains(parameter->paramID)) continue;

      if (processor.parameters.parameterTypes[i] == "Slider")
      {
        auto* aSlider = sliders.add(new atom::Slider());

        // 跟踪模型特定 slider 指针
        if (parameter->paramID == "chorusrate")
          chorusRateSlider = aSlider;
        else if (parameter->paramID == "predelay")
          preDelaySlider = aSlider;
        else if (parameter->paramID == "chorusamount")
          chorusAmountSlider = aSlider;
        else if (parameter->paramID == "dry")
          drySlider = aSlider;
        else if (parameter->paramID == "wet")
          wetSlider = aSlider;
        else if (parameter->paramID == "chorusfeedback")
          chorusFeedbackSlider = aSlider;
        else if (parameter->paramID == "phase90rate")
          phase90RateSlider = aSlider;
        else if (parameter->paramID == "center")
          centerSlider = aSlider;
        else if (parameter->paramID == "phase90amount")
          phase90AmountSlider = aSlider;
        else if (parameter->paramID == "phase90feedback")
          phase90FeedbackSlider = aSlider;
        else if (parameter->paramID == "mix")
          mixSlider = aSlider;

        aSlider->setTextValueSuffix(parameter->label);
        aSlider->setValueLabelPos(atom::Slider::ValueLabelPos::Right);

        atom::SliderStyleOverride styleOverride;
        styleOverride.colors.labelText = parameter->name;
        styleOverride.metrics.linearHorizontalLabelReserveDlu = labelReserveDlu;
        styleOverride.metrics.linearHorizontalValueLabelReserveDlu = valueReserveDlu;
        atomLookAndFeel.setSliderStyleOverride(*aSlider, styleOverride);

        sliderAttachments.add(
            new SliderAttachment(processor.parameters.valueTreeState, parameter->paramID, *aSlider));

        bodyContent.addAndMakeVisible(aSlider);
        bodyComponents.add(aSlider);
        bodyContentHeight += sliderHeight + bodyPadding;
      }
      else if (processor.parameters.parameterTypes[i] == "ToggleButton")
      {
        auto* aButton = toggles.add(new atom::ToggleButton(parameter->paramID, {}));
        aButton->setToggleState(parameter->getDefaultValue(), juce::dontSendNotification);

        buttonAttachments.add(
            new ButtonAttachment(processor.parameters.valueTreeState, parameter->paramID, *aButton));

        auto* row = new SettingsCardRow(parameter->paramID + "Row", parameter->name, *aButton, cardRowHeight);
        settingRows.add(row);
        bodyContent.addAndMakeVisible(row);
        bodyComponents.add(row);
        bodyContentHeight += cardRowHeight + bodyPadding;
      }
      else if (processor.parameters.parameterTypes[i] == "ComboBox")
      {
        auto* aComboBox = comboBoxes.add(new atom::ComboBox());
        aComboBox->setEditableText(false);
        aComboBox->setJustificationType(juce::Justification::centredLeft);
        aComboBox->addItemList(processor.parameters.comboBoxItemLists[comboBoxCounter++], 1);

        comboBoxAttachments.add(
            new ComboBoxAttachment(processor.parameters.valueTreeState, parameter->paramID, *aComboBox));

        auto* row = new SettingsCardRow(parameter->paramID + "Row", parameter->name, *aComboBox, cardRowHeight);
        settingRows.add(row);
        bodyContent.addAndMakeVisible(row);
        bodyComponents.add(row);
        bodyContentHeight += cardRowHeight + bodyPadding;
      }
    }
  }

  bodyContentHeight += bodyMargin;

  applyZoom(1.0f);
  updateModelUI();
  startTimerHz(30);

#if JucePlugin_Build_Standalone
  juce::Desktop::getInstance().addDarkModeSettingListener(this);
#endif
}

ChorusAudioProcessorEditor::~ChorusAudioProcessorEditor()
{
  stopTimer();

#if JucePlugin_Build_Standalone
  juce::Desktop::getInstance().removeDarkModeSettingListener(this);
#endif

  for (auto* slider : sliders) atomLookAndFeel.clearSliderStyleOverride(*slider);

  juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

int ChorusAudioProcessorEditor::getHeaderHeight() const noexcept
{
  return juce::roundToInt((float)headerBaseHeight * zoomFactor);
}

int ChorusAudioProcessorEditor::getFooterHeight() const noexcept
{
  return juce::roundToInt((float)footerBaseHeight * zoomFactor);
}

int ChorusAudioProcessorEditor::getBodyContentHeight() const noexcept
{
  return juce::roundToInt((float)bodyContentHeight * zoomFactor);
}

int ChorusAudioProcessorEditor::getEditorWidth()
{
  const int headerW = headerBar.getMinimumContentWidth(getHeaderHeight());
  const int footerW = footerBar.getMinimumContentWidth(getFooterHeight());
  return juce::jmax(headerW, footerW);
}

int ChorusAudioProcessorEditor::getNaturalHeight() const noexcept
{
  return getHeaderHeight() + getBodyContentHeight() + getFooterHeight();
}

void ChorusAudioProcessorEditor::applyZoom(float newZoom)
{
  zoomFactor = juce::jlimit(0.75f, 1.25f, newZoom);

  const int width = getEditorWidth();
  const int height = getNaturalHeight();

  setResizeLimits(width, height, width, height);
  setSize(width, height);

  bodyContent.setSize(width, getBodyContentHeight());
  resized();
}

void ChorusAudioProcessorEditor::setTunerVisible (bool shouldShow)
{
  processor.setTunerEnabled (shouldShow);
  tunerOverlay.setVisible (shouldShow);

  if (shouldShow)
  {
    tunerOverlay.setBounds (getLocalBounds());
    tunerOverlay.toFront (false);
    tunerOverlay.getContent().clearPitch();
  }
}

void ChorusAudioProcessorEditor::setSpectrumVisible (bool shouldShow)
{
  processor.setSpectrumEnabled (shouldShow);
  spectrumOverlay.setVisible (shouldShow);

  if (shouldShow)
  {
    spectrumOverlay.setBounds (getLocalBounds());
    spectrumOverlay.toFront (false);
    spectrumOverlay.getContent().setFftSize (processor.getSpectrumFftSize());
    spectrumOverlay.getContent().clearSpectrum();
    lastSpectrumFrameId = 0;
    startTimerHz (60);
  }
  else
  {
    startTimerHz (30);
  }
}

void ChorusAudioProcessorEditor::timerCallback()
{
  headerBar.setMeterLevels(processor.getMeterLevelMono(), processor.getMeterLevelLeft(),
                           processor.getMeterLevelRight());

  if (tunerOverlay.isVisible())
    tunerOverlay.getContent().setPitchResult (processor.getTunerResult());

  if (spectrumOverlay.isVisible()
      && processor.copySpectrumMagnitudesIfNew (lastSpectrumFrameId, spectrumScratch))
  {
    spectrumOverlay.getContent().setSpectrumMagnitudes (spectrumScratch,
                                                        processor.getSpectrumSampleRate(),
                                                        processor.getSpectrumFftSize());
  }
}

void ChorusAudioProcessorEditor::paint(juce::Graphics& g)
{
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ChorusAudioProcessorEditor::resized()
{
  auto bounds = getLocalBounds();
  const int headerH = getHeaderHeight();
  const int footerH = getFooterHeight();

  headerBar.setBounds(bounds.removeFromTop(headerH));
  footerBar.setBounds(bounds.removeFromBottom(footerH));
  bodyViewport.setBounds(bounds);

  if (tunerOverlay.isVisible())
    tunerOverlay.setBounds(getLocalBounds());

  if (spectrumOverlay.isVisible())
    spectrumOverlay.setBounds(getLocalBounds());

  bodyContent.setSize(juce::jmax(getWidth(), getEditorWidth()), juce::jmax(getBodyContentHeight(), bounds.getHeight()));

  auto area = bodyContent.getLocalBounds().reduced(juce::roundToInt((float)bodyMargin * zoomFactor), 0);
  for (auto* component : bodyComponents)
  {
    if (!component->isVisible()) continue;
    const int rowHeight = dynamic_cast<atom::Slider*>(component) != nullptr
                              ? juce::roundToInt((float)sliderHeight * zoomFactor)
                              : juce::roundToInt((float)cardRowHeight * zoomFactor);
    component->setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(juce::roundToInt((float)bodyPadding * zoomFactor));
  }
}

//==============================================================================
// 模型切换：显示/隐藏对应的参数
//==============================================================================

void ChorusAudioProcessorEditor::updateModelUI()
{
  const bool isChorus = (processor.getEffectModel() == ChorusAudioProcessor::kChorus);

  // Chorus 参数可见性
  if (chorusRateSlider != nullptr) chorusRateSlider->setVisible(isChorus);
  if (preDelaySlider != nullptr) preDelaySlider->setVisible(isChorus);
  if (chorusAmountSlider != nullptr) chorusAmountSlider->setVisible(isChorus);
  if (drySlider != nullptr) drySlider->setVisible(isChorus);
  if (wetSlider != nullptr) wetSlider->setVisible(isChorus);
  if (chorusFeedbackSlider != nullptr) chorusFeedbackSlider->setVisible(isChorus);

  // Phase90 参数可见性
  if (phase90RateSlider != nullptr) phase90RateSlider->setVisible(!isChorus);
  if (centerSlider != nullptr) centerSlider->setVisible(!isChorus);
  if (phase90AmountSlider != nullptr) phase90AmountSlider->setVisible(!isChorus);
  if (phase90FeedbackSlider != nullptr) phase90FeedbackSlider->setVisible(!isChorus);
  if (mixSlider != nullptr) mixSlider->setVisible(!isChorus);

  // 更新 body content 高度并重新布局
  bodyContentHeight = bodyMargin;
  for (auto* component : bodyComponents)
  {
    if (!component->isVisible()) continue;
    const int rowHeight = dynamic_cast<atom::Slider*>(component) != nullptr ? sliderHeight : cardRowHeight;
    bodyContentHeight += rowHeight + bodyPadding;
  }
  bodyContentHeight += bodyMargin;

  applyZoom(zoomFactor);
}

#if JucePlugin_Build_Standalone
void ChorusAudioProcessorEditor::applyAudioSettingsDialogTitleBarTheme()
{
  if (audioSettingsDialog == nullptr) return;

  applySystemNativeTitleBarTheme(*audioSettingsDialog);
}

void ChorusAudioProcessorEditor::darkModeSettingChanged() { applyAudioSettingsDialogTitleBarTheme(); }

void ChorusAudioProcessorEditor::showAudioSettingsDialog()
{
  if (audioSettingsDialog != nullptr)
  {
    audioSettingsDialog->toFront(true);
    audioSettingsDialog->grabKeyboardFocus();
    return;
  }

  auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
  if (window == nullptr) return;

  auto* panel = new AudioSettingsPanel(window->getDeviceManager(), atomLookAndFeel);

  panel->setSize(560, 420);

  const int minPanelW = panel->getMinimumPanelWidth();
  const int minPanelH = panel->getMinimumPanelHeight();
  constexpr int kMaxMinW = 520;
  constexpr int kMaxMinH = 600;
  const int clampedMinW = juce::jmin(minPanelW, kMaxMinW);
  const int clampedMinH = juce::jmin(minPanelH, kMaxMinH);
  const int minDialogW = juce::jmax(400, clampedMinW + 20);
  const int minDialogH = juce::jmax(300, clampedMinH + 40);

  juce::DialogWindow::LaunchOptions options;
  options.dialogTitle = "Audio Settings";
  options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
  options.useBottomRightCornerResizer = false;
  options.content.setOwned(panel);
  options.componentToCentreAround = window;

  auto* dialog = options.create();
  audioSettingsDialog = dialog;

  if (dialog != nullptr)
  {
    dialog->setResizeLimits(minDialogW, minDialogH, 1600, 1200);
    dialog->setAlwaysOnTop(true);
    applyAudioSettingsDialogTitleBarTheme();

    juce::Component::SafePointer<juce::Component> safeDialog(dialog);
    juce::Timer::callAfterDelay(0,
                                [safeDialog]()
                                {
                                  if (safeDialog != nullptr) applySystemNativeTitleBarTheme(*safeDialog);
                                });

    juce::Component::SafePointer<ChorusAudioProcessorEditor> safeEditor(this);
    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safeEditor](int)
                                {
                                  if (safeEditor != nullptr) safeEditor->audioSettingsDialog = nullptr;
                                }),
                            true);
  }
}

void ChorusAudioProcessorEditor::showStandaloneOptionsMenu()
{
  auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
  if (window == nullptr) return;

  juce::PopupMenu menu;
  menu.addItem(1, TRANS("Audio/MIDI Settings..."));
  menu.addSeparator();
  menu.addItem(2, TRANS("Save current state..."));
  menu.addItem(3, TRANS("Load a saved state..."));
  menu.addSeparator();
  menu.addItem(4, TRANS("Reset to default state"));

  juce::Component::SafePointer<juce::StandaloneFilterWindow> safeWindow(window);
  menu.showMenuAsync(
      juce::PopupMenu::Options(),
      [safeWindow, safeEditor = juce::Component::SafePointer<ChorusAudioProcessorEditor>(this)](int result)
      {
        if (result == 0) return;

        if (result == 1)
        {
          if (safeEditor != nullptr) safeEditor->showAudioSettingsDialog();
          return;
        }

        if (safeWindow != nullptr) safeWindow->handleMenuResult(result);
      });
}
#endif
