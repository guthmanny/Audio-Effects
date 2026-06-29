/*
  ==============================================================================

    Chorus plugin — DSP via NuDSP camel/chorus.

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginParameter.h"

#include "nudsp/amplitude/dry_wet.hpp"
#include "nudsp/amplitude/dry_wet_f32.h"
#include "nudsp/extensions/camel/chorus.hpp"

#include <array>
#include <atomic>
#include <memory>

//==============================================================================

class ChorusAudioProcessor : public AudioProcessor
{
public:
    //==============================================================================

    ChorusAudioProcessor();
    ~ChorusAudioProcessor() override;

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

    PluginParameterLinSlider paramInputGain;
    PluginParameterLinSlider paramGateThreshold;
    PluginParameterLinSlider paramOutputGain;
    PluginParameterLinSlider paramRate;
    PluginParameterLinSlider paramDelay;
    PluginParameterLinSlider paramAmount;
    PluginParameterLinSlider paramDry;
    PluginParameterLinSlider paramWet;
    PluginParameterLinSlider paramFeedback;
    PluginParameterToggle paramBypass;

    float getMeterLevelMono() const noexcept { return meterMono.load(); }
    float getMeterLevelLeft() const noexcept { return meterLeft.load(); }
    float getMeterLevelRight() const noexcept { return meterRight.load(); }

private:
    //==============================================================================

    static constexpr int maxChannels = 2;

    float readParameterValue (const String& paramId, float fallback) const;
    void syncParametersFromValueTree();
    void updateEffectParameters();
    void ensureEffectInstances();

    void processInputGain (AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb);
    void processGate (AudioSampleBuffer& buffer, int numChannels, int numSamples, float thresholdDb);
    void processOutputGain (AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb);

    std::unique_ptr<nudsp::camel::ChorusF32> chorus;
    std::array<std::unique_ptr<nudsp::DryWetF32>, maxChannels> dryWets;

    AudioSampleBuffer dryBuffer;
    AudioSampleBuffer monoBuffer;
    AudioSampleBuffer chorusBuffer;

    double currentSampleRate = 44100.0;
    std::array<float, maxChannels> gateEnvelope {};
    std::array<float, maxChannels> gateGain { 1.0f, 1.0f };

    std::atomic<float> meterMono { 0.0f };
    std::atomic<float> meterLeft { 0.0f };
    std::atomic<float> meterRight { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusAudioProcessor)
};
