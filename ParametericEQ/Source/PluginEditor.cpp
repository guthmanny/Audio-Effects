#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "AudioSettingsPanel.h"
#endif

namespace
{
#if JucePlugin_Build_Standalone
void applySystemNativeTitleBarTheme(juce::Component& target)
{
  atom::setNativeTitleBarDarkMode(target, juce::Desktop::getInstance().isDarkModeActive());
}
#endif
}  // namespace

ParametricEQAudioProcessorEditor::ParametricEQAudioProcessorEditor(ParametricEQAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
  juce::LookAndFeel::setDefaultLookAndFeel(&atomLookAndFeel);

  addAndMakeVisible(headerBar);
  addAndMakeVisible(eqCurve);
  addAndMakeVisible(footerBar);

  headerBar.getBtnSettings().onClick = [this]
  {
#if JucePlugin_Build_Standalone
    showStandaloneOptionsMenu();
#endif
  };

  footerBar.onZoomChanged = [this](float scale) { applyZoom(scale); };

  footerBar.getQualityComboBox().onChange = [this]
  {
    switch (footerBar.getQualityComboBox().getSelectedId())
    {
      case 1:  processor.setOversampleFactor(2); break;
      case 2:  processor.setOversampleFactor(4); break;
      default: break;
    }

    updateEQCurve();
  };

  switch (processor.getOversampleFactor())
  {
    case 4:  footerBar.getQualityComboBox().setSelectedId(2, juce::dontSendNotification); break;
    default: footerBar.getQualityComboBox().setSelectedId(1, juce::dontSendNotification); break;
  }

  // --- 创建参数控件 ---
  sliderAttachments.add(
      new SliderAttachment(processor.parameters.valueTreeState, "inputgain", headerBar.getSliderInput()));
  sliderAttachments.add(
      new SliderAttachment(processor.parameters.valueTreeState, "outputgain", headerBar.getSliderOutput()));

  const juce::Array<juce::AudioProcessorParameter*>& params = processor.getParameters();
  int comboBoxCounter = 0;

  const juce::StringArray headerParamIds{"inputgain", "outputgain"};

  for (int i = 0; i < params.size(); ++i)
  {
    if (const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*>(params[i]))
    {
      if (headerParamIds.contains(parameter->paramID)) continue;

      if (processor.parameters.parameterTypes[i] == "Slider")
      {
        auto* aSlider = sliders.add(new atom::Slider());
        aSlider->setTextValueSuffix(parameter->label);
        aSlider->setValueLabelPos(atom::Slider::ValueLabelPos::Right);
        sliderAttachments.add(
            new SliderAttachment(processor.parameters.valueTreeState, parameter->paramID, *aSlider));
        addAndMakeVisible(aSlider);
        paramRows.add(aSlider);
      }
      else if (processor.parameters.parameterTypes[i] == "ToggleButton")
      {
        auto* aButton = toggles.add(new atom::ToggleButton(parameter->paramID, {}));
        aButton->setToggleState(parameter->getDefaultValue(), juce::dontSendNotification);
        buttonAttachments.add(
            new ButtonAttachment(processor.parameters.valueTreeState, parameter->paramID, *aButton));
        addAndMakeVisible(aButton);
        paramRows.add(aButton);
      }
      else if (processor.parameters.parameterTypes[i] == "ComboBox")
      {
        const auto items = processor.parameters.comboBoxItemLists[comboBoxCounter];
        auto* typeBtn = new juce::TextButton(parameter->paramID, items[0]);
        typeBtn->setButtonText(items[0]);
        typeBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A3E));
        typeBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF0A84FF));
        typeBtn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF0A84FF));

        const String paramId = parameter->paramID;
        typeBtn->onClick = [this, typeBtn, items, paramId]()
        {
          if (auto* param = processor.parameters.valueTreeState.getParameter(paramId))
          {
            int current = (int)param->convertFrom0to1(param->getValue());
            current = (current + 1) % items.size();
            param->setValueNotifyingHost(param->convertTo0to1((float)current));
            typeBtn->setButtonText(items[current]);
          }
        };
        comboBoxCounter++;
        addAndMakeVisible(typeBtn);
        paramRows.add(typeBtn);
        controlButtons.add(typeBtn);
      }
    }
  }

  updateEQCurve();
  applyZoom(1.0f);

#if JucePlugin_Build_Standalone
  juce::Desktop::getInstance().addDarkModeSettingListener(this);
#endif
}

