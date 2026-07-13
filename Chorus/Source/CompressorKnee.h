#pragma once

#include "nudsp/dynamic/knee.hpp"

/** Compatibility alias — knee math now lives in NuDSP. */
namespace CompressorKnee
{
constexpr float defaultWidthDb = nudsp::dynamic::kDefaultKneeWidthDb;
constexpr float minWidthDb = nudsp::dynamic::kMinKneeWidthDb;

inline float sanitiseWidthDb (float kneeWidthDb) noexcept
{
    return nudsp::dynamic::sanitiseWidthDb (kneeWidthDb);
}

inline float computeCompressorOutputDb (float inputDb,
                                        float thresholdDb,
                                        float ratio,
                                        bool softKnee,
                                        float kneeWidthDb)
{
    return nudsp::dynamic::computeCompressorOutputDb (inputDb, thresholdDb, ratio, softKnee, kneeWidthDb);
}

inline float computeExpanderOutputDb (float inputDb,
                                      float thresholdDb,
                                      float ratio,
                                      bool softKnee,
                                      float kneeWidthDb)
{
    return nudsp::dynamic::computeExpanderOutputDb (inputDb, thresholdDb, ratio, softKnee, kneeWidthDb);
}

inline float computeOutputDb (float inputDb,
                              float thresholdDb,
                              float ratio,
                              float makeupDb,
                              bool expanderMode,
                              bool softKnee,
                              float kneeWidthDb)
{
    return nudsp::dynamic::computeOutputDb (inputDb, thresholdDb, ratio, makeupDb, expanderMode, softKnee,
                                            kneeWidthDb);
}

}  // namespace CompressorKnee
