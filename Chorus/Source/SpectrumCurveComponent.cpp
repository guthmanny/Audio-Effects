#include "SpectrumCurveComponent.h"

#include "atom/CurveControl.h"

#include <cmath>

//==============================================================================

SpectrumCurveComponent::SpectrumCurveComponent()
{
    setOpaque (true);
    style_ = atom::CurveControl::Style::fromTheme (atom::Theme::getDarkTheme());
    style_.metrics.gridMode = atom::CurveControl::GridMode::Logarithmic;
    style_.metrics.logXConfig = { true, (double) freqMin, (double) freqMax };
    style_.metrics.plotBufferX = 8;
    style_.metrics.plotBufferY = 8;
    rebuildMagGrid();
    rebuildFreqGrid();
}

void SpectrumCurveComponent::clearSpectrum()
{
    spectrumPoints.clear();
    lastMagnitudesDb.clear();
    lastSampleRate = 0.0;
    lastFftSize = 0;
    invalidatePaths();
    repaint();
}

void SpectrumCurveComponent::setVerticalRangeDb (float minDb, float maxDb)
{
    minDb = juce::jmax (absoluteMagMinDb, minDb);
    maxDb = juce::jmin (absoluteMagMaxDb, maxDb);
    if (maxDb <= minDb)
        return;

    magMinDb = minDb;
    magMaxDb = maxDb;
    rebuildMagGrid();
    invalidatePaths();
    repaint();
}

void SpectrumCurveComponent::setFrequencyScale (FrequencyScale scale)
{
    if (frequencyScale == scale)
        return;

    frequencyScale = scale;
    style_.metrics.gridMode = (scale == FrequencyScale::Logarithmic)
                                  ? atom::CurveControl::GridMode::Logarithmic
                                  : atom::CurveControl::GridMode::Linear;
    style_.metrics.logXConfig.enabled = (scale == FrequencyScale::Logarithmic);
    rebuildFreqGrid();
    rebuildSpectrumPoints();
    invalidatePaths();
    repaint();
}

void SpectrumCurveComponent::setInterpolationMethod (InterpolationMethod method)
{
    if (interpolationMethod == method)
        return;

    interpolationMethod = method;
    rebuildSpectrumPoints();
    invalidatePaths();
    repaint();
}

float SpectrumCurveComponent::sampleMagnitude (float binF) const noexcept
{
    const int numBins = (int) lastMagnitudesDb.size();
    if (numBins <= 0)
        return magMinDb;

    if (numBins == 1)
        return lastMagnitudesDb[0];

    const auto readBin = [this, numBins] (int index) noexcept -> float
    {
        return lastMagnitudesDb[(size_t) juce::jlimit (0, numBins - 1, index)];
    };

    switch (interpolationMethod)
    {
        case InterpolationMethod::Nearest:
            return readBin ((int) std::lround (binF));

        case InterpolationMethod::Cubic:
        {
            const int i1 = (int) std::floor (binF);
            const float t = binF - (float) i1;
            const float y0 = readBin (i1 - 1);
            const float y1 = readBin (i1);
            const float y2 = readBin (i1 + 1);
            const float y3 = readBin (i1 + 2);
            // Catmull-Rom spline.
            const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
            const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c = -0.5f * y0 + 0.5f * y2;
            const float d = y1;
            return ((a * t + b) * t + c) * t + d;
        }

        case InterpolationMethod::Linear:
        default:
        {
            const int bin0 = juce::jlimit (0, numBins - 1, (int) std::floor (binF));
            const int bin1 = juce::jlimit (0, numBins - 1, bin0 + 1);
            const float frac = binF - (float) bin0;
            return readBin (bin0) * (1.0f - frac) + readBin (bin1) * frac;
        }
    }
}

