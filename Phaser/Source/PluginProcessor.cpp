/*
  ==============================================================================

    Phaser plugin — DSP via NuDSP camel.
    4 phaser models: Phaser, Phase90, OTA Phaser, JFET Phaser.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include <cmath>

#include "PluginEditor.h"
#include "PluginParameter.h"

//==============================================================================

PhaserAudioProcessor::PhaserAudioProcessor()
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
      // 通用参数（四个模型复用）
      paramRate(parameters, "Rate", "Hz", 0.1f, 20.0f, 1.0f),
      paramCenter(parameters, "Center", "Hz", 20.0f, 10000.0f, 1000.0f),
      paramAmount(parameters, "Amount", "oct", 0.0f, 4.0f, 1.0f),
      paramFeedback(parameters, "Feedback", "", 0.0f, 0.95f, 0.7f),
      paramMix(parameters, "Mix", "", 0.0f, 1.0f, 0.5f)
{
  parameters.valueTreeState.state = ValueTree(Identifier(getName().removeCharacters("- ")));
}

PhaserAudioProcessor::~PhaserAudioProcessor() {}

//==============================================================================

//==============================================================================

float PhaserAudioProcessor::readParameterValue(const String& paramId, float fallback) const
{
  if (auto* param = parameters.valueTreeState.getParameter(paramId)) return param->convertFrom0to1(param->getValue());

  if (auto* value = parameters.valueTreeState.getRawParameterValue(paramId)) return value->load();

  return fallback;
}

void PhaserAudioProcessor::syncParametersFromValueTree()
{
  paramInputGain.setCurrentAndTargetValue(readParameterValue(paramInputGain.paramID, paramInputGain.defaultValue));
  paramGateThreshold.setCurrentAndTargetValue(
      readParameterValue(paramGateThreshold.paramID, paramGateThreshold.defaultValue));
  paramOutputGain.setCurrentAndTargetValue(readParameterValue(paramOutputGain.paramID, paramOutputGain.defaultValue));
  // 通用参数
  paramRate.setCurrentAndTargetValue(readParameterValue(paramRate.paramID, paramRate.defaultValue));
  paramCenter.setCurrentAndTargetValue(readParameterValue(paramCenter.paramID, paramCenter.defaultValue));
  paramAmount.setCurrentAndTargetValue(readParameterValue(paramAmount.paramID, paramAmount.defaultValue));
  paramFeedback.setCurrentAndTargetValue(readParameterValue(paramFeedback.paramID, paramFeedback.defaultValue));
  paramMix.setCurrentAndTargetValue(readParameterValue(paramMix.paramID, paramMix.defaultValue));
  paramBypass.setCurrentAndTargetValue(readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState));
}

void PhaserAudioProcessor::ensureEffectInstances()
{
  if (phaser == nullptr) phaser = std::make_unique<nudsp::camel::PhaserF32>();
  if (phase90 == nullptr) phase90 = std::make_unique<nudsp::camel::Phase90F32>();
  if (otaPhaser == nullptr) otaPhaser = std::make_unique<nudsp::camel::OtaPhaserF32>();
  if (jfetPhaser == nullptr) jfetPhaser = std::make_unique<nudsp::camel::JfetPhaserF32>();
  for (auto& dw : dryWets)
  {
    if (dw == nullptr) dw = std::make_unique<nudsp::DryWetF32>();
  }
}

void PhaserAudioProcessor::updateEffectParameters()
{
  // ===== 通用参数读取 =====
  const double rate = jmax(0.1, (double)readParameterValue(paramRate.paramID, paramRate.defaultValue));
  const double center = jlimit(20.0, 10000.0, (double)readParameterValue(paramCenter.paramID, paramCenter.defaultValue));
  const double amount = jlimit(0.0, 4.0, (double)readParameterValue(paramAmount.paramID, paramAmount.defaultValue));
  const double feedback = jlimit(0.0, 0.95, (double)readParameterValue(paramFeedback.paramID, paramFeedback.defaultValue));
  const float mix = jlimit(0.0f, 1.0f, readParameterValue(paramMix.paramID, paramMix.defaultValue));
  const bool bypass = readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState) >= 0.5f;

  // ===== Phaser (basic) =====
  if (phaser != nullptr)
  {
    phaser->setRate(rate);
    phaser->setCenter(center);
    phaser->setAmount(amount);
    phaser->setBypass(bypass);
  }

  // ===== Phase90 =====
  if (phase90 != nullptr)
  {
    phase90->setRate(rate);
    phase90->setCenter(center);
    phase90->setAmount(amount);
    phase90->setFeedback(feedback);
    phase90->setMix(mix);
    phase90->setBypass(bypass);
  }

  // ===== OTA Phaser =====
  if (otaPhaser != nullptr)
  {
    otaPhaser->setRate(rate);
    otaPhaser->setCenter(center);
    otaPhaser->setAmount(amount);
    otaPhaser->setFeedback(feedback);
    otaPhaser->setMix(mix);
    otaPhaser->setBypass(bypass);
  }

  // ===== JFET Phaser =====
  if (jfetPhaser != nullptr)
  {
    jfetPhaser->setRate(rate);
    jfetPhaser->setCenter(center);
    jfetPhaser->setAmount(amount);
    jfetPhaser->setFeedback(feedback);
    jfetPhaser->setMix(mix);
    jfetPhaser->setBypass(bypass);
  }

  for (auto& dw : dryWets)
  {
    if (dw == nullptr) continue;
    dw->setWet(1.0f);
    dw->setBypass(bypass);
  }
}

void PhaserAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  currentSampleRate = sampleRate;
  gateEnvelope = {};
  gateGain = {1.0f, 1.0f};

  const double smoothTime = 1e-3;
  paramInputGain.reset(sampleRate, smoothTime);
  paramGateThreshold.reset(sampleRate, smoothTime);
  paramOutputGain.reset(sampleRate, smoothTime);
  paramRate.reset(sampleRate, smoothTime);
  paramCenter.reset(sampleRate, smoothTime);
  paramAmount.reset(sampleRate, smoothTime);
  paramFeedback.reset(sampleRate, smoothTime);
  paramMix.reset(sampleRate, smoothTime);
  paramBypass.reset(sampleRate, smoothTime);

  syncParametersFromValueTree();
  ensureEffectInstances();

  // Prepare all 4 phaser instances
  phaser->prepare(sampleRate);
  phaser->reset();
  phaser->tick(1);

  phase90->prepare(sampleRate);
  phase90->reset();
  phase90->tick(1);

  otaPhaser->prepare(sampleRate);
  otaPhaser->reset();
  otaPhaser->tick(1);

  jfetPhaser->prepare(sampleRate);
  jfetPhaser->reset();
  jfetPhaser->tick(1);

  for (auto& dw : dryWets)
  {
    dw->setSmoothMode(NX_SMOOTH_EXPONENTIAL, (float)sampleRate, 10.0f);
    nx_dry_wet_tick_f32(dw->getRawPointer(), 1);
  }

  const int numChannels = jmax(getTotalNumInputChannels(), getTotalNumOutputChannels());
  dryBuffer.setSize(numChannels, samplesPerBlock);
  fxBuffer.setSize(numChannels, samplesPerBlock);

  updateEffectParameters();
  phaser->tick(1);
  phase90->tick(1);
  otaPhaser->tick(1);
  jfetPhaser->tick(1);
  for (auto& dw : dryWets) nx_dry_wet_tick_f32(dw->getRawPointer(), 1);
}

void PhaserAudioProcessor::releaseResources()
{
  dryBuffer.setSize(0, 0);
  fxBuffer.setSize(0, 0);
  gateEnvelope = {};
  gateGain = {1.0f, 1.0f};
}

void PhaserAudioProcessor::processInputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb)
{
  const float gain = Decibels::decibelsToGain(gainDb);

  for (int channel = 0; channel < numChannels; ++channel) buffer.applyGain(channel, 0, numSamples, gain);
}

void PhaserAudioProcessor::processGate(AudioSampleBuffer& buffer, int numChannels, int numSamples, float thresholdDb)
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

void PhaserAudioProcessor::processOutputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb)
{
  const float gain = Decibels::decibelsToGain(gainDb);

  for (int channel = 0; channel < numChannels; ++channel) buffer.applyGain(channel, 0, numSamples, gain);
}

void PhaserAudioProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
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
    fxBuffer.setSize(bufCh, numSamples, false, false, true);
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

  // 保存干信号
  for (int ch = 0; ch < numInputChannels; ++ch)
    dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

  // 根据当前模型选择处理路径
  const float* inPtrs[maxChannels] = {};
  float* outPtrs[maxChannels] = {};
  for (int ch = 0; ch < procChannels; ++ch)
  {
    inPtrs[(size_t)ch] = dryBuffer.getReadPointer(ch);
    outPtrs[(size_t)ch] = fxBuffer.getWritePointer(ch);
  }

  switch (model)
  {
    case kPhaser:
      if (phaser != nullptr)
      {
        phaser->tick(frameSize);
        phaser->processMulti(inPtrs, outPtrs, frameSize, (size_t)procChannels);
      }
      break;

    case kPhase90:
      if (phase90 != nullptr)
      {
        phase90->tick(frameSize);
        phase90->processMulti(inPtrs, outPtrs, frameSize, (size_t)procChannels);
      }
      break;

    case kOtaPhaser:
      if (otaPhaser != nullptr)
      {
        otaPhaser->tick(frameSize);
        otaPhaser->processMulti(inPtrs, outPtrs, frameSize, (size_t)procChannels);
      }
      break;

    case kJfetPhaser:
      if (jfetPhaser != nullptr)
      {
        jfetPhaser->tick(frameSize);
        jfetPhaser->processMulti(inPtrs, outPtrs, frameSize, (size_t)procChannels);
      }
      break;
  }

  // 混合输出
  for (int ch = 0; ch < procChannels; ++ch)
  {
    FloatVectorOperations::copy(buffer.getWritePointer(ch), fxBuffer.getReadPointer(ch), numSamples);
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

void PhaserAudioProcessor::getStateInformation(MemoryBlock& destData)
{
  std::unique_ptr<XmlElement> xml(parameters.valueTreeState.state.createXml());
  copyXmlToBinary(*xml, destData);
}

void PhaserAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState != nullptr)
    if (xmlState->hasTagName(parameters.valueTreeState.state.getType()))
      parameters.valueTreeState.state = ValueTree::fromXml(*xmlState);
}

//==============================================================================

bool PhaserAudioProcessor::hasEditor() const { return true; }

AudioProcessorEditor* PhaserAudioProcessor::createEditor() { return new PhaserAudioProcessorEditor(*this); }

//==============================================================================

#ifndef JucePlugin_PreferredChannelConfigurations
bool PhaserAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

const String PhaserAudioProcessor::getName() const { return JucePlugin_Name; }

bool PhaserAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool PhaserAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool PhaserAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double PhaserAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int PhaserAudioProcessor::getNumPrograms() { return 1; }

int PhaserAudioProcessor::getCurrentProgram() { return 0; }

void PhaserAudioProcessor::setCurrentProgram(int index) { ignoreUnused(index); }

const String PhaserAudioProcessor::getProgramName(int index)
{
  ignoreUnused(index);
  return {};
}

void PhaserAudioProcessor::changeProgramName(int index, const String& newName) { ignoreUnused(index, newName); }

//==============================================================================

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PhaserAudioProcessor(); }

//==============================================================================
