/*
  ==============================================================================

    Chorus / Phase90 plugin — DSP via minibuss Track + Processors.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include <algorithm>
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
      paramGateThreshMin(parameters, "Gate Thresh Min", "dB", -80.0f, 0.0f, -80.0f),
      paramGateThreshMax(parameters, "Gate Thresh Max", "dB", -80.0f, 0.0f, 0.0f),
      paramGateOffAtMin(parameters, "Gate Off At Min", {"Off", "On"}, 0),
      paramGateRatio(parameters, "Gate Ratio", "", 1.0f, 10.0f, 4.0f),
      paramGateAttack(parameters, "Gate Attack", "ms", 5.0f, 100.0f, 5.0f),
      paramGateRelease(parameters, "Gate Release", "ms", 100.0f, 10000.0f, 100.0f),
      paramGateKnee(parameters, "Gate Knee", {"Hard", "Soft"}, 0),
      paramGateKneeWidth(parameters, "Gate Knee Width", "dB", 0.1f, 24.0f, 6.0f),
      paramOutputGain(parameters, "Output Gain", "dB", -100.0f, 0.0f, 0.0f),
      paramBypass(parameters, "Bypass", false),
      // Chorus 参数（domain 与 MonoChorusProcessor: rate/delay/amount/coeff_fb/wet）
      paramChorusRate(parameters, "Chorus Rate", "Hz", 0.01f, 20.0f, 0.5f),
      paramChorusDelay(parameters, "Chorus Delay", "ms", 1.0f, 100.0f, 25.0f),
      paramChorusAmount(parameters, "Chorus Amount", "ms", 0.0f, 50.0f, 5.0f),
      paramChorusWet(parameters, "Chorus Wet", "", 0.0f, 1.0f, 0.5f),
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

ChorusAudioProcessor::~ChorusAudioProcessor()
{
  spectrumEnabled.store(false);
  spectrumAnalyzer.stopAnalysis();
  minibussEngine.release();
}

void ChorusAudioProcessor::setEffectModel(EffectModel model) noexcept
{
  currentModel.store(model);
  minibussEngine.setEffectModel(model == kPhase90 ? MinibussChorusEngine::EffectModel::Phase90
                                                  : MinibussChorusEngine::EffectModel::Chorus);
}

void ChorusAudioProcessor::setTunerEnabled(bool shouldEnable) noexcept
{
  const bool wasEnabled = tunerEnabled.exchange(shouldEnable);
  if (shouldEnable && !wasEnabled)
  {
    pitchDetector.reset();
    const juce::SpinLock::ScopedLockType lock(tunerLock);
    tunerResult = {};
  }
}

QPitchDetector::Result ChorusAudioProcessor::getTunerResult() const noexcept
{
  const juce::SpinLock::ScopedLockType lock(tunerLock);
  return tunerResult;
}

void ChorusAudioProcessor::setTunerPeriodicityThreshold(float threshold) noexcept
{
  const float clamped = juce::jlimit(0.0f, 1.0f, threshold);
  tunerPeriodicityThreshold.store(clamped);
  pitchDetector.setPeriodicityThreshold(clamped);
}

void ChorusAudioProcessor::setSpectrumEnabled(bool shouldEnable) noexcept
{
  if (shouldEnable)
  {
    spectrumAnalyzer.ensureReady();
    spectrumAnalyzer.reset();
    spectrumFftSize.store(spectrumAnalyzer.getFftSize());
    spectrumAnalyzer.startAnalysis();
    spectrumEnabled.store(true);
  }
  else
  {
    spectrumEnabled.store(false);
    spectrumAnalyzer.stopAnalysis();
  }
}

void ChorusAudioProcessor::setSpectrumFftSize(int fftSize)
{
  const bool wasEnabled = spectrumEnabled.exchange(false);
  spectrumAnalyzer.stopAnalysis();

  spectrumAnalyzer.setFftSize(fftSize);
  spectrumAnalyzer.ensureReady();
  spectrumAnalyzer.reset();
  spectrumFftSize.store(spectrumAnalyzer.getFftSize());

  if (wasEnabled)
  {
    spectrumAnalyzer.startAnalysis();
    spectrumEnabled.store(true);
  }
}

bool ChorusAudioProcessor::copySpectrumMagnitudesIfNew(uint32_t& lastFrameId, std::vector<float>& dest) const
{
  return spectrumAnalyzer.copyMagnitudesIfNew(lastFrameId, dest);
}

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
  paramGateThreshMin.setCurrentAndTargetValue(
      readParameterValue(paramGateThreshMin.paramID, paramGateThreshMin.defaultValue));
  paramGateThreshMax.setCurrentAndTargetValue(
      readParameterValue(paramGateThreshMax.paramID, paramGateThreshMax.defaultValue));
  paramGateOffAtMin.setCurrentAndTargetValue(
      readParameterValue(paramGateOffAtMin.paramID, (float)paramGateOffAtMin.defaultChoice));
  paramGateRatio.setCurrentAndTargetValue(readParameterValue(paramGateRatio.paramID, paramGateRatio.defaultValue));
  paramGateAttack.setCurrentAndTargetValue(readParameterValue(paramGateAttack.paramID, paramGateAttack.defaultValue));
  paramGateRelease.setCurrentAndTargetValue(
      readParameterValue(paramGateRelease.paramID, paramGateRelease.defaultValue));
  paramGateKnee.setCurrentAndTargetValue(readParameterValue(paramGateKnee.paramID, (float)paramGateKnee.defaultChoice));
  paramGateKneeWidth.setCurrentAndTargetValue(
      readParameterValue(paramGateKneeWidth.paramID, paramGateKneeWidth.defaultValue));
  paramOutputGain.setCurrentAndTargetValue(readParameterValue(paramOutputGain.paramID, paramOutputGain.defaultValue));
  // Chorus
  paramChorusRate.setCurrentAndTargetValue(readParameterValue(paramChorusRate.paramID, paramChorusRate.defaultValue));
  paramChorusDelay.setCurrentAndTargetValue(readParameterValue(paramChorusDelay.paramID, paramChorusDelay.defaultValue));
  paramChorusAmount.setCurrentAndTargetValue(
      readParameterValue(paramChorusAmount.paramID, paramChorusAmount.defaultValue));
  paramChorusWet.setCurrentAndTargetValue(readParameterValue(paramChorusWet.paramID, paramChorusWet.defaultValue));
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

void ChorusAudioProcessor::ensureScratchBuffers(int numChannels, int numSamples)
{
  const int channels = jmax(1, numChannels);
  const int samples = jmax(1, numSamples);

  if (dryBuffer.getNumChannels() < channels || dryBuffer.getNumSamples() < samples)
    dryBuffer.setSize(channels, samples, false, false, true);

  if (monoBuffer.getNumChannels() < 1 || monoBuffer.getNumSamples() < samples)
    monoBuffer.setSize(1, samples, false, false, true);

  if (processBuffer.getNumChannels() < channels || processBuffer.getNumSamples() < samples)
    processBuffer.setSize(channels, samples, false, false, true);
}

void ChorusAudioProcessor::updateEffectParameters()
{
  const bool bypass = readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState) >= 0.5f;
  minibussEngine.setBypass(bypass);

  minibussEngine.setParamDomain(minibussEngine.gainId(), "gain",
                                readParameterValue(paramInputGain.paramID, paramInputGain.defaultValue));

  float threshMinDb = readParameterValue(paramGateThreshMin.paramID, paramGateThreshMin.defaultValue);
  float threshMaxDb = readParameterValue(paramGateThreshMax.paramID, paramGateThreshMax.defaultValue);
  if (threshMinDb > threshMaxDb)
    std::swap(threshMinDb, threshMaxDb);
  minibussEngine.setParamDomain(minibussEngine.gateId(), "thresh_min", threshMinDb);
  minibussEngine.setParamDomain(minibussEngine.gateId(), "thresh_max", threshMaxDb);

  const float thresholdDb =
      jlimit(threshMinDb, threshMaxDb,
             readParameterValue(paramGateThreshold.paramID, paramGateThreshold.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.gateId(), "threshold", thresholdDb);
  minibussEngine.setParamDomain(
      minibussEngine.gateId(), "off_at_min",
      readParameterValue(paramGateOffAtMin.paramID, (float)paramGateOffAtMin.defaultChoice) >= 0.5f ? 1.f : 0.f);
  minibussEngine.setParamDomain(minibussEngine.gateId(), "ratio",
                                readParameterValue(paramGateRatio.paramID, paramGateRatio.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.gateId(), "attack",
                                readParameterValue(paramGateAttack.paramID, paramGateAttack.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.gateId(), "release",
                                readParameterValue(paramGateRelease.paramID, paramGateRelease.defaultValue));
  minibussEngine.setParamDomain(
      minibussEngine.gateId(), "knee_mode",
      readParameterValue(paramGateKnee.paramID, (float)paramGateKnee.defaultChoice) >= 0.5f ? 1.f : 0.f);
  minibussEngine.setParamDomain(minibussEngine.gateId(), "knee_width",
                                readParameterValue(paramGateKneeWidth.paramID, paramGateKneeWidth.defaultValue));

  minibussEngine.setParamDomain(minibussEngine.levelId(), "level",
                                readParameterValue(paramOutputGain.paramID, paramOutputGain.defaultValue));

  minibussEngine.setParamDomain(minibussEngine.chorusId(), "rate",
                                readParameterValue(paramChorusRate.paramID, paramChorusRate.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.chorusId(), "delay",
                                readParameterValue(paramChorusDelay.paramID, paramChorusDelay.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.chorusId(), "amount",
                                readParameterValue(paramChorusAmount.paramID, paramChorusAmount.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.chorusId(), "coeff_fb",
                                readParameterValue(paramChorusFeedback.paramID, paramChorusFeedback.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.chorusId(), "wet",
                                readParameterValue(paramChorusWet.paramID, paramChorusWet.defaultValue));

  minibussEngine.setParamDomain(minibussEngine.phase90Id(), "rate",
                                readParameterValue(paramPhase90Rate.paramID, paramPhase90Rate.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.phase90Id(), "center",
                                readParameterValue(paramCenter.paramID, paramCenter.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.phase90Id(), "amount",
                                readParameterValue(paramPhase90Amount.paramID, paramPhase90Amount.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.phase90Id(), "feedback",
                                readParameterValue(paramPhase90Feedback.paramID, paramPhase90Feedback.defaultValue));
  minibussEngine.setParamDomain(minibussEngine.phase90Id(), "mix",
                                readParameterValue(paramMix.paramID, paramMix.defaultValue));

  minibussEngine.setEffectModel(currentModel.load() == kPhase90 ? MinibussChorusEngine::EffectModel::Phase90
                                                                : MinibussChorusEngine::EffectModel::Chorus);
}

void ChorusAudioProcessor::pushAnalysisMono(const AudioSampleBuffer& buffer, int numChannels, int numSamples)
{
  if ((!tunerEnabled.load() && !spectrumEnabled.load()) || numChannels <= 0 || numSamples <= 0) return;

  if (numChannels >= 2)
  {
    const float* left = buffer.getReadPointer(0);
    const float* right = buffer.getReadPointer(1);
    for (int i = 0; i < numSamples; ++i) monoBuffer.setSample(0, i, 0.5f * (left[i] + right[i]));
  }
  else
  {
    monoBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
  }

  const float* mono = monoBuffer.getReadPointer(0);

  if (tunerEnabled.load())
  {
    const bool updated = pitchDetector.process(mono, numSamples);
    if (updated)
    {
      const juce::SpinLock::ScopedLockType lock(tunerLock);
      tunerResult = pitchDetector.getResult();
    }
  }

  if (spectrumEnabled.load()) spectrumAnalyzer.pushSamples(mono, numSamples);
}

void ChorusAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  currentSampleRate = sampleRate;

  const double smoothTime = 1e-3;
  paramInputGain.reset(sampleRate, smoothTime);
  paramGateThreshold.reset(sampleRate, smoothTime);
  paramGateThreshMin.reset(sampleRate, smoothTime);
  paramGateThreshMax.reset(sampleRate, smoothTime);
  paramGateOffAtMin.reset(sampleRate, smoothTime);
  paramGateRatio.reset(sampleRate, smoothTime);
  paramGateAttack.reset(sampleRate, smoothTime);
  paramGateRelease.reset(sampleRate, smoothTime);
  paramGateKnee.reset(sampleRate, smoothTime);
  paramGateKneeWidth.reset(sampleRate, smoothTime);
  paramOutputGain.reset(sampleRate, smoothTime);
  paramChorusRate.reset(sampleRate, smoothTime);
  paramChorusDelay.reset(sampleRate, smoothTime);
  paramChorusAmount.reset(sampleRate, smoothTime);
  paramChorusWet.reset(sampleRate, smoothTime);
  paramChorusFeedback.reset(sampleRate, smoothTime);
  paramPhase90Rate.reset(sampleRate, smoothTime);
  paramCenter.reset(sampleRate, smoothTime);
  paramPhase90Amount.reset(sampleRate, smoothTime);
  paramPhase90Feedback.reset(sampleRate, smoothTime);
  paramMix.reset(sampleRate, smoothTime);
  paramBypass.reset(sampleRate, smoothTime);

  syncParametersFromValueTree();

  pitchDetector.prepare(sampleRate, 50.0f, 500.0f);
  pitchDetector.setPeriodicityThreshold(tunerPeriodicityThreshold.load());
  {
    const juce::SpinLock::ScopedLockType lock(tunerLock);
    tunerResult = {};
  }

  spectrumAnalyzer.setSampleRate(sampleRate);
  if (spectrumEnabled.load())
  {
    spectrumAnalyzer.ensureReady();
    spectrumAnalyzer.reset();
    spectrumAnalyzer.startAnalysis();
  }

  const int numChannels = jmax(1, jmax(getTotalNumInputChannels(), getTotalNumOutputChannels()));
  ensureScratchBuffers(numChannels, samplesPerBlock);

  minibussEngine.prepare((float)sampleRate, (std::uint32_t)jmax(1, samplesPerBlock));
  updateEffectParameters();
}

void ChorusAudioProcessor::releaseResources()
{
  if (!spectrumEnabled.load()) spectrumAnalyzer.stopAnalysis();
  // Keep scratch buffers allocated: JACK/PipeWire may restart the device while
  // the audio callback is still draining.
}

void ChorusAudioProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
  ignoreUnused(midiMessages);
  ScopedNoDenormals noDenormals;

  const int numInputChannels = getTotalNumInputChannels();
  const int numOutputChannels = getTotalNumOutputChannels();
  const int numSamples = buffer.getNumSamples();

  if (numSamples == 0) return;

  const int bufCh = jmax(1, jmax(numInputChannels, numOutputChannels));
  ensureScratchBuffers(bufCh, numSamples);
  updateEffectParameters();

  // Analysis tap on pre-effect input.
  pushAnalysisMono(buffer, numInputChannels, numSamples);

  // Separate in/out buffers — do not process in-place through minibuss.
  constexpr int engCh = 2;
  for (int ch = 0; ch < engCh; ++ch)
  {
    const int src = jmin(ch, jmax(0, numInputChannels - 1));
    if (numInputChannels > 0)
      processBuffer.copyFrom(ch, 0, buffer, src, 0, numSamples);
    else
      processBuffer.clear(ch, 0, numSamples);
  }

  // Keep dryBuffer as a distinct output destination from processBuffer inputs.
  for (int ch = 0; ch < engCh; ++ch)
    dryBuffer.clear(ch, 0, numSamples);

  const float* inPtrs[2] = { processBuffer.getReadPointer(0), processBuffer.getReadPointer(1) };
  float* outPtrs[2] = { dryBuffer.getWritePointer(0), dryBuffer.getWritePointer(1) };

  if (minibussEngine.isReady())
    minibussEngine.process(inPtrs, outPtrs, (std::uint32_t) numSamples);

  const int procChannels = jmin(numOutputChannels, engCh);
  for (int ch = 0; ch < procChannels; ++ch)
    buffer.copyFrom(ch, 0, dryBuffer, ch, 0, numSamples);

  for (int ch = procChannels; ch < numOutputChannels; ++ch)
    buffer.clear(ch, 0, numSamples);

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
