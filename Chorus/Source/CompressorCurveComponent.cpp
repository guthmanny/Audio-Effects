#include "CompressorCurveComponent.h"

#include "atom/CurveControl.h"

#include "CompressorKnee.h"

#include <cmath>

//==============================================================================

namespace
{
void fillUniformGrid (std::vector<float>& out, float minDb, float maxDb, float stepDb)
{
    out.clear();
    if (maxDb <= minDb || stepDb <= 0.0f)
        return;

    out.push_back (minDb);
    for (float v = std::ceil (minDb / stepDb) * stepDb; v < maxDb - 0.001f; v += stepDb)
    {
        if (v > minDb + 0.001f)
            out.push_back (v);
    }
    out.push_back (maxDb);
}
} // namespace

CompressorCurveComponent::CompressorCurveComponent()
{
    style_ = atom::CurveControl::Style::fromTheme (atom::Theme::getDarkTheme());
    rebuildGridLines();
    rebuildCurve();
}

void CompressorCurveComponent::setParameters (float newThresholdDb,
                                              float newRatio,
                                              float newMakeupDb,
                                              bool newExpanderMode,
                                              bool newSoftKnee,
                                              float newKneeWidthDb)
{
    thresholdDb = newThresholdDb;
    ratio = juce::jmax (1.0f, newRatio);
    makeupDb = newMakeupDb;
    expanderMode = newExpanderMode;
    softKnee = newSoftKnee;
    kneeWidthDb = newKneeWidthDb;
    rebuildCurve();

    if (displayGainReductionDb < 0.05f && dynamicInputDb <= inputMinDb + 1.0f)
    {
        displayMarkerInputDb = inputMinDb;
        displayMarkerOutputDb = CompressorKnee::computeOutputDb (inputMinDb,
                                                                 thresholdDb,
                                                                 ratio,
                                                                 makeupDb,
                                                                 expanderMode,
                                                                 softKnee,
                                                                 kneeWidthDb);
    }

    repaint();
}

void CompressorCurveComponent::setCurveTitle (const juce::String& title)
{
    curveTitle = title;
    repaint();
}

void CompressorCurveComponent::setPlotRange (float newInputMinDb, float newInputMaxDb,
                                             float newOutputMinDb, float newOutputMaxDb)
{
    if (newInputMaxDb <= newInputMinDb || newOutputMaxDb <= newOutputMinDb)
        return;

    inputMinDb = newInputMinDb;
    inputMaxDb = newInputMaxDb;
    outputMinDb = newOutputMinDb;
    outputMaxDb = newOutputMaxDb;

    displayMarkerInputDb = juce::jlimit (inputMinDb, inputMaxDb, displayMarkerInputDb);
    displayMarkerOutputDb = juce::jlimit (outputMinDb, outputMaxDb, displayMarkerOutputDb);
    dynamicInputDb = juce::jlimit (inputMinDb, inputMaxDb, dynamicInputDb);

    rebuildGridLines();
    rebuildCurve();
    repaint();
}

void CompressorCurveComponent::rebuildGridLines()
{
    const float inputSpan = inputMaxDb - inputMinDb;
    const float outputSpan = outputMaxDb - outputMinDb;
    const float inputStep = inputSpan > 70.0f ? 20.0f : 12.0f;
    const float outputStep = outputSpan > 70.0f ? 20.0f : 12.0f;
    fillUniformGrid (inputGridDb, inputMinDb, inputMaxDb, inputStep);
    fillUniformGrid (outputGridDb, outputMinDb, outputMaxDb, outputStep);
}

void CompressorCurveComponent::setDynamicMeter (float inputDb, float gainReductionDb, float attackSec, float releaseSec)
{
    dynamicInputDb = juce::jlimit (inputMinDb, inputMaxDb, inputDb);
    dynamicGainReductionDb = jmax (0.0f, gainReductionDb);

    constexpr float refreshHz = 60.0f;
    const float dt = 1.0f / refreshHz;
    const float markerAttackSec = juce::jmax (0.0001f, attackSec);
    const float markerReleaseSec = juce::jmax (0.001f, releaseSec);

    const auto smoothToward = [] (float current,
                                  float target,
                                  float attackTau,
                                  float releaseTau,
                                  float deltaTime,
                                  float deadbandDb)
    {
        if (target > current && std::abs (target - current) < deadbandDb)
            return current;

        const float tau = target > current ? attackTau : releaseTau;
        const float coeff = 1.0f - std::exp (-deltaTime / tau);
        return current + coeff * (target - current);
    };

    // GR readout: DSP already applies attack; only smooth the release leg for display.
    displayGainReductionDb = smoothToward (displayGainReductionDb,
                                           dynamicGainReductionDb,
                                           0.0001f,
                                           markerReleaseSec,
                                           dt,
                                           0.10f);

    const bool idle = dynamicGainReductionDb < 0.05f && dynamicInputDb <= inputMinDb + 1.0f;
    const float targetMarkerInputDb = idle ? inputMinDb : juce::jlimit (inputMinDb, inputMaxDb, dynamicInputDb);

    displayMarkerInputDb = smoothToward (displayMarkerInputDb, targetMarkerInputDb, markerAttackSec, markerReleaseSec, dt, 0.0f);
    displayMarkerOutputDb = CompressorKnee::computeOutputDb (displayMarkerInputDb,
                                                             thresholdDb,
                                                             ratio,
                                                             makeupDb,
                                                             expanderMode,
                                                             softKnee,
                                                             kneeWidthDb);

    repaint();
}

