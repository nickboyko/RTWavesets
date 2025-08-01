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

class RTEFC_Engine
{
public:
    // ===========================================================
    RTEFC_Engine();
    
    void prepare(double sampleRate);
    void reset();

    // takes waveset and returns the chosen representative from its cluster
    const juce::AudioBuffer<float>& processWaveset(const juce::AudioBuffer<float>& newWaveset);
    
    // called from processor
    void setParameters(float newRadius, float newAlpha, float newWeight, float newMaxClusters);
    
private:
    // ===========================================================
    // feature (centroids) vector matrix S
    std::vector<std::vector<float>> centroids;
    
    // representative audio wavesets for each cluster
    std::vector<juce::AudioBuffer<float>> representativeWavesets;
    
    // waveset history
    juce::AudioBuffer<float> lastChosenWaveset;
    
    // real-time normalization params with welford's algorithm
    long long wavesetCount { 0 };
    double lengthMean      { 0.0 };
    double lengthM2        { 0.0 }; // sum of squares difference from mean
    double rmsMean         { 0.0 };
    double rmsM2           { 0.0 };
    
    // RTEFC parameters
    float radius           { 1.5f };
    float alpha            { 0.98f };
    float weight           { 5.0f };
    float maxClusters      { 10.0f };
    
    // helper methods
    // calculates waveset length & rms features for a single waveset
    std::vector<float> extractFeatures(const juce::AudioBuffer<float>& waveset);
    
    // updates online mean & variance using welford's algorithm
    void updateNormalizers(const std::vector<float>& rawFeatures);
    
    // uses running stats to normalize raw features
    std::vector<float> getNormalizedFeatures(const std::vector<float>& rawFeatures);
    
    // finds index of closest centroid to given feature vector
    int findClosestCentroid(const std::vector<float>& features, float& distanceFound);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RTEFC_Engine)
};
