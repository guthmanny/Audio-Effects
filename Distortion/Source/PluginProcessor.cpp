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
    , paramTone (parameters, "Tone", "", 0.0f, 1.0f, 0.5f)
    , paramLevel (parameters, "Level", "%", 0.0f, 100.0f, 100.0f)
    , paramBypass (parameters, "Bypass", false)
{
    parameters.valueTreeState.state = ValueTree (Identifier (getName().removeCharacters ("- ")));
}

int DistortionAudioProcessor::getOversampleFactor() const noexcept { return oversampleFactor.load(); }
void DistortionAudioProcessor::setOversampleFactor (int factor) noexcept { oversampleFactor.store (factor); }
DistortionAudioProcessor::DistortionModel DistortionAudioProcessor::getDistortionModel() const noexcept { return currentModel.load(); }
void DistortionAudioProcessor::setDistortionModel (DistortionModel model) noexcept { currentModel.store (model); }
float DistortionAudioProcessor::getCpuLoadPeak() noexcept { const float p = cpuLoadPeak.exchange (0.0f); return p; }

DistortionAudioProcessor::~DistortionAudioProcessor()
{
    for (auto& us : upSamplers)
        if (us) nx_upsampler_destroy_f32 (us, nullptr);
    for (auto& ds : downSamplers)
        if (ds) nx_downsampler_destroy_f32 (ds, nullptr);
}

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
    paramTone.setCurrentAndTargetValue (readParameterValue (paramTone.paramID, paramTone.defaultValue));
    paramLevel.setCurrentAndTargetValue (readParameterValue (paramLevel.paramID, paramLevel.defaultValue));
    paramBypass.setCurrentAndTargetValue (readParameterValue (paramBypass.paramID, (float) paramBypass.defaultState));
}

void DistortionAudioProcessor::ensureEffectInstances()
{
    static const double c15TaperTable[33] =
    {
        0.00000, 0.33000, 0.38000, 0.42800, 0.47400, 0.51800, 0.56000,
        0.60000, 0.63800, 0.67400, 0.70800, 0.74000, 0.77000, 0.79800,
        0.82400, 0.84800, 0.87000, 0.89000, 0.90800, 0.92400, 0.93800,
        0.95000, 0.96000, 0.96800, 0.97500, 0.98100, 0.98600, 0.99000,
        0.99300, 0.99550, 0.99750, 0.99900, 1.00000
    };

    for (auto& dist : distortionPlusChains)
    {
        if (dist) continue;
        auto d = std::make_unique<nudsp::white_box::DistortionPlusF32>();
        if (auto* raw = d->getRawPointer())
        {
            nx_distortion_plus_config_t config;
            if (nx_distortion_plus_get_config_f32 (raw, &config) == NX_SUCCESS)
            {
                config.opamp.distortion.pot_params.taper = NX_POT_TAPER_TABLE;
                config.opamp.distortion.pot_params.table = c15TaperTable;
                config.opamp.distortion.pot_params.table_size = 33;
                nx_distortion_plus_set_config_f32 (raw, &config);
            }
        }
        dist = std::move (d);
    }
    for (auto& ts : ts9Chains) if (!ts) ts = std::make_unique<nudsp::white_box::Ts9F32>();
    if (!outputGain) outputGain = std::make_unique<nudsp::GainF32>();
}

void DistortionAudioProcessor::updateEffectParameters()
{
    const double distortion = jlimit (0.0, 1.0, (double) readParameterValue (paramDistortion.paramID, paramDistortion.defaultValue));
    const double tone = jlimit (0.0, 1.0, (double) readParameterValue (paramTone.paramID, paramTone.defaultValue));
    const bool bypass = readParameterValue (paramBypass.paramID, (float) paramBypass.defaultState) >= 0.5f;
    const double level = jlimit (0.0, 1.0, (double) readParameterValue (paramLevel.paramID, paramLevel.defaultValue) / 100.0);

    for (auto& dist : distortionPlusChains) if (dist) { dist->setOpampDistortionControl (distortion); dist->setLevelControl (level); dist->setBypass (bypass); }
    for (auto& ts : ts9Chains) if (ts) { ts->setDriveControl (distortion); ts->setToneControl (tone); ts->setLevelControl (level); ts->setBypass (bypass); }
}

