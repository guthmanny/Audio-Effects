#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

/**
    Analysis-only STFT magnitude spectrum.

    Audio thread only writes samples into a lock-free FIFO.
    A background thread performs windowing / FFT / dB conversion and
    publishes a small display-sized spectrum for the UI.
*/
class SpectrumAnalyzer final : private juce::Thread
{
public:
    static constexpr int minFftOrder = 10; // 1024
    static constexpr int maxFftOrder = 16; // 65536
    static constexpr int defaultFftOrder = 11; // 2048
    static constexpr float magMinDb = -200.0f;
    static constexpr float targetRefreshHz = 24.0f;
    static constexpr int publishBins = 512;

    SpectrumAnalyzer()
        : juce::Thread ("SpectrumAnalyzer")
    {
    }

    ~SpectrumAnalyzer() override
    {
        stopAnalysis();
    }

    static int clampFftSize (int size) noexcept
    {
        int order = 0;
        while ((1 << order) < size)
            ++order;
        order = juce::jlimit (minFftOrder, maxFftOrder, order);
        return 1 << order;
    }

    static int orderFromSize (int size) noexcept
    {
        const int clamped = clampFftSize (size);
        int order = minFftOrder;
        while ((1 << order) < clamped)
            ++order;
        return order;
    }

    void setSampleRate (double sampleRate) noexcept
    {
        sr.store (sampleRate > 0.0 ? sampleRate : 44100.0, std::memory_order_relaxed);
        updateHopSize();
    }

    /** Change FFT size. Call from message thread while analysis is stopped or paused. */
    void setFftSize (int newSize)
    {
        const int newOrder = orderFromSize (newSize);
        if (fft != nullptr && fftOrder == newOrder && fftData.size() == (size_t) getFftSize() * 2)
        {
            updateHopSize();
            return;
        }

        const bool wasRunning = isThreadRunning();
        stopAnalysis();

        fftOrder = newOrder;
        allocateBuffers();

        if (wasRunning)
            startAnalysis();
    }

    void ensureReady()
    {
        if (fft != nullptr && fftData.size() == (size_t) getFftSize() * 2)
        {
            updateHopSize();
            return;
        }

        const bool wasRunning = isThreadRunning();
        stopAnalysis();
        allocateBuffers();
        if (wasRunning)
            startAnalysis();
    }

    void startAnalysis()
    {
        ensureReady();
        if (! isThreadRunning())
        {
            resetState();
            startThread (juce::Thread::Priority::low);
        }
    }

    void stopAnalysis()
    {
        signalThreadShouldExit();
        notify();
        stopThread (2000);
    }

    void reset()
    {
        const bool wasRunning = isThreadRunning();
        if (wasRunning)
            stopAnalysis();

        resetState();

        if (wasRunning)
            startAnalysis();
    }

    /** Realtime-safe: push mono samples into the FIFO. Never allocates / never FFTs. */
    void pushSamples (const float* samples, int numSamples) noexcept
    {
        if (samples == nullptr || numSamples <= 0)
            return;

        const juce::SpinLock::ScopedTryLockType lock (configLock);
        if (! lock.isLocked() || fifo == nullptr)
            return;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo->prepareToWrite (numSamples, start1, size1, start2, size2);

        if (size1 > 0)
            juce::FloatVectorOperations::copy (fifoData.data() + start1, samples, size1);
        if (size2 > 0)
            juce::FloatVectorOperations::copy (fifoData.data() + start2, samples + size1, size2);

        const int written = size1 + size2;
        fifo->finishedWrite (written);

        samplesSinceNotify += written;
        if (samplesSinceNotify >= hopSize.load (std::memory_order_relaxed))
        {
            samplesSinceNotify = 0;
            notify();
        }
    }

    bool copyMagnitudesIfNew (uint32_t& lastFrameId, std::vector<float>& dest) const
    {
        const uint32_t current = frameId.load (std::memory_order_acquire);
        if (current == lastFrameId)
            return false;

        const int idx = publishedIndex.load (std::memory_order_acquire);
        const auto& src = publishedDb[(size_t) idx];
        dest.resize (src.size());
        if (! src.empty())
            juce::FloatVectorOperations::copy (dest.data(), src.data(), (int) src.size());

        lastFrameId = current;
        return true;
    }

