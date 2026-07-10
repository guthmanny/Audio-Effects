/*
  ==============================================================================

    ParametricEQ plugin — multi-band parametric equalizer via NuDSP SVF filters.

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "EQCurveComponent.h"
#include "PluginParameter.h"
#include "nudsp/filters/svf_f32.h"
#include "nudsp/resampling/down_sampler_f32.h"
#include "nudsp/resampling/up_sampler_f32.h"

//==============================================================================

struct SvfBandState
{
  float lp = 0.0f;
  float bp = 0.0f;
};

//==============================================================================

class ParametricEQAudioProcessor : public AudioProcessor
{
 public:
  //==============================================================================

  static constexpr int numBands = 1;

  struct BandParams
  {
    BandParams(PluginParametersManager& pm, int bandIndex, const StringArray& typeNames)
        : frequency(pm, "Band " + String(bandIndex + 1) + " Frequency", "Hz", 20.0f, 20000.0f, 1000.0f),
          q(pm, "Band " + String(bandIndex + 1) + " Q", "", 0.1f, 10.0f, 0.707f),
          gain(pm, "Band " + String(bandIndex + 1) + " Gain", "dB", -18.0f, 18.0f, 0.0f),
          type(pm, "Band " + String(bandIndex + 1) + " Type", typeNames, 0)
    {
    }

    PluginParameterLogSlider frequency;
    PluginParameterLinSlider q;
    PluginParameterLinSlider gain;
    PluginParameterComboBox type;
  };

  //==============================================================================

  ParametricEQAudioProcessor();
  ~ParametricEQAudioProcessor() override;

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

  PluginParametersManager parameters;

  PluginParameterLinSlider paramInputGain;
  PluginParameterLinSlider paramOutputGain;
  PluginParameterToggle paramBypass;

  std::array<std::unique_ptr<BandParams>, numBands> bands;

  float getMeterLevelMono() const noexcept { return meterMono.load(); }
  float getMeterLevelLeft() const noexcept { return meterLeft.load(); }
  float getMeterLevelRight() const noexcept { return meterRight.load(); }
  int getOversampleFactor() const noexcept;
  void setOversampleFactor(int factor) noexcept;
  double getEffectiveSampleRate() const noexcept;

  static const StringArray& getFilterTypeNames();

  std::vector<EQCurveComponent::BandConfig> getBandConfigs() const;

 private:
  //==============================================================================

  static constexpr int maxChannels = 2;

  float readParameterValue(const String& paramId, float fallback) const;
  static float advanceSmoothedValue(PluginParameter& parameter, int numSamples);
  void ensureSvfInstances();
  void prepareSvfInstances(double sampleRate);
  void syncParametersFromValueTree();
  void resetAllSvfs();

  using SvfChain = std::array<nx_svf_f32_t*, numBands>;
  using BandStateChain = std::array<SvfBandState, numBands>;
  std::array<SvfChain, maxChannels> svfChains{};
  std::array<BandStateChain, maxChannels> svfStates{};
  std::array<int, numBands> lastBandTypeIndices{};

  double currentSampleRate = 44100.0;
  int preparedBlockSize = 0;
  static constexpr int maxOversampleFactor = 4;

  std::array<AudioSampleBuffer, maxChannels> oversampleBuffers{};
  std::array<nx_upsampler_t*, maxChannels> upSamplers{};
  std::array<nx_downsampler_t*, maxChannels> downSamplers{};

  std::atomic<float> meterMono{0.0f};
  std::atomic<float> meterLeft{0.0f};
  std::atomic<float> meterRight{0.0f};
  std::atomic<int> oversampleFactor{2};
  int lastOsFactor = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParametricEQAudioProcessor)
};
