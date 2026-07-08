/*
  ==============================================================================

    ParametricEQ plugin — multi-band parametric equalizer via NuDSP SVF model.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include <cmath>
#include <limits>

#include "PluginEditor.h"
#include "PluginParameter.h"

//==============================================================================

namespace
{
constexpr double kBandParamSmoothSec = 0.02;
constexpr double kGainParamSmoothSec = 0.05;
constexpr float kMinQ = 0.1f;
constexpr float kMaxQ = 10.0f;
constexpr float kMinFreqHz = 20.0f;
constexpr float kMaxFreqHz = 20000.0f;
constexpr float kMinGainDb = -18.0f;
constexpr float kMaxGainDb = 18.0f;
constexpr float kSvfMaxFcRatio = 0.49f;

void resetSvfState(SvfBandState& state)
{
  state = {};
}

float sanitizeFinite(float value, float fallback) noexcept
{
  return std::isfinite(value) ? value : fallback;
}

float clampBandFrequencyHz(float freqHz, double sampleRate) noexcept
{
  const float maxHz = (float)(sampleRate > 0.0 ? sampleRate * (double)kSvfMaxFcRatio : (double)kMaxFreqHz);
  return jlimit(kMinFreqHz, jmin(kMaxFreqHz, maxHz), sanitizeFinite(freqHz, 1000.0f));
}

float clampBandQ(float q) noexcept
{
  return jlimit(kMinQ, kMaxQ, sanitizeFinite(q, 0.707f));
}

float clampBandGainDb(float gainDb) noexcept
{
  return jlimit(kMinGainDb, kMaxGainDb, sanitizeFinite(gainDb, 0.0f));
}
}  // namespace

const StringArray& ParametricEQAudioProcessor::getFilterTypeNames()
{
  static const StringArray names = { "Peaking", "Low Shelf", "High Shelf", "Low Pass", "High Pass", "Band Pass" };
  return names;
}

static double clamp01(double value)
{
  return juce::jlimit(0.0, 1.0, value);
}

static double linearValueToControl(double value, double minValue, double maxValue)
{
  if (!(maxValue > minValue)) return 0.5;
  return clamp01((value - minValue) / (maxValue - minValue));
}

static nx_svf_output_t comboIndexToSvfOutput(int index)
{
  switch (index)
  {
    case 0:  return NX_SVF_OUTPUT_PEAKING;
    case 1:  return NX_SVF_OUTPUT_LOW_SHELF;
    case 2:  return NX_SVF_OUTPUT_HIGH_SHELF;
    case 3:  return NX_SVF_OUTPUT_LP;
    case 4:  return NX_SVF_OUTPUT_HP;
    case 5:  return NX_SVF_OUTPUT_BP;
    default: return NX_SVF_OUTPUT_PEAKING;
  }
}

static nx_svf_config_t makeSvfBandConfig(double freqHz, double q, double gainDb, int typeIdx)
{
  static constexpr double kFreqMin = 20.0;
  static constexpr double kFreqMax = 20000.0;
  static constexpr double kQMin = 0.1;
  static constexpr double kQMax = 10.0;
  static constexpr double kGainMin = -18.0;
  static constexpr double kGainMax = 18.0;

  nx_svf_config_t config;
  nx_svf_config_init(&config);

  config.cutoff.control_params.min_value = kFreqMin;
  config.cutoff.control_params.max_value = kFreqMax;
  config.cutoff.control_params.taper = NX_CONTROL_LINEAR;
  config.cutoff.control_params.control = linearValueToControl(freqHz, kFreqMin, kFreqMax);
  config.cutoff.control_params.value = juce::jlimit(kFreqMin, kFreqMax, freqHz);
  config.cutoff.smoother_params.time_ms = kBandParamSmoothSec * 1000.0;

  config.Q.control_params.min_value = kQMin;
  config.Q.control_params.max_value = kQMax;
  config.Q.control_params.taper = NX_CONTROL_LINEAR;
  config.Q.control_params.control = linearValueToControl(q, kQMin, kQMax);
  config.Q.control_params.value = juce::jlimit(kQMin, kQMax, q);
  config.Q.smoother_params.time_ms = kBandParamSmoothSec * 1000.0;

  config.gain_db.control_params.min_value = kGainMin;
  config.gain_db.control_params.max_value = kGainMax;
  config.gain_db.control_params.taper = NX_CONTROL_LINEAR;
  config.gain_db.control_params.control = linearValueToControl(gainDb, kGainMin, kGainMax);
  config.gain_db.control_params.value = juce::jlimit(kGainMin, kGainMax, gainDb);
  config.gain_db.smoother_params.time_ms = kBandParamSmoothSec * 1000.0;

  config.output = comboIndexToSvfOutput(typeIdx);
  return config;
}

//==============================================================================

ParametricEQAudioProcessor::ParametricEQAudioProcessor()
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
      paramOutputGain(parameters, "Output Gain", "dB", -100.0f, 0.0f, 0.0f),
      paramBypass(parameters, "Bypass", false)
{
  parameters.valueTreeState.state = ValueTree(Identifier(getName().removeCharacters("- ")));

  const auto& typeNames = getFilterTypeNames();
  for (int i = 0; i < numBands; ++i)
    bands[(size_t)i] = std::make_unique<BandParams>(parameters, i, typeNames);

  lastBandTypeIndices.fill(-1);
  lastBandFreqHz.fill(-1.0f);
  lastBandQ.fill(-1.0f);
  lastBandGainDb.fill(std::numeric_limits<float>::quiet_NaN());
  lastBypassState = false;
  resetAllSvfs();
}

int ParametricEQAudioProcessor::getOversampleFactor() const noexcept { return oversampleFactor.load(); }

void ParametricEQAudioProcessor::setOversampleFactor(int factor) noexcept
{
  oversampleFactor.store(juce::jlimit(2, 4, factor));
}

double ParametricEQAudioProcessor::getEffectiveSampleRate() const noexcept
{
  const double baseRate = currentSampleRate > 0.0 ? currentSampleRate : getSampleRate();
  return (baseRate > 0.0 ? baseRate : 48000.0) * (double)oversampleFactor.load();
}

ParametricEQAudioProcessor::~ParametricEQAudioProcessor()
{
  for (auto& channelSvfs : svfChains)
    for (auto& svf : channelSvfs)
      if (svf != nullptr)
        nx_svf_destroy_f32(svf, nullptr);

  for (auto& us : upSamplers)
    if (us != nullptr)
      nx_upsampler_destroy_f32(us, nullptr);

  for (auto& ds : downSamplers)
    if (ds != nullptr)
      nx_downsampler_destroy_f32(ds, nullptr);
}

static void setupOversamplers(nx_upsampler_t* ups[],
                              nx_downsampler_t* downs[],
                              int numChannels,
                              int factor)
{
  for (int ch = 0; ch < numChannels; ++ch)
  {
    if (ups[ch] != nullptr)
    {
      nx_upsampler_set_factor_f32(ups[ch], factor);
      nx_upsampler_set_mode_f32(ups[ch], NX_UPSAMPLER_MODE_CUBIC);
      nx_upsampler_reset_f32(ups[ch]);
    }

    if (downs[ch] != nullptr)
    {
      nx_downsampler_set_factor_f32(downs[ch], factor);
      nx_downsampler_set_mode_f32(downs[ch], NX_DOWNSAMPLER_MODE_CUBIC);
      nx_downsampler_reset_f32(downs[ch]);
    }
  }
}

void ParametricEQAudioProcessor::ensureSvfInstances()
{
  for (auto& channelSvfs : svfChains)
    for (auto& svf : channelSvfs)
      if (svf == nullptr)
        svf = nx_svf_create_f32(nullptr);
}

void ParametricEQAudioProcessor::prepareSvfInstances(double sampleRate)
{
  ensureSvfInstances();

  for (auto& channelSvfs : svfChains)
    for (auto* svf : channelSvfs)
      if (svf != nullptr)
        nx_svf_prepare_f32(svf, sampleRate);
}

//==============================================================================

float ParametricEQAudioProcessor::readParameterValue(const String& paramId, float fallback) const
{
  if (auto* value = parameters.valueTreeState.getRawParameterValue(paramId)) return value->load();

  if (auto* param = parameters.valueTreeState.getParameter(paramId)) return param->convertFrom0to1(param->getValue());

  return fallback;
}

float ParametricEQAudioProcessor::advanceSmoothedValue(PluginParameter& parameter, int numSamples)
{
  float value = parameter.getCurrentValue();
  for (int sample = 0; sample < numSamples; ++sample)
    value = parameter.getNextValue();
  return value;
}

void ParametricEQAudioProcessor::syncParametersFromValueTree()
{
  paramInputGain.setCurrentAndTargetValue(readParameterValue(paramInputGain.paramID, paramInputGain.defaultValue));
  paramOutputGain.setCurrentAndTargetValue(readParameterValue(paramOutputGain.paramID, paramOutputGain.defaultValue));
  paramBypass.setCurrentAndTargetValue(readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState));

  for (int i = 0; i < numBands; ++i)
  {
    auto& b = *bands[(size_t)i];
    b.frequency.setCurrentAndTargetValue(readParameterValue(b.frequency.paramID, b.frequency.defaultValue));
    b.q.setCurrentAndTargetValue(readParameterValue(b.q.paramID, b.q.defaultValue));
    b.gain.setCurrentAndTargetValue(readParameterValue(b.gain.paramID, b.gain.defaultValue));
    b.type.setCurrentAndTargetValue(readParameterValue(b.type.paramID, (float)b.type.defaultChoice));
  }
}

void ParametricEQAudioProcessor::resetAllSvfs()
{
  for (int ch = 0; ch < maxChannels; ++ch)
    for (int b = 0; b < numBands; ++b)
    {
      resetSvfState(svfStates[(size_t)ch][(size_t)b]);

      if (auto* svf = svfChains[(size_t)ch][(size_t)b]; svf != nullptr)
        nx_svf_reset_f32(svf, &svfStates[(size_t)ch][(size_t)b].lp, &svfStates[(size_t)ch][(size_t)b].bp, 1);
    }
}

std::vector<EQCurveComponent::BandConfig> ParametricEQAudioProcessor::getBandConfigs() const
{
  std::vector<EQCurveComponent::BandConfig> configs;
  configs.reserve((size_t)numBands);

  for (int b = 0; b < numBands; ++b)
  {
    auto& band = *bands[(size_t)b];

    const double freqHz = (double)jlimit(20.0f, 20000.0f,
        readParameterValue(band.frequency.paramID, band.frequency.defaultValue));
    const double q = (double)jlimit(0.1f, 10.0f,
        readParameterValue(band.q.paramID, band.q.defaultValue));
    const double gainDb = (double)jlimit(-18.0f, 18.0f,
        readParameterValue(band.gain.paramID, band.gain.defaultValue));
    const int typeIdx = (int)jlimit(0, 5,
        (int)readParameterValue(band.type.paramID, (float)band.type.defaultChoice));

    EQCurveComponent::BandConfig bc;
    bc.config = makeSvfBandConfig(freqHz, q, gainDb, typeIdx);
    configs.push_back(std::move(bc));
  }

  return configs;
}

void ParametricEQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  currentSampleRate = sampleRate;
  lastBandTypeIndices.fill(-1);
  lastBandFreqHz.fill(-1.0f);
  lastBandQ.fill(-1.0f);
  lastBandGainDb.fill(std::numeric_limits<float>::quiet_NaN());
  lastBypassState = false;

  const int osFactor = oversampleFactor.load();
  preparedBlockSize = jmax(1, samplesPerBlock);

  for (int ch = 0; ch < maxChannels; ++ch)
  {
    if (upSamplers[(size_t)ch] == nullptr)
    {
      upSamplers[(size_t)ch] = nx_upsampler_create_f32(nullptr);
      nx_upsampler_set_mode_f32(upSamplers[(size_t)ch], NX_UPSAMPLER_MODE_CUBIC);
    }

    if (downSamplers[(size_t)ch] == nullptr)
    {
      downSamplers[(size_t)ch] = nx_downsampler_create_f32(nullptr);
      nx_downsampler_set_mode_f32(downSamplers[(size_t)ch], NX_DOWNSAMPLER_MODE_CUBIC);
    }
  }

  setupOversamplers(upSamplers.data(), downSamplers.data(), maxChannels, osFactor);
  prepareSvfInstances(sampleRate * (double)osFactor);
  lastOsFactor = osFactor;

  for (auto& ob : oversampleBuffers)
    ob.setSize(1, preparedBlockSize * maxOversampleFactor + 8, false, false, true);

  paramInputGain.reset(sampleRate, kGainParamSmoothSec);
  paramOutputGain.reset(sampleRate, kGainParamSmoothSec);
  paramBypass.reset(sampleRate, 0.0);

  for (int i = 0; i < numBands; ++i)
  {
    auto& b = *bands[(size_t)i];
    b.frequency.reset(sampleRate, kBandParamSmoothSec);
    b.q.reset(sampleRate, kBandParamSmoothSec);
    b.gain.reset(sampleRate, kBandParamSmoothSec);
    b.type.reset(sampleRate, 0.0);
  }

  syncParametersFromValueTree();
  for (int b = 0; b < numBands; ++b)
  {
    auto& band = *bands[(size_t)b];
    const auto initialConfig = makeSvfBandConfig(readParameterValue(band.frequency.paramID, band.frequency.defaultValue),
                                                 readParameterValue(band.q.paramID, band.q.defaultValue),
                                                 readParameterValue(band.gain.paramID, band.gain.defaultValue),
                                                 (int)jlimit(0, 5, (int)readParameterValue(band.type.paramID,
                                                                                           (float)band.type.defaultChoice)));
    for (int ch = 0; ch < maxChannels; ++ch)
      if (auto* svf = svfChains[(size_t)ch][(size_t)b]; svf != nullptr)
        nx_svf_set_config_f32(svf, &initialConfig);
  }
  resetAllSvfs();
}

void ParametricEQAudioProcessor::releaseResources()
{
  preparedBlockSize = 0;
  for (auto& ob : oversampleBuffers)
    ob.setSize(0, 0);
}

void ParametricEQAudioProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
  ignoreUnused(midiMessages);
  ScopedNoDenormals noDenormals;

  const int numInputChannels = getTotalNumInputChannels();
  const int numOutputChannels = getTotalNumOutputChannels();
  const int numSamples = buffer.getNumSamples();

  if (numSamples == 0 || !(currentSampleRate > 0.0)) return;

  const int procChannels = jmin(numInputChannels, numOutputChannels, maxChannels);

  for (int ch = procChannels; ch < numOutputChannels; ++ch)
    buffer.clear(ch, 0, numSamples);

  const bool bypass = readParameterValue(paramBypass.paramID, (float)paramBypass.defaultState) >= 0.5f;
  const int osFactor = oversampleFactor.load();
  const size_t osNumSamples = (size_t)numSamples * (size_t)osFactor;
  const double osSampleRate = currentSampleRate * (double)osFactor;

  if (bypass && !lastBypassState)
    resetAllSvfs();
  lastBypassState = bypass;

  const float inputGainStart = Decibels::decibelsToGain(paramInputGain.getCurrentValue());
  const float inputGainEnd = Decibels::decibelsToGain(advanceSmoothedValue(paramInputGain, numSamples));
  if (std::abs(inputGainStart - 1.0f) > 1.0e-6f || std::abs(inputGainEnd - 1.0f) > 1.0e-6f)
    for (int ch = 0; ch < numInputChannels; ++ch)
      buffer.applyGainRamp(ch, 0, numSamples, inputGainStart, inputGainEnd);

  if (!bypass)
  {
    jassert((int)osNumSamples <= oversampleBuffers[0].getNumSamples());

    if (osFactor != lastOsFactor)
    {
      setupOversamplers(upSamplers.data(), downSamplers.data(), maxChannels, osFactor);
      prepareSvfInstances(osSampleRate);
      resetAllSvfs();
      lastOsFactor = osFactor;
    }

    for (int ch = 0; ch < procChannels; ++ch)
    {
      const float* inData = buffer.getReadPointer(ch);
      float* osBuf = oversampleBuffers[(size_t)ch].getWritePointer(0);

      if (upSamplers[(size_t)ch] != nullptr)
        nx_upsampler_process_f32(upSamplers[(size_t)ch], inData, osBuf, (size_t)numSamples);
      else
        juce::FloatVectorOperations::copy(osBuf, inData, numSamples);
    }

    for (int b = 0; b < numBands; ++b)
    {
      auto& band = *bands[(size_t)b];
      const float bandFreqHz =
          clampBandFrequencyHz(readParameterValue(band.frequency.paramID, band.frequency.defaultValue), osSampleRate);
      const float bandQ = clampBandQ(readParameterValue(band.q.paramID, band.q.defaultValue));
      const float bandGainDb = clampBandGainDb(readParameterValue(band.gain.paramID, band.gain.defaultValue));
      const int bandTypeIdx =
          (int)jlimit(0, 5, (int)readParameterValue(band.type.paramID, (float)band.type.defaultChoice));

      if (lastBandTypeIndices[(size_t)b] != bandTypeIdx)
      {
        lastBandTypeIndices[(size_t)b] = bandTypeIdx;
        for (int resetCh = 0; resetCh < maxChannels; ++resetCh)
          if (auto* svf = svfChains[(size_t)resetCh][(size_t)b]; svf != nullptr)
          {
            nx_svf_set_output_f32(svf, comboIndexToSvfOutput(bandTypeIdx));
            nx_svf_reset_f32(svf, &svfStates[(size_t)resetCh][(size_t)b].lp, &svfStates[(size_t)resetCh][(size_t)b].bp, 1);
          }
      }

      const bool freqChanged = std::abs(bandFreqHz - lastBandFreqHz[(size_t)b]) > 1.0e-6f;
      const bool qChanged = std::abs(bandQ - lastBandQ[(size_t)b]) > 1.0e-6f;
      const bool gainChanged =
          !std::isfinite(lastBandGainDb[(size_t)b]) || std::abs(bandGainDb - lastBandGainDb[(size_t)b]) > 1.0e-6f;

      for (int ch = 0; ch < procChannels; ++ch)
        if (auto* svf = svfChains[(size_t)ch][(size_t)b]; svf != nullptr)
        {
          if (freqChanged)
            nx_svf_set_cutoff_f32(svf, bandFreqHz);
          if (qChanged)
            nx_svf_set_Q_f32(svf, bandQ);
          if (gainChanged)
            nx_svf_set_gain_db_f32(svf, bandGainDb);
          nx_svf_tick_f32(svf, osNumSamples);
        }

      for (int ch = 0; ch < procChannels; ++ch)
      {
        float* osBuf = oversampleBuffers[(size_t)ch].getWritePointer(0);
        if (auto* svf = svfChains[(size_t)ch][(size_t)b]; svf != nullptr)
          nx_svf_process_out_f32(svf,
                                 osBuf,
                                 osBuf,
                                 &svfStates[(size_t)ch][(size_t)b].lp,
                                 &svfStates[(size_t)ch][(size_t)b].bp,
                                 1,
                                 osNumSamples);
      }

      lastBandFreqHz[(size_t)b] = bandFreqHz;
      lastBandQ[(size_t)b] = bandQ;
      lastBandGainDb[(size_t)b] = bandGainDb;
    }

    for (int ch = 0; ch < procChannels; ++ch)
    {
      float* osBuf = oversampleBuffers[(size_t)ch].getWritePointer(0);
      if (downSamplers[(size_t)ch] != nullptr)
        nx_downsampler_process_f32(downSamplers[(size_t)ch], osBuf, buffer.getWritePointer(ch), osNumSamples);
      else
        juce::FloatVectorOperations::copy(buffer.getWritePointer(ch), osBuf, numSamples);
    }

  }
  else
  {
    lastBandTypeIndices.fill(-1);
    lastBandFreqHz.fill(-1.0f);
    lastBandQ.fill(-1.0f);
    lastBandGainDb.fill(std::numeric_limits<float>::quiet_NaN());
  }

  const float outputGainStart = Decibels::decibelsToGain(paramOutputGain.getCurrentValue());
  const float outputGainEnd = Decibels::decibelsToGain(advanceSmoothedValue(paramOutputGain, numSamples));
  if (std::abs(outputGainStart - 1.0f) > 1.0e-6f || std::abs(outputGainEnd - 1.0f) > 1.0e-6f)
    for (int ch = 0; ch < numOutputChannels; ++ch)
      buffer.applyGainRamp(ch, 0, numSamples, outputGainStart, outputGainEnd);

  meterMono.store(0.0f);
  meterLeft.store(0.0f);
  meterRight.store(0.0f);
}

//==============================================================================

void ParametricEQAudioProcessor::getStateInformation(MemoryBlock& destData)
{
  std::unique_ptr<XmlElement> xml(parameters.valueTreeState.state.createXml());
  copyXmlToBinary(*xml, destData);
}

void ParametricEQAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
  if (xmlState != nullptr)
    if (xmlState->hasTagName(parameters.valueTreeState.state.getType()))
      parameters.valueTreeState.state = ValueTree::fromXml(*xmlState);

  lastBandTypeIndices.fill(-1);
  lastBandFreqHz.fill(-1.0f);
  lastBandQ.fill(-1.0f);
  lastBandGainDb.fill(std::numeric_limits<float>::quiet_NaN());
  syncParametersFromValueTree();
  resetAllSvfs();
}

//==============================================================================

bool ParametricEQAudioProcessor::hasEditor() const { return true; }

AudioProcessorEditor* ParametricEQAudioProcessor::createEditor() { return new ParametricEQAudioProcessorEditor(*this); }

//==============================================================================

#ifndef JucePlugin_PreferredChannelConfigurations
bool ParametricEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

const String ParametricEQAudioProcessor::getName() const { return JucePlugin_Name; }

bool ParametricEQAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool ParametricEQAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool ParametricEQAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double ParametricEQAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ParametricEQAudioProcessor::getNumPrograms() { return 1; }

int ParametricEQAudioProcessor::getCurrentProgram() { return 0; }

void ParametricEQAudioProcessor::setCurrentProgram(int index) { ignoreUnused(index); }

const String ParametricEQAudioProcessor::getProgramName(int index)
{
  ignoreUnused(index);
  return {};
}

void ParametricEQAudioProcessor::changeProgramName(int index, const String& newName) { ignoreUnused(index, newName); }

//==============================================================================

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ParametricEQAudioProcessor(); }

//==============================================================================
