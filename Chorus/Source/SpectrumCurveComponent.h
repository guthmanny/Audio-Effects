#pragma once

#include <vector>

#include <juce_atom_theme/juce_atom_theme.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Live STFT magnitude curve using Atom CurveControl styling utilities. */
class SpectrumCurveComponent final : public juce::Component
{
public:
    enum class FrequencyScale
    {
        Logarithmic = 0,
        Linear = 1
    };

    enum class InterpolationMethod
    {
        Nearest = 0,
        Linear = 1,
        Cubic = 2
    };

    static constexpr float defaultMagMinDb = -90.0f;
    static constexpr float defaultMagMaxDb = 0.0f;
    static constexpr float absoluteMagMinDb = -200.0f;
    static constexpr float absoluteMagMaxDb = 0.0f;

    SpectrumCurveComponent();
    ~SpectrumCurveComponent() override = default;

    void setSpectrumMagnitudes (const std::vector<float>& magnitudesDb,
                                double sampleRate,
                                int fftSize);

    /** Swaps with @p magnitudesDb to avoid an extra copy (preserves caller capacity). */
    void setSpectrumMagnitudes (std::vector<float>& magnitudesDb,
                                double sampleRate,
                                int fftSize);

    void clearSpectrum();

    /** Set vertical display range in dB (magMinDb < magMaxDb). */
    void setVerticalRangeDb (float minDb, float maxDb);
    float getMagMinDb() const noexcept { return magMinDb; }
    float getMagMaxDb() const noexcept { return magMaxDb; }

    void setFrequencyScale (FrequencyScale scale);
    FrequencyScale getFrequencyScale() const noexcept { return frequencyScale; }

    void setInterpolationMethod (InterpolationMethod method);
    InterpolationMethod getInterpolationMethod() const noexcept { return interpolationMethod; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    float freqToX (float freqHz) const noexcept;
    float magToY (float magDb) const noexcept;
    float sampleMagnitude (float binF) const noexcept;
    juce::Rectangle<float> getPlotArea() const noexcept;
    void rebuildMagGrid();
    void rebuildFreqGrid();
    void rebuildSpectrumPoints();
    void rebuildCachedPaths();
    void invalidatePaths() noexcept { pathsDirty = true; }

    atom::CurveControl::Style style_;

    std::vector<std::pair<float, float>> spectrumPoints;
    std::vector<float> magGridValues;
    std::vector<float> freqGridValues;
    std::vector<float> lastMagnitudesDb;
    std::vector<float> cachedDisplayFreqs;
    juce::Path cachedStrokePath;
    juce::Path cachedFillPath;
    bool pathsDirty = true;

    double lastSampleRate = 0.0;
    int lastFftSize = 0;
    float cachedMaxFreq = 0.0f;
    FrequencyScale cachedScale = FrequencyScale::Logarithmic;

    static constexpr float freqMin = 20.0f;
    static constexpr float freqMax = 20000.0f;
    float magMinDb = defaultMagMinDb;
    float magMaxDb = defaultMagMaxDb;
    FrequencyScale frequencyScale = FrequencyScale::Logarithmic;
    InterpolationMethod interpolationMethod = InterpolationMethod::Linear;
    static constexpr int numDisplayPoints = 256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumCurveComponent)
};
