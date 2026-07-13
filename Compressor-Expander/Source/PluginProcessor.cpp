/*
  ==============================================================================

    Compressor / Expander plugin — DSP via NuDSP camel.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "PluginParameter.h"

#include <cmath>

//==============================================================================

CompressorExpanderAudioProcessor::CompressorExpanderAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                    #if ! JucePlugin_IsMidiEffect
                     #if ! JucePlugin_IsSynth
                      .withInput  ("Input",  AudioChannelSet::stereo(), true)
                     #endif
                      .withOutput ("Output", AudioChannelSet::stereo(), true)
                    #endif
                       )
#endif
    , parameters (*this)
    , paramMode (parameters, "Mode", {"Compressor / Limiter", "Expander / Noise gate"}, 1)
    , paramKnee (parameters, "Knee", {"Hard", "Soft"}, 0)
    , paramKneeWidth (parameters, "Knee width", "dB", 1.0f, 24.0f, CompressorKnee::defaultWidthDb)
    , paramThreshold (parameters, "Threshold", "dB", -60.0f, 0.0f, -24.0f)
    , paramRatio (parameters, "Ratio", ":1", 1.0f, 100.0f, 50.0f)
    , paramAttack (parameters, "Attack", "ms", 0.1f, 100.0f, 2.0f, [](float value) { return value * 0.001f; })
    , paramRelease (parameters, "Release", "ms", 10.0f, 1000.0f, 300.0f, [](float value) { return value * 0.001f; })
    , paramMakeupGain (parameters, "Makeup gain", "dB", -12.0f, 12.0f, 0.0f)
    , paramBypass (parameters, "Bypass")
{
    parameters.valueTreeState.state = ValueTree (Identifier (getName().removeCharacters ("- ")));
}

CompressorExpanderAudioProcessor::~CompressorExpanderAudioProcessor()
{
}

//==============================================================================

float CompressorExpanderAudioProcessor::readParameterValue (const String& paramId, float fallback) const
{
    if (auto* param = parameters.valueTreeState.getParameter (paramId))
        return param->convertFrom0to1 (param->getValue());

    if (auto* value = parameters.valueTreeState.getRawParameterValue (paramId))
        return value->load();

    return fallback;
}

void CompressorExpanderAudioProcessor::syncParametersFromValueTree()
{
    paramThreshold.setCurrentAndTargetValue (readParameterValue (paramThreshold.paramID, paramThreshold.defaultValue));
    paramKneeWidth.setCurrentAndTargetValue (readParameterValue (paramKneeWidth.paramID, paramKneeWidth.defaultValue));
    paramRatio.setCurrentAndTargetValue (readParameterValue (paramRatio.paramID, paramRatio.defaultValue));
    paramAttack.setCurrentAndTargetValue (readParameterValue (paramAttack.paramID, paramAttack.defaultValue * 0.001f));
    paramRelease.setCurrentAndTargetValue (readParameterValue (paramRelease.paramID, paramRelease.defaultValue * 0.001f));
    paramMakeupGain.setCurrentAndTargetValue (readParameterValue (paramMakeupGain.paramID, paramMakeupGain.defaultValue));
    paramBypass.setCurrentAndTargetValue (readParameterValue (paramBypass.paramID, (float) paramBypass.defaultState));
}

void CompressorExpanderAudioProcessor::ensureEffectInstances()
{
    if (compressor == nullptr)
        compressor = std::make_unique<nudsp::camel::CompressorF32>();

    if (noiseGate == nullptr)
        noiseGate = std::make_unique<nudsp::camel::NoiseGateF32>();
}

void CompressorExpanderAudioProcessor::updateEffectParameters()
{
    const double thresholdDb = (double) readParameterValue (paramThreshold.paramID, paramThreshold.defaultValue);
    const double ratio = (double) readParameterValue (paramRatio.paramID, paramRatio.defaultValue);
    const double attackMs = (double) readParameterValue (paramAttack.paramID, paramAttack.defaultValue);
    const double releaseMs = (double) readParameterValue (paramRelease.paramID, paramRelease.defaultValue);
    const bool bypass = readParameterValue (paramBypass.paramID, (float) paramBypass.defaultState) >= 0.5f;
    const bool softKnee = readParameterValue (paramKnee.paramID, 0.0f) >= 0.5f;
    const double kneeWidthDb =
        (double) readParameterValue (paramKneeWidth.paramID, paramKneeWidth.defaultValue);
    const nx_knee_mode_e kneeMode = softKnee ? NX_KNEE_SOFT : NX_KNEE_HARD;

    if (compressor != nullptr)
    {
        nx_compressor_set_threshold_f32 (compressor->getRawPointer(), thresholdDb);
        nx_compressor_set_ratio_f32 (compressor->getRawPointer(), jlimit (0.0, 1.0, 1.0 / ratio));
        nx_compressor_set_attack_ms_f32 (compressor->getRawPointer(), attackMs);
        nx_compressor_set_release_ms_f32 (compressor->getRawPointer(), releaseMs);
        nx_compressor_set_gain_f32 (compressor->getRawPointer(), 0.0);
        nx_compressor_set_knee_mode_f32 (compressor->getRawPointer(), kneeMode);
        nx_compressor_set_knee_width_db_f32 (compressor->getRawPointer(), kneeWidthDb);
        compressor->setBypass (bypass);
    }

    if (noiseGate != nullptr)
    {
        nx_noise_gate_config_t config;
        nudsp::camel::NoiseGateF32::configInit (&config);
        config.thresh.control_params.value = thresholdDb;
        config.ratio.control_params.value = jlimit (1.0, 10.0, ratio);
        config.attack_ms.control_params.value = jlimit (5.0, 100.0, attackMs);
        config.release_ms.control_params.value = jlimit (100.0, 10000.0, releaseMs);
        config.knee_mode = kneeMode;
        config.knee_width.control_params.value = kneeWidthDb;
        noiseGate->setConfig (config);
    }
}

void CompressorExpanderAudioProcessor::mixSidechain (const AudioSampleBuffer& buffer,
                                                     int numInputChannels,
                                                     int numSamples)
{
    sidechainBuffer.clear();

    if (numInputChannels <= 0)
        return;

    const float scale = 1.0f / (float) numInputChannels;

    for (int channel = 0; channel < numInputChannels; ++channel)
        sidechainBuffer.addFrom (0, 0, buffer, channel, 0, numSamples, scale);
}

void CompressorExpanderAudioProcessor::processCompressorMode (AudioSampleBuffer& buffer,
                                                              int numInputChannels,
                                                              int numSamples,
                                                              float makeupGain,
                                                              bool /*softKnee*/)
{
    const float* sidechain = sidechainBuffer.getReadPointer (0);
    float* wet = monoWetBuffer.getWritePointer (0);

    compressor->process (sidechain, sidechain, wet, (size_t) numSamples);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float dry = sidechain[sample];
        const float gain = (std::abs (dry) > 1.0e-8f) ? (wet[sample] / dry) * makeupGain : makeupGain;

        for (int channel = 0; channel < numInputChannels; ++channel)
            buffer.setSample (channel, sample, buffer.getSample (channel, sample) * gain);
    }
}

