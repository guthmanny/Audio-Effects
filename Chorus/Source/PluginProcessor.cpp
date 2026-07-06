/*
  ==============================================================================

    Chorus / Phase90 plugin — DSP via NuDSP camel.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include <cmath>

#include "PluginEditor.h"
#include "PluginParameter.h"

//==============================================================================

ChorusAudioProcessor::ChorusAudioProcessor()
    :
#ifndef JucePlugin_PreferredChannelConfigurations
      AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
                         .withInput("Input", AudioChannelSet::stereo(), true)
#endif
                         .withOutput("Output", AudioChannelSet::stereo(), true)
#endif
                         ),
#endif
      parameters(*this),
      paramInputGain(parameters, "Input Gain", "dB", -12.0f, 12.0f, 0.0f),
      paramGateThreshold(parameters, "Gate Threshold", "dB", -80.0f, 0.0f, -40.0f),
      paramOutputGain(parameters, "Output Gain", "dB", -100.0f, 0.0f, 0.0f),
      paramBypass(parameters, "Bypass", false),
      // Chorus 参数
      paramChorusRate(parameters, "Chorus Rate", "Hz", 0.01f, 20.0f, 0.5f),
      paramPreDelay(parameters, "PreDelay", "ms", 0.0f, 50.0f, 20.0f),
      paramChorusAmount(parameters, "Chorus Amount", "ms", 0.0f, 50.0f, 10.0f),
      paramDry(parameters, "Dry", "", 0.0f, 1.0f, 0.7f),
      paramWet(parameters, "Wet", "", 0.0f, 1.0f, 0.5f),
      paramChorusFeedback(parameters, "Chorus Feedback", "", -1.0f, 1.0f, 0.15f),
      // Phase90 参数
      paramPhase90Rate(parameters, "Phase90 Rate", "Hz", 0.1f, 20.0f, 1.0f),
      paramCenter(parameters, "Center", "Hz", 20.0f, 10000.0f, 1000.0f),
      paramPhase90Amount(parameters, "Phase90 Amount", "oct", 0.0f, 4.0f, 1.0f),
      paramPhase90Feedback(parameters, "Phase90 Feedback", "", 0.0f, 0.95f, 0.7f),
      paramMix(parameters, "Mix", "", 0.0f, 1.0f, 0.5f)
{
  parameters.valueTreeState.state = ValueTree(Identifier(getName().removeCharacters("- ")));
}

ChorusAudioProcessor::~ChorusAudioProcessor() {}

//==============================================================================

//==============================================================================

float ChorusAudioProcessor::readParameterValue(const String& paramId, float fallback) const
{
  if (auto* param = parameters.valueTreeState.getParameter(paramId)) return param->convertFrom0to1(param->getValue());

  if (auto* value = parameters.valueTreeState.getRawParameterValue(paramId)) return value->load();

  return fallback;
}

void ChorusAudioProcessor::syncParametersFromValueTree()
{
  paramInputGain.setCurrentAndTargetValue(readParameterValue(paramInputGain.paramID, paramInputGain.defaultValue));
  paramGateThreshold.setCurrentAndTargetValue(
      readParameterValue(paramGateThreshold.paramID, paramGateThreshold.defaultValue));
  paramOutputGain.setCurrentAndTargetValue(readParameterValue(paramOutputGain.paramID, paramOutputGain.defaultValue));
  // Chorus
  paramChorusRate.setCurrentAndTargetValue(readParameterValue(paramChorusRate.paramID, paramChorusRate.defaultValue));
  paramPreDelay.setCurrentAndTargetValue(readParameterValue(paramPreDelay.paramID, paramPreDelay.defaultValue));
  paramChorusAmount.setCurrentAndTargetValue(
      readParameterValue(paramChorusAmount.paramID, paramChorusAmount.defaultValue));
  paramDry.setCurrentAndTargetValue(readParameterValue(paramDry.paramID, paramDry.defaultValue));
  paramWet.setCurrentAndTargetValue(readParameterValue(paramWet.paramID, paramWet.defaultValue));
  paramChorusFeedback.setCurrentAndTargetValue(
      readParameterValue(paramChorusFeedback.paramID, paramChorusFeedback.defaultValue));
  // Phase90
  paramPhase90Rate.setCurrentAndTargetValue(
      readParameterValue(paramPhase90Rate.paramID, paramPhase90Rate.defaultValue));
  paramCenter.setCurrentAndTargetValue(readParameterValue(paramCenter.paramID, paramCenter.defaultValue));
  paramPhase90Amount.setCurrentAndTargetValue(
      readParameterValue(paramPhase90Amount.paramID, paramPhase90Amount.defaultValue));
  paramPhase90Feedback.setCurrentAndTargetValue(
      readParameterValue(paramPhase90Feedback.paramID, paramPhase90Feedback.defaultValue));
  paramMix.setCurrentAndTargetValue(readParameterValue(paramMix.paramID, paramMix.defaultValue));
  paramBypass.setCurrentAndTargetValue(readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState));
}

void ChorusAudioProcessor::ensureEffectInstances()
{
  // Chorus
  if (chorus == nullptr) chorus = std::make_unique<nudsp::camel::ChorusF32>();
  for (auto& dw : dryWets)
  {
    if (dw == nullptr) dw = std::make_unique<nudsp::DryWetF32>();
  }
  // Phase90
  if (phase90 == nullptr) phase90 = std::make_unique<nudsp::camel::Phase90F32>();
}

void ChorusAudioProcessor::updateEffectParameters()
{
  // ===== Chorus 参数更新 =====
  {
    const double rate = jmax(0.01, (double)readParameterValue(paramChorusRate.paramID, paramChorusRate.defaultValue));
    const double preDelay = (double)readParameterValue(paramPreDelay.paramID, paramPreDelay.defaultValue);
    const double amount = (double)readParameterValue(paramChorusAmount.paramID, paramChorusAmount.defaultValue);
    const double feedback =
        (double)readParameterValue(paramChorusFeedback.paramID, paramChorusFeedback.defaultValue);
    const float wet = jlimit(0.0f, 1.0f, readParameterValue(paramWet.paramID, paramWet.defaultValue));
    const bool bypass = readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState) >= 0.5f;

    if (chorus != nullptr)
    {
      chorus->setRate(rate);
      chorus->setDelay(preDelay + amount * 0.5);
      chorus->setAmount(amount * 0.5);
      chorus->setCoeffX(0.0);
      chorus->setCoeffMod(1.0);
      chorus->setCoeffFb(feedback);
      chorus->setBypass(bypass);
    }

    for (auto& dw : dryWets)
    {
      if (dw == nullptr) continue;
      dw->setWet(wet);
      dw->setBypass(bypass);
    }
  }

  // ===== Phase90 参数更新 =====
  {
    const double rate = jmax(0.1, (double)readParameterValue(paramPhase90Rate.paramID, paramPhase90Rate.defaultValue));
    const double center =
        jlimit(20.0, 10000.0, (double)readParameterValue(paramCenter.paramID, paramCenter.defaultValue));
    const double amount =
        jlimit(0.0, 4.0, (double)readParameterValue(paramPhase90Amount.paramID, paramPhase90Amount.defaultValue));
    const double feedback =
        jlimit(0.0, 0.95, (double)readParameterValue(paramPhase90Feedback.paramID, paramPhase90Feedback.defaultValue));
    const float mix = jlimit(0.0f, 1.0f, readParameterValue(paramMix.paramID, paramMix.defaultValue));
    const bool bypass = readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState) >= 0.5f;

    if (phase90 != nullptr)
    {
      phase90->setRate(rate);
      phase90->setCenter(center);
      phase90->setAmount(amount);
      phase90->setFeedback(feedback);
      phase90->setMix(mix);
      phase90->setBypass(bypass);
    }
  }
}

void ChorusAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  currentSampleRate = sampleRate;
  gateEnvelope = {};
  gateGain = {1.0f, 1.0f};

  const double smoothTime = 1e-3;
  paramInputGain.reset(sampleRate, smoothTime);
  paramGateThreshold.reset(sampleRate, smoothTime);
  paramOutputGain.reset(sampleRate, smoothTime);
  // Chorus
  paramChorusRate.reset(sampleRate, smoothTime);
  paramPreDelay.reset(sampleRate, smoothTime);
  paramChorusAmount.reset(sampleRate, smoothTime);
  paramDry.reset(sampleRate, smoothTime);
  paramWet.reset(sampleRate, smoothTime);
  paramChorusFeedback.reset(sampleRate, smoothTime);
  // Phase90
  paramPhase90Rate.reset(sampleRate, smoothTime);
  paramCenter.reset(sampleRate, smoothTime);
  paramPhase90Amount.reset(sampleRate, smoothTime);
  paramPhase90Feedback.reset(sampleRate, smoothTime);
  paramMix.reset(sampleRate, smoothTime);
  paramBypass.reset(sampleRate, smoothTime);

  syncParametersFromValueTree();
  ensureEffectInstances();

  // Prepare Chorus
  chorus->prepare(sampleRate);
  chorus->reset();
  chorus->tick(1);
  for (auto& dw : dryWets)
  {
    dw->setSmoothMode(NX_SMOOTH_EXPONENTIAL, (float)sampleRate, 10.0f);
    nx_dry_wet_tick_f32(dw->getRawPointer(), 1);
  }

  // Prepare Phase90
  phase90->prepare(sampleRate);
  phase90->reset();
  phase90->tick(1);

  const int numChannels = jmax(getTotalNumInputChannels(), getTotalNumOutputChannels());
  dryBuffer.setSize(numChannels, samplesPerBlock);
  monoBuffer.setSize(1, samplesPerBlock);
  chorusBuffer.setSize(1, samplesPerBlock);
  phase90Buffer.setSize(numChannels, samplesPerBlock);

  updateEffectParameters();
  chorus->tick(1);
  for (auto& dw : dryWets) nx_dry_wet_tick_f32(dw->getRawPointer(), 1);
  phase90->tick(1);
}

void ChorusAudioProcessor::releaseResources()
{
  dryBuffer.setSize(0, 0);
  monoBuffer.setSize(0, 0);
  chorusBuffer.setSize(0, 0);
  phase90Buffer.setSize(0, 0);
  gateEnvelope = {};
  gateGain = {1.0f, 1.0f};
}

void ChorusAudioProcessor::processInputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb)
{
  const float gain = Decibels::decibelsToGain(gainDb);

  for (int channel = 0; channel < numChannels; ++channel) buffer.applyGain(channel, 0, numSamples, gain);
}

void ChorusAudioProcessor::processGate(AudioSampleBuffer& buffer, int numChannels, int numSamples, float thresholdDb)
{
  const float envRelease = (float)std::exp(-1.0 / (0.050 * currentSampleRate));
  const float gainAttack = (float)std::exp(-1.0 / (0.002 * currentSampleRate));
  const float gainRelease = (float)std::exp(-1.0 / (0.050 * currentSampleRate));
  const int channels = jmin(numChannels, maxChannels);

  for (int channel = 0; channel < channels; ++channel)
  {
    float* data = buffer.getWritePointer(channel);
    float env = gateEnvelope[(size_t)channel];
    float g = gateGain[(size_t)channel];

    for (int i = 0; i < numSamples; ++i)
    {
      const float absSample = std::abs(data[i]);
      env = jmax(absSample, env * envRelease);

      const float levelDb = Decibels::gainToDecibels(env + 1.0e-10f);
      const float targetGain = levelDb >= thresholdDb ? 1.0f : 0.0f;
      const float smoothCoeff = targetGain > g ? gainAttack : gainRelease;
      g = targetGain + smoothCoeff * (g - targetGain);

      data[i] *= g;
    }

    gateEnvelope[(size_t)channel] = env;
    gateGain[(size_t)channel] = g;
  }
}

void ChorusAudioProcessor::processOutputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb)
{
  const float gain = Decibels::decibelsToGain(gainDb);

  for (int channel = 0; channel < numChannels; ++channel) buffer.applyGain(channel, 0, numSamples, gain);
}

void ChorusAudioProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
  ignoreUnused(midiMessages);
  ScopedNoDenormals noDenormals;

  const int numInputChannels = getTotalNumInputChannels();
  const int numOutputChannels = getTotalNumOutputChannels();
  const int numSamples = buffer.getNumSamples();

  if (numSamples == 0) return;

  // 确保缓冲区足够大
  const int bufCh = jmax(numInputChannels, numOutputChannels);
  if (dryBuffer.getNumSamples() < numSamples)
  {
    dryBuffer.setSize(bufCh, numSamples, false, false, true);
    monoBuffer.setSize(1, numSamples, false, false, true);
    chorusBuffer.setSize(1, numSamples, false, false, true);
    phase90Buffer.setSize(bufCh, numSamples, false, false, true);
  }

  ensureEffectInstances();
  updateEffectParameters();

  const float inputGainDb = readParameterValue(paramInputGain.paramID, paramInputGain.defaultValue);
  const float gateThresholdDb = readParameterValue(paramGateThreshold.paramID, paramGateThreshold.defaultValue);
  const float outputGainDb = readParameterValue(paramOutputGain.paramID, paramOutputGain.defaultValue);

  processInputGain(buffer, numInputChannels, numSamples, inputGainDb);
  processGate(buffer, jmin(numInputChannels, numOutputChannels), numSamples, gateThresholdDb);

  const auto frameSize = (size_t)numSamples;
  const int procChannels = jmin(numInputChannels, numOutputChannels, maxChannels);

  const EffectModel model = currentModel.load();

  if (model == kChorus)
  {
    // === Chorus 处理路径 ===
    // 保存干信号
    for (int ch = 0; ch < numInputChannels; ++ch)
      dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // 混音到单声道
    float* monoData = monoBuffer.getWritePointer(0);
    if (numInputChannels >= 2)
    {
      const float* left = dryBuffer.getReadPointer(0);
      const float* right = dryBuffer.getReadPointer(1);
      for (int i = 0; i < numSamples; ++i) monoData[i] = 0.5f * (left[i] + right[i]);
    }
    else
    {
      FloatVectorOperations::copy(monoData, dryBuffer.getReadPointer(0), numSamples);
    }

    if (chorus != nullptr)
    {
      chorus->tick(frameSize);
      float* chorusData = chorusBuffer.getWritePointer(0);
      chorus->process(monoData, chorusData, frameSize);
    }
    else
    {
      FloatVectorOperations::copy(chorusBuffer.getWritePointer(0), monoData, numSamples);
    }

    // 干湿混合
    const float* chorusData = chorusBuffer.getReadPointer(0);
    for (int ch = 0; ch < procChannels; ++ch)
    {
      auto& dw = dryWets[(size_t)ch];
      if (dw == nullptr) continue;
      if (auto* raw = dw->getRawPointer()) nx_dry_wet_tick_f32(raw, frameSize);
      const float* dryData = dryBuffer.getReadPointer(ch);
      float* outData = buffer.getWritePointer(ch);
      dw->process(dryData, chorusData, outData, frameSize);
    }
  }
  else  // kPhase90
  {
    // === Phase90 处理路径 ===
    // Phase90 内置干湿混合（Mix 参数），直接处理每个声道
    if (phase90 != nullptr)
    {
      const float* inPtrs[maxChannels] = {};
      float* outPtrs[maxChannels] = {};
      for (int ch = 0; ch < procChannels; ++ch)
      {
        inPtrs[(size_t)ch] = buffer.getReadPointer(ch);
        outPtrs[(size_t)ch] = phase90Buffer.getWritePointer(ch);
      }

      phase90->tick(frameSize);
      phase90->processMulti(inPtrs, outPtrs, frameSize, (size_t)procChannels);

      for (int ch = 0; ch < procChannels; ++ch)
      {
        FloatVectorOperations::copy(buffer.getWritePointer(ch), phase90Buffer.getReadPointer(ch), numSamples);
      }
    }
  }

  for (int ch = procChannels; ch < numOutputChannels; ++ch) buffer.clear(ch, 0, numSamples);

  processOutputGain(buffer, numOutputChannels, numSamples, outputGainDb);

  float monoPeak = 0.0f, leftPeak = 0.0f, rightPeak = 0.0f;
  if (numOutputChannels > 0) leftPeak = buffer.getMagnitude(0, 0, numSamples);
  if (numOutputChannels > 1) rightPeak = buffer.getMagnitude(1, 0, numSamples);
  monoPeak = numOutputChannels > 1 ? 0.5f * (leftPeak + rightPeak) : leftPeak;

  meterMono.store(monoPeak);
  meterLeft.store(leftPeak);
  meterRight.store(rightPeak);
}

//==============================================================================

void ChorusAudioProcessor::getStateInformation(MemoryBlock& destData)
{
  std::unique_ptr<XmlElement> xml(parameters.valueTreeState.state.createXml());
  copyXmlToBinary(*xml, destData);
}

void ChorusAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState != nullptr)
    if (xmlState->hasTagName(parameters.valueTreeState.state.getType()))
      parameters.valueTreeState.state = ValueTree::fromXml(*xmlState);
}

//==============================================================================

bool ChorusAudioProcessor::hasEditor() const { return true; }

AudioProcessorEditor* ChorusAudioProcessor::createEditor() { return new ChorusAudioProcessorEditor(*this); }

//==============================================================================

#ifndef JucePlugin_PreferredChannelConfigurations
bool ChorusAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
  ignoreUnused(layouts);
  return true;
#else
  if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
    return false;

#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet()) return false;
#endif

  return true;
#endif
}
#endif

//==============================================================================

const String ChorusAudioProcessor::getName() const { return JucePlugin_Name; }

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

double ChorusAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ChorusAudioProcessor::getNumPrograms() { return 1; }

int ChorusAudioProcessor::getCurrentProgram() { return 0; }

void ChorusAudioProcessor::setCurrentProgram(int index) { ignoreUnused(index); }

const String ChorusAudioProcessor::getProgramName(int index)
{
  ignoreUnused(index);
  return {};
}

void ChorusAudioProcessor::changeProgramName(int index, const String& newName) { ignoreUnused(index, newName); }

//==============================================================================

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChorusAudioProcessor(); }

//==============================================================================
