/*
  ==============================================================================

    Delay / Analog Delay plugin — DSP via NuDSP camel.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include <cmath>

#include "PluginEditor.h"
#include "PluginParameter.h"

//==============================================================================

DelayAudioProcessor::DelayAudioProcessor()
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
      // Delay 参数
      paramDelayTime(parameters, "Delay Time", "s", 0.0f, 5.0f, 0.1f),
      paramFeedback(parameters, "Feedback", "", 0.0f, 0.9f, 0.7f),
      paramMix(parameters, "Mix", "", 0.0f, 1.0f, 1.0f),
      // Analog Delay 参数
      paramAnalogDelayTime(parameters, "Analog Delay Time", "s", 0.0f, 5.0f, 0.1f),
      paramAnalogFeedback(parameters, "Analog Feedback", "", 0.0f, 0.9f, 0.7f),
      paramAnalogMix(parameters, "Analog Mix", "", 0.0f, 1.0f, 1.0f),
      paramLfoRate(parameters, "LFO Rate", "Hz", 0.0f, 20.0f, 0.0f),
      paramLfoDepth(parameters, "LFO Depth", "Hz", 0.0f, 100.0f, 0.0f)
{
  parameters.valueTreeState.state = ValueTree(Identifier(getName().removeCharacters("- ")));
}

DelayAudioProcessor::~DelayAudioProcessor() {}

//==============================================================================

//==============================================================================

float DelayAudioProcessor::readParameterValue(const String& paramId, float fallback) const
{
  if (auto* param = parameters.valueTreeState.getParameter(paramId)) return param->convertFrom0to1(param->getValue());

  if (auto* value = parameters.valueTreeState.getRawParameterValue(paramId)) return value->load();

  return fallback;
}

void DelayAudioProcessor::syncParametersFromValueTree()
{
  paramInputGain.setCurrentAndTargetValue(readParameterValue(paramInputGain.paramID, paramInputGain.defaultValue));
  paramGateThreshold.setCurrentAndTargetValue(
      readParameterValue(paramGateThreshold.paramID, paramGateThreshold.defaultValue));
  paramOutputGain.setCurrentAndTargetValue(readParameterValue(paramOutputGain.paramID, paramOutputGain.defaultValue));
  // Delay
  paramDelayTime.setCurrentAndTargetValue(readParameterValue(paramDelayTime.paramID, paramDelayTime.defaultValue));
  paramFeedback.setCurrentAndTargetValue(readParameterValue(paramFeedback.paramID, paramFeedback.defaultValue));
  paramMix.setCurrentAndTargetValue(readParameterValue(paramMix.paramID, paramMix.defaultValue));
  // Analog Delay
  paramAnalogDelayTime.setCurrentAndTargetValue(
      readParameterValue(paramAnalogDelayTime.paramID, paramAnalogDelayTime.defaultValue));
  paramAnalogFeedback.setCurrentAndTargetValue(
      readParameterValue(paramAnalogFeedback.paramID, paramAnalogFeedback.defaultValue));
  paramAnalogMix.setCurrentAndTargetValue(
      readParameterValue(paramAnalogMix.paramID, paramAnalogMix.defaultValue));
  paramLfoRate.setCurrentAndTargetValue(readParameterValue(paramLfoRate.paramID, paramLfoRate.defaultValue));
  paramLfoDepth.setCurrentAndTargetValue(readParameterValue(paramLfoDepth.paramID, paramLfoDepth.defaultValue));
  paramBypass.setCurrentAndTargetValue(readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState));
}

void DelayAudioProcessor::ensureEffectInstances()
{
  // Delay
  if (delay == nullptr) delay = std::make_unique<nudsp::camel::DelayF32>();
  // Analog Delay
  if (analogDelay == nullptr) analogDelay = std::make_unique<nudsp::camel::AnalogDelayF32>();
}

void DelayAudioProcessor::updateEffectParameters()
{
  const bool bypass = readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState) >= 0.5f;

  // ===== Delay 参数更新 =====
  {
    const double delayTime =
        jlimit(0.0, 5.0, (double)readParameterValue(paramDelayTime.paramID, paramDelayTime.defaultValue));
    const double feedback =
        jlimit(0.0, 0.9, (double)readParameterValue(paramFeedback.paramID, paramFeedback.defaultValue));
    const double mix =
        jlimit(0.0, 1.0, (double)readParameterValue(paramMix.paramID, paramMix.defaultValue));

    if (delay != nullptr)
    {
      delay->setDelayTime(delayTime);
      delay->setFeedback(feedback);
      delay->setMix(mix);
      delay->setBypass(bypass);
    }
  }

  // ===== Analog Delay 参数更新 =====
  {
    const double delayTime =
        jlimit(0.0, 5.0,
               (double)readParameterValue(paramAnalogDelayTime.paramID, paramAnalogDelayTime.defaultValue));
    const double feedback =
        jlimit(0.0, 0.9,
               (double)readParameterValue(paramAnalogFeedback.paramID, paramAnalogFeedback.defaultValue));
    const double mix =
        jlimit(0.0, 1.0, (double)readParameterValue(paramAnalogMix.paramID, paramAnalogMix.defaultValue));
    const double lfoRate =
        jlimit(0.0, 20.0, (double)readParameterValue(paramLfoRate.paramID, paramLfoRate.defaultValue));
    const double lfoDepth =
        jlimit(0.0, 100.0, (double)readParameterValue(paramLfoDepth.paramID, paramLfoDepth.defaultValue));

    if (analogDelay != nullptr)
    {
      analogDelay->setDelayTime(delayTime);
      analogDelay->setFeedback(feedback);
      analogDelay->setMix(mix);
      analogDelay->setLfoRate(lfoRate);
      analogDelay->setLfoDepth(lfoDepth);
      analogDelay->setBypass(bypass);
    }
  }
}

void DelayAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  currentSampleRate = sampleRate;
  gateEnvelope = {};
  gateGain = {1.0f, 1.0f};

  const double smoothTime = 1e-3;
  paramInputGain.reset(sampleRate, smoothTime);
  paramGateThreshold.reset(sampleRate, smoothTime);
  paramOutputGain.reset(sampleRate, smoothTime);
  // Delay
  paramDelayTime.reset(sampleRate, smoothTime);
  paramFeedback.reset(sampleRate, smoothTime);
  paramMix.reset(sampleRate, smoothTime);
  // Analog Delay
  paramAnalogDelayTime.reset(sampleRate, smoothTime);
  paramAnalogFeedback.reset(sampleRate, smoothTime);
  paramAnalogMix.reset(sampleRate, smoothTime);
  paramLfoRate.reset(sampleRate, smoothTime);
  paramLfoDepth.reset(sampleRate, smoothTime);
  paramBypass.reset(sampleRate, smoothTime);

  syncParametersFromValueTree();
  ensureEffectInstances();

  // Prepare Delay
  delay->prepare(sampleRate);
  delay->reset();
  delay->tick(1);

  // Prepare Analog Delay
  analogDelay->prepare(sampleRate);
  analogDelay->reset();
  analogDelay->tick(1);

  updateEffectParameters();
  delay->tick(1);
  analogDelay->tick(1);
}

void DelayAudioProcessor::releaseResources()
{
  gateEnvelope = {};
  gateGain = {1.0f, 1.0f};
}

void DelayAudioProcessor::processInputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb)
{
  const float gain = Decibels::decibelsToGain(gainDb);

  for (int channel = 0; channel < numChannels; ++channel) buffer.applyGain(channel, 0, numSamples, gain);
}

void DelayAudioProcessor::processGate(AudioSampleBuffer& buffer, int numChannels, int numSamples, float thresholdDb)
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

void DelayAudioProcessor::processOutputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb)
{
  const float gain = Decibels::decibelsToGain(gainDb);

  for (int channel = 0; channel < numChannels; ++channel) buffer.applyGain(channel, 0, numSamples, gain);
}

void DelayAudioProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
  ignoreUnused(midiMessages);
  ScopedNoDenormals noDenormals;

  const int numInputChannels = getTotalNumInputChannels();
  const int numOutputChannels = getTotalNumOutputChannels();
  const int numSamples = buffer.getNumSamples();

  if (numSamples == 0) return;

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

  if (model == kDelay)
  {
    // === Delay 处理路径（内置干湿混合） ===
    if (delay != nullptr)
    {
      delay->tick(frameSize);
      // Delay 使用 processMulti 进行多声道处理
      const float* inPtrs[maxChannels] = {};
      float* outPtrs[maxChannels] = {};
      for (int ch = 0; ch < procChannels; ++ch)
      {
        inPtrs[(size_t)ch] = buffer.getReadPointer(ch);
        outPtrs[(size_t)ch] = buffer.getWritePointer(ch);
      }

      delay->processMulti(inPtrs, outPtrs, frameSize, (size_t)procChannels);
    }
  }
  else  // kAnalogDelay
  {
    // === Analog Delay 处理路径（内置干湿混合） ===
    if (analogDelay != nullptr)
    {
      analogDelay->tick(frameSize);
      const float* inPtrs[maxChannels] = {};
      float* outPtrs[maxChannels] = {};
      for (int ch = 0; ch < procChannels; ++ch)
      {
        inPtrs[(size_t)ch] = buffer.getReadPointer(ch);
        outPtrs[(size_t)ch] = buffer.getWritePointer(ch);
      }

      analogDelay->processMulti(inPtrs, outPtrs, frameSize, (size_t)procChannels);
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

void DelayAudioProcessor::getStateInformation(MemoryBlock& destData)
{
  std::unique_ptr<XmlElement> xml(parameters.valueTreeState.state.createXml());
  copyXmlToBinary(*xml, destData);
}

void DelayAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState != nullptr)
    if (xmlState->hasTagName(parameters.valueTreeState.state.getType()))
      parameters.valueTreeState.state = ValueTree::fromXml(*xmlState);
}

//==============================================================================

bool DelayAudioProcessor::hasEditor() const { return true; }

AudioProcessorEditor* DelayAudioProcessor::createEditor() { return new DelayAudioProcessorEditor(*this); }

//==============================================================================

#ifndef JucePlugin_PreferredChannelConfigurations
bool DelayAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

const String DelayAudioProcessor::getName() const { return JucePlugin_Name; }

bool DelayAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool DelayAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool DelayAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double DelayAudioProcessor::getTailLengthSeconds() const { return 5.0; }

int DelayAudioProcessor::getNumPrograms() { return 1; }

int DelayAudioProcessor::getCurrentProgram() { return 0; }

void DelayAudioProcessor::setCurrentProgram(int index) { ignoreUnused(index); }

const String DelayAudioProcessor::getProgramName(int index)
{
  ignoreUnused(index);
  return {};
}

void DelayAudioProcessor::changeProgramName(int index, const String& newName) { ignoreUnused(index, newName); }

//==============================================================================

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DelayAudioProcessor(); }

//==============================================================================
