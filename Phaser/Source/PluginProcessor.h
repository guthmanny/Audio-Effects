/*
  ==============================================================================

    Phaser plugin — DSP via NuDSP camel.
    4 phaser models: Phaser, Phase90, OTA Phaser, JFET Phaser.

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginParameter.h"
#include "nudsp/extensions/camel/phaser.hpp"
#include "nudsp/extensions/camel/phase90.hpp"
#include "nudsp/extensions/camel/ota_phaser.hpp"
#include "nudsp/extensions/camel/jfet_phaser.hpp"
#include "nudsp/amplitude/dry_wet.hpp"
#include "nudsp/amplitude/dry_wet_f32.h"

//==============================================================================

class PhaserAudioProcessor : public AudioProcessor
{
 public:
  //==============================================================================

  PhaserAudioProcessor();
  ~PhaserAudioProcessor() override;

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

  enum EffectModel { kPhaser = 0, kPhase90 = 1, kOtaPhaser = 2, kJfetPhaser = 3 };

  PluginParametersManager parameters;

  // 公共参数（四个模型共用）
  PluginParameterLinSlider paramInputGain;
  PluginParameterLinSlider paramGateThreshold;
  PluginParameterLinSlider paramOutputGain;
  PluginParameterToggle paramBypass;

  // Phaser / Phase90 / OTA / JFET 通用参数
  PluginParameterLogSlider paramRate;
  PluginParameterLogSlider paramCenter;
  PluginParameterLinSlider paramAmount;
  PluginParameterLinSlider paramFeedback;
  PluginParameterLinSlider paramMix;

  float getMeterLevelMono() const noexcept { return meterMono.load(); }
  float getMeterLevelLeft() const noexcept { return meterLeft.load(); }
  float getMeterLevelRight() const noexcept { return meterRight.load(); }

  EffectModel getEffectModel() const noexcept { return currentModel.load(); }
  void setEffectModel(EffectModel model) noexcept { currentModel.store(model); }

  // 获取当前模型名称（用于 UI）
  static const char* getEffectModelName(EffectModel model) noexcept
  {
    switch (model)
    {
      case kPhaser:      return "Phaser";
      case kPhase90:     return "Phase90";
      case kOtaPhaser:   return "OTA Phaser";
      case kJfetPhaser:  return "JFET Phaser";
      default:           return "Phaser";
    }
  }

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

  // Phaser DSP instances (one per model)
  std::unique_ptr<nudsp::camel::PhaserF32> phaser;
  std::unique_ptr<nudsp::camel::Phase90F32> phase90;
  std::unique_ptr<nudsp::camel::OtaPhaserF32> otaPhaser;
  std::unique_ptr<nudsp::camel::JfetPhaserF32> jfetPhaser;
  std::array<std::unique_ptr<nudsp::DryWetF32>, maxChannels> dryWets;
  AudioSampleBuffer dryBuffer;
  AudioSampleBuffer fxBuffer;

  double currentSampleRate = 44100.0;
  std::array<float, maxChannels> gateEnvelope{};
  std::array<float, maxChannels> gateGain{1.0f, 1.0f};

  std::atomic<float> meterMono{0.0f};
  std::atomic<float> meterLeft{0.0f};
  std::atomic<float> meterRight{0.0f};
  std::atomic<EffectModel> currentModel{kPhaser};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaserAudioProcessor)
};
