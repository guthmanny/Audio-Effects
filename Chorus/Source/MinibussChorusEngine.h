#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "minibuss/audio_engine.hpp"
#include "minibuss/parameter.hpp"
#include "minibuss/plugin_format_manager.hpp"
#include "minibuss/static_plugin_format.hpp"

/** Owns a minibuss AudioEngine with one stereo track:
    Gain → NoiseGate → MonoChorus → Phase90 → Level
    Exactly one of Chorus/Phase90 is active based on EffectModel. */
class MinibussChorusEngine
{
public:
    enum class EffectModel
    {
        Chorus = 0,
        Phase90 = 1
    };

    MinibussChorusEngine();
    ~MinibussChorusEngine();

    void prepare (float sampleRate, std::uint32_t maxBlockSize);
    void release();

    void setEffectModel (EffectModel model);
    EffectModel getEffectModel() const noexcept { return model_; }

    void setBypass (bool shouldBypass);

    /** Push domain value to a processor parameter (uses minibuss reflection). */
    void setParamDomain (minibuss::ObjectId processorId, std::string_view paramId, float domainValue);

    [[nodiscard]] const minibuss::ParameterDescriptor* paramDescriptor (
        minibuss::ObjectId processorId, std::string_view paramId) const;

    void process (std::span<const float* const> inputs,
                  std::span<float* const> outputs,
                  std::uint32_t numFrames);

    bool isReady() const noexcept { return ready_; }

    minibuss::ObjectId gainId() const noexcept { return gainId_; }
    minibuss::ObjectId gateId() const noexcept { return gateId_; }
    minibuss::ObjectId chorusId() const noexcept { return chorusId_; }
    minibuss::ObjectId phase90Id() const noexcept { return phase90Id_; }
    minibuss::ObjectId levelId() const noexcept { return levelId_; }

private:
    [[nodiscard]] minibuss::PluginDescription makeDesc (const char* uid,
                                                        const char* name,
                                                        std::uint32_t io = 2) const;
    void applyModelBypass();
    [[nodiscard]] minibuss::Processor* processor (minibuss::ObjectId id) const;

    minibuss::PluginFormatManager formats_;
    std::unique_ptr<minibuss::AudioEngine> engine_;

    minibuss::ObjectId trackId_ = minibuss::kInvalidObjectId;
    minibuss::ObjectId gainId_ = minibuss::kInvalidObjectId;
    minibuss::ObjectId gateId_ = minibuss::kInvalidObjectId;
    minibuss::ObjectId chorusId_ = minibuss::kInvalidObjectId;
    minibuss::ObjectId phase90Id_ = minibuss::kInvalidObjectId;
    minibuss::ObjectId levelId_ = minibuss::kInvalidObjectId;

    EffectModel model_ = EffectModel::Chorus;
    bool ready_ = false;
    bool bypassAll_ = false;
};
