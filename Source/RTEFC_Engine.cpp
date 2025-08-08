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
    resetAll();
}

void RTEFC_Engine::prepare(double sampleRate)
{
    resetAll();
}

void RTEFC_Engine::resetAll()
{
    // reset online normalizer state
    wavesetCount = 0;
    lengthMean = rmsMean = 0.0;
    lengthVarEma = rmsVarEma = 1.0;
    distanceEma = 0.0f;
    resetClustersOnly();
}

void RTEFC_Engine::resetClustersOnly()
{
    // clear matrices and waveset buffer
    centroids.clear();
    representatives.clear();
    lastChosenWaveset.setSize(0, 0);
    reserveForMaxClusters();
}

void RTEFC_Engine::reserveForMaxClusters()
{
    const int cap = (int) std::max(8.0f, maxClusters.load());
    centroids.reserve((size_t) cap);
    representatives.reserve((size_t) cap);
}

void RTEFC_Engine::setParameters(float newRadius, float newAlpha, float newLenWeight, float newMaxClusters, float newNormHalfLifeWavesets, bool newAutoRadius)
{
    radius.store(newRadius);
    alpha.store(newAlpha);
    weight.store(newLenWeight);
    maxClusters.store(newMaxClusters);
    autoRadius.store(newAutoRadius);
    
    if (newNormHalfLifeWavesets > 1.f && newNormHalfLifeWavesets != normHalfLifeWavesets) {
        normHalfLifeWavesets = newNormHalfLifeWavesets;
        beta = kLn2 / normHalfLifeWavesets;
        beta = juce::jlimit(0.001f, 0.5f, beta);
    }
    
    reserveForMaxClusters();
}

const juce::AudioBuffer<float>& RTEFC_Engine::processWaveset(const juce::AudioBuffer<float> &newWaveset)
{
    if (newWaveset.getNumSamples() <= 0 || newWaveset.getNumChannels() <= 0)
        return lastChosenWaveset;
    
    // waveset length & rms feature extraction
    auto raw = extractFeatures(newWaveset);
    
    wavesetCount++;
    emaUpdate(raw[0], beta, lengthMean, lengthVarEma);
    emaUpdate(raw[1], beta, rmsMean,    rmsVarEma);
    
    auto features = getNormalizedFeatures(raw);
    
    lastProcessedFeatures = features;
    recentPoints.push_back(features);
    if (recentPoints.size() > maxRecentPoints)
        recentPoints.erase(recentPoints.begin());
    
    // RTEFC algorithm
    if (centroids.empty())
    {
        // first waveset, becomes first centroid
        centroids.push_back(features);
        representatives.emplace_back();
        representatives.back().makeCopyOf(newWaveset);
        lastChosenWaveset = representatives.back();
        return lastChosenWaveset;
    }
    
    if (centroids.size() != representatives.size())
    {
        const size_t n = std::min(centroids.size(), representatives.size());
        centroids.resize(n);
        representatives.resize(n);
        if (n == 0)
        {
            centroids.push_back(features);
            representatives.emplace_back();
            representatives.back().makeCopyOf(newWaveset);
            lastChosenWaveset = representatives.back();
            return lastChosenWaveset;
        }
    }
    
    // find closest existing centroid
    float d_close = 0.0f;
    const int closest_idx = findClosestCentroid(features, d_close);
    if (closest_idx < 0 || closest_idx >= (int)centroids.size())
   {
       centroids.push_back(features);
       representatives.emplace_back();
       representatives.back().makeCopyOf(newWaveset);
       lastChosenWaveset = representatives.back();
       return lastChosenWaveset;
   }
    
    distanceEma = (1.0f - distanceEmaBeta) * distanceEma + distanceEmaBeta * d_close;
    
    float radiusEff = radius.load();
    if (autoRadius.load() && distanceEma > 0.0f)
        radiusEff = std::max(radiusEff, 1.25f * distanceEma);
    
    const bool haveRoom = (int)centroids.size() < (int)maxClusters.load();
    
    // if new case is novel, and we have room to look for more clusters...
    if (d_close > radiusEff && haveRoom)
    {
        // add s_new as new centroid
        centroids.push_back(features);
        
        // new waveset becomes representative for this cluster
        representatives.emplace_back();
        representatives.back().makeCopyOf(newWaveset);
        lastChosenWaveset = representatives.back();
    }
    else
    {
        // otherwise, we just update the closest existing centroid with exponential filtering
        auto& s_close = centroids[(size_t) closest_idx];
        DBG("now playing: " << centroids[closest_idx][0]);
        const float a = alpha.load();
        for (size_t i = 0; i < s_close.size(); ++i)
            s_close[i] = a * s_close[i] + (1.0f - a) * features[i];
        
        // use representative waveset of closest cluster
        if ((size_t)closest_idx < representatives.size())
            lastChosenWaveset = representatives[(size_t) closest_idx];
        else
        {
            const size_t n = std::min(centroids.size(), representatives.size());
            centroids.resize(n);
            representatives.resize(n);
            if (n == 0)
            {
                centroids.push_back(features);
                representatives.emplace_back();
            }
            representatives.back().makeCopyOf(newWaveset);
            lastChosenWaveset = representatives.back();
        }
        
    }
    
    return lastChosenWaveset;
}

// =============================================
// private helper methods
// =============================================

std::array<float,2> RTEFC_Engine::extractFeatures(const juce::AudioBuffer<float> &waveset) const
{
    float length = static_cast<float>(waveset.getNumSamples());
    // feature extraction is only left channel for now
    float rms = waveset.getRMSLevel(0, 0, waveset.getNumSamples());
    return { length, rms };
}

std::array<float,2> RTEFC_Engine::getNormalizedFeatures(const std::array<float,2> &raw) const
{
    // compute std from EMA variances with caution to avoid divide-by-0
    const double lenStd = std::sqrt(std::max(1e-10, lengthVarEma));
    const double rmsStd = std::sqrt(std::max(1e-10, rmsVarEma));

    float f0 = (float)((raw[0] - lengthMean) / lenStd);
    const double logR = std::log(std::max(1e-6f, raw[1]));
    const double logRmean = std::log(std::max(1e-6, rmsMean));
    float f1 = (float)((logR - logRmean) / std::max(1e-6, rmsStd));

    f0 *= weight.load();

    return { f0, f1 };
}

int RTEFC_Engine::findClosestCentroid(const std::array<float,2> &features, float &distanceFound) const
{
    int closestIndex = -1;
    float minDistanceSq = std::numeric_limits<float>::max();
    
    for (size_t i = 0; i < centroids.size(); ++i)
    {
        const auto& c = centroids[i];
        const float dx = features[0] - c[0];
        const float dy = features[1] - c[1];
        const float d2 = dx*dx + dy*dy;

        if (d2 < minDistanceSq)
        {
            minDistanceSq = d2;
            closestIndex = (int) i;
        }
    }
    
    distanceFound = std::sqrt(std::max(0.0f, minDistanceSq));
    return closestIndex;
}

std::vector<std::array<float,2>> RTEFC_Engine::getVisualizationCentroids() const
{
    return centroids;
}

std::vector<std::array<float,2>> RTEFC_Engine::getRecentPoints() const
{
    return recentPoints;
}

std::optional<std::array<float,2>> RTEFC_Engine::getCurrentPoint() const
{
    return lastProcessedFeatures;
}
