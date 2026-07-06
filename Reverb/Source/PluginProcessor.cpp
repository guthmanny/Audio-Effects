/*
  ==============================================================================

    Reverb plugin — Freeverb / Plate reverb via NuDSP camel.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginParameter.h"

#include <cmath>

//==============================================================================

ReverbAudioProcessor::ReverbAudioProcessor()
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
      // Freeverb 参数
      paramRoomsize(parameters, "Roomsize", "", 0.0f, 1.0f, 0.5f),
      paramDamp(parameters, "Damp", "", 0.0f, 1.0f, 0.5f),
      paramWidth(parameters, "Width", "", 0.0f, 1.0f, 1.0f),
      paramFreeverbWet(parameters, "FV Wet", "", 0.0f, 1.0f, 0.333f),
      paramFreeverbDry(parameters, "FV Dry", "", 0.0f, 1.0f, 0.0f),
      paramFreeze(parameters, "Freeze", "", 0.0f, 1.0f, 0.0f),
      // Plate Reverb 参数
      paramPredelay(parameters, "Predelay", "s", 0.0f, 0.1f, 0.0f),
      paramBandwidth(parameters, "Bandwidth", "Hz", 20.0f, 20000.0f, 20000.0f),
      paramDamping(parameters, "Damping", "Hz", 20.0f, 20000.0f, 20000.0f),
      paramDecay(parameters, "Decay", "", 0.0f, 0.999f, 0.5f),
      paramPlateWet(parameters, "Plate Wet", "", 0.0f, 1.0f, 0.5f)
{
  parameters.valueTreeState.state = ValueTree(Identifier(getName().removeCharacters("- ")));
}

ReverbAudioProcessor::~ReverbAudioProcessor() {}

//==============================================================================

float ReverbAudioProcessor::readParameterValue(const String& paramId, float fallback) const
{
  if (auto* param = parameters.valueTreeState.getParameter(paramId))
    return param->convertFrom0to1(param->getValue());
  if (auto* value = parameters.valueTreeState.getRawParameterValue(paramId))
    return value->load();
  return fallback;
}

void ReverbAudioProcessor::syncParametersFromValueTree()
{
  paramInputGain.setCurrentAndTargetValue(readParameterValue(paramInputGain.paramID, paramInputGain.defaultValue));
  paramGateThreshold.setCurrentAndTargetValue(
      readParameterValue(paramGateThreshold.paramID, paramGateThreshold.defaultValue));
  paramOutputGain.setCurrentAndTargetValue(readParameterValue(paramOutputGain.paramID, paramOutputGain.defaultValue));
  // Freeverb
  paramRoomsize.setCurrentAndTargetValue(readParameterValue(paramRoomsize.paramID, paramRoomsize.defaultValue));
  paramDamp.setCurrentAndTargetValue(readParameterValue(paramDamp.paramID, paramDamp.defaultValue));
  paramWidth.setCurrentAndTargetValue(readParameterValue(paramWidth.paramID, paramWidth.defaultValue));
  paramFreeverbWet.setCurrentAndTargetValue(readParameterValue(paramFreeverbWet.paramID, paramFreeverbWet.defaultValue));
  paramFreeverbDry.setCurrentAndTargetValue(readParameterValue(paramFreeverbDry.paramID, paramFreeverbDry.defaultValue));
  paramFreeze.setCurrentAndTargetValue(readParameterValue(paramFreeze.paramID, paramFreeze.defaultValue));
  // Plate
  paramPredelay.setCurrentAndTargetValue(readParameterValue(paramPredelay.paramID, paramPredelay.defaultValue));
  paramBandwidth.setCurrentAndTargetValue(readParameterValue(paramBandwidth.paramID, paramBandwidth.defaultValue));
  paramDamping.setCurrentAndTargetValue(readParameterValue(paramDamping.paramID, paramDamping.defaultValue));
  paramDecay.setCurrentAndTargetValue(readParameterValue(paramDecay.paramID, paramDecay.defaultValue));
  paramPlateWet.setCurrentAndTargetValue(readParameterValue(paramPlateWet.paramID, paramPlateWet.defaultValue));
  paramBypass.setCurrentAndTargetValue(readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState));
}

void ReverbAudioProcessor::ensureEffectInstances()
{
  if (freeverb == nullptr) freeverb = std::make_unique<nudsp::camel::FreeverbF32>();
  if (plate == nullptr) plate = std::make_unique<nudsp::camel::ReverbF32>();
}

void ReverbAudioProcessor::updateEffectParameters()
{
  const bool bypass = readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState) >= 0.5f;

  // Freeverb
  if (freeverb != nullptr)
  {
    freeverb->setRoomsize(readParameterValue(paramRoomsize.paramID, paramRoomsize.defaultValue));
    freeverb->setDamp(readParameterValue(paramDamp.paramID, paramDamp.defaultValue));
    freeverb->setWidth(readParameterValue(paramWidth.paramID, paramWidth.defaultValue));
    freeverb->setWet(readParameterValue(paramFreeverbWet.paramID, paramFreeverbWet.defaultValue));
    freeverb->setDry(readParameterValue(paramFreeverbDry.paramID, paramFreeverbDry.defaultValue));
    freeverb->setFreeze(readParameterValue(paramFreeze.paramID, paramFreeze.defaultValue));
    freeverb->setBypass(bypass);
  }

  // Plate
  if (plate != nullptr)
  {
    plate->setPredelay(readParameterValue(paramPredelay.paramID, paramPredelay.defaultValue));
    plate->setBandwidth(readParameterValue(paramBandwidth.paramID, paramBandwidth.defaultValue));
    plate->setDamping(readParameterValue(paramDamping.paramID, paramDamping.defaultValue));
    plate->setDecay(readParameterValue(paramDecay.paramID, paramDecay.defaultValue));
    plate->setWet(readParameterValue(paramPlateWet.paramID, paramPlateWet.defaultValue));
    plate->setBypass(bypass);
  }
}

void ReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  currentSampleRate = sampleRate;
  gateEnvelope = {};
  gateGain = {1.0f, 1.0f};

  const double smoothTime = 1e-3;
  paramInputGain.reset(sampleRate, smoothTime);
  paramGateThreshold.reset(sampleRate, smoothTime);
  paramOutputGain.reset(sampleRate, smoothTime);
  paramRoomsize.reset(sampleRate, smoothTime);
  paramDamp.reset(sampleRate, smoothTime);
  paramWidth.reset(sampleRate, smoothTime);
  paramFreeverbWet.reset(sampleRate, smoothTime);
  paramFreeverbDry.reset(sampleRate, smoothTime);
  paramFreeze.reset(sampleRate, smoothTime);
  paramPredelay.reset(sampleRate, smoothTime);
  paramBandwidth.reset(sampleRate, smoothTime);
  paramDamping.reset(sampleRate, smoothTime);
  paramDecay.reset(sampleRate, smoothTime);
  paramPlateWet.reset(sampleRate, smoothTime);
  paramBypass.reset(sampleRate, smoothTime);

  syncParametersFromValueTree();
  ensureEffectInstances();

  // Prepare both models
  freeverb->prepare(sampleRate);
  freeverb->reset(0.0f, 0.0f, nullptr, nullptr);
  freeverb->tick(1);

  plate->prepare(sampleRate);
  plate->reset(0.0f, 0.0f, nullptr, nullptr);
  plate->tick(1);

  updateEffectParameters();
  freeverb->tick(1);
  plate->tick(1);
}

void ReverbAudioProcessor::releaseResources()
{
  gateEnvelope = {};
  gateGain = {1.0f, 1.0f};
}

void ReverbAudioProcessor::processInputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb)
{
  const float gain = Decibels::decibelsToGain(gainDb);
  for (int channel = 0; channel < numChannels; ++channel)
    buffer.applyGain(channel, 0, numSamples, gain);
}

void ReverbAudioProcessor::processGate(AudioSampleBuffer& buffer, int numChannels, int numSamples, float thresholdDb)
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

void ReverbAudioProcessor::processOutputGain(AudioSampleBuffer& buffer, int numChannels, int numSamples, float gainDb)
{
  const float gain = Decibels::decibelsToGain(gainDb);
  for (int channel = 0; channel < numChannels; ++channel)
    buffer.applyGain(channel, 0, numSamples, gain);
}

void ReverbAudioProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
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
  const ReverbModel model = currentModel.load();

  if (model == kFreeverb)
  {
    if (freeverb != nullptr)
    {
      freeverb->tick(frameSize);
      // Freeverb 立体声处理：直接传入左右声道
      if (procChannels >= 2)
      {
        const float* leftIn = buffer.getReadPointer(0);
        const float* rightIn = buffer.getReadPointer(1);
        float* leftOut = buffer.getWritePointer(0);
        float* rightOut = buffer.getWritePointer(1);
        freeverb->process(leftIn, rightIn, leftOut, rightOut, frameSize);
      }
      else if (procChannels == 1)
      {
        // 单声道：复制到左右再处理
        float* monoData = buffer.getWritePointer(0);
        freeverb->process(monoData, monoData, monoData, monoData, frameSize);
      }
    }
  }
  else // kPlate
  {
    if (plate != nullptr)
    {
      plate->tick(frameSize);
      if (procChannels >= 2)
      {
        const float* leftIn = buffer.getReadPointer(0);
        const float* rightIn = buffer.getReadPointer(1);
        float* leftOut = buffer.getWritePointer(0);
        float* rightOut = buffer.getWritePointer(1);
        plate->process(leftIn, rightIn, leftOut, rightOut, frameSize);
      }
      else if (procChannels == 1)
      {
        float* monoData = buffer.getWritePointer(0);
        plate->process(monoData, monoData, monoData, monoData, frameSize);
      }
    }
  }

  for (int ch = procChannels; ch < numOutputChannels; ++ch)
    buffer.clear(ch, 0, numSamples);

  processOutputGain(buffer, numOutputChannels, numSamples, outputGainDb);

  float leftPeak = 0.0f, rightPeak = 0.0f;
  if (numOutputChannels > 0) leftPeak = buffer.getMagnitude(0, 0, numSamples);
  if (numOutputChannels > 1) rightPeak = buffer.getMagnitude(1, 0, numSamples);
  meterMono.store(numOutputChannels > 1 ? 0.5f * (leftPeak + rightPeak) : leftPeak);
  meterLeft.store(leftPeak);
  meterRight.store(rightPeak);
}

//==============================================================================

void ReverbAudioProcessor::getStateInformation(MemoryBlock& destData)
{
  std::unique_ptr<XmlElement> xml(parameters.valueTreeState.state.createXml());
  copyXmlToBinary(*xml, destData);
}

void ReverbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState != nullptr)
    if (xmlState->hasTagName(parameters.valueTreeState.state.getType()))
      parameters.valueTreeState.state = ValueTree::fromXml(*xmlState);
}

bool ReverbAudioProcessor::hasEditor() const { return true; }
AudioProcessorEditor* ReverbAudioProcessor::createEditor() { return new ReverbAudioProcessorEditor(*this); }

#ifndef JucePlugin_PreferredChannelConfigurations
bool ReverbAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
  ignoreUnused(layouts);
  return true;
#else
  if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
    return false;
#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif
  return true;
#endif
}
#endif

const String ReverbAudioProcessor::getName() const { return JucePlugin_Name; }
bool ReverbAudioProcessor::acceptsMidi() const { return false; }
bool ReverbAudioProcessor::producesMidi() const { return false; }
bool ReverbAudioProcessor::isMidiEffect() const { return false; }
double ReverbAudioProcessor::getTailLengthSeconds() const { return 3.0; }
int ReverbAudioProcessor::getNumPrograms() { return 1; }
int ReverbAudioProcessor::getCurrentProgram() { return 0; }
void ReverbAudioProcessor::setCurrentProgram(int) {}
const String ReverbAudioProcessor::getProgramName(int) { return {}; }
void ReverbAudioProcessor::changeProgramName(int, const String&) {}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ReverbAudioProcessor(); }