static void setupOversamplers (nx_upsampler_t* ups[], nx_downsampler_t* downs[], int maxChannels, int factor)
{
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        if (ups[ch]) { nx_upsampler_set_factor_f32 (ups[ch], factor); nx_upsampler_set_mode_f32 (ups[ch], NX_UPSAMPLER_MODE_CUBIC); nx_upsampler_reset_f32 (ups[ch]); }
        if (downs[ch]) { nx_downsampler_set_factor_f32 (downs[ch], factor); nx_downsampler_set_mode_f32 (downs[ch], NX_DOWNSAMPLER_MODE_CUBIC); nx_downsampler_reset_f32 (downs[ch]); }
    }
}

void DistortionAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    gateEnvelope = {};
    gateGain = { 1.0f, 1.0f };

    const int osFactor = oversampleFactor.load();

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        if (!upSamplers[ch]) { upSamplers[ch] = nx_upsampler_create_f32 (nullptr); nx_upsampler_set_mode_f32 (upSamplers[ch], NX_UPSAMPLER_MODE_CUBIC); }
        if (!downSamplers[ch]) { downSamplers[ch] = nx_downsampler_create_f32 (nullptr); nx_downsampler_set_mode_f32 (downSamplers[ch], NX_DOWNSAMPLER_MODE_CUBIC); }
    }
    setupOversamplers (upSamplers.data(), downSamplers.data(), maxChannels, osFactor);
    lastOsFactor = osFactor;

    const double smoothTime = 1e-3;
    paramInputGain.reset (sampleRate, smoothTime);
    paramGateThreshold.reset (sampleRate, smoothTime);
    paramOutputGain.reset (sampleRate, smoothTime);
    paramDistortion.reset (sampleRate, smoothTime);
    paramTone.reset (sampleRate, smoothTime);
    paramLevel.reset (sampleRate, smoothTime);
    paramBypass.reset (sampleRate, smoothTime);

    syncParametersFromValueTree();
    ensureEffectInstances();

    for (auto& dist : distortionPlusChains) { dist->prepare (sampleRate * osFactor); dist->reset(); dist->tick (1); }
    for (auto& ts : ts9Chains) { ts->prepare (sampleRate * osFactor); ts->reset(); ts->tick (1); }

    if (outputGain)
    {
        nx_gain_config_t gc; nx_gain_config_init (&gc);
        gc.gain.control_params.value = (double) paramOutputGain.defaultValue;
        if (auto* raw = outputGain->getRawPointer()) nx_gain_set_config_f32 (raw, &gc);
        outputGain->setSmoothMode (NX_SMOOTH_EXPONENTIAL, (float) sampleRate, 10.0f);
        nx_gain_tick_f32 (outputGain->getRawPointer(), 1);
    }

    const int numCh = jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());
    outputBuffer.setSize (numCh, samplesPerBlock);
    for (auto& ob : oversampleBuffers) ob.setSize (1, samplesPerBlock * osFactor);

    updateEffectParameters();
    for (auto& dist : distortionPlusChains) dist->tick (1);
    for (auto& ts : ts9Chains) ts->tick (1);
}

void DistortionAudioProcessor::releaseResources()
{
    outputBuffer.setSize (0, 0);
    for (auto& ob : oversampleBuffers) ob.setSize (0, 0);
    gateEnvelope = {}; gateGain = { 1.0f, 1.0f };
}

void DistortionAudioProcessor::processInputGain (AudioSampleBuffer& buffer, int numCh, int numSamples, float gainDb)
{
    const float gain = Decibels::decibelsToGain (gainDb);
    for (int ch = 0; ch < numCh; ++ch) buffer.applyGain (ch, 0, numSamples, gain);
}

