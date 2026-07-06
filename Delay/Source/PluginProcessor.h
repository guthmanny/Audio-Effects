/*
  ==============================================================================

    Delay / Analog Delay plugin — DSP via NuDSP camel.

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginParameter.h"
#include "nudsp/extensions/camel/delay.hpp"
#include "nudsp/extensions/camel/analog_delay.hpp"

//==============================================================================

class DelayAudioProcessor : public AudioProcessor
{
 public:
  //==============================================================================

  DelayAudioProcessor();
  ~DelayAudioProcessor() override;

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

  enum EffectModel { kDelay = 0, kAnalogDelay = 1 };

  PluginParametersManager parameters;

  // 公共参数（两个模型共用）
  PluginParameterLinSlider paramInputGain;
  PluginParameterLinSlider paramGateThreshold;
  PluginParameterLinSlider paramOutputGain;
  PluginParameterToggle paramBypass;

  // Delay 模型参数
  PluginParameterLinSlider paramDelayTime;
  PluginParameterLinSlider paramFeedback;
  PluginParameterLinSlider paramMix;

  // Analog Delay 模型参数（额外多了 LFO 调制）
  PluginParameterLinSlider paramAnalogDelayTime;
  PluginParameterLinSlider paramAnalogFeedback;
  PluginParameterLinSlider paramAnalogMix;
  PluginParameterLinSlider paramLfoRate;
  PluginParameterLinSlider paramLfoDepth;

  float getMeterLevelMono() const noexcept { return meterMono.load(); }
  float getMeterLevelLeft() const noexcept { return meterLeft.load(); }
  float getMeterLevelRight() const noexcept { return meterRight.load(); }

  EffectModel getEffectModel() const noexcept { return currentModel.load(); }
  void setEffectModel(EffectModel model) noexcept { currentModel.store(model); }

 private:
  //==============================================================================

  static constexpr int maxChannels = 2;

  float readParameterValue(const String& paramId, float fallback) const;
  void syncParametersFromValueTree();
  void updateEffectParameters();
  void ensureEffectInstances();

  void processInputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb);
  void processGate(AudioSampleBuffer& buffer, int numChannels, int numSamples, float thresholdDb);
  void processOutputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb);

  // Delay DSP
  std::unique_ptr<nudsp::camel::DelayF32> delay;

  // Analog Delay DSP
  std::unique_ptr<nudsp::camel::AnalogDelayF32> analogDelay;

  double currentSampleRate = 44100.0;
  std::array<float, maxChannels> gateEnvelope{};
  std::array<float, maxChannels> gateGain{1.0f, 1.0f};

  std::atomic<float> meterMono{0.0f};
  std::atomic<float> meterLeft{0.0f};
  std::atomic<float> meterRight{0.0f};
  std::atomic<EffectModel> currentModel{kDelay};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DelayAudioProcessor)
};
