/*
  ==============================================================================

    Compressor / Expander plugin — DSP via NuDSP camel.

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginParameter.h"
#include "CompressorKnee.h"
#include "nudsp/extensions/camel/compressor.hpp"
#include "nudsp/extensions/camel/compressor_f32.h"
#include "nudsp/extensions/camel/noise_gate.hpp"

//==============================================================================

class CompressorExpanderAudioProcessor : public AudioProcessor
{
public:
    //==============================================================================

    CompressorExpanderAudioProcessor();
    ~CompressorExpanderAudioProcessor() override;

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
    bool isMidiEffect () const override;
    double getTailLengthSeconds() const override;

    //==============================================================================

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const String getProgramName (int index) override;
    void changeProgramName (int index, const String& newName) override;

    //==============================================================================

    PluginParametersManager parameters;

    PluginParameterComboBox paramMode;
    PluginParameterComboBox paramKnee;
    PluginParameterLinSlider paramKneeWidth;
    PluginParameterLinSlider paramThreshold;
    PluginParameterLinSlider paramRatio;
    PluginParameterLinSlider paramAttack;
    PluginParameterLinSlider paramRelease;
    PluginParameterLinSlider paramMakeupGain;
    PluginParameterToggle paramBypass;

    float getMeterInputDb() const noexcept { return meterInputDb.load (std::memory_order_relaxed); }
    float getMeterGainReductionDb() const noexcept { return meterGainReductionDb.load (std::memory_order_relaxed); }

private:
    //==============================================================================

    float readParameterValue (const String& paramId, float fallback) const;
    void syncParametersFromValueTree();
    void ensureEffectInstances();
    void updateEffectParameters();
    void mixSidechain (const AudioSampleBuffer& buffer, int numInputChannels, int numSamples);
    void processCompressorMode (AudioSampleBuffer& buffer, int numInputChannels, int numSamples, float makeupGain, bool softKnee);
    void processExpanderMode (AudioSampleBuffer& buffer, int numInputChannels, int numSamples, float makeupGain, bool softKnee);
    void processSoftKneeMode (AudioSampleBuffer& buffer,
                              int numInputChannels,
                              int numSamples,
                              float makeupGainDb,
                              bool expanderMode);
    float calculateAttackOrRelease (float timeSeconds) const;
    void updateTransferMeters (const AudioSampleBuffer& buffer,
                               int numInputChannels,
                               int numSamples,
                               bool expanderMode,
                               bool softKnee);

    std::unique_ptr<nudsp::camel::CompressorF32> compressor;
    std::unique_ptr<nudsp::camel::NoiseGateF32> noiseGate;

    AudioSampleBuffer sidechainBuffer;
    AudioSampleBuffer monoWetBuffer;
    std::array<float, 2> gateEnvelopeState {};

    double currentSampleRate = 44100.0;
    float envelopeDb = 0.0f;
    float expanderLevel = 0.0f;
    float meterSmoothedInputDb = -80.0f;
    float meterSmoothedGainReductionDb = 0.0f;

    std::atomic<float> meterInputDb { -80.0f };
    std::atomic<float> meterGainReductionDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorExpanderAudioProcessor)
};