void DistortionAudioProcessor::processGate (AudioSampleBuffer& buffer, int numCh, int numSamples, float thrDb)
{
    const float envR = float (exp (-1.0 / (0.050 * currentSampleRate)));
    const float gA = float (exp (-1.0 / (0.002 * currentSampleRate)));
    const float gR = float (exp (-1.0 / (0.050 * currentSampleRate)));
    const int chs = jmin (numCh, maxChannels);
    for (int ch = 0; ch < chs; ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        float e = gateEnvelope[(size_t) ch];
        float g = gateGain[(size_t) ch];
        for (int i = 0; i < numSamples; ++i)
        {
            float a = fabsf (d[i]);
            e = jmax (a, e * envR);
            float l = Decibels::gainToDecibels (e + 1.0e-10f);
            float tg = l >= thrDb ? 1.0f : 0.0f;
            float sc = tg > g ? gA : gR;
            g = tg + sc * (g - tg);
            d[i] *= g;
        }
        gateEnvelope[(size_t) ch] = e;
        gateGain[(size_t) ch] = g;
    }
}

void DistortionAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    ignoreUnused (midiMessages);
    ScopedNoDenormals noDenormals;
    const auto blockStart = std::chrono::high_resolution_clock::now();

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamp = buffer.getNumSamples();
    if (numSamp == 0) return;

    if (outputBuffer.getNumSamples() < numSamp)
    {
        const int numCh = jmax (numIn, numOut);
        outputBuffer.setSize (numCh, numSamp, false, false, true);
    }

    ensureEffectInstances();
    updateEffectParameters();

    const float inputGainDb = readParameterValue (paramInputGain.paramID, paramInputGain.defaultValue);
    const float gateThrDb = readParameterValue (paramGateThreshold.paramID, paramGateThreshold.defaultValue);
    const float outGainDb = readParameterValue (paramOutputGain.paramID, paramOutputGain.defaultValue);

    processInputGain (buffer, numIn, numSamp, inputGainDb);
    processGate (buffer, jmin (numIn, numOut), numSamp, gateThrDb);

    const int procCh = jmin (numOut, maxChannels);
    if (numIn > 0 && numIn < procCh)
        for (int ch = numIn; ch < procCh; ++ch)
            buffer.copyFrom (ch, 0, buffer, numIn - 1, 0, numSamp);
    else if (procCh >= 2 && numIn >= 2)
    {
        const float lPk = buffer.getMagnitude (0, 0, numSamp);
        const float rPk = buffer.getMagnitude (1, 0, numSamp);
        if (lPk > 1.0e-6f && rPk <= 1.0e-6f)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamp);
    }

    const auto model = currentModel.load();
    const bool useTs9 = (model == kTs9);
    const int osFactor = oversampleFactor.load();
    const size_t fs = (size_t) numSamp;
    const size_t osFs = fs * (size_t) osFactor;

    for (auto& ob : oversampleBuffers)
        if (ob.getNumSamples() < (int) osFs)
            ob.setSize (1, (int) osFs + 4, false, false, true);

    if (osFactor != lastOsFactor)
    {
        setupOversamplers (upSamplers.data(), downSamplers.data(), maxChannels, osFactor);
        lastOsFactor = osFactor;
    }

    for (int ch = 0; ch < procCh; ++ch)
    {
        const float* inData = buffer.getReadPointer (ch);
        float* osBuf = oversampleBuffers[(size_t) ch].getWritePointer (0);
        float* effectOut = outputBuffer.getWritePointer (ch);

        // Upsample
        if (upSamplers[(size_t) ch])
            nx_upsampler_process_f32 (upSamplers[(size_t) ch], inData, osBuf, fs);
        else
            FloatVectorOperations::copy (osBuf, inData, numSamp);

        // Process at oversampled rate
        if (useTs9)
        {
            auto& ts = ts9Chains[(size_t) ch];
            if (ts) { ts->tick (osFs); ts->process (osBuf, osBuf, osFs); }
            else FloatVectorOperations::copy (effectOut, osBuf, (int) osFs);
        }
        else
        {
            auto& dist = distortionPlusChains[(size_t) ch];
            if (dist) { dist->tick (osFs); dist->process (osBuf, osBuf, osFs); }
            else FloatVectorOperations::copy (effectOut, osBuf, (int) osFs);
        }

        // Downsample
        if (downSamplers[(size_t) ch])
            nx_downsampler_process_f32 (downSamplers[(size_t) ch], osBuf, effectOut, osFs);
        else
            FloatVectorOperations::copy (effectOut, osBuf, numSamp);
    }

    for (int ch = procCh; ch < numOut; ++ch) outputBuffer.clear (ch, 0, numSamp);
    processOutputGain (outputBuffer, buffer, numOut, numSamp, outGainDb);

    float lPk = 0.0f, rPk = 0.0f;
    if (numOut > 0) lPk = buffer.getMagnitude (0, 0, numSamp);
    if (numOut > 1) rPk = buffer.getMagnitude (1, 0, numSamp);
    meterMono.store (numOut > 1 ? 0.5f * (lPk + rPk) : lPk);
    meterLeft.store (lPk);
    meterRight.store (rPk);

    const auto blockEnd = std::chrono::high_resolution_clock::now();
    const double el = std::chrono::duration<double> (blockEnd - blockStart).count();
    const double bufSec = (double) numSamp / currentSampleRate;
    const float lv = (float) jlimit (0.0, 1.0, (bufSec > 0.0 ? el / bufSec : 0.0));
    cpuLoad.store (lv);
    if (lv > cpuLoadPeak.load()) cpuLoadPeak.store (lv);
}

