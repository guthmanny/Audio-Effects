/*
  ==============================================================================

    Reverb plugin — Freeverb / Plate reverb via NuDSP camel.

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginParameter.h"
#include "nudsp/extensions/camel/freeverb.hpp"
#include "nudsp/extensions/camel/reverb.hpp"

//==============================================================================

class ReverbAudioProcessor : public AudioProcessor
{
 public:
  //==============================================================================

  ReverbAudioProcessor();
  ~ReverbAudioProcessor() override;

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

  enum ReverbModel { kFreeverb = 0, kPlate = 1 };

  PluginParametersManager parameters;

  // 公共参数
  PluginParameterLinSlider paramInputGain;
  PluginParameterLinSlider paramGateThreshold;
  PluginParameterLinSlider paramOutputGain;
  PluginParameterToggle paramBypass;

  // Freeverb 参数
  PluginParameterLinSlider paramRoomsize;
  PluginParameterLinSlider paramDamp;
  PluginParameterLinSlider paramWidth;
  PluginParameterLinSlider paramFreeverbWet;
  PluginParameterLinSlider paramFreeverbDry;
  PluginParameterLinSlider paramFreeze;

  // Plate Reverb (Dattorro) 参数
  PluginParameterLinSlider paramPredelay;
  PluginParameterLogSlider paramBandwidth;
  PluginParameterLogSlider paramDamping;
  PluginParameterLinSlider paramDecay;
  PluginParameterLinSlider paramPlateWet;

  float getMeterLevelMono() const noexcept { return meterMono.load(); }
  float getMeterLevelLeft() const noexcept { return meterLeft.load(); }
  float getMeterLevelRight() const noexcept { return meterRight.load(); }

  ReverbModel getReverbModel() const noexcept { return currentModel.load(); }
  void setReverbModel(ReverbModel model) noexcept { currentModel.store(model); }

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

  // Freeverb DSP
  std::unique_ptr<nudsp::camel::FreeverbF32> freeverb;

  // Plate Reverb DSP
  std::unique_ptr<nudsp::camel::ReverbF32> plate;

  double currentSampleRate = 44100.0;
  std::array<float, maxChannels> gateEnvelope{};
  std::array<float, maxChannels> gateGain{1.0f, 1.0f};

  std::atomic<float> meterMono{0.0f};
  std::atomic<float> meterLeft{0.0f};
  std::atomic<float> meterRight{0.0f};
  std::atomic<ReverbModel> currentModel{kFreeverb};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbAudioProcessor)
};
