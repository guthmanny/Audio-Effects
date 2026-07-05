/*
  ==============================================================================

    Distortion plugin — DS+ opamp → diode clipper via NuDSP.

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginParameter.h"

#include "nudsp/amplitude/gain.hpp"
#include "nudsp/amplitude/gain_f32.h"
#include "nudsp/extensions/white_box/distortion_plus.hpp"
#include "nudsp/extensions/white_box/ts9.hpp"
#include "nudsp/resampling/up_sampler_f32.h"
#include "nudsp/resampling/down_sampler_f32.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>

//==============================================================================

class DistortionAudioProcessor : public AudioProcessor
{
public:
    //==============================================================================

    DistortionAudioProcessor();
    ~DistortionAudioProcessor() override;

    //==============================================================================

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (AudioSampleBuffer&, MidiBuffer&) override;

    //==============================================================================

    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
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
    void setCurrentProgram (int index) override;
    const String getProgramName (int index) override;
    void changeProgramName (int index, const String& newName) override;

    //==============================================================================

    PluginParametersManager parameters;

    enum DistortionModel { kDistortionPlus = 0, kTs9 = 1 };

    PluginParameterLinSlider paramInputGain;
    PluginParameterLinSlider paramGateThreshold;
    PluginParameterLinSlider paramOutputGain;
    PluginParameterLinSlider paramDistortion;
    PluginParameterLinSlider paramTone;
    PluginParameterLinSlider paramLevel;
    PluginParameterToggle paramBypass;

    float getMeterLevelMono() const noexcept { return meterMono.load(); }
    float getMeterLevelLeft() const noexcept { return meterLeft.load(); }
    float getMeterLevelRight() const noexcept { return meterRight.load(); }
    float getCpuLoad() const noexcept { return cpuLoad.load(); }
    float getCpuLoadPeak() noexcept;
    int getOversampleFactor() const noexcept;
    void setOversampleFactor (int factor) noexcept;
    DistortionModel getDistortionModel() const noexcept;
    void setDistortionModel (DistortionModel model) noexcept;

private:
    //==============================================================================

    static constexpr int maxChannels = 2;

    float readParameterValue (const String& paramId, float fallback) const;
    void syncParametersFromValueTree();
    void updateEffectParameters();
    void ensureEffectInstances();

    void processInputGain (AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb);
    void processGate (AudioSampleBuffer& buffer, int numChannels, int numSamples, float thresholdDb);
    void processOutputGain (const AudioSampleBuffer& inBuffer,
                            AudioSampleBuffer& outBuffer,
                            int numChannels,
                            int numSamples,
                            float gainDb);

    std::array<std::unique_ptr<nudsp::white_box::DistortionPlusF32>, maxChannels> distortionPlusChains;
    std::array<std::unique_ptr<nudsp::white_box::Ts9F32>, maxChannels> ts9Chains;
    std::unique_ptr<nudsp::GainF32> outputGain;

    AudioSampleBuffer outputBuffer;
    std::array<AudioSampleBuffer, maxChannels> oversampleBuffers;

    double currentSampleRate = 44100.0;
    std::array<float, maxChannels> gateEnvelope {};
    std::array<float, maxChannels> gateGain { 1.0f, 1.0f };

    std::array<nx_upsampler_t*, maxChannels> upSamplers {};
    std::array<nx_downsampler_t*, maxChannels> downSamplers {};

    std::atomic<float> meterMono { 0.0f };
    std::atomic<float> meterLeft { 0.0f };
    std::atomic<float> meterRight { 0.0f };
    std::atomic<float> cpuLoad { 0.0f };
    std::atomic<float> cpuLoadPeak { 0.0f };
    std::atomic<int> oversampleFactor { 4 };
    std::atomic<DistortionModel> currentModel { kDistortionPlus };
    int lastOsFactor = 0;  // audio-thread only: tracks sampler config state
    DistortionModel lastModel = kDistortionPlus;  // audio-thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistortionAudioProcessor)
};
