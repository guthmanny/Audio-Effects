/*
  ==============================================================================

    Chorus plugin — DSP via NuDSP camel/chorus.

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginParameter.h"

#include "nudsp/extensions/camel/chorus.hpp"

#include <array>
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

    PluginParameterLinSlider paramRate;
    PluginParameterLinSlider paramDelay;
    PluginParameterLinSlider paramAmount;
    PluginParameterLinSlider paramDry;
    PluginParameterLinSlider paramWet;
    PluginParameterLinSlider paramFeedback;
    PluginParameterToggle paramBypass;

private:
    //==============================================================================

    static constexpr int maxChannels = 2;

    void updateChorusParameters();
    void ensureChorusInstances (int numChannels);

    std::array<std::unique_ptr<nudsp::camel::ChorusF32>, maxChannels> choruses;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusAudioProcessor)
};
