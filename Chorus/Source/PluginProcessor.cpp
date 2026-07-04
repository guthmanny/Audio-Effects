/*
  ==============================================================================

    Chorus plugin — DSP via NuDSP camel/chorus.

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
      paramRate(parameters, "Rate", "Hz", 0.01f, 20.0f, 0.5f),
      paramPreDelay(parameters, "PreDelay", "ms", 0.0f, 50.0f, 20.0f),
      paramAmount(parameters, "Amount", "ms", 0.0f, 50.0f, 10.0f),
      paramDry(parameters, "Dry", "", 0.0f, 1.0f, 0.7f),
      paramWet(parameters, "Wet", "", 0.0f, 1.0f, 0.5f),
      paramFeedback(parameters, "Feedback", "", -1.0f, 1.0f, 0.15f),
      paramBypass(parameters, "Bypass", false)
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
  paramRate.setCurrentAndTargetValue(readParameterValue(paramRate.paramID, paramRate.defaultValue));
  paramPreDelay.setCurrentAndTargetValue(readParameterValue(paramPreDelay.paramID, paramPreDelay.defaultValue));
  paramAmount.setCurrentAndTargetValue(readParameterValue(paramAmount.paramID, paramAmount.defaultValue));
  paramDry.setCurrentAndTargetValue(readParameterValue(paramDry.paramID, paramDry.defaultValue));
  paramWet.setCurrentAndTargetValue(readParameterValue(paramWet.paramID, paramWet.defaultValue));
  paramFeedback.setCurrentAndTargetValue(readParameterValue(paramFeedback.paramID, paramFeedback.defaultValue));
  paramBypass.setCurrentAndTargetValue(readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState));
}

void ChorusAudioProcessor::ensureEffectInstances()
{
  if (chorus == nullptr) chorus = std::make_unique<nudsp::camel::ChorusF32>();

  for (auto& dryWet : dryWets)
  {
    if (dryWet == nullptr) dryWet = std::make_unique<nudsp::DryWetF32>();
  }
}

void ChorusAudioProcessor::updateEffectParameters()
{
  // 参数平滑由 DSP 层 delay_line 内部的 smoother 处理，
  // JUCE 侧直接读取瞬时值，不额外平滑。
  const double rate = jmax(0.01, (double)readParameterValue(paramRate.paramID, paramRate.defaultValue));
  const double preDelay = (double)readParameterValue(paramPreDelay.paramID, paramPreDelay.defaultValue);
  const double amount = (double)readParameterValue(paramAmount.paramID, paramAmount.defaultValue);
  const double feedback = (double)readParameterValue(paramFeedback.paramID, paramFeedback.defaultValue);
  const float mix = jlimit(0.0f, 1.0f, readParameterValue(paramWet.paramID, paramWet.defaultValue));
  const bool bypass = readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState) >= 0.5f;

  // 验证参数是否正确读取
  // DBG("preDelay=" + String(preDelay) + " amount=" + String(amount) + " -> center=" + String(preDelay + amount *
  // 0.5));

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

  for (auto& dryWet : dryWets)
  {
    if (dryWet == nullptr) continue;
    dryWet->setWet(mix);
    dryWet->setBypass(bypass);
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
  paramRate.reset(sampleRate, smoothTime);
  paramPreDelay.reset(sampleRate, smoothTime);
  paramAmount.reset(sampleRate, smoothTime);
  paramDry.reset(sampleRate, smoothTime);
  paramWet.reset(sampleRate, smoothTime);
  paramFeedback.reset(sampleRate, smoothTime);
  paramBypass.reset(sampleRate, smoothTime);

  syncParametersFromValueTree();
  ensureEffectInstances();

  chorus->prepare(sampleRate);
  chorus->reset();
  chorus->tick(1);

  for (auto& dryWet : dryWets)
  {
    dryWet->setSmoothMode(NX_SMOOTH_EXPONENTIAL, (float)sampleRate, 10.0f);
    nx_dry_wet_tick_f32(dryWet->getRawPointer(), 1);
  }

  const int numChannels = jmax(getTotalNumInputChannels(), getTotalNumOutputChannels());
  dryBuffer.setSize(numChannels, samplesPerBlock);
  monoBuffer.setSize(1, samplesPerBlock);
  chorusBuffer.setSize(1, samplesPerBlock);

  updateEffectParameters();
  chorus->tick(1);

  for (auto& dryWet : dryWets) nx_dry_wet_tick_f32(dryWet->getRawPointer(), 1);
}

void ChorusAudioProcessor::releaseResources()
{
  dryBuffer.setSize(0, 0);
  monoBuffer.setSize(0, 0);
  chorusBuffer.setSize(0, 0);
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

  if (dryBuffer.getNumSamples() < numSamples)
  {
    dryBuffer.setSize(jmax(numInputChannels, numOutputChannels), numSamples, false, false, true);
    monoBuffer.setSize(1, numSamples, false, false, true);
    chorusBuffer.setSize(1, numSamples, false, false, true);
  }

  // 参数平滑由 DSP 层 delay_line 内部处理，JUCE 侧不额外平滑
  ensureEffectInstances();
  updateEffectParameters();

  const float inputGainDb = readParameterValue(paramInputGain.paramID, paramInputGain.defaultValue);
  const float gateThresholdDb = readParameterValue(paramGateThreshold.paramID, paramGateThreshold.defaultValue);
  const float outputGainDb = readParameterValue(paramOutputGain.paramID, paramOutputGain.defaultValue);

  processInputGain(buffer, numInputChannels, numSamples, inputGainDb);
  processGate(buffer, jmin(numInputChannels, numOutputChannels), numSamples, gateThresholdDb);

  const auto frameSize = (size_t)numSamples;

  for (int channel = 0; channel < numInputChannels; ++channel)
    dryBuffer.copyFrom(channel, 0, buffer, channel, 0, numSamples);

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
    // 传递实际 frameSize 给 tick，使 delay_line 内部 smoother 正确推进
    chorus->tick(frameSize);

    float* chorusData = chorusBuffer.getWritePointer(0);
    chorus->process(monoData, chorusData, frameSize);
  }
  else
  {
    FloatVectorOperations::copy(chorusBuffer.getWritePointer(0), monoData, numSamples);
  }

  const float* chorusData = chorusBuffer.getReadPointer(0);
  const int mixChannels = jmin(numInputChannels, numOutputChannels, maxChannels);

  for (int channel = 0; channel < mixChannels; ++channel)
  {
    auto& dryWet = dryWets[(size_t)channel];
    if (dryWet == nullptr) continue;

    if (auto* dryWetRaw = dryWet->getRawPointer()) nx_dry_wet_tick_f32(dryWetRaw, frameSize);

    const float* dryData = dryBuffer.getReadPointer(channel);
    float* outData = buffer.getWritePointer(channel);
    dryWet->process(dryData, chorusData, outData, frameSize);
  }

  for (int channel = numInputChannels; channel < numOutputChannels; ++channel) buffer.clear(channel, 0, numSamples);

  processOutputGain(buffer, numOutputChannels, numSamples, outputGainDb);

  float monoPeak = 0.0f;
  float leftPeak = 0.0f;
  float rightPeak = 0.0f;

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