void CompressorExpanderAudioProcessor::processExpanderMode (AudioSampleBuffer& buffer,
                                                            int numInputChannels,
                                                            int numSamples,
                                                            float makeupGain,
                                                            bool /*softKnee*/)
{
    const float* sidechain = sidechainBuffer.getReadPointer (0);

    for (int channel = 0; channel < numInputChannels; ++channel)
    {
        const float* input = buffer.getReadPointer (channel);
        float* output = buffer.getWritePointer (channel);

        noiseGate->process (input,
                            sidechain,
                            output,
                            &gateEnvelopeState[(size_t) channel],
                            1,
                            (size_t) numSamples);
    }

    if (makeupGain != 1.0f)
        for (int channel = 0; channel < numInputChannels; ++channel)
            buffer.applyGain (channel, 0, numSamples, makeupGain);
}

float CompressorExpanderAudioProcessor::calculateAttackOrRelease (float timeSeconds) const
{
    if (timeSeconds <= 0.0f || currentSampleRate <= 0.0)
        return 0.0f;

    return (float) std::exp (-1.0 / (timeSeconds * currentSampleRate));
}

void CompressorExpanderAudioProcessor::updateTransferMeters (const AudioSampleBuffer& buffer,
                                                               int numInputChannels,
                                                               int numSamples,
                                                               bool expanderMode,
                                                               bool /*softKnee*/)
{
    if (numSamples <= 0 || currentSampleRate <= 0.0)
        return;

    const float* sidechain = sidechainBuffer.getReadPointer (0);
    double inputSumSquares = 0.0;
    float outputPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        inputSumSquares += (double) sidechain[i] * (double) sidechain[i];

    for (int channel = 0; channel < numInputChannels; ++channel)
    {
        const float* channelData = buffer.getReadPointer (channel);
        for (int i = 0; i < numSamples; ++i)
            outputPeak = jmax (outputPeak, std::abs (channelData[i]));
    }

    const float inputRms = (float) std::sqrt (inputSumSquares / (double) numSamples);
    const float blockInputDb = inputRms > 1.0e-8f ? Decibels::gainToDecibels (inputRms) : -80.0f;
    float blockGainReductionDb = 0.0f;

    if (expanderMode)
    {
        const float outputDb = outputPeak > 1.0e-8f ? Decibels::gainToDecibels (outputPeak) : -80.0f;
        blockGainReductionDb = jmax (0.0f, blockInputDb - outputDb);
    }
    else if (compressor != nullptr)
    {
        blockGainReductionDb = jmax (0.0f, -compressor->getGainReductionDb());
    }

    meterSmoothedInputDb = blockInputDb;
    meterSmoothedGainReductionDb = blockGainReductionDb;

    meterInputDb.store (meterSmoothedInputDb, std::memory_order_relaxed);
    meterGainReductionDb.store (meterSmoothedGainReductionDb, std::memory_order_relaxed);
}

