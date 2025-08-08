/*
  ==============================================================================

    KMeansWindowEngine.h
    Created: 7 Aug 2025 8:45:29pm
    Author:  Nicholas Boyko

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <atomic>
#include <limits>

class KMeansWindowEngine
{
public:
    KMeansWindowEngine();
    
    void prepare(double sampleRate);
    
    void resetAll();
    
    // parameters (set from processor)
    void setParameters(int kClusters,
                       int windowSizeWavesets,
                       int refreshIntervalWavesets,
                       int iterationsPerRefresh,
                       float lengthWeight);
    
    // called per completed waveset; returns a representative buffer
    const juce::AudioBuffer<float>& processWaveset(const juce::AudioBuffer<float>& newWaveset);
    
    int getNumClusters() const noexcept { return (int) centroids.size(); }
    int getWindowCount() const noexcept { return countInWindow; }
    
private:
    struct Entry
    {
        int length = 0;
        float rms = 0.0f;
        juce::AudioBuffer<float> audio;
    };
    
    // ring buffer for window data
    std::vector<Entry> ring; // size = windowSize
    int ringWriteIndex = 0;
    int countInWindow = 0; // number of valid entries [0..windowSize]
    
    struct PendingParams
    {
        std::atomic<bool> hasChanges { false };
        std::atomic<int> k { 8 };
        std::atomic<int> windowSize { 256 };
        std::atomic<int> refreshInterval { 32 };
        std::atomic<int> iterations { 3 };
        std::atomic<float> lengthWeight { 5.0f };
    };
    
    PendingParams pending;
    int currentK = 8;
    int currentWindowSize = 256;
    int currentRefreshInterval = 32;
    int currentIterations = 3;
    float currentLengthWeight = 5.0f;
    
    // Apply pending parameter changes safely (audio thread only)
    void applyPendingParams();
    
    int wavesetsSinceRefresh = 0;
    
    std::vector<std::array<float,2>> centroids;
    std::vector<int> representatives; // index into ring
    
    juce::AudioBuffer<float> lastChosen;
    
    float meanLen = 0.0f, stdLen = 1.0f;
    float meanRms = 0.0f, stdRms = 1.0f;
    
    std::vector<std::array<float,2>> featuresNorm;
    std::vector<int> assignments;
    
    double sampleRate;
    
    static std::array<float,2> extractFeatures(const juce::AudioBuffer<float>& waveset);
    void ensureWindowCapacity();
    void writeEntry(const juce::AudioBuffer<float>& ws, const std::array<float,2>& raw);
    
    void refreshModel(); // compute mean/std, normalize, run k-means, pick reps
    
    void computeWindowStats(float& muLen, float& sdLen, float& muRms, float& sdRms) const;
    std::array<float,2> normalizeFeature(const std::array<float,2>& raw) const;
    
    int nearestCentroid(const std::array<float,2>& x) const;
    float distance2(const std::array<float,2>& a, const std::array<float,2>& b) const;

    // Quantization: returns representative index or -1
    int quantizeIndexFor(const std::array<float,2>& raw) const;

    // Safety
    static inline float safeStd(float s) { return s < 1e-6f ? 1.0f : s; }
};