    uint32_t getFrameId() const noexcept { return frameId.load (std::memory_order_acquire); }
    double getSampleRate() const noexcept { return sr.load (std::memory_order_relaxed); }
    int getFftSize() const noexcept { return 1 << fftOrder; }
    int getNumBins() const noexcept { return getFftSize() / 2 + 1; }
    int getFftOrder() const noexcept { return fftOrder; }
    static constexpr int getPublishBins() noexcept { return publishBins; }

private:
    static constexpr float smoothCoeff = 0.55f;
    static constexpr int maxFftSize = 1 << maxFftOrder;
    static constexpr int fifoCapacity = maxFftSize * 2;

    void allocateBuffers()
    {
        const juce::SpinLock::ScopedLockType lock (configLock);

        const int fftSize = getFftSize();
        const int bins = getNumBins();

        fifo = std::make_unique<juce::AbstractFifo> (fifoCapacity);
        fifoData.assign ((size_t) fifoCapacity, 0.0f);
        fftData.assign ((size_t) fftSize * 2, 0.0f);
        window.assign ((size_t) fftSize, 0.0f);
        analysisRing.assign ((size_t) fftSize, 0.0f);
        workMags.assign ((size_t) bins, magMinDb);
        publishedDb[0].assign ((size_t) publishBins, magMinDb);
        publishedDb[1].assign ((size_t) publishBins, magMinDb);

        for (int i = 0; i < fftSize; ++i)
        {
            const float phase = juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1);
            window[(size_t) i] = 0.5f * (1.0f - std::cos (phase));
        }

        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        analysisWrite = 0;
        analysisFilled = 0;
        updateHopSize();
        publishedIndex.store (0, std::memory_order_relaxed);
        frameId.store (0, std::memory_order_relaxed);
    }

    void resetState()
    {
        const juce::SpinLock::ScopedLockType lock (configLock);

        if (fifo != nullptr)
            fifo->reset();

        std::fill (fifoData.begin(), fifoData.end(), 0.0f);
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        std::fill (analysisRing.begin(), analysisRing.end(), 0.0f);
        std::fill (workMags.begin(), workMags.end(), magMinDb);
        std::fill (publishedDb[0].begin(), publishedDb[0].end(), magMinDb);
        std::fill (publishedDb[1].begin(), publishedDb[1].end(), magMinDb);
        analysisWrite = 0;
        analysisFilled = 0;
        samplesSinceNotify = 0;
        frameId.fetch_add (1, std::memory_order_release);
    }

    void updateHopSize() noexcept
    {
        const int fftSize = getFftSize();
        const double sampleRate = sr.load (std::memory_order_relaxed);
        const int targetHop = juce::jmax (1, (int) std::lround (sampleRate / (double) targetRefreshHz));
        // Keep overlap for smooth UI, but never hop slower than ~targetRefreshHz.
        // 16384 @ 48k: hop = min(8192, max(2048, 2000)) = 2048 → ~23 Hz.
        const int hop = juce::jmin (fftSize / 2, juce::jmax (fftSize / 8, targetHop));
        hopSize.store (juce::jmax (1, hop), std::memory_order_relaxed);
    }

    void run() override
    {
        std::vector<float> hopScratch;
        hopScratch.reserve ((size_t) maxFftSize);

        while (! threadShouldExit())
        {
            if (fifo == nullptr || fft == nullptr)
            {
                wait (20);
                continue;
            }

            const int fftSize = getFftSize();
            const int hop = hopSize.load (std::memory_order_relaxed);
            hopScratch.resize ((size_t) hop);

            if (fifo->getNumReady() < hop)
            {
                wait (10);
                continue;
            }

            // If the audio thread got ahead, keep only the newest hop chunk.
            const int ready = fifo->getNumReady();
            if (ready > hop * 4)
            {
                const int toDrop = ready - hop;
                int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
                fifo->prepareToRead (toDrop, start1, size1, start2, size2);
                fifo->finishedRead (size1 + size2);
            }

            {
                int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
                fifo->prepareToRead (hop, start1, size1, start2, size2);
                if (size1 > 0)
                    juce::FloatVectorOperations::copy (hopScratch.data(), fifoData.data() + start1, size1);
                if (size2 > 0)
                    juce::FloatVectorOperations::copy (hopScratch.data() + size1, fifoData.data() + start2, size2);
                fifo->finishedRead (size1 + size2);
            }

            // Slide hop samples into the analysis ring (overlap-add style window).
            for (int i = 0; i < hop; ++i)
            {
                analysisRing[(size_t) analysisWrite] = hopScratch[(size_t) i];
                analysisWrite = (analysisWrite + 1) % fftSize;
            }
            analysisFilled = juce::jmin (fftSize, analysisFilled + hop);

            if (analysisFilled < fftSize)
                continue;

            computeFrame (fftSize);
        }
    }

    void computeFrame (int fftSize)
    {
        const int bins = fftSize / 2 + 1;
        if ((int) workMags.size() != bins)
            workMags.assign ((size_t) bins, magMinDb);

        // Unwrap ring so index 0 is the oldest sample.
        const int first = fftSize - analysisWrite;
        if (first > 0)
            juce::FloatVectorOperations::copy (fftData.data(), analysisRing.data() + analysisWrite, first);
        if (analysisWrite > 0)
            juce::FloatVectorOperations::copy (fftData.data() + first, analysisRing.data(), analysisWrite);

        juce::FloatVectorOperations::multiply (fftData.data(), window.data(), fftSize);
        juce::FloatVectorOperations::clear (fftData.data() + fftSize, fftSize);

        fft->performFrequencyOnlyForwardTransform (fftData.data(), true);

        const float norm = 2.0f / (float) fftSize;
        const float oneMinusSmooth = 1.0f - smoothCoeff;

        for (int bin = 0; bin < bins; ++bin)
        {
            const float mag = fftData[(size_t) bin] * norm;
            const float db = (mag > 1.0e-20f) ? (20.0f * std::log10 (mag)) : magMinDb;
            const float clamped = db < magMinDb ? magMinDb : db;
            workMags[(size_t) bin] = smoothCoeff * workMags[(size_t) bin] + oneMinusSmooth * clamped;
        }

        // Downsample to a small publish buffer with peak pooling (UI-friendly).
        const int writeIdx = 1 - publishedIndex.load (std::memory_order_relaxed);
        auto& dest = publishedDb[(size_t) writeIdx];
        if ((int) dest.size() != publishBins)
            dest.assign ((size_t) publishBins, magMinDb);

        for (int i = 0; i < publishBins; ++i)
        {
            const int start = (i * (bins - 1)) / publishBins;
            const int end = (((i + 1) * (bins - 1)) / publishBins);
            float peak = magMinDb;
            for (int b = start; b <= end; ++b)
                peak = juce::jmax (peak, workMags[(size_t) b]);
            dest[(size_t) i] = peak;
        }

        publishedIndex.store (writeIdx, std::memory_order_release);
        frameId.fetch_add (1, std::memory_order_release);
    }

    std::atomic<double> sr { 44100.0 };
    int fftOrder = defaultFftOrder;
    std::atomic<int> hopSize { 2048 };
    int samplesSinceNotify = 0;
    int analysisWrite = 0;
    int analysisFilled = 0;

    std::unique_ptr<juce::AbstractFifo> fifo;
    std::vector<float> fifoData;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftData;
    std::vector<float> window;
    std::vector<float> analysisRing;
    std::vector<float> workMags;
    std::array<std::vector<float>, 2> publishedDb;
    std::atomic<int> publishedIndex { 0 };
    std::atomic<uint32_t> frameId { 0 };
    mutable juce::SpinLock configLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};