//==============================================================================

void CompressorExpanderAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    envelopeDb = 0.0f;
    expanderLevel = 0.0f;
    meterSmoothedInputDb = -80.0f;
    meterSmoothedGainReductionDb = 0.0f;
    meterInputDb.store (-80.0f, std::memory_order_relaxed);
    meterGainReductionDb.store (0.0f, std::memory_order_relaxed);

    const double smoothTime = 1e-3;
    paramThreshold.reset (sampleRate, smoothTime);
    paramKneeWidth.reset (sampleRate, smoothTime);
    paramRatio.reset (sampleRate, smoothTime);
    paramAttack.reset (sampleRate, smoothTime);
    paramRelease.reset (sampleRate, smoothTime);
    paramMakeupGain.reset (sampleRate, smoothTime);
    paramBypass.reset (sampleRate, smoothTime);

    syncParametersFromValueTree();
    ensureEffectInstances();

    sidechainBuffer.setSize (1, samplesPerBlock);
    monoWetBuffer.setSize (1, samplesPerBlock);
    gateEnvelopeState = {};

    compressor->prepare (sampleRate);
    compressor->reset (0.0f, 0.0f, nullptr);
    compressor->tick (1);

    noiseGate->prepare (sampleRate);
    noiseGate->reset (gateEnvelopeState.data(), gateEnvelopeState.size());
    noiseGate->tick (1);

    updateEffectParameters();
}

void CompressorExpanderAudioProcessor::releaseResources()
{
}

void CompressorExpanderAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    ignoreUnused (midiMessages);
    ScopedNoDenormals noDenormals;

    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    if ((bool) paramBypass.getTargetValue())
    {
        meterSmoothedInputDb = -80.0f;
        meterSmoothedGainReductionDb = 0.0f;
        meterInputDb.store (-80.0f, std::memory_order_relaxed);
        meterGainReductionDb.store (0.0f, std::memory_order_relaxed);

        for (int channel = numInputChannels; channel < numOutputChannels; ++channel)
            buffer.clear (channel, 0, numSamples);

        return;
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        (void) paramThreshold.getNextValue();
        (void) paramKneeWidth.getNextValue();
        (void) paramRatio.getNextValue();
        (void) paramAttack.getNextValue();
        (void) paramRelease.getNextValue();
        (void) paramMakeupGain.getNextValue();
    }

    updateEffectParameters();

    const bool expanderMode = readParameterValue (paramMode.paramID, 1.0f) >= 0.5f;
    const bool softKnee = readParameterValue (paramKnee.paramID, 0.0f) >= 0.5f;
    const float makeupGain = Decibels::decibelsToGain (readParameterValue (paramMakeupGain.paramID,
                                                                             paramMakeupGain.defaultValue));

    mixSidechain (buffer, numInputChannels, numSamples);

    if (expanderMode)
    {
        processExpanderMode (buffer, numInputChannels, numSamples, makeupGain, softKnee);
        noiseGate->tick ((size_t) numSamples);
    }
    else
    {
        processCompressorMode (buffer, numInputChannels, numSamples, makeupGain, softKnee);
        compressor->tick ((size_t) numSamples);
    }

    updateTransferMeters (buffer, numInputChannels, numSamples, expanderMode, softKnee);

    for (int channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);
}

//==============================================================================

void CompressorExpanderAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = parameters.valueTreeState.state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CompressorExpanderAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary (data, sizeInBytes))
        if (xmlState->hasTagName (parameters.valueTreeState.state.getType()))
            parameters.valueTreeState.state = ValueTree::fromXml (*xmlState);
}

//==============================================================================

bool CompressorExpanderAudioProcessor::hasEditor() const
{
    return true;
}

AudioProcessorEditor* CompressorExpanderAudioProcessor::createEditor()
{
    return new CompressorExpanderAudioProcessorEditor (*this);
}

//==============================================================================

#ifndef JucePlugin_PreferredChannelConfigurations
bool CompressorExpanderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

const String CompressorExpanderAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CompressorExpanderAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool CompressorExpanderAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CompressorExpanderAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CompressorExpanderAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CompressorExpanderAudioProcessor::getNumPrograms()
{
    return 1;
}

int CompressorExpanderAudioProcessor::getCurrentProgram()
{
    return 0;
}

void CompressorExpanderAudioProcessor::setCurrentProgram (int index)
{
    ignoreUnused (index);
}

const String CompressorExpanderAudioProcessor::getProgramName (int index)
{
    ignoreUnused (index);
    return {};
}

void CompressorExpanderAudioProcessor::changeProgramName (int index, const String& newName)
{
    ignoreUnused (index, newName);
}

//==============================================================================

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompressorExpanderAudioProcessor();
}
