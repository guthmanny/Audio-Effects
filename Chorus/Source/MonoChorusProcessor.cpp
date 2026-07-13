#include "MonoChorusProcessor.h"

#include <algorithm>
#include <cassert>
#include <chrono>

#include "minibuss/parameter.hpp"
#include "minibuss/version.hpp"
#include "nudsp/amplitude/dry_wet_f32.h"

namespace chorus_plugin
{

MonoChorusProcessor::MonoChorusProcessor (minibuss::HostContext* host)
    : host_ (host), description_ (make_description())
{
    set_max_channels (2, 2);
    set_channels (2, 2);

    nx_chorus_config_t cfg {};
    assert (nudsp::camel::ChorusF32::configInit (&cfg) == NX_SUCCESS);
    assert (register_parameter (minibuss::make_parameter_from_control ("rate", "Rate", cfg.rate)) ==
            minibuss::Status::Ok);
    assert (register_parameter (minibuss::make_parameter_from_control ("delay", "Delay", cfg.delay)) ==
            minibuss::Status::Ok);
    assert (register_parameter (minibuss::make_parameter_from_control ("amount", "Amount", cfg.amount)) ==
            minibuss::Status::Ok);
    assert (register_parameter (minibuss::make_parameter_from_control ("coeff_fb", "Feedback", cfg.coeff_fb)) ==
            minibuss::Status::Ok);
    assert (register_parameter (minibuss::make_float_parameter ("wet", "Wet", "", 0.f, 1.f, 0.5f)) ==
            minibuss::Status::Ok);

    for (std::size_t i = 0; i < params_norm_.size(); ++i)
    {
        if (const auto* desc = parameter_by_index (static_cast<std::uint32_t> (i)))
            params_norm_[i] = desc->default_normalized();
    }
}

minibuss::PluginDescription MonoChorusProcessor::make_description()
{
    minibuss::PluginDescription d;
    d.uid = "com.chorus.nudsp.camel.mono_chorus";
    d.name = "Mono Chorus";
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

void MonoChorusProcessor::ensure_scratch (std::uint32_t num_frames)
{
    if (monoIn_.size() < num_frames)
    {
        monoIn_.assign (num_frames, 0.f);
        monoWet_.assign (num_frames, 0.f);
    }
}

void MonoChorusProcessor::apply_cached_params()
{
    if (! chorus_)
        return;

    const auto* rate = parameter ("rate");
    const auto* delay = parameter ("delay");
    const auto* amount = parameter ("amount");
    const auto* coeff_fb = parameter ("coeff_fb");
    const auto* wet = parameter ("wet");
    if (! rate || ! delay || ! amount || ! coeff_fb || ! wet)
        return;

    chorus_->setRate (rate->denormalize (params_norm_[0]));
    chorus_->setDelay (delay->denormalize (params_norm_[1]));
    chorus_->setAmount (amount->denormalize (params_norm_[2]));
    chorus_->setCoeffX (0.0);
    chorus_->setCoeffMod (1.0);
    chorus_->setCoeffFb (coeff_fb->denormalize (params_norm_[3]));
    chorus_->setBypass (bypassed());

    const float wetVal = wet->denormalize (params_norm_[4]);
    for (auto& dw : dryWets_)
    {
        if (! dw)
            continue;
        dw->setWet (wetVal);
        dw->setBypass (bypassed());
    }
}

minibuss::Status MonoChorusProcessor::init (float sample_rate, std::uint32_t max_block_size)
{
    sample_rate_ = sample_rate;
    ensure_scratch (max_block_size);

    chorus_.emplace();
    if (chorus_->getRawPointer() == nullptr)
    {
        chorus_.reset();
        return minibuss::Status::InitFailed;
    }

    for (auto& dw : dryWets_)
    {
        dw.emplace();
        if (dw->getRawPointer() == nullptr)
        {
            chorus_.reset();
            for (auto& d : dryWets_)
                d.reset();
            return minibuss::Status::InitFailed;
        }
        dw->setSmoothMode (NX_SMOOTH_EXPONENTIAL, sample_rate_, 10.0f);
        nx_dry_wet_tick_f32 (dw->getRawPointer(), 1);
    }

    apply_cached_params();
    chorus_->prepare (sample_rate_);
    chorus_->reset();
    chorus_->tick (1);

    if (host_ != nullptr)
        host_->log (minibuss::HostContext::LogLevel::Info, "MonoChorusProcessor init");

    return minibuss::Status::Ok;
}

void MonoChorusProcessor::prepare (float sample_rate, std::uint32_t max_block_size)
{
    sample_rate_ = sample_rate;
    ensure_scratch (max_block_size);
    if (chorus_)
        chorus_->prepare (sample_rate_);
    for (auto& dw : dryWets_)
    {
        if (dw)
            dw->setSmoothMode (NX_SMOOTH_EXPONENTIAL, sample_rate_, 10.0f);
    }
}

void MonoChorusProcessor::reset()
{
    if (chorus_)
        chorus_->reset();
}

void MonoChorusProcessor::process (const minibuss::ProcessContext&,
                                   std::span<const float* const> inputs,
                                   std::span<float* const> outputs,
                                   std::uint32_t num_frames)
{
    if (! chorus_ || inputs.empty() || outputs.empty() || num_frames == 0)
        return;

    const std::size_t channels = std::min ({ inputs.size(), outputs.size(), static_cast<std::size_t> (2) });
    if (channels == 0 || inputs[0] == nullptr || outputs[0] == nullptr)
        return;

    if (bypassed())
    {
        for (std::size_t ch = 0; ch < channels; ++ch)
        {
            if (inputs[ch] != nullptr && outputs[ch] != nullptr)
                std::copy_n (inputs[ch], num_frames, outputs[ch]);
        }
        return;
    }

    ensure_scratch (num_frames);

    if (channels >= 2 && inputs[1] != nullptr)
    {
        for (std::uint32_t i = 0; i < num_frames; ++i)
            monoIn_[i] = 0.5f * (inputs[0][i] + inputs[1][i]);
    }
    else
    {
        std::copy_n (inputs[0], num_frames, monoIn_.data());
    }

    chorus_->setBypass (false);
    chorus_->tick (num_frames);
    chorus_->process (monoIn_.data(), monoWet_.data(), num_frames);

    for (std::size_t ch = 0; ch < channels; ++ch)
    {
        if (outputs[ch] == nullptr || ! dryWets_[ch])
            continue;

        if (auto* raw = dryWets_[ch]->getRawPointer())
            nx_dry_wet_tick_f32 (raw, num_frames);

        const float* dry = (ch < inputs.size() && inputs[ch] != nullptr) ? inputs[ch] : inputs[0];
        dryWets_[ch]->process (dry, monoWet_.data(), outputs[ch], num_frames);
    }
}

minibuss::Status MonoChorusProcessor::set_parameter (std::uint32_t index, float normalized)
{
    if (parameter_by_index (index) == nullptr)
        return minibuss::Status::ParameterError;

    params_norm_[index] = std::clamp (normalized, 0.f, 1.f);
    apply_cached_params();
    if (chorus_)
        chorus_->tick (1);
    for (auto& dw : dryWets_)
    {
        if (dw && dw->getRawPointer())
            nx_dry_wet_tick_f32 (dw->getRawPointer(), 1);
    }
    return minibuss::Status::Ok;
}

minibuss::Status MonoChorusProcessor::get_parameter (std::uint32_t index, float& out_normalized) const
{
    if (parameter_by_index (index) == nullptr)
        return minibuss::Status::ParameterError;
    out_normalized = params_norm_[index];
    return minibuss::Status::Ok;
}

} // namespace chorus_plugin
