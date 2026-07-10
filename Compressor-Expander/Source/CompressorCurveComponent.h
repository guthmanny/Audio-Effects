#pragma once

#include <vector>

#include "../JuceLibraryCode/JuceHeader.h"
#include <juce_atom_theme/juce_atom_theme.h>

#include "CompressorKnee.h"

//==============================================================================
/** Compressor / expander input-output transfer curve (Atom CurveControl styling). */
class CompressorCurveComponent final : public juce::Component
{
public:
    CompressorCurveComponent();
    ~CompressorCurveComponent() override = default;

    void setParameters (float thresholdDb,
                        float ratio,
                        float makeupDb,
                        bool expanderMode,
                        bool softKnee,
                        float kneeWidthDb);

    /** Updates the live operating point (input dB, gain-reduction dB, attack/release seconds). */
    void setDynamicMeter (float inputDb, float gainReductionDb, float attackSec, float releaseSec);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void rebuildCurve();

    float inputToX (float inputDb) const noexcept;
    float outputToY (float outputDb) const noexcept;
    juce::Rectangle<float> getPlotArea() const noexcept;

    atom::CurveControl::Style style_;

    float thresholdDb = -24.0f;
    float ratio = 6.0f;
    float makeupDb = 0.0f;
    bool expanderMode = false;
    bool softKnee = false;
    float kneeWidthDb = CompressorKnee::defaultWidthDb;

    float dynamicInputDb = -80.0f;
    float dynamicGainReductionDb = 0.0f;
    float displayGainReductionDb = 0.0f;
    float displayMarkerInputDb = inputMinDb;
    float displayMarkerOutputDb = inputMinDb;

    std::vector<std::pair<float, float>> transferCurve;
    std::vector<std::pair<float, float>> unityCurve;

    static constexpr float inputMinDb = -60.0f;
    static constexpr float inputMaxDb = 0.0f;
    static constexpr float outputMinDb = -60.0f;
    static constexpr float outputMaxDb = 12.0f;
    static constexpr int numPoints = 256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorCurveComponent)
};
