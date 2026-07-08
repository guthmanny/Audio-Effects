#include "EQCurveComponent.h"

#include "atom/CurveControl.h"

#include <cmath>

//==============================================================================

EQCurveComponent::EQCurveComponent()
{
    // 预生成对数分布的频率分析点
    analysisFreqs.resize((size_t)numPoints);
    for (int i = 0; i < numPoints; ++i)
    {
        const double norm = (double)i / (double)(numPoints - 1);
        analysisFreqs[(size_t)i] = freqMin * std::pow(freqMax / freqMin, norm);
    }

    magBuffer.resize((size_t)numPoints);
    phaseBuffer.resize((size_t)numPoints);
    singleBandMag.resize((size_t)numPoints);
}

void EQCurveComponent::setBandConfigs(const std::vector<BandConfig>& configs, double sr)
{
    bandConfigs = configs;
    sampleRate = sr;
    rebuildCurve();
    repaint();
}

void EQCurveComponent::setBandMarkers(const std::vector<std::pair<float, float>>& freqQ)
{
    bandMarkers = freqQ;
    repaint();
}

void EQCurveComponent::rebuildCurve()
{
    curvePoints.clear();
    if (bandConfigs.empty()) return;

    // 初始化总幅度为 0 dB
    std::fill(magBuffer.begin(), magBuffer.end(), 0.0);

    // 对每个频段计算 AC 响应并累加（dB 域相加 = 线性域相乘）
    for (const auto& band : bandConfigs)
    {
        std::fill(singleBandMag.begin(), singleBandMag.end(), 0.0);
        std::fill(phaseBuffer.begin(), phaseBuffer.end(), 0.0);

        nx_svf_ac_static_f32(
            &band.config,
            sampleRate,
            band.config.output,
            analysisFreqs.data(),
            singleBandMag.data(),
            phaseBuffer.data(),
            (size_t)numPoints);

        // dB 累加：总响应 = sum(band_mag_dB)
        for (size_t i = 0; i < (size_t)numPoints; ++i)
            magBuffer[i] += singleBandMag[i];
    }

    // 转换为曲线点集
    curvePoints.reserve((size_t)numPoints);
    for (int i = 0; i < numPoints; ++i)
    {
        const float freq = (float)analysisFreqs[(size_t)i];
        const float gain = (float)magBuffer[(size_t)i];
        curvePoints.emplace_back(freq, gain);
    }
}

float EQCurveComponent::evaluateTotalGainAtFrequency(float freqHz) const
{
    if (bandConfigs.empty() || sampleRate <= 0.0)
        return 0.0f;

    const double analysisFreq = (double)juce::jlimit(freqMin, freqMax, freqHz);
    double totalGainDb = 0.0;
    double singleBandGainDb = 0.0;
    double phaseDegrees = 0.0;

    for (const auto& band : bandConfigs)
    {
        nx_svf_ac_static_f32(&band.config,
                             sampleRate,
                             band.config.output,
                             &analysisFreq,
                             &singleBandGainDb,
                             &phaseDegrees,
                             1);
        totalGainDb += singleBandGainDb;
    }

    return (float)totalGainDb;
}

//==============================================================================
// 坐标映射
//==============================================================================

float EQCurveComponent::xToFreq(float x) const noexcept
{
    const auto area = getLocalBounds().reduced(4);
    if (area.getWidth() <= 0) return freqMin;
    const double norm = (double)(x - (float)area.getX()) / (double)area.getWidth();
    return freqMin * std::pow(freqMax / freqMin, jlimit(0.0, 1.0, norm));
}

float EQCurveComponent::yToGain(float y) const noexcept
{
    const auto area = getLocalBounds().reduced(4);
    if (area.getHeight() <= 0) return gainMin;
    const double norm = 1.0 - (double)(y - (float)area.getY()) / (double)area.getHeight();
    return (float)(gainMin + norm * (gainMax - gainMin));
}

float EQCurveComponent::freqToX(float freqHz) const noexcept
{
    const auto area = getLocalBounds().reduced(4);
    if (area.getWidth() <= 0) return 0;
    const double norm = std::log(freqHz / freqMin) / std::log(freqMax / freqMin);
    return (float)area.getX() + (float)(jlimit(0.0, 1.0, norm) * area.getWidth());
}

float EQCurveComponent::gainToY(float gainDb) const noexcept
{
    const auto area = getLocalBounds().reduced(4);
    if (area.getHeight() <= 0) return 0;
    const double norm = (double)(gainDb - gainMin) / (double)(gainMax - gainMin);
    return (float)area.getY() + (float)((1.0 - jlimit(0.0, 1.0, norm)) * area.getHeight());
}

//==============================================================================
// 绘制
//==============================================================================