ParametricEQAudioProcessorEditor::~ParametricEQAudioProcessorEditor()
{
  stopTimer();

#if JucePlugin_Build_Standalone
  juce::Desktop::getInstance().removeDarkModeSettingListener(this);
#endif

  for (auto* slider : sliders) atomLookAndFeel.clearSliderStyleOverride(*slider);
  juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void ParametricEQAudioProcessorEditor::applyZoom(float newZoom)
{
  zoomFactor = juce::jlimit(0.75f, 1.25f, newZoom);

  // 计算所有控件总高度
  int totalH = getHeaderHeight() + juce::roundToInt((float)eqCurveHeight * zoomFactor);

  for (int i = 0; i < paramRows.size(); ++i)
    totalH += juce::roundToInt((float)sliderHeight * zoomFactor);

  totalH += getFooterHeight();

  const int headerW = headerBar.getMinimumContentWidth(getHeaderHeight());
  const int footerW = footerBar.getMinimumContentWidth(getFooterHeight());
  const int width = juce::jmax(headerW, footerW);

  setResizeLimits(width, totalH, width, totalH);
  setSize(width, totalH);
}

int ParametricEQAudioProcessorEditor::getHeaderHeight() const noexcept
{
  return juce::roundToInt((float)headerBaseHeight * zoomFactor);
}

int ParametricEQAudioProcessorEditor::getFooterHeight() const noexcept
{
  return juce::roundToInt((float)footerBaseHeight * zoomFactor);
}

void ParametricEQAudioProcessorEditor::timerCallback()
{
}

bool ParametricEQAudioProcessorEditor::refreshEQCurveIfNeeded()
{
  const auto currentSampleRate = processor.getEffectiveSampleRate();
  auto& vts = processor.parameters.valueTreeState;

  auto getVal = [&](const String& pid, float def)
  {
    if (auto* v = vts.getRawParameterValue(pid)) return v->load();
    if (auto* p = vts.getParameter(pid)) return p->convertFrom0to1(p->getValue());
    return def;
  };

  bool changed = !eqCurveCacheValid || std::abs(currentSampleRate - lastCurveSampleRate) > 1.0e-6;

  for (int b = 0; b < ParametricEQAudioProcessor::numBands; ++b)
  {
    const auto& band = *processor.bands[(size_t)b];
    const float freq = getVal(band.frequency.paramID, band.frequency.defaultValue);
    const float q = getVal(band.q.paramID, band.q.defaultValue);
    const float gain = getVal(band.gain.paramID, band.gain.defaultValue);
    const int type = (int)juce::jlimit(0, 5, (int)getVal(band.type.paramID, (float)band.type.defaultChoice));

    if (!eqCurveCacheValid ||
        std::abs(freq - lastCurveFreqHz[(size_t)b]) > 1.0e-6f ||
        std::abs(q - lastCurveQ[(size_t)b]) > 1.0e-6f ||
        std::abs(gain - lastCurveGainDb[(size_t)b]) > 1.0e-6f ||
        type != lastCurveType[(size_t)b])
    {
      changed = true;
    }

    lastCurveFreqHz[(size_t)b] = freq;
    lastCurveQ[(size_t)b] = q;
    lastCurveGainDb[(size_t)b] = gain;
    lastCurveType[(size_t)b] = type;
  }

  if (!changed)
    return false;

  lastCurveSampleRate = currentSampleRate;
  eqCurveCacheValid = true;
  updateEQCurve();
  return true;
}

void ParametricEQAudioProcessorEditor::updateEQCurve()
{
  eqCurve.setBandConfigs(processor.getBandConfigs(), processor.getEffectiveSampleRate());

  // 传递频段滑块位置标记（频率 + Q）
  std::vector<std::pair<float, float>> markers;
  auto& vts = processor.parameters.valueTreeState;
  for (int b = 0; b < ParametricEQAudioProcessor::numBands; ++b)
  {
    const auto& band = *processor.bands[(size_t)b];
    auto getVal = [&](const String& pid, float def) {
      if (auto* p = vts.getParameter(pid)) return p->convertFrom0to1(p->getValue());
      if (auto* v = vts.getRawParameterValue(pid)) return v->load();
      return def;
    };
    const float freq = getVal(band.frequency.paramID, band.frequency.defaultValue);
    const float q = getVal(band.q.paramID, band.q.defaultValue);
    markers.emplace_back(freq, q);
  }
  eqCurve.setBandMarkers(markers);
}

void ParametricEQAudioProcessorEditor::paint(juce::Graphics& g)
{
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ParametricEQAudioProcessorEditor::resized()
{
  auto bounds = getLocalBounds();
  const int headerH = getHeaderHeight();
  const int footerH = getFooterHeight();
  const int eqH = juce::roundToInt((float)eqCurveHeight * zoomFactor);
  const int rowH = juce::roundToInt((float)sliderHeight * zoomFactor);

  headerBar.setBounds(bounds.removeFromTop(headerH));
  eqCurve.setBounds(bounds.removeFromTop(eqH));
  footerBar.setBounds(bounds.removeFromBottom(footerH));

  // 剩余空间平均分配给参数控件
  for (auto* row : paramRows)
  {
    if (row->isVisible())
      row->setBounds(bounds.removeFromTop(rowH));
  }
}

#if JucePlugin_Build_Standalone
void ParametricEQAudioProcessorEditor::applyAudioSettingsDialogTitleBarTheme()
{
  if (audioSettingsDialog == nullptr) return;
  applySystemNativeTitleBarTheme(*audioSettingsDialog);
}

void ParametricEQAudioProcessorEditor::darkModeSettingChanged() { applyAudioSettingsDialogTitleBarTheme(); }

void ParametricEQAudioProcessorEditor::showAudioSettingsDialog()
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

    juce::Component::SafePointer<ParametricEQAudioProcessorEditor> safeEditor(this);
    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safeEditor](int)
                                {
                                  if (safeEditor != nullptr) safeEditor->audioSettingsDialog = nullptr;
                                }),
                            true);
  }
}

void ParametricEQAudioProcessorEditor::showStandaloneOptionsMenu()
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
      [safeWindow, safeEditor = juce::Component::SafePointer<ParametricEQAudioProcessorEditor>(this)](int result)
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