void CompressorCurveComponent::rebuildCurve()
{
    transferCurve.clear();
    unityCurve.clear();
    transferCurve.reserve ((size_t) numPoints);
    unityCurve.reserve ((size_t) numPoints);

    for (int i = 0; i < numPoints; ++i)
    {
        const float t = (float) i / (float) (numPoints - 1);
        const float inputDb = inputMinDb + t * (inputMaxDb - inputMinDb);

        transferCurve.emplace_back (inputDb,
                                    CompressorKnee::computeOutputDb (inputDb,
                                                                     thresholdDb,
                                                                     ratio,
                                                                     makeupDb,
                                                                     expanderMode,
                                                                     softKnee,
                                                                     kneeWidthDb));
        unityCurve.emplace_back (inputDb, inputDb);
    }
}

juce::Rectangle<float> CompressorCurveComponent::getPlotArea() const noexcept
{
    const auto bounds = getLocalBounds().toFloat();
    const float padX = (float) style_.metrics.plotBufferX;
    const float padY = (float) style_.metrics.plotBufferY;
    return bounds.reduced (padX, padY);
}

float CompressorCurveComponent::inputToX (float inputDb) const noexcept
{
    const auto area = getPlotArea();
    if (area.getWidth() <= 0.0f || inputMaxDb <= inputMinDb)
        return area.getX();

    const float norm = juce::jlimit (0.0f, 1.0f, (inputDb - inputMinDb) / (inputMaxDb - inputMinDb));
    return area.getX() + norm * area.getWidth();
}

float CompressorCurveComponent::outputToY (float outputDb) const noexcept
{
    const auto area = getPlotArea();
    if (area.getHeight() <= 0.0f || outputMaxDb <= outputMinDb)
        return area.getY();

    const float norm = juce::jlimit (0.0f, 1.0f, (outputDb - outputMinDb) / (outputMaxDb - outputMinDb));
    return area.getBottom() - norm * area.getHeight();
}

