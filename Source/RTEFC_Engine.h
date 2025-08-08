/*
  ==============================================================================

    RTEFC_Engine.h
    Created: 31 Jul 2025 4:36:49pm
    Author:  Nicholas Boyko

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <limits>

class RTEFC_Engine
{
public:
    // ===========================================================
    RTEFC_Engine();
    
    void prepare(double sampleRate);
    
    void resetAll();          // hard reset: stats + clusters
    void resetClustersOnly(); // soft reset, no stats

    // takes waveset and returns the chosen representative from its cluster
    const juce::AudioBuffer<float>& processWaveset(const juce::AudioBuffer<float>& newWaveset);
    
    // called from processor
    void setParameters(float newRadius, float newAlpha, float newWeight, float newMaxClusters, float newNormHalfLifeWavesets, bool newAutoRadius);
    
    int getNumClusters() const noexcept { return (int) centroids.size(); }
    float getDistanceEMA() const noexcept { return distanceEma; }
    
private:
    // ===========================================================
    // feature (centroids) vector matrix S
    std::vector<std::array<float,2>> centroids;
    
    // representative audio wavesets for each cluster
    std::vector<juce::AudioBuffer<float>> representatives;
    
    // waveset history
    juce::AudioBuffer<float> lastChosenWaveset;
    
    // real-time normalization params with EMA
    double lengthMean{0.0}, lengthVarEma{1.0};
    double rmsMean{0.0},    rmsVarEma{1.0};
    long long wavesetCount{0};
    
    float normHalfLifeWavesets{64.f};
    float beta{0.0108f};
    static constexpr float kLn2 = 0.69314718056f;
    
    // RTEFC parameters
    std::atomic<float> radius           { 1.5f };
    std::atomic<float> alpha            { 0.98f };
    std::atomic<float> weight           { 5.0f };
    std::atomic<float> maxClusters      { 128.f };
    std::atomic<bool>  autoRadius       { false };
    
    float distanceEma{0.0f};
    float distanceEmaBeta{0.05f};
    
    // helper methods
    // calculates waveset length & rms features for a single waveset
    std::array<float,2> extractFeatures(const juce::AudioBuffer<float>& waveset) const;
    
    static inline void emaUpdate(double x, float b, double& mean, double& varEma)
    {
        mean = (1.0 - b) * mean + b * x;
        const double diff = x - mean;
        varEma = (1.0 - b) * varEma + b * (diff * diff);
    }
    
    // uses running stats to normalize raw features
    std::array<float,2> getNormalizedFeatures(const std::array<float,2>& raw) const;
    
    // finds index of closest centroid to given feature vector
    int findClosestCentroid(const std::array<float,2>& features, float& distanceFound) const;
    
    void reserveForMaxClusters();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RTEFC_Engine)
};
