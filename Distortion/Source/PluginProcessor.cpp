/*
  ==============================================================================

    Distortion plugin — DS+ opamp → diode clipper via NuDSP.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginParameter.h"

#include <cmath>

//==============================================================================

DistortionAudioProcessor::DistortionAudioProcessor():
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
    , paramInputGain (parameters, "Input Gain", "dB", -12.0f, 12.0f, 0.0f)
    , paramGateThreshold (parameters, "Gate Threshold", "dB", -80.0f, 0.0f, -40.0f)
    , paramOutputGain (parameters, "Output Gain", "dB", -100.0f, 0.0f, 0.0f)
    , paramDistortion (parameters, "Distortion", "", 0.0f, 1.0f, 0.5f)
    , paramBypass (parameters, "Bypass", false)
{
    parameters.valueTreeState.state = ValueTree (Identifier (getName().removeCharacters ("- ")));
}

DistortionAudioProcessor::~DistortionAudioProcessor()
{
}

//==============================================================================

float DistortionAudioProcessor::readParameterValue (const String& paramId, float fallback) const
{
    if (auto* param = parameters.valueTreeState.getParameter (paramId))
        return param->convertFrom0to1 (param->getValue());

    if (auto* value = parameters.valueTreeState.getRawParameterValue (paramId))
        return value->load();

    return fallback;
}

void DistortionAudioProcessor::syncParametersFromValueTree()
{
    paramInputGain.setCurrentAndTargetValue (readParameterValue (paramInputGain.paramID, paramInputGain.defaultValue));
    paramGateThreshold.setCurrentAndTargetValue (readParameterValue (paramGateThreshold.paramID, paramGateThreshold.defaultValue));
    paramOutputGain.setCurrentAndTargetValue (readParameterValue (paramOutputGain.paramID, paramOutputGain.defaultValue));
    paramDistortion.setCurrentAndTargetValue (readParameterValue (paramDistortion.paramID, paramDistortion.defaultValue));
    paramBypass.setCurrentAndTargetValue (readParameterValue (paramBypass.paramID, (float) paramBypass.defaultState));
}

void DistortionAudioProcessor::ensureEffectInstances()
{
    for (auto& chain : channelChains)
    {
        if (chain.opamp == nullptr)
            chain.opamp = std::make_unique<nudsp::DsPlusOpampF32>();

        if (chain.clipper == nullptr)
            chain.clipper = std::make_unique<nudsp::DiodeClipperF32>();
    }

    if (outputGain == nullptr)
        outputGain = std::make_unique<nudsp::GainF32>();
}

void DistortionAudioProcessor::updateEffectParameters()
{
    const double distortion = jlimit (0.0, 1.0, (double) readParameterValue (paramDistortion.paramID, paramDistortion.defaultValue));
    const bool bypass = readParameterValue (paramBypass.paramID, (float) paramBypass.defaultState) >= 0.5f;

    for (auto& chain : channelChains)
    {
        if (chain.opamp != nullptr)
        {
            chain.opamp->setDistortionControl (distortion);
            chain.opamp->setBypass (bypass);
        }

        if (chain.clipper != nullptr)
            chain.clipper->setBypass (bypass);
    }
}

void DistortionAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    gateEnvelope = {};
    gateGain = { 1.0f, 1.0f };

    const double smoothTime = 1e-3;
    paramInputGain.reset (sampleRate, smoothTime);
    paramGateThreshold.reset (sampleRate, smoothTime);
    paramOutputGain.reset (sampleRate, smoothTime);
    paramDistortion.reset (sampleRate, smoothTime);
    paramBypass.reset (sampleRate, smoothTime);

    syncParametersFromValueTree();
    ensureEffectInstances();

    for (auto& chain : channelChains)
    {
        chain.opamp->prepare (sampleRate);
        chain.opamp->reset();
        chain.opamp->tick (1);

        chain.clipper->prepare (sampleRate);
        chain.clipper->reset();
        chain.clipper->tick (1);
    }

    if (outputGain != nullptr)
    {
        nx_gain_config_t gainConfig;
        nx_gain_config_init (&gainConfig);
        gainConfig.gain.control_params.min_value = -100.0;
        gainConfig.gain.control_params.max_value = 0.0;
        gainConfig.gain.control_params.value = (double) paramOutputGain.defaultValue;

        if (auto* gainRaw = outputGain->getRawPointer())
            nx_gain_set_config_f32 (gainRaw, &gainConfig);

        outputGain->setSmoothMode (NX_SMOOTH_EXPONENTIAL, (float) sampleRate, 10.0f);
        nx_gain_tick_f32 (outputGain->getRawPointer(), 1);
    }

    const int numChannels = jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());
    outputBuffer.setSize (numChannels, samplesPerBlock);

    updateEffectParameters();

    for (auto& chain : channelChains)
    {
        chain.opamp->tick (1);
        chain.clipper->tick (1);
    }

}

void DistortionAudioProcessor::releaseResources()
{
    outputBuffer.setSize (0, 0);
    gateEnvelope = {};
    gateGain = { 1.0f, 1.0f };
}

void DistortionAudioProcessor::processInputGain (AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb)
{
    const float gain = Decibels::decibelsToGain (gainDb);

    for (int channel = 0; channel < numChannels; ++channel)
        buffer.applyGain (channel, 0, numSamples, gain);
}

void DistortionAudioProcessor::processGate (AudioSampleBuffer& buffer, int numChannels, int numSamples, float thresholdDb)
{
    const float envRelease = (float) std::exp (-1.0 / (0.050 * currentSampleRate));
    const float gainAttack = (float) std::exp (-1.0 / (0.002 * currentSampleRate));
    const float gainRelease = (float) std::exp (-1.0 / (0.050 * currentSampleRate));
    const int channels = jmin (numChannels, maxChannels);

    for (int channel = 0; channel < channels; ++channel)
    {
        float* data = buffer.getWritePointer (channel);
        float env = gateEnvelope[(size_t) channel];
        float g = gateGain[(size_t) channel];

        for (int i = 0; i < numSamples; ++i)
        {
            const float absSample = std::abs (data[i]);
            env = jmax (absSample, env * envRelease);

            const float levelDb = Decibels::gainToDecibels (env + 1.0e-10f);
            const float targetGain = levelDb >= thresholdDb ? 1.0f : 0.0f;
            const float smoothCoeff = targetGain > g ? gainAttack : gainRelease;
            g = targetGain + smoothCoeff * (g - targetGain);

            data[i] *= g;
        }

        gateEnvelope[(size_t) channel] = env;
        gateGain[(size_t) channel] = g;
    }
}

void DistortionAudioProcessor::processOutputGain (const AudioSampleBuffer& inBuffer,
                                                  AudioSampleBuffer& outBuffer,
                                                  int numChannels,
                                                  int numSamples,
                                                  float gainDb)
{
    if (outputGain == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const auto frameSize = (size_t) numSamples;
    const auto channels = (size_t) jmin (numChannels, maxChannels);

    const float* inputs[maxChannels] {};
    float* outputs[maxChannels] {};

    for (int channel = 0; channel < (int) channels; ++channel)
    {
        inputs[(size_t) channel] = inBuffer.getReadPointer (channel);
        outputs[(size_t) channel] = outBuffer.getWritePointer (channel);
    }

    outputGain->setGainDb ((double) gainDb);

    if (auto* gainRaw = outputGain->getRawPointer())
        nx_gain_tick_f32 (gainRaw, frameSize);

    outputGain->processMulti (inputs, outputs, frameSize, channels);
}

void DistortionAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    ignoreUnused (midiMessages);
    ScopedNoDenormals noDenormals;

    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples == 0)
        return;

    if (outputBuffer.getNumSamples() < numSamples)
    {
        const int numChannels = jmax (numInputChannels, numOutputChannels);
        outputBuffer.setSize (numChannels, numSamples, false, false, true);
    }

    ensureEffectInstances();
    updateEffectParameters();

    const float inputGainDb = readParameterValue (paramInputGain.paramID, paramInputGain.defaultValue);
    const float gateThresholdDb = readParameterValue (paramGateThreshold.paramID, paramGateThreshold.defaultValue);
    const float outputGainDb = readParameterValue (paramOutputGain.paramID, paramOutputGain.defaultValue);

    processInputGain (buffer, numInputChannels, numSamples, inputGainDb);
    processGate (buffer, jmin (numInputChannels, numOutputChannels), numSamples, gateThresholdDb);

    const int processChannels = jmin (numOutputChannels, maxChannels);

    if (numInputChannels > 0 && numInputChannels < processChannels)
    {
        for (int channel = numInputChannels; channel < processChannels; ++channel)
            buffer.copyFrom (channel, 0, buffer, numInputChannels - 1, 0, numSamples);
    }
    else if (processChannels >= 2 && numInputChannels >= 2)
    {
        const float leftPeak = buffer.getMagnitude (0, 0, numSamples);
        const float rightPeak = buffer.getMagnitude (1, 0, numSamples);

        if (leftPeak > 1.0e-6f && rightPeak <= 1.0e-6f)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    }

    const auto frameSize = (size_t) numSamples;

    for (int channel = 0; channel < processChannels; ++channel)
    {
        auto& chain = channelChains[(size_t) channel];
        const float* inData = buffer.getReadPointer (channel);
        float* effectOut = outputBuffer.getWritePointer (channel);

        if (chain.opamp != nullptr)
        {
            chain.opamp->tick (frameSize);
            chain.opamp->process (inData, effectOut, frameSize);
        }
        else
        {
            FloatVectorOperations::copy (effectOut, inData, numSamples);
        }

        if (chain.clipper != nullptr)
        {
            chain.clipper->tick (frameSize);
            chain.clipper->process (effectOut, effectOut, frameSize);
        }
    }

    for (int channel = processChannels; channel < numOutputChannels; ++channel)
        outputBuffer.clear (channel, 0, numSamples);

    processOutputGain (outputBuffer, buffer, numOutputChannels, numSamples, outputGainDb);

    float leftPeak = 0.0f;
    float rightPeak = 0.0f;

    if (numOutputChannels > 0)
        leftPeak = buffer.getMagnitude (0, 0, numSamples);

    if (numOutputChannels > 1)
        rightPeak = buffer.getMagnitude (1, 0, numSamples);

    const float monoPeak = numOutputChannels > 1 ? 0.5f * (leftPeak + rightPeak) : leftPeak;

    meterMono.store (monoPeak);
    meterLeft.store (leftPeak);
    meterRight.store (rightPeak);
}

//==============================================================================

void DistortionAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    std::unique_ptr<XmlElement> xml (parameters.valueTreeState.state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DistortionAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName (parameters.valueTreeState.state.getType()))
            parameters.valueTreeState.state = ValueTree::fromXml (*xmlState);
}

//==============================================================================

bool DistortionAudioProcessor::hasEditor() const
{
    return true;
}

AudioProcessorEditor* DistortionAudioProcessor::createEditor()
{
    return new DistortionAudioProcessorEditor (*this);
}

//==============================================================================

#ifndef JucePlugin_PreferredChannelConfigurations
bool DistortionAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

const String DistortionAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DistortionAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool DistortionAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool DistortionAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double DistortionAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DistortionAudioProcessor::getNumPrograms()
{
    return 1;
}

int DistortionAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DistortionAudioProcessor::setCurrentProgram (int index)
{
    ignoreUnused (index);
}

const String DistortionAudioProcessor::getProgramName (int index)
{
    ignoreUnused (index);
    return {};
}

void DistortionAudioProcessor::changeProgramName (int index, const String& newName)
{
    ignoreUnused (index, newName);
}

//==============================================================================

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DistortionAudioProcessor();
}

//==============================================================================
