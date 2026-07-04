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

  headerBar.getBtnSettings().onClick = [this]
  {
#if JucePlugin_Build_Standalone
    showStandaloneOptionsMenu();
#endif
  };

  headerBar.getTapTempo().onBPMChanged = [this](double bpm)
  {
    if (auto* param = processor.parameters.valueTreeState.getParameter("rate"))
    {
      const float rateHz = juce::jlimit(0.01f, 20.0f, (float)(bpm / 60.0));
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
  const juce::StringArray hiddenParamIds{"dry"};  // 不可用的参数，不在 UI 中显示

  const auto uiFont = AtomLookAndFeel::getUIFont(AtomLookAndFeel::getSystemUIFontHeight(), juce::Font::plain);
  const float uiFontHeight = AtomLookAndFeel::getSystemUIFontHeight();
  float maxParamLabelWidth = 0.0f;
  float maxValueTextWidth = 0.0f;

  for (int i = 0; i < parameters.size(); ++i)
  {
    if (const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameters[i]))
    {
      if (headerParamIds.contains(parameter->paramID) || hiddenParamIds.contains(parameter->paramID)) continue;

      if (processor.parameters.parameterTypes[i] != "Slider") continue;

      maxParamLabelWidth = juce::jmax(maxParamLabelWidth, uiFont.getStringWidthFloat(parameter->name));

      if (auto* param = processor.parameters.valueTreeState.getParameter(parameter->paramID))
      {
        // 数值文本宽度（如 "20.00"）
        const float numW0 = uiFont.getStringWidthFloat(param->getText(0.0f, 0));
        const float numW1 = uiFont.getStringWidthFloat(param->getText(1.0f, 0));
        // 加上后缀宽度（如 "ms"），因为 atom::Slider 实际显示的是 getTextFromValue() = 数值 + 后缀
        const float suffixW = uiFont.getStringWidthFloat(parameter->label);
        maxValueTextWidth = juce::jmax(maxValueTextWidth, numW0 + suffixW, numW1 + suffixW);
      }
    }
  }

  const float labelReserveDlu = maxParamLabelWidth > 0.0f ? maxParamLabelWidth * 8.0f / uiFontHeight : 0.0f;
  const float valueReserveDlu = maxValueTextWidth > 0.0f ? (maxValueTextWidth + 12.0f) * 8.0f / uiFontHeight : 0.0f;

  for (int i = 0; i < parameters.size(); ++i)
  {
    if (const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameters[i]))
    {
      if (headerParamIds.contains(parameter->paramID) || hiddenParamIds.contains(parameter->paramID)) continue;

      if (processor.parameters.parameterTypes[i] == "Slider")
      {
        auto* aSlider = sliders.add(new atom::Slider());
        aSlider->setTextValueSuffix(parameter->label);
        aSlider->setValueLabelPos(atom::Slider::ValueLabelPos::Right);

        atom::SliderStyleOverride styleOverride;
        styleOverride.colors.labelText = parameter->name;
        styleOverride.metrics.linearHorizontalLabelReserveDlu = labelReserveDlu;
        styleOverride.metrics.linearHorizontalValueLabelReserveDlu = valueReserveDlu;
        atomLookAndFeel.setSliderStyleOverride(*aSlider, styleOverride);

        sliderAttachments.add(new SliderAttachment(processor.parameters.valueTreeState, parameter->paramID, *aSlider));

        bodyContent.addAndMakeVisible(aSlider);
        bodyComponents.add(aSlider);
        bodyContentHeight += sliderHeight + bodyPadding;
      }
      else if (processor.parameters.parameterTypes[i] == "ToggleButton")
      {
        auto* aButton = toggles.add(new atom::ToggleButton(parameter->paramID, {}));
        aButton->setToggleState(parameter->getDefaultValue(), juce::dontSendNotification);

        buttonAttachments.add(new ButtonAttachment(processor.parameters.valueTreeState, parameter->paramID, *aButton));

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

int ChorusAudioProcessorEditor::getEditorWidth() { return headerBar.getMinimumContentWidth(getHeaderHeight()); }

int ChorusAudioProcessorEditor::getNaturalHeight() const noexcept
{
  return getHeaderHeight() + getBodyContentHeight() + getFooterHeight();
}

void ChorusAudioProcessorEditor::applyZoom(float newZoom)
{
  zoomFactor = juce::jlimit(0.75f, 1.25f, newZoom);

  const int width = getEditorWidth();
  const int height = getNaturalHeight();

  // Fixed size: the editor (and therefore the Standalone window) is not
  // freely stretchable. Size changes only happen via the footer VIEW zoom.
  // min == max makes the host treat the editor as non-resizable, so the
  // outer window always matches the editor bounds and no background mismatch
  // can appear from dragging the window frame.
  setResizeLimits(width, height, width, height);
  setSize(width, height);

  bodyContent.setSize(width, getBodyContentHeight());
  resized();
}

void ChorusAudioProcessorEditor::timerCallback()
{
  headerBar.setMeterLevels(processor.getMeterLevelMono(), processor.getMeterLevelLeft(),
                           processor.getMeterLevelRight());
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

  bodyContent.setSize(juce::jmax(getWidth(), getEditorWidth()), juce::jmax(getBodyContentHeight(), bounds.getHeight()));

  auto area = bodyContent.getLocalBounds().reduced(juce::roundToInt((float)bodyMargin * zoomFactor), 0);
  for (auto* component : bodyComponents)
  {
    const int rowHeight = dynamic_cast<atom::Slider*>(component) != nullptr
                              ? juce::roundToInt((float)sliderHeight * zoomFactor)
                              : juce::roundToInt((float)cardRowHeight * zoomFactor);
    component->setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(juce::roundToInt((float)bodyPadding * zoomFactor));
  }
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

  // 初始大小使用固定值，与反向约束的最小值解耦
  panel->setSize(560, 420);

  // 计算反向约束的最小值，用于限制对话框缩小
  const int minPanelW = panel->getMinimumPanelWidth();
  const int minPanelH = panel->getMinimumPanelHeight();
  // 最小值上限 520px，避免因设备名过长导致对话框无法缩小到合理尺寸
  constexpr int kMaxMinW = 520;
  constexpr int kMaxMinH = 600;
  const int clampedMinW = juce::jmin(minPanelW, kMaxMinW);
  const int clampedMinH = juce::jmin(minPanelH, kMaxMinH);
  // 对话框最小尺寸（内容 + 窗口装饰预留）
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
