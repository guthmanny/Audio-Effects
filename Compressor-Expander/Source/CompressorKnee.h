#pragma once

#include <algorithm>
#include <cmath>

namespace CompressorKnee
{
constexpr float defaultWidthDb = 6.0f;
constexpr float minWidthDb = 0.1f;

inline float sanitiseWidthDb (float kneeWidthDb) noexcept
{
    return std::max (minWidthDb, kneeWidthDb);
}

inline float computeCompressorOutputDb (float inputDb,
                                        float thresholdDb,
                                        float ratio,
                                        bool softKnee,
                                        float kneeWidthDb)
{
    if (! softKnee)
    {
        if (inputDb <= thresholdDb)
            return inputDb;

        return thresholdDb + (inputDb - thresholdDb) / ratio;
    }

    const float widthDb = sanitiseWidthDb (kneeWidthDb);
    const float halfKnee = widthDb * 0.5f;

    if (inputDb < thresholdDb - halfKnee)
        return inputDb;

    if (inputDb > thresholdDb + halfKnee)
        return thresholdDb + (inputDb - thresholdDb) / ratio;

    const float delta = inputDb - thresholdDb + halfKnee;
    return inputDb + (1.0f / ratio - 1.0f) * delta * delta / (2.0f * widthDb);
}

inline float computeExpanderOutputDb (float inputDb,
                                      float thresholdDb,
                                      float ratio,
                                      bool softKnee,
                                      float kneeWidthDb)
{
    if (! softKnee)
    {
        if (inputDb > thresholdDb)
            return inputDb;

        return thresholdDb + (inputDb - thresholdDb) * ratio;
    }

    const float widthDb = sanitiseWidthDb (kneeWidthDb);
    const float halfKnee = widthDb * 0.5f;

    if (inputDb > thresholdDb + halfKnee)
        return inputDb;

    if (inputDb < thresholdDb - halfKnee)
        return thresholdDb + (inputDb - thresholdDb) * ratio;

    const float t = (inputDb - (thresholdDb - halfKnee)) / widthDb;
    const float outLow = thresholdDb - halfKnee * ratio;
    const float outHigh = thresholdDb + halfKnee;
    const float oneMinusT = 1.0f - t;
    return oneMinusT * oneMinusT * outLow + 2.0f * oneMinusT * t * thresholdDb + t * t * outHigh;
}

inline float computeOutputDb (float inputDb,
                              float thresholdDb,
                              float ratio,
                              float makeupDb,
                              bool expanderMode,
                              bool softKnee,
                              float kneeWidthDb)
{
    const float core = expanderMode ? computeExpanderOutputDb (inputDb, thresholdDb, ratio, softKnee, kneeWidthDb)
                                    : computeCompressorOutputDb (inputDb, thresholdDb, ratio, softKnee, kneeWidthDb);
    return core + makeupDb;
}

}  // namespace CompressorKnee