void EQCurveComponent::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().reduced(4);
    if (area.isEmpty()) return;

    // ---- 背景 ----
    g.setColour(juce::Colour(0xFF1A1A2E));
    g.fillRect(area);

    // ---- 网格 ----
    g.setColour(juce::Colour(0xFF2A2A3E));

    // 垂直网格线 (频率) — 仅 100 / 1k / 10k
    constexpr float freqGridValues[] = { 100.0f, 1000.0f, 10000.0f };
    for (auto f : freqGridValues)
    {
        const float x = freqToX(f);
        g.drawVerticalLine((int)x, (float)area.getY(), (float)area.getBottom());
    }

    // 水平网格线 (增益) — 每 5 dB，含边界线
    constexpr float gainGridValues[] = { -20.0f, -15.0f, -10.0f, -5.0f, 0.0f, 5.0f, 10.0f, 15.0f, 20.0f };
    for (auto gDb : gainGridValues)
    {
        const float y = gainToY(gDb);
        g.drawHorizontalLine((int)y, (float)area.getX(), (float)area.getRight());
    }

    // ---- 0dB 参考线 ----
    g.setColour(juce::Colour(0xFF4A4A5E));
    const float zeroY = gainToY(0.0f);
    g.drawHorizontalLine((int)zeroY, (float)area.getX(), (float)area.getRight());

    // ---- 频响曲线 ----
    if (curvePoints.size() < 2)
    {
        // 无数据时画一条平线
        g.setColour(juce::Colour(0xFF0A84FF));
        g.drawHorizontalLine((int)zeroY, (float)area.getX(), (float)area.getRight());
        return;
    }

    // 构建 Path：超出纵向显示范围的线段不绘制（不压到边界上）
    juce::Path path;
    juce::Path fillPath;
    atom::buildVerticallyClippedPolylinePaths(
        curvePoints,
        gainMin,
        gainMax,
        [this](float freq, float gain)
        {
            return juce::Point<float>(freqToX(freq), gainToY(gain));
        },
        path,
        &fillPath,
        (float)area.getBottom());

    // 绘制曲线描边
    g.setColour(juce::Colour(0xFF0A84FF));
    g.strokePath(path, juce::PathStrokeType(2.0f));

    // 绘制曲线下填充
    g.setColour(juce::Colour(0x220A84FF));
    g.fillPath(fillPath);

    // ---- 频率标签 ----
    g.setFont((float)juce::jmin(10, area.getHeight() / 14));
    g.setColour(juce::Colour(0xFF888888));
    for (auto f : freqGridValues)
    {
        const float x = freqToX(f);
        juce::String label;
        if (f >= 1000.0f)
            label = juce::String((int)(f / 1000.0f)) + "k";
        else
            label = juce::String((int)f);

        const float tw = g.getCurrentFont().getStringWidthFloat(label);
        g.drawText(label,
                   juce::Rectangle<float>(x - tw / 2, (float)area.getBottom() - 12, tw, 10),
                   juce::Justification::centred);
    }

    // ---- 增益标签 — 边界 ±20 dB 不显示文字 ----
    for (auto gDb : gainGridValues)
    {
        if (gDb == gainMin || gDb == gainMax)
            continue;

        const float y = gainToY(gDb);
        juce::String label = (gDb > 0.0f ? "+" : "") + juce::String((int)gDb) + "dB";
        g.drawText(label, 2, (int)y - 6, 36, 12, juce::Justification::centredLeft);
    }

    // ---- 频段滑块位置标记 ----
    for (const auto& marker : bandMarkers)
    {
        const float freq = marker.first;

        // 频率竖线
        const float mx = freqToX(freq);
        g.setColour(juce::Colour(0x660A84FF));
        g.drawVerticalLine((int)mx, (float)area.getY(), (float)area.getBottom());

        // 在精确频率点计算总响应，避免高 Q 时落在邻近采样点上。
        const float gainAtFreq = evaluateTotalGainAtFrequency(freq);
        if (atom::isValueWithinVerticalRange(gainAtFreq, gainMin, gainMax))
        {
            const float my = gainToY(gainAtFreq);
            g.setColour(juce::Colour(0xFF0A84FF));
            g.fillEllipse(mx - 4.0f, my - 4.0f, 8.0f, 8.0f);
        }

        // 频率值标签
        juce::String freqLabel = freq >= 1000.0f
            ? juce::String(freq / 1000.0f, 1) + "kHz"
            : juce::String((int)freq) + "Hz";
        g.setFont(11.0f);
        g.drawText(freqLabel,
                   juce::Rectangle<float>(mx - 30.0f, (float)area.getY() + 2.0f, 60.0f, 14.0f),
                   juce::Justification::centred);
    }

    // ---- 边框 ----
    g.setColour(juce::Colour(0xFF3A3A4E));
    g.drawRect(area);
}

void EQCurveComponent::resized()
{
}
