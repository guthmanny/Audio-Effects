#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "minibuss/plugin_description.hpp"
#include "minibuss/processor.hpp"
#include "nudsp/amplitude/dry_wet.hpp"
#include "nudsp/extensions/camel/chorus.hpp"

namespace chorus_plugin
{

/** Mono-sum → ChorusF32 (coeff_x=0, coeff_mod=1) → per-channel DryWet. */
class MonoChorusProcessor final : public minibuss::Processor
{
public:
    explicit MonoChorusProcessor (minibuss::HostContext* host = nullptr);

    [[nodiscard]] static minibuss::PluginDescription make_description();

    [[nodiscard]] minibuss::Status init (float sample_rate, std::uint32_t max_block_size) override;
    void prepare (float sample_rate, std::uint32_t max_block_size) override;
    void reset() override;

    void process (const minibuss::ProcessContext& ctx,
                  std::span<const float* const> inputs,
                  std::span<float* const> outputs,
                  std::uint32_t num_frames) override;

    [[nodiscard]] minibuss::Status set_parameter (std::uint32_t index, float normalized) override;
    [[nodiscard]] minibuss::Status get_parameter (std::uint32_t index, float& out_normalized) const override;

    [[nodiscard]] const minibuss::PluginDescription& description() const override { return description_; }

private:
    enum class Param : std::uint32_t
    {
        Rate = 0,
        Delay,
        Amount,
        CoeffFb,
        Wet,
    };

    void apply_cached_params();
    void ensure_scratch (std::uint32_t num_frames);

    minibuss::HostContext* host_ = nullptr;
    minibuss::PluginDescription description_;
    std::optional<nudsp::camel::ChorusF32> chorus_;
    std::array<std::optional<nudsp::DryWetF32>, 2> dryWets_;
    std::array<float, 5> params_norm_ {};
    std::vector<float> monoIn_;
    std::vector<float> monoWet_;
    float sample_rate_ = 48000.f;
};

} // namespace chorus_plugin