void SpectrumCurveComponent::rebuildMagGrid()
{
    magGridValues.clear();

    const float span = magMaxDb - magMinDb;
    float step = 20.0f;
    if (span <= 50.0f)
        step = 10.0f;
    else if (span <= 100.0f)
        step = 20.0f;
    else if (span <= 150.0f)
        step = 30.0f;
    else
        step = 40.0f;

    float first = std::ceil (magMinDb / step) * step;
    if (first < magMinDb + 0.01f)
        first += step;

    for (float mag = first; mag < magMaxDb - 0.01f; mag += step)
        magGridValues.push_back (mag);

    if (std::abs (magMaxDb) < 0.01f || std::fmod (std::abs (magMaxDb), step) < 0.01f)
        magGridValues.push_back (magMaxDb);
}

void SpectrumCurveComponent::rebuildFreqGrid()
{
    freqGridValues.clear();

    if (frequencyScale == FrequencyScale::Logarithmic)
    {
        constexpr float logGrid[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
        for (auto f : logGrid)
            freqGridValues.push_back (f);
    }
    else
    {
        constexpr float linGrid[] = { 2000.0f, 4000.0f, 6000.0f, 8000.0f, 10000.0f, 12000.0f, 14000.0f, 16000.0f, 18000.0f };
        for (auto f : linGrid)
            if (f > freqMin && f < freqMax)
                freqGridValues.push_back (f);
    }
}

void SpectrumCurveComponent::rebuildSpectrumPoints()
{
    spectrumPoints.clear();

    if (lastMagnitudesDb.empty() || lastSampleRate <= 0.0)
        return;

    // Published data is peak-pooled over 0..nyquist (not raw FFT bins).
    const int numBins = (int) lastMagnitudesDb.size();
    const float nyquist = (float) lastSampleRate * 0.5f;
    const float maxFreq = juce::jmin (freqMax, nyquist);
    const float binHz = nyquist / (float) juce::jmax (1, numBins - 1);

    if (cachedDisplayFreqs.size() != (size_t) numDisplayPoints
        || cachedScale != frequencyScale
        || cachedMaxFreq != maxFreq)
    {
        cachedDisplayFreqs.resize ((size_t) numDisplayPoints);
        for (int i = 0; i < numDisplayPoints; ++i)
        {
            const double norm = (double) i / (double) (numDisplayPoints - 1);
            if (frequencyScale == FrequencyScale::Logarithmic)
                cachedDisplayFreqs[(size_t) i] = freqMin * std::pow (maxFreq / freqMin, (float) norm);
            else
                cachedDisplayFreqs[(size_t) i] = freqMin + (float) norm * (maxFreq - freqMin);
        }
        cachedScale = frequencyScale;
        cachedMaxFreq = maxFreq;
    }

    spectrumPoints.resize ((size_t) numDisplayPoints);

    for (int i = 0; i < numDisplayPoints; ++i)
    {
        const float freq = cachedDisplayFreqs[(size_t) i];
        spectrumPoints[(size_t) i] = { freq, sampleMagnitude (freq / binHz) };
    }
}

void SpectrumCurveComponent::rebuildCachedPaths()
{
    cachedStrokePath.clear();
    cachedFillPath.clear();

    const auto plotArea = getPlotArea();
    if (spectrumPoints.size() < 2 || plotArea.isEmpty())
    {
        pathsDirty = false;
        return;
    }

    atom::buildVerticallyClippedPolylinePaths (
        spectrumPoints,
        magMinDb,
        magMaxDb,
        [this] (float freq, float mag)
        {
            return juce::Point<float> (freqToX (freq), magToY (mag));
        },
        cachedStrokePath,
        &cachedFillPath,
        plotArea.getBottom());

    pathsDirty = false;
}

void SpectrumCurveComponent::setSpectrumMagnitudes (const std::vector<float>& magnitudesDb,
                                                    double sampleRate,
                                                    int fftSize)
{
    lastMagnitudesDb = magnitudesDb;
    lastSampleRate = sampleRate;
    lastFftSize = fftSize;
    rebuildSpectrumPoints();
    invalidatePaths();
    repaint();
}

void SpectrumCurveComponent::setSpectrumMagnitudes (std::vector<float>& magnitudesDb,
                                                    double sampleRate,
                                                    int fftSize)
{
    lastMagnitudesDb.swap (magnitudesDb);
    lastSampleRate = sampleRate;
    lastFftSize = fftSize;
    rebuildSpectrumPoints();
    invalidatePaths();
    repaint();
}

juce::Rectangle<float> SpectrumCurveComponent::getPlotArea() const noexcept
{
    const auto& m = style_.metrics;
    return getLocalBounds().toFloat().reduced ((float) m.plotBufferX, (float) m.plotBufferY);
}

float SpectrumCurveComponent::freqToX (float freqHz) const noexcept
{
    const auto area = getPlotArea();
    if (area.getWidth() <= 0.0f)
        return area.getX();

    const float clamped = juce::jlimit (freqMin, freqMax, freqHz);
    double norm = 0.0;

    if (frequencyScale == FrequencyScale::Logarithmic)
    {
        norm = std::log ((double) clamped / (double) freqMin)
             / std::log ((double) freqMax / (double) freqMin);
    }
    else
    {
        norm = ((double) clamped - (double) freqMin) / ((double) freqMax - (double) freqMin);
    }

    return area.getX() + (float) juce::jlimit (0.0, 1.0, norm) * area.getWidth();
}

float SpectrumCurveComponent::magToY (float magDb) const noexcept
{
    const auto area = getPlotArea();
    if (area.getHeight() <= 0.0f)
        return area.getY();

    const float denom = magMaxDb - magMinDb;
    if (denom <= 0.0f)
        return area.getBottom();

    const float norm = juce::jlimit (0.0f, 1.0f, (magDb - magMinDb) / denom);
    return area.getBottom() - norm * area.getHeight();
}

void SpectrumCurveComponent::resized()
{
    invalidatePaths();
    repaint();
}

void SpectrumCurveComponent::paint (juce::Graphics& g)
{
    const auto& colors = style_.colors;
    const auto& metrics = style_.metrics;
    const auto plotArea = getPlotArea();

    if (plotArea.isEmpty())
        return;

    g.fillAll (colors.background);

    g.setColour (colors.grid);
    g.setOpacity (0.65f);

    for (auto f : freqGridValues)
    {
        const float x = freqToX (f);
        g.drawLine (x, plotArea.getY(), x, plotArea.getBottom(), metrics.gridStrokeSize);
    }

    for (auto mag : magGridValues)
    {
        const float y = magToY (mag);
        g.drawLine (plotArea.getX(), y, plotArea.getRight(), y, metrics.gridStrokeSize);
    }

    g.setOpacity (1.0f);

    if (pathsDirty)
        rebuildCachedPaths();

    if (! cachedStrokePath.isEmpty())
    {
        g.setColour (colors.path.withAlpha (0.18f));
        g.fillPath (cachedFillPath);

        g.setColour (colors.path);
        g.strokePath (cachedStrokePath, juce::PathStrokeType (metrics.pathStrokeSize));
    }

    g.setFont (AtomLookAndFeel::getUIFont (10.0f, juce::Font::plain));
    g.setColour (colors.grid.brighter (0.55f));

    for (auto f : freqGridValues)
    {
        if (frequencyScale == FrequencyScale::Logarithmic)
        {
            if (f < 100.0f || f > 10000.0f)
                continue;
        }
        else if (((int) f % 4000) != 0)
        {
            continue;
        }

        const float x = freqToX (f);
        juce::String label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k"
                                          : juce::String ((int) f);
        const float tw = g.getCurrentFont().getStringWidthFloat (label);
        g.drawText (label,
                    juce::Rectangle<float> (x - tw * 0.5f, plotArea.getBottom() - 12.0f, tw, 10.0f),
                    juce::Justification::centred);
    }

    for (auto mag : magGridValues)
    {
        if (mag <= magMinDb + 0.1f || mag >= magMaxDb - 0.1f)
            continue;

        const float y = magToY (mag);
        g.drawText (juce::String ((int) mag),
                    juce::Rectangle<float> (plotArea.getX() + 2.0f, y - 6.0f, 28.0f, 12.0f),
                    juce::Justification::centredLeft);
    }
}
