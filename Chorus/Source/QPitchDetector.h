#pragma once

#include <q/fx/signal_conditioner.hpp>
#include <q/pitch/pitch_detector.hpp>
#include <q/support/literals.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

/** Streaming tuner front-end using q::signal_conditioner + q::pitch_detector. */
class QPitchDetector
{
public:
    struct Result
    {
        bool valid = false;
        float frequencyHz = 0.0f;
        float periodicity = 0.0f;
        float midiNote = 0.0f;
        float cents = 0.0f;
        int noteIndex = 0;
        int octave = 4;
    };

    explicit QPitchDetector (double sampleRate = 44100.0,
                             float minFreqHz = 50.0f,
                             float maxFreqHz = 500.0f)
    {
        prepare (sampleRate, minFreqHz, maxFreqHz);
    }

    void prepare (double sampleRate, float minFreqHz = 50.0f, float maxFreqHz = 500.0f)
    {
        using namespace cycfi::q::literals;

        sps = sampleRate > 0.0 ? sampleRate : 44100.0;
        minFreq = std::max (20.0f, minFreqHz);
        maxFreq = std::max (minFreq + 1.0f, maxFreqHz);

        const cycfi::q::frequency lowest { (double) minFreq };
        const cycfi::q::frequency highest { (double) maxFreq };
        const cycfi::q::signal_conditioner::config conf;

        conditioner = std::make_unique<cycfi::q::signal_conditioner> (conf, lowest, highest, (float) sps);
        detector = std::make_unique<cycfi::q::pitch_detector> (lowest, highest, (float) sps, -40_dB);
        last = {};
    }

    void setPeriodicityThreshold (float threshold) noexcept
    {
        periodicityThreshold = std::clamp (threshold, 0.0f, 1.0f);
        // Re-evaluate validity against the new threshold using the last estimate.
        if (last.frequencyHz > 0.0f)
            last.valid = last.periodicity >= periodicityThreshold;
    }

    float getPeriodicityThreshold() const noexcept { return periodicityThreshold; }

    void reset()
    {
        const float thr = periodicityThreshold;
        prepare (sps, minFreq, maxFreq);
        periodicityThreshold = thr;
        last = {};
    }

    /** Push mono samples. Returns true when a new estimate is available. */
    bool process (const float* samples, int numSamples)
    {
        if (samples == nullptr || numSamples <= 0 || detector == nullptr || conditioner == nullptr)
            return false;

        bool updated = false;

        for (int i = 0; i < numSamples; ++i)
        {
            const float conditioned = (*conditioner) (samples[i]);
            const bool ready = (*detector) (conditioned);

            if (! ready)
                continue;

            updated = true;
            last = makeResult (detector->get_frequency(), detector->periodicity());
        }

        // Between BACF frames, still refresh with current smoothed frequency
        // so the UI stays responsive while holding a note.
        if (! updated && detector->get_frequency() > 0.0f)
        {
            last = makeResult (detector->get_frequency(), detector->periodicity());
            updated = true;
        }

        return updated;
    }

    const Result& getResult() const noexcept { return last; }

    static const char* noteName (int noteIndex) noexcept
    {
        static constexpr const char* names[12] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        const int i = ((noteIndex % 12) + 12) % 12;
        return names[i];
    }

private:
    Result makeResult (float frequencyHz, float periodicity) const
    {
        Result r;
        r.frequencyHz = frequencyHz;
        r.periodicity = periodicity;

        if (frequencyHz <= 0.0f)
            return r;

        const float midi = 69.0f + 12.0f * std::log2 (frequencyHz / 440.0f);
        const int nearest = (int) std::lround (midi);
        r.midiNote = midi;
        r.cents = (midi - (float) nearest) * 100.0f;
        r.noteIndex = ((nearest % 12) + 12) % 12;
        r.octave = nearest / 12 - 1;
        r.valid = periodicity >= periodicityThreshold;
        return r;
    }

    double sps = 44100.0;
    float minFreq = 50.0f;
    float maxFreq = 500.0f;
    float periodicityThreshold = 0.7f;

    std::unique_ptr<cycfi::q::signal_conditioner> conditioner;
    std::unique_ptr<cycfi::q::pitch_detector> detector;
    Result last;
};
