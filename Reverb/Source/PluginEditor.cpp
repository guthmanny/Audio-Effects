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

ReverbAudioProcessorEditor::ReverbAudioProcessorEditor(ReverbAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
  juce::LookAndFeel::setDefaultLookAndFeel(&atomLookAndFeel);

  addAndMakeVisible(headerBar);
  addAndMakeVisible(footerBar);
  addAndMakeVisible(bodyViewport);
  bodyViewport.setViewedComponent(&bodyContent, false);
  bodyViewport.setScrollBarsShown(true, false);
  bodyViewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, juce::Colours::grey);

  // === 模型选择：通过 Footer MIDI 端口图标 ===
  footerBar.getBtnMidiPort().onClick = [this]
  {
    juce::PopupMenu menu;
    menu.addItem(1, "Freeverb", true, processor.getReverbModel() == ReverbAudioProcessor::kFreeverb);
    menu.addItem(2, "Plate", true, processor.getReverbModel() == ReverbAudioProcessor::kPlate);
    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [this](int result)
                       {
                         if (result == 0) return;
                         processor.setReverbModel(result == 2 ? ReverbAudioProcessor::kPlate
                                                              : ReverbAudioProcessor::kFreeverb);
                         updateModelUI();
                       });
  };

  headerBar.getBtnSettings().onClick = [this]
  {
#if JucePlugin_Build_Standalone
    showStandaloneOptionsMenu();
#endif
  };

  footerBar.onZoomChanged = [this](float scale) { applyZoom(scale); };

  // Header attachments
  sliderAttachments.add(
      new SliderAttachment(processor.parameters.valueTreeState, "inputgain", headerBar.getSliderInput()));
  sliderAttachments.add(
      new SliderAttachment(processor.parameters.valueTreeState, "gatethreshold", headerBar.getSliderGate()));
  sliderAttachments.add(
      new SliderAttachment(processor.parameters.valueTreeState, "outputgain", headerBar.getSliderOutput()));

  const juce::Array<juce::AudioProcessorParameter*>& params = processor.getParameters();
  int comboBoxCounter = 0;
  bodyContentHeight = bodyMargin;
  const juce::StringArray headerParamIds{"inputgain", "gatethreshold", "outputgain"};

  const auto uiFont = AtomLookAndFeel::getUIFont(AtomLookAndFeel::getSystemUIFontHeight(), juce::Font::plain);
  const float uiFontHeight = AtomLookAndFeel::getSystemUIFontHeight();
  float maxParamLabelWidth = 0.0f, maxValueTextWidth = 0.0f;

  for (int i = 0; i < params.size(); ++i)
  {
    if (auto* p = dynamic_cast<const juce::AudioProcessorParameterWithID*>(params[i]))
    {
      if (headerParamIds.contains(p->paramID)) continue;
      if (processor.parameters.parameterTypes[i] != "Slider") continue;
      maxParamLabelWidth = juce::jmax(maxParamLabelWidth, uiFont.getStringWidthFloat(p->name));
      if (auto* param = processor.parameters.valueTreeState.getParameter(p->paramID))
      {
        const float n0 = uiFont.getStringWidthFloat(param->getText(0.0f, 0));
        const float n1 = uiFont.getStringWidthFloat(param->getText(1.0f, 0));
        const float sw = uiFont.getStringWidthFloat(p->label);
        maxValueTextWidth = juce::jmax(maxValueTextWidth, n0 + sw, n1 + sw);
      }
    }
  }

  savedLabelReserveDlu = maxParamLabelWidth > 0.0f ? maxParamLabelWidth * 8.0f / uiFontHeight : 0.0f;
  savedValueReserveDlu = maxValueTextWidth > 0.0f ? (maxValueTextWidth + 12.0f) * 8.0f / uiFontHeight : 0.0f;

  for (int i = 0; i < params.size(); ++i)
  {
    if (auto* p = dynamic_cast<const juce::AudioProcessorParameterWithID*>(params[i]))
    {
      if (headerParamIds.contains(p->paramID)) continue;

      if (processor.parameters.parameterTypes[i] == "Slider")
      {
        auto* slider = sliders.add(new atom::Slider());

        // Track slider pointers
        if (p->paramID == "roomsize") roomsizeSlider = slider;
        else if (p->paramID == "damp") dampSlider = slider;
        else if (p->paramID == "width") widthSlider = slider;
        else if (p->paramID == "fvwet") freeverbWetSlider = slider;
        else if (p->paramID == "fvdry") freeverbDrySlider = slider;
        else if (p->paramID == "freeze") freezeSlider = slider;
        else if (p->paramID == "predelay") predelaySlider = slider;
        else if (p->paramID == "bandwidth") bandwidthSlider = slider;
        else if (p->paramID == "damping") dampingSlider = slider;
        else if (p->paramID == "decay") decaySlider = slider;
        else if (p->paramID == "platewet") plateWetSlider = slider;

        slider->setTextValueSuffix(p->label);
        slider->setValueLabelPos(atom::Slider::ValueLabelPos::Right);

        atom::SliderStyleOverride styleOverride;
        styleOverride.colors.labelText = p->name;
        styleOverride.metrics.linearHorizontalLabelReserveDlu = savedLabelReserveDlu;
        styleOverride.metrics.linearHorizontalValueLabelReserveDlu = savedValueReserveDlu;
        atomLookAndFeel.setSliderStyleOverride(*slider, styleOverride);

        sliderAttachments.add(new SliderAttachment(processor.parameters.valueTreeState, p->paramID, *slider));

        bodyContent.addAndMakeVisible(slider);
        bodyComponents.add(slider);
        bodyContentHeight += sliderHeight + bodyPadding;
      }
      else if (processor.parameters.parameterTypes[i] == "ToggleButton")
      {
        auto* btn = toggles.add(new atom::ToggleButton(p->paramID, {}));
        btn->setToggleState(p->getDefaultValue(), juce::dontSendNotification);
        buttonAttachments.add(new ButtonAttachment(processor.parameters.valueTreeState, p->paramID, *btn));
        auto* row = new SettingsCardRow(p->paramID + "Row", p->name, *btn, cardRowHeight);
        settingRows.add(row);
        bodyContent.addAndMakeVisible(row);
        bodyComponents.add(row);
        bodyContentHeight += cardRowHeight + bodyPadding;
      }
      else if (processor.parameters.parameterTypes[i] == "ComboBox")
      {
        auto* cb = comboBoxes.add(new atom::ComboBox());
        cb->setEditableText(false);
        cb->setJustificationType(juce::Justification::centredLeft);
        cb->addItemList(processor.parameters.comboBoxItemLists[comboBoxCounter++], 1);
        comboBoxAttachments.add(new ComboBoxAttachment(processor.parameters.valueTreeState, p->paramID, *cb));
        auto* row = new SettingsCardRow(p->paramID + "Row", p->name, *cb, cardRowHeight);
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

ReverbAudioProcessorEditor::~ReverbAudioProcessorEditor()
{
  stopTimer();
#if JucePlugin_Build_Standalone
  juce::Desktop::getInstance().removeDarkModeSettingListener(this);
#endif
  for (auto* s : sliders) atomLookAndFeel.clearSliderStyleOverride(*s);
  juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

int ReverbAudioProcessorEditor::getHeaderHeight() const noexcept
{
  return juce::roundToInt((float)headerBaseHeight * zoomFactor);
}
int ReverbAudioProcessorEditor::getFooterHeight() const noexcept
{
  return juce::roundToInt((float)footerBaseHeight * zoomFactor);
}
int ReverbAudioProcessorEditor::getBodyContentHeight() const noexcept
{
  return juce::roundToInt((float)bodyContentHeight * zoomFactor);
}
int ReverbAudioProcessorEditor::getEditorWidth()
{
  return juce::jmax(headerBar.getMinimumContentWidth(getHeaderHeight()),
                    footerBar.getMinimumContentWidth(getFooterHeight()));
}
int ReverbAudioProcessorEditor::getNaturalHeight() const noexcept
{
  return getHeaderHeight() + getBodyContentHeight() + getFooterHeight();
}

void ReverbAudioProcessorEditor::applyZoom(float newZoom)
{
  zoomFactor = juce::jlimit(0.75f, 1.25f, newZoom);
  const int w = getEditorWidth(), h = getNaturalHeight();
  setResizeLimits(w, h, w, h);
  setSize(w, h);
  bodyContent.setSize(w, getBodyContentHeight());
  resized();
}

void ReverbAudioProcessorEditor::timerCallback()
{
  headerBar.setMeterLevels(processor.getMeterLevelMono(), processor.getMeterLevelLeft(),
                           processor.getMeterLevelRight());
}

void ReverbAudioProcessorEditor::paint(juce::Graphics& g)
{
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ReverbAudioProcessorEditor::resized()
{
  auto bounds = getLocalBounds();
  const int headerH = getHeaderHeight(), footerH = getFooterHeight();
  headerBar.setBounds(bounds.removeFromTop(headerH));
  footerBar.setBounds(bounds.removeFromBottom(footerH));
  bodyViewport.setBounds(bounds);
  bodyContent.setSize(juce::jmax(getWidth(), getEditorWidth()),
                      juce::jmax(getBodyContentHeight(), bounds.getHeight()));
  auto area = bodyContent.getLocalBounds().reduced(juce::roundToInt((float)bodyMargin * zoomFactor), 0);
  for (auto* c : bodyComponents)
  {
    if (!c->isVisible()) continue;
    const int rh = dynamic_cast<atom::Slider*>(c) ? juce::roundToInt((float)sliderHeight * zoomFactor)
                                                  : juce::roundToInt((float)cardRowHeight * zoomFactor);
    c->setBounds(area.removeFromTop(rh));
    area.removeFromTop(juce::roundToInt((float)bodyPadding * zoomFactor));
  }
}

//==============================================================================
// 模型切换
//==============================================================================

void ReverbAudioProcessorEditor::updateModelUI()
{
  const bool isFreeverb = (processor.getReverbModel() == ReverbAudioProcessor::kFreeverb);

  // Freeverb 参数可见性
  if (roomsizeSlider) roomsizeSlider->setVisible(isFreeverb);
  if (dampSlider) dampSlider->setVisible(isFreeverb);
  if (widthSlider) widthSlider->setVisible(isFreeverb);
  if (freeverbWetSlider) freeverbWetSlider->setVisible(isFreeverb);
  if (freeverbDrySlider) freeverbDrySlider->setVisible(isFreeverb);
  if (freezeSlider) freezeSlider->setVisible(isFreeverb);

  // Plate 参数可见性
  if (predelaySlider) predelaySlider->setVisible(!isFreeverb);
  if (bandwidthSlider) bandwidthSlider->setVisible(!isFreeverb);
  if (dampingSlider) dampingSlider->setVisible(!isFreeverb);
  if (decaySlider) decaySlider->setVisible(!isFreeverb);
  if (plateWetSlider) plateWetSlider->setVisible(!isFreeverb);

  // 重新计算高度
  bodyContentHeight = bodyMargin;
  for (auto* c : bodyComponents)
  {
    if (!c->isVisible()) continue;
    bodyContentHeight += (dynamic_cast<atom::Slider*>(c) ? sliderHeight : cardRowHeight) + bodyPadding;
  }
  bodyContentHeight += bodyMargin;
  applyZoom(zoomFactor);
}

//==============================================================================
// Standalone helpers
//==============================================================================

#if JucePlugin_Build_Standalone
void ReverbAudioProcessorEditor::applyAudioSettingsDialogTitleBarTheme()
{
  if (audioSettingsDialog) applySystemNativeTitleBarTheme(*audioSettingsDialog);
}
void ReverbAudioProcessorEditor::darkModeSettingChanged() { applyAudioSettingsDialogTitleBarTheme(); }

void ReverbAudioProcessorEditor::showAudioSettingsDialog()
{
  if (audioSettingsDialog)
  {
    audioSettingsDialog->toFront(true);
    audioSettingsDialog->grabKeyboardFocus();
    return;
  }
  auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
  if (!window) return;
  auto* panel = new AudioSettingsPanel(window->getDeviceManager(), atomLookAndFeel);
  panel->setSize(560, 420);

  const int minPW = panel->getMinimumPanelWidth(), minPH = panel->getMinimumPanelHeight();
  constexpr int kMaxMinW = 520, kMaxMinH = 600;
  const int minDW = juce::jmax(400, juce::jmin(minPW, kMaxMinW) + 20);
  const int minDH = juce::jmax(300, juce::jmin(minPH, kMaxMinH) + 40);

  juce::DialogWindow::LaunchOptions opts;
  opts.dialogTitle = "Audio Settings";
  opts.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
  opts.escapeKeyTriggersCloseButton = true;
  opts.useNativeTitleBar = true;
  opts.resizable = true;
  opts.useBottomRightCornerResizer = false;
  opts.content.setOwned(panel);
  opts.componentToCentreAround = window;
  auto* dialog = opts.create();
  audioSettingsDialog = dialog;
  if (dialog)
  {
    dialog->setResizeLimits(minDW, minDH, 1600, 1200);
    dialog->setAlwaysOnTop(true);
    applyAudioSettingsDialogTitleBarTheme();
    juce::Component::SafePointer<juce::Component> sd(dialog);
    juce::Timer::callAfterDelay(0, [sd] { if (sd) applySystemNativeTitleBarTheme(*sd); });
    juce::Component::SafePointer<ReverbAudioProcessorEditor> se(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([se](int) { if (se) se->audioSettingsDialog = nullptr; }), true);
  }
}

void ReverbAudioProcessorEditor::showStandaloneOptionsMenu()
{
  auto* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
  if (!window) return;
  juce::PopupMenu menu;
  menu.addItem(1, TRANS("Audio/MIDI Settings..."));
  menu.addSeparator();
  menu.addItem(2, TRANS("Save current state..."));
  menu.addItem(3, TRANS("Load a saved state..."));
  menu.addSeparator();
  menu.addItem(4, TRANS("Reset to default state"));
  juce::Component::SafePointer<juce::StandaloneFilterWindow> sw(window);
  menu.showMenuAsync(juce::PopupMenu::Options(),
                     [sw, se = juce::Component::SafePointer<ReverbAudioProcessorEditor>(this)](int r)
                     {
                       if (r == 0) return;
                       if (r == 1) { if (se) se->showAudioSettingsDialog(); return; }
                       if (sw) sw->handleMenuResult(r);
                     });
}
#endif
