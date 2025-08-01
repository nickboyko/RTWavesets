/*
  ==============================================================================

    RTEFC_Engine.cpp
    Created: 31 Jul 2025 4:36:49pm
    Author:  Nicholas Boyko

  ==============================================================================
*/

#include "RTEFC_Engine.h"

// ===========================================================
RTEFC_Engine::RTEFC_Engine()
{
    reset();
}

void RTEFC_Engine::prepare(double sampleRate)
{
    reset();
}

void RTEFC_Engine::reset()
{
    // clear matrices and waveset buffer
    centroids.clear();
    representativeWavesets.clear();
    lastChosenWaveset.setSize(0, 0);
    
    // reset online normalizer state
    wavesetCount = 0;
    lengthMean = 0.0;
    lengthM2 = 0.0;
    rmsMean = 0.0;
    rmsM2 = 0.0;
}

void RTEFC_Engine::setParameters(float newRadius, float newAlpha, float newWeight, float newMaxClusters)
{
    radius = newRadius;
    alpha = newAlpha;
    weight = newWeight;
    maxClusters = newMaxClusters;
}

const juce::AudioBuffer<float>& RTEFC_Engine::processWaveset(const juce::AudioBuffer<float> &newWaveset)
{
    // waveset length & rms feature extraction
    auto rawFeatures = extractFeatures(newWaveset);
    
    updateNormalizers(rawFeatures);
    auto normalizedFeatures = getNormalizedFeatures(rawFeatures);
    
    // RTEFC algorithm
    if (centroids.empty())
    {
        // first waveset, becomes first centroid
        centroids.push_back(normalizedFeatures);
        representativeWavesets.push_back(newWaveset);
        lastChosenWaveset = newWaveset;
        return lastChosenWaveset;
    }
    
    // find closest existing centroid
    float d_close = 0.0f;
    int closest_idx = findClosestCentroid(normalizedFeatures, d_close);
    
    // if new case is novel, and we have room to look for more clusters...
    if (d_close > radius && centroids.size() < maxClusters)
    {
        // add s_new as new centroid
        centroids.push_back(normalizedFeatures);
        
        // new waveset becomes representative for this cluster
        representativeWavesets.push_back(newWaveset);
        lastChosenWaveset = newWaveset;
    }
    else
    {
        // otherwise, we just update the closest existing centroid with exponential filtering
        auto& s_close = centroids[closest_idx];
        for (size_t i = 0; i < s_close.size(); ++i)
        {
            s_close[i] = alpha * s_close[i] + (1.0f - alpha) * normalizedFeatures[i];
        }
        
        // use representative waveset of closest cluster
        lastChosenWaveset = representativeWavesets[closest_idx];
    }
    
    return lastChosenWaveset;
}

// =============================================
// private helper methods
// =============================================

std::vector<float> RTEFC_Engine::extractFeatures(const juce::AudioBuffer<float> &waveset)
{
    float length = static_cast<float>(waveset.getNumSamples());
    
    float rms = waveset.getRMSLevel(0, 0, waveset.getNumSamples());
    
    return { length, rms };
}

void RTEFC_Engine::updateNormalizers(const std::vector<float> &rawFeatures)
{
    wavesetCount++;
    float length = rawFeatures[0];
    float rms = rawFeatures[1];
    
    // welford's algorithm:
    // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
    double delta_len = length - lengthMean;
    lengthMean += delta_len / wavesetCount;
    double delta2_len = length - lengthMean;
    lengthM2 += delta_len * delta2_len;
    
    double delta_rms = rms - rmsMean;
    rmsMean += delta_rms / wavesetCount;
    double delta2_rms = rms - rmsMean;
    rmsM2 += delta_rms * delta2_rms;
}

std::vector<float> RTEFC_Engine::getNormalizedFeatures(const std::vector<float> &rawFeatures)
{
    if (wavesetCount < 2)
    {
        // not enough data to normalize, so return un-normalized but weighted features
        return { rawFeatures[0] * weight, rawFeatures[1] };
    }
    
    double lengthVar = lengthM2 / wavesetCount;
    double rmsVar = rmsM2 / wavesetCount;
    
    double lengthStdDev = std::sqrt(lengthVar) + 1e-8;
    double rmsStdDev = std::sqrt(rmsVar) + 1e-8;
    
    float scaledLength = (rawFeatures[0] - lengthMean) / lengthStdDev;
    float scaledRms = (rawFeatures[1] - rmsMean) / rmsStdDev;
    
    return { scaledLength * weight, scaledRms };
}

int RTEFC_Engine::findClosestCentroid(const std::vector<float> &features, float &distanceFound)
{
    int closestIndex = -1;
    float minDistanceSq = std::numeric_limits<float>::max();
    
    for (size_t i = 0; i < centroids.size(); ++i)
    {
        // squared Euclidean distance
        float distSq = 0.0f;
        const auto& centroid = centroids[i];
        
        float diff0 = features[0] - centroid[0];
        float diff1 = features[1] - centroid[1];
        distSq = (diff0 * diff0) + (diff1 * diff1);
        
        if (distSq < minDistanceSq)
        {
            minDistanceSq = distSq;
            closestIndex = static_cast<int>(i);
        }
    }
    
    distanceFound = std::sqrt(minDistanceSq);
    return closestIndex;
}
