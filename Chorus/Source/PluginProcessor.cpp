/*
  ==============================================================================

    Chorus plugin — DSP via NuDSP camel/chorus.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginParameter.h"

//==============================================================================

ChorusAudioProcessor::ChorusAudioProcessor():
#ifndef JucePlugin_PreferredChannelConfigurations
    AudioProcessor (BusesProperties()
                    #if ! JucePlugin_IsMidiEffect
                     #if ! JucePlugin_IsSynth
                      .withInput  ("Input",  AudioChannelSet::stereo(), true)
                     #endif
                      .withOutput ("Output", AudioChannelSet::stereo(), true)
                    #endif
                   ),
#endif
    parameters (*this)
    , paramRate (parameters, "Rate", "Hz", 0.01f, 20.0f, 0.5f)
    , paramDelay (parameters, "Delay", "ms", 1.0f, 100.0f, 25.0f)
    , paramAmount (parameters, "Amount", "ms", 0.0f, 50.0f, 10.0f)
    , paramDry (parameters, "Dry", "", 0.0f, 1.0f, 0.7f)
    , paramWet (parameters, "Wet", "", 0.0f, 1.0f, 0.5f)
    , paramFeedback (parameters, "Feedback", "", -1.0f, 1.0f, 0.15f)
    , paramBypass (parameters, "Bypass", false)
{
    parameters.valueTreeState.state = ValueTree (Identifier (getName().removeCharacters ("- ")));
}

ChorusAudioProcessor::~ChorusAudioProcessor()
{
}

//==============================================================================

void ChorusAudioProcessor::ensureChorusInstances (int numChannels)
{
    const int channels = jmin (numChannels, maxChannels);

    for (int i = 0; i < channels; ++i)
    {
        if (choruses[(size_t) i] == nullptr)
            choruses[(size_t) i] = std::make_unique<nudsp::camel::ChorusF32>();
    }
}

void ChorusAudioProcessor::updateChorusParameters()
{
    const double rate = (double) paramRate.getTargetValue();
    const double delay = (double) paramDelay.getTargetValue();
    const double amount = (double) paramAmount.getTargetValue();
    const double dry = (double) paramDry.getTargetValue();
    const double wet = (double) paramWet.getTargetValue();
    const double feedback = (double) paramFeedback.getTargetValue();
    const bool bypass = paramBypass.getTargetValue() >= 0.5f;

    for (auto& chorus : choruses)
    {
        if (chorus == nullptr)
            continue;

        chorus->setRate (rate);
        chorus->setDelay (delay);
        chorus->setAmount (amount);
        chorus->setCoeffX (dry);
        chorus->setCoeffMod (wet);
        chorus->setCoeffFb (feedback);
        chorus->setBypass (bypass);
    }
}

void ChorusAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    ignoreUnused (samplesPerBlock);

    const double smoothTime = 1e-3;
    paramRate.reset (sampleRate, smoothTime);
    paramDelay.reset (sampleRate, smoothTime);
    paramAmount.reset (sampleRate, smoothTime);
    paramDry.reset (sampleRate, smoothTime);
    paramWet.reset (sampleRate, smoothTime);
    paramFeedback.reset (sampleRate, smoothTime);
    paramBypass.reset (sampleRate, smoothTime);

    ensureChorusInstances (getTotalNumInputChannels());

    for (auto& chorus : choruses)
    {
        if (chorus == nullptr)
            continue;

        chorus->prepare (sampleRate);
        chorus->reset();
    }

    updateChorusParameters();
}

void ChorusAudioProcessor::releaseResources()
{
}

void ChorusAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    ignoreUnused (midiMessages);
    ScopedNoDenormals noDenormals;

    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    ensureChorusInstances (numInputChannels);
    updateChorusParameters();

    const auto frameSize = (size_t) numSamples;

    for (int channel = 0; channel < numInputChannels; ++channel)
    {
        auto& chorus = choruses[(size_t) channel];
        if (chorus == nullptr)
            continue;

        chorus->tick (frameSize);

        float* channelData = buffer.getWritePointer (channel);
        chorus->process (channelData, channelData, frameSize);
    }

    for (int channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);
}

//==============================================================================

void ChorusAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    std::unique_ptr<XmlElement> xml (parameters.valueTreeState.state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ChorusAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName (parameters.valueTreeState.state.getType()))
            parameters.valueTreeState.state = ValueTree::fromXml (*xmlState);
}

//==============================================================================

bool ChorusAudioProcessor::hasEditor() const
{
    return true;
}

AudioProcessorEditor* ChorusAudioProcessor::createEditor()
{
    return new ChorusAudioProcessorEditor (*this);
}

//==============================================================================

#ifndef JucePlugin_PreferredChannelConfigurations
bool ChorusAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

//==============================================================================

const String ChorusAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ChorusAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool ChorusAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool ChorusAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double ChorusAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ChorusAudioProcessor::getNumPrograms()
{
    return 1;
}

int ChorusAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ChorusAudioProcessor::setCurrentProgram (int index)
{
    ignoreUnused (index);
}

const String ChorusAudioProcessor::getProgramName (int index)
{
    ignoreUnused (index);
    return {};
}

void ChorusAudioProcessor::changeProgramName (int index, const String& newName)
{
    ignoreUnused (index, newName);
}

//==============================================================================

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChorusAudioProcessor();
}

//==============================================================================
