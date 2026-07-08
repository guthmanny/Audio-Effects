#pragma once

#include <vector>

#include "../JuceLibraryCode/JuceHeader.h"
#include "nudsp/filters/svf_f32.h"

//==============================================================================
/**
    EQ 频响曲线绘制组件。
    使用 nx_svf_ac_static_f32 计算每个频段的频率响应，
    求和得到总响应，并绘制在 log-frequency 坐标轴上。
 */
class EQCurveComponent final : public juce::Component
{
public:
    /** 单个频段的配置，用于 AC 分析 */
    struct BandConfig
    {
        nx_svf_config_t config;
    };

    EQCurveComponent();
    ~EQCurveComponent() override = default;

    /** 设置当前所有频段的配置，触发曲线重绘 */
    void setBandConfigs (const std::vector<BandConfig>& configs, double sampleRate);

    /** 设置频段频率/Q 值，用于在曲线上绘制标记 */
    void setBandMarkers (const std::vector<std::pair<float, float>>& freqQ);

    /** 设置参考电平（0dB 参考线位置） */
    void setReferenceLevel (float refDb) { referenceDb = refDb; repaint(); }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    /** 计算并缓存频响曲线 */
    void rebuildCurve();

    /** 计算指定频率处的总响应，用于精确放置标记点 */
    float evaluateTotalGainAtFrequency (float freqHz) const;

    /** 从组件坐标映射到频率/增益坐标 */
    float xToFreq (float x) const noexcept;
    float yToGain (float y) const noexcept;
    float freqToX (float freqHz) const noexcept;
    float gainToY (float gainDb) const noexcept;

    //==============================================================================
    std::vector<BandConfig> bandConfigs;
    double sampleRate = 48000.0;

    // 缓存的频响曲线点集 (freq_hz, gain_db)
    std::vector<std::pair<float, float>> curvePoints;

    // 频段滑块位置标记 (freq_hz, q)
    std::vector<std::pair<float, float>> bandMarkers;

    // 绘图参数
    static constexpr float freqMin = 20.0f;
    static constexpr float freqMax = 20000.0f;
    static constexpr float gainMin = -20.0f;
    static constexpr float gainMax = 20.0f;
    static constexpr int numPoints = 512;
    float referenceDb = 0.0f;

    // 分析频率点 (log-spaced)
    std::vector<double> analysisFreqs;
    std::vector<double> magBuffer;
    std::vector<double> phaseBuffer;
    std::vector<double> singleBandMag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQCurveComponent)
};