void DistortionAudioProcessor::processOutputGain (const AudioSampleBuffer& inBuffer, AudioSampleBuffer& outBuffer,
                                                  int numCh, int numSamp, float gainDb)
{
    if (!outputGain || numCh <= 0 || numSamp <= 0) return;
    const auto frm = (size_t) numSamp;
    const auto chs = (size_t) jmin (numCh, maxChannels);
    const float* in[maxChannels] {}; float* out[maxChannels] {};
    for (int ch = 0; ch < (int) chs; ++ch) { in[(size_t) ch] = inBuffer.getReadPointer (ch); out[(size_t) ch] = outBuffer.getWritePointer (ch); }
    outputGain->setGainDb ((double) gainDb);
    if (auto* raw = outputGain->getRawPointer()) nx_gain_tick_f32 (raw, frm);
    outputGain->processMulti (in, out, frm, chs);
}

void DistortionAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    std::unique_ptr<XmlElement> xml (parameters.valueTreeState.state.createXml());
    copyXmlToBinary (*xml, destData);
}
void DistortionAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState && xmlState->hasTagName (parameters.valueTreeState.state.getType()))
        parameters.valueTreeState.state = ValueTree::fromXml (*xmlState);
}
bool DistortionAudioProcessor::hasEditor() const { return true; }
AudioProcessorEditor* DistortionAudioProcessor::createEditor() { return new DistortionAudioProcessorEditor (*this); }

#ifndef JucePlugin_PreferredChannelConfigurations
bool DistortionAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    ignoreUnused (layouts); return true;
  #else
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono() && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo()) return false;
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet()) return false;
   #endif
    return true;
  #endif
}
#endif

const String DistortionAudioProcessor::getName() const { return JucePlugin_Name; }
bool DistortionAudioProcessor::acceptsMidi() const { return false; }
bool DistortionAudioProcessor::producesMidi() const { return false; }
bool DistortionAudioProcessor::isMidiEffect() const { return false; }
double DistortionAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int DistortionAudioProcessor::getNumPrograms() { return 1; }
int DistortionAudioProcessor::getCurrentProgram() { return 0; }
void DistortionAudioProcessor::setCurrentProgram (int) {}
const String DistortionAudioProcessor::getProgramName (int) { return {}; }
void DistortionAudioProcessor::changeProgramName (int, const String&) {}
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DistortionAudioProcessor(); }