void CompressorCurveComponent::paint (juce::Graphics& g)
{
    const auto& colors = style_.colors;
    const auto& metrics = style_.metrics;
    const auto plotArea = getPlotArea();

    if (plotArea.isEmpty())
        return;

    g.fillAll (colors.background);

    // ---- Grid ----
    g.setColour (colors.grid);
    g.setOpacity (0.65f);

    for (auto inputDb : inputGridDb)
    {
        const float x = inputToX (inputDb);
        g.drawLine (x, plotArea.getY(), x, plotArea.getBottom(), metrics.gridStrokeSize);
    }

    for (auto outputDb : outputGridDb)
    {
        const float y = outputToY (outputDb);
        g.drawLine (plotArea.getX(), y, plotArea.getRight(), y, metrics.gridStrokeSize);
    }

    g.setOpacity (1.0f);

    const auto mapPoint = [this] (float inputDb, float outputDb)
    {
        return juce::Point<float> (inputToX (inputDb), outputToY (outputDb));
    };

    // ---- Unity reference (1:1 + makeup) ----
    {
        juce::Path unityPath;
        atom::buildVerticallyClippedPolylinePaths (unityCurve,
                                                   outputMinDb,
                                                   outputMaxDb,
                                                   mapPoint,
                                                   unityPath);

        g.setColour (colors.grid.brighter (0.25f));
        g.strokePath (unityPath, juce::PathStrokeType (1.0f));
    }

    // ---- Transfer curve ----
    {
        juce::Path strokePath;
        juce::Path fillPath;
        atom::buildVerticallyClippedPolylinePaths (transferCurve,
                                                   outputMinDb,
                                                   outputMaxDb,
                                                   mapPoint,
                                                   strokePath,
                                                   &fillPath,
                                                   plotArea.getBottom());

        g.setColour (colors.path.withAlpha (0.18f));
        g.fillPath (fillPath);

        g.setColour (colors.path);
        g.strokePath (strokePath, juce::PathStrokeType (metrics.pathStrokeSize));
    }

    // ---- Static threshold reference ----
    {
        const float markerX = inputToX (thresholdDb);
        g.setColour (colors.endpointHighlight.withAlpha (0.18f));
        g.drawVerticalLine ((int) markerX, plotArea.getY(), (int) plotArea.getBottom());
    }

    // ---- Dynamic threshold / operating point ----
    {
        const float markerInputDb = displayMarkerInputDb;
        const float markerOutputDb = displayMarkerOutputDb;
        const float markerX = inputToX (markerInputDb);
        const float markerY = outputToY (markerOutputDb);
        const float kneeX = inputToX (thresholdDb);
        const float kneeY = outputToY (CompressorKnee::computeOutputDb (thresholdDb,
                                                                       thresholdDb,
                                                                       ratio,
                                                                       makeupDb,
                                                                       expanderMode,
                                                                       softKnee,
                                                                       kneeWidthDb));

        if (displayGainReductionDb > 0.05f)
        {
            g.setColour (colors.endpointHighlight.withAlpha (0.25f));
            g.drawLine (kneeX, kneeY, markerX, markerY, 1.5f);
        }

        if (atom::isValueWithinVerticalRange (markerOutputDb, outputMinDb, outputMaxDb))
        {
            const float pulse = 6.0f + juce::jmin (6.0f, displayGainReductionDb * 0.35f);
            g.setColour (colors.dotOver.withAlpha (0.22f));
            g.fillEllipse (markerX - pulse, markerY - pulse, pulse * 2.0f, pulse * 2.0f);

            g.setColour (colors.dotOver);
            g.fillEllipse (markerX - 5.0f, markerY - 5.0f, 10.0f, 10.0f);
            g.setColour (colors.endpointOutline);
            g.drawEllipse (markerX - 5.0f, markerY - 5.0f, 10.0f, 10.0f, 1.25f);
        }

        if (atom::isValueWithinVerticalRange (markerOutputDb, outputMinDb, outputMaxDb))
        {
            const auto grLabel = juce::String (displayGainReductionDb, 1) + " dB GR";
            g.setFont (10.5f);
            g.setColour (colors.dotOver.brighter (0.2f));
            g.drawText (grLabel,
                        juce::Rectangle<float> (markerX - 36.0f, markerY - 22.0f, 72.0f, 12.0f),
                        juce::Justification::centred);
        }
    }

    // ---- Static knee marker ----
    {
        const float kneeInputDb = thresholdDb;
        const float kneeOutputDb = CompressorKnee::computeOutputDb (kneeInputDb,
                                                                    thresholdDb,
                                                                    ratio,
                                                                    makeupDb,
                                                                    expanderMode,
                                                                    softKnee,
                                                                    kneeWidthDb);
        const float markerX = inputToX (kneeInputDb);
        const float markerY = outputToY (kneeOutputDb);

        if (atom::isValueWithinVerticalRange (kneeOutputDb, outputMinDb, outputMaxDb))
        {
            g.setColour (colors.endpointHighlight.withAlpha (0.55f));
            g.fillEllipse (markerX - 3.5f, markerY - 3.5f, 7.0f, 7.0f);
            g.setColour (colors.endpointOutline.withAlpha (0.8f));
            g.drawEllipse (markerX - 3.5f, markerY - 3.5f, 7.0f, 7.0f, 1.0f);
        }
    }

    // ---- Axis labels ----
    g.setFont (11.0f);
    g.setColour (colors.grid.brighter (0.45f));

    for (auto inputDb : inputGridDb)
    {
        const auto label = juce::String ((int) inputDb);
        const float x = inputToX (inputDb);
        const float tw = g.getCurrentFont().getStringWidthFloat (label);
        g.drawText (label,
                    juce::Rectangle<float> (x - tw * 0.5f, plotArea.getBottom() + 2.0f, tw, 12.0f),
                    juce::Justification::centred);
    }

    for (auto outputDb : outputGridDb)
    {
        const auto label = (outputDb > 0.0f ? "+" : "") + juce::String ((int) outputDb);
        const float y = outputToY (outputDb);
        g.drawText (label, 2, (int) y - 6, 34, 12, juce::Justification::centredLeft);
    }

    g.drawText ("Input dB",
                plotArea.withY (plotArea.getBottom() + 14.0f).withHeight (14.0f),
                juce::Justification::centred);
    g.drawText ("Output",
                juce::Rectangle<float> (2.0f, plotArea.getY() - 16.0f, 48.0f, 12.0f),
                juce::Justification::centredLeft);

    // ---- Parameter readout ----
    const auto modeText = curveTitle.isNotEmpty()
                              ? curveTitle
                              : (expanderMode ? juce::String ("Expander") : juce::String ("Compressor"));
    const auto kneeText = softKnee ? juce::String ("Soft ") + juce::String (kneeWidthDb, 1) + " dB"
                                   : juce::String ("Hard");
    const auto readout = modeText + "  |  Knee " + kneeText
                       + "  |  Threshold "
                       + juce::String (thresholdDb, 1) + " dB  |  Ratio "
                       + juce::String (ratio, 1) + ":1"
                       + (std::abs (makeupDb) > 0.05f
                              ? ("  |  Makeup " + juce::String (makeupDb > 0.0f ? "+" : "")
                                 + juce::String (makeupDb, 1) + " dB")
                              : juce::String());

    g.setColour (colors.path);
    g.setFont (12.0f);
    g.drawText (readout,
                plotArea.withHeight (16.0f).withY (plotArea.getY() + 2.0f),
                juce::Justification::centred);

    g.setColour (colors.endpointOutline);
    g.drawRect (plotArea);
}

void CompressorCurveComponent::resized()
{
}
