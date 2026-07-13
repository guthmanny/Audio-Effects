#include "Phase90Processor.h"

#include <algorithm>
#include <cassert>
#include <chrono>

#include "minibuss/parameter.hpp"
#include "minibuss/version.hpp"

namespace chorus_plugin
{

Phase90Processor::Phase90Processor (minibuss::HostContext* host)
    : host_ (host), description_ (make_description())
{
    set_max_channels (2, 2);
    set_channels (2, 2);

    nx_phase90_config_t cfg {};
    assert (nudsp::camel::Phase90F32::configInit (&cfg) == NX_SUCCESS);
    assert (register_parameter (minibuss::make_parameter_from_control ("rate", "Rate", cfg.rate)) ==
            minibuss::Status::Ok);
    assert (register_parameter (minibuss::make_parameter_from_control ("center", "Center", cfg.center)) ==
            minibuss::Status::Ok);
    assert (register_parameter (minibuss::make_parameter_from_control ("amount", "Amount", cfg.amount)) ==
            minibuss::Status::Ok);
    assert (register_parameter (minibuss::make_parameter_from_control ("feedback", "Feedback", cfg.feedback)) ==
            minibuss::Status::Ok);
    assert (register_parameter (minibuss::make_parameter_from_control ("mix", "Mix", cfg.mix)) ==
            minibuss::Status::Ok);

    for (std::size_t i = 0; i < params_norm_.size(); ++i)
    {
        if (const auto* desc = parameter_by_index (static_cast<std::uint32_t> (i)))
            params_norm_[i] = desc->default_normalized();
    }
}

minibuss::PluginDescription Phase90Processor::make_description()
{
    minibuss::PluginDescription d;
    d.uid = "com.chorus.nudsp.camel.phase90";
    d.name = "Phase90";
    d.vendor = "Chorus / NuDSP";
    d.version = "1.0.0";
    d.category = "Effect";
    d.format_name = minibuss::kPluginFormatNameStatic;
    d.file_or_identifier = d.uid;
    d.num_inputs = 2;
    d.num_outputs = 2;
    d.last_info_update_time = std::chrono::system_clock::now();
    return d;
}

void Phase90Processor::apply_cached_params()
{
    if (! dsp_)
        return;

    const auto* rate = parameter ("rate");
    const auto* center = parameter ("center");
    const auto* amount = parameter ("amount");
    const auto* feedback = parameter ("feedback");
    const auto* mix = parameter ("mix");
    if (! rate || ! center || ! amount || ! feedback || ! mix)
        return;

    dsp_->setRate (rate->denormalize (params_norm_[0]));
    dsp_->setCenter (center->denormalize (params_norm_[1]));
    dsp_->setAmount (amount->denormalize (params_norm_[2]));
    dsp_->setFeedback (feedback->denormalize (params_norm_[3]));
    dsp_->setMix (mix->denormalize (params_norm_[4]));
    dsp_->setBypass (bypassed());
}

minibuss::Status Phase90Processor::init (float sample_rate, std::uint32_t)
{
    sample_rate_ = sample_rate;
    dsp_.emplace();
    if (dsp_->getRawPointer() == nullptr)
    {
        dsp_.reset();
        return minibuss::Status::InitFailed;
    }

    apply_cached_params();
    dsp_->prepare (sample_rate_);
    dsp_->tick (1);

    if (host_ != nullptr)
        host_->log (minibuss::HostContext::LogLevel::Info, "Phase90Processor init (NuDSP camel)");

    return minibuss::Status::Ok;
}

void Phase90Processor::prepare (float sample_rate, std::uint32_t)
{
    sample_rate_ = sample_rate;
    if (dsp_)
        dsp_->prepare (sample_rate_);
}

void Phase90Processor::reset()
{
    if (dsp_)
        dsp_->reset();
}

void Phase90Processor::process (const minibuss::ProcessContext&,
                                std::span<const float* const> inputs,
                                std::span<float* const> outputs,
                                std::uint32_t num_frames)
{
    if (! dsp_ || inputs.empty() || outputs.empty())
        return;

    dsp_->setBypass (bypassed());
    dsp_->tick (num_frames);

    const std::size_t channels = std::min ({ inputs.size(), outputs.size(), static_cast<std::size_t> (2) });
    if (channels == 0)
        return;

    const float* in_ptrs[2] = { inputs[0], channels > 1 ? inputs[1] : inputs[0] };
    float* out_ptrs[2] = { outputs[0], channels > 1 ? outputs[1] : outputs[0] };
    if (in_ptrs[0] == nullptr || out_ptrs[0] == nullptr)
        return;
    if (channels > 1 && (in_ptrs[1] == nullptr || out_ptrs[1] == nullptr))
        return;

    dsp_->processMulti (in_ptrs, out_ptrs, num_frames, channels);
}

minibuss::Status Phase90Processor::set_parameter (std::uint32_t index, float normalized)
{
    const auto* desc = parameter_by_index (index);
    if (desc == nullptr)
        return minibuss::Status::ParameterError;

    params_norm_[index] = std::clamp (normalized, 0.f, 1.f);
    if (! dsp_)
        return minibuss::Status::Ok;

    const float value = desc->denormalize (params_norm_[index]);
    nx_result_t rc = NX_SUCCESS;
    switch (static_cast<Param> (index))
    {
        case Param::Rate:     rc = dsp_->setRate (value); break;
        case Param::Center:   rc = dsp_->setCenter (value); break;
        case Param::Amount:   rc = dsp_->setAmount (value); break;
        case Param::Feedback: rc = dsp_->setFeedback (value); break;
        case Param::Mix:      rc = dsp_->setMix (value); break;
    }
    return rc == NX_SUCCESS ? minibuss::Status::Ok : minibuss::Status::ParameterError;
}

minibuss::Status Phase90Processor::get_parameter (std::uint32_t index, float& out_normalized) const
{
    if (parameter_by_index (index) == nullptr)
        return minibuss::Status::ParameterError;
    out_normalized = params_norm_[index];
    return minibuss::Status::Ok;
}

} // namespace chorus_plugin
