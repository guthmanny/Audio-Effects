/*
  ==============================================================================

    Chorus / Phase90 plugin — DSP via minibuss Track + Processors.

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "../JuceLibraryCode/JuceHeader.h"
#include "MinibussChorusEngine.h"
#include "QPitchDetector.h"
#include "SpectrumAnalyzer.h"
#include "PluginParameter.h"

//==============================================================================

class ChorusAudioProcessor : public AudioProcessor
{
 public:
  //==============================================================================

  ChorusAudioProcessor();
  ~ChorusAudioProcessor() override;

  //==============================================================================

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  using AudioProcessor::processBlock;
  void processBlock(AudioSampleBuffer&, MidiBuffer&) override;

  //==============================================================================

  void getStateInformation(MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  //==============================================================================

  AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override;

  //==============================================================================

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

  //==============================================================================

  const String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  //==============================================================================

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const String getProgramName(int index) override;
  void changeProgramName(int index, const String& newName) override;

  //==============================================================================

  enum EffectModel { kChorus = 0, kPhase90 = 1 };

  PluginParametersManager parameters;

  // 公共参数（两个模型共用）
  PluginParameterLinSlider paramInputGain;
  PluginParameterLinSlider paramGateThreshold;
  PluginParameterLinSlider paramGateThreshMin;
  PluginParameterLinSlider paramGateThreshMax;
  PluginParameterComboBox paramGateOffAtMin;
  PluginParameterLinSlider paramGateRatio;
  PluginParameterLinSlider paramGateAttack;
  PluginParameterLinSlider paramGateRelease;
  PluginParameterComboBox paramGateKnee;
  PluginParameterLinSlider paramGateKneeWidth;
  PluginParameterLinSlider paramOutputGain;
  PluginParameterToggle paramBypass;

  // Chorus 模型参数（与 MonoChorusProcessor 反射 id 对齐）
  PluginParameterLinSlider paramChorusRate;
  PluginParameterLinSlider paramChorusDelay;
  PluginParameterLinSlider paramChorusAmount;
  PluginParameterLinSlider paramChorusWet;
  PluginParameterLinSlider paramChorusFeedback;

  // Settings → MIDI CC：Chorus 参数的 CC 分配（0=Off, 1..128 = CC0..CC127）
  PluginParameterComboBox paramMidiCcChorusRate;
  PluginParameterComboBox paramMidiCcChorusDelay;
  PluginParameterComboBox paramMidiCcChorusAmount;
  PluginParameterComboBox paramMidiCcChorusWet;
  PluginParameterComboBox paramMidiCcChorusFeedback;

  // Phase90 模型参数
  PluginParameterLogSlider paramPhase90Rate;
  PluginParameterLogSlider paramCenter;
  PluginParameterLinSlider paramPhase90Amount;
  PluginParameterLinSlider paramPhase90Feedback;
  PluginParameterLinSlider paramMix;

  float getMeterLevelMono() const noexcept { return meterMono.load(); }
  float getMeterLevelLeft() const noexcept { return meterLeft.load(); }
  float getMeterLevelRight() const noexcept { return meterRight.load(); }

  EffectModel getEffectModel() const noexcept { return currentModel.load(); }
  void setEffectModel(EffectModel model) noexcept;

  void setTunerEnabled(bool shouldEnable) noexcept;
  bool isTunerEnabled() const noexcept { return tunerEnabled.load(); }
  QPitchDetector::Result getTunerResult() const noexcept;

  void setTunerPeriodicityThreshold(float threshold) noexcept;
  float getTunerPeriodicityThreshold() const noexcept { return tunerPeriodicityThreshold.load(); }

  void setSpectrumEnabled(bool shouldEnable) noexcept;
  bool isSpectrumEnabled() const noexcept { return spectrumEnabled.load(); }
  void setSpectrumFftSize(int fftSize);
  /** Copies new spectrum data into @p dest if frame advanced since @p lastFrameId. */
  bool copySpectrumMagnitudesIfNew (uint32_t& lastFrameId, std::vector<float>& dest) const;
  int getSpectrumFftSize() const noexcept { return spectrumFftSize.load(); }
  double getSpectrumSampleRate() const noexcept { return currentSampleRate; }

 private:
  //==============================================================================

  static constexpr int maxChannels = 2;

  float readParameterValue(const String& paramId, float fallback) const;
  void syncParametersFromValueTree();
  void updateEffectParameters();
  void ensureScratchBuffers(int numChannels, int numSamples);
  void pushAnalysisMono(const AudioSampleBuffer& buffer, int numChannels, int numSamples);
  void handleIncomingMidi (const MidiBuffer& midiMessages);
  void applyMidiCcToParameter (PluginParameterLinSlider& target, float controllerNormalized);

  /** Choice index 0 = Off; 1..128 = CC 0..127. Returns -1 if Off. */
  static int midiCcChoiceToNumber (float choiceValue) noexcept;

  MinibussChorusEngine minibussEngine;
  AudioSampleBuffer dryBuffer;
  AudioSampleBuffer monoBuffer;
  AudioSampleBuffer processBuffer;

  double currentSampleRate = 44100.0;

  std::atomic<float> meterMono{0.0f};
  std::atomic<float> meterLeft{0.0f};
  std::atomic<float> meterRight{0.0f};
  std::atomic<EffectModel> currentModel{kChorus};

  std::atomic<bool> tunerEnabled{false};
  std::atomic<float> tunerPeriodicityThreshold{0.7f};
  QPitchDetector pitchDetector;
  mutable juce::SpinLock tunerLock;
  QPitchDetector::Result tunerResult{};

  std::atomic<bool> spectrumEnabled{false};
  std::atomic<int> spectrumFftSize{1 << SpectrumAnalyzer::defaultFftOrder};
  SpectrumAnalyzer spectrumAnalyzer;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChorusAudioProcessor)
};
