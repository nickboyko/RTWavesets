/*
  ==============================================================================

    KMeansWindowEngine.cpp
    Created: 7 Aug 2025 8:45:29pm
    Author:  Nicholas Boyko

  ==============================================================================
*/

#include "KMeansWindowEngine.h"

KMeansWindowEngine::KMeansWindowEngine() {}

void KMeansWindowEngine::prepare(double sr)
{
    sampleRate = sr;
    resetAll();
}


void KMeansWindowEngine::resetAll()
{
    ring.clear();
    ringWriteIndex = 0;
    countInWindow = 0;
    
    centroids.clear();
    representatives.clear();
    
    featuresNorm.clear();
    assignments.clear();
    
    lastChosen.setSize(0, 0);
    
    wavesetsSinceRefresh = 0;
    
    ensureWindowCapacity();
}


void KMeansWindowEngine::setParameters(int kClusters, int windowSizeWavesets, int refreshIntervalWavesets, int iterationsPerRefresh, float lengthWeightParam)
{
    pending.k.store(juce::jlimit(2, 48, kClusters));
    pending.windowSize.store(juce::jlimit(64, 1024, windowSizeWavesets));
    pending.refreshInterval.store(juce::jlimit(1, 128, refreshIntervalWavesets));
    pending.iterations.store(juce::jlimit(1, 8, iterationsPerRefresh));
    pending.lengthWeight.store(juce::jlimit(0.1f, 24.0f, lengthWeightParam));
    
    pending.hasChanges.store(true);
}

void KMeansWindowEngine::applyPendingParams()
{
    if (!pending.hasChanges.load())
        return;
        
    // Apply all changes atomically on audio thread
    currentK = pending.k.load();
    currentWindowSize = pending.windowSize.load();
    currentRefreshInterval = pending.refreshInterval.load();
    currentIterations = pending.iterations.load();
    currentLengthWeight = pending.lengthWeight.load();
    
    // Resize data structures safely
    countInWindow = std::min(countInWindow, currentWindowSize);
    
    // Resize ring buffer
    if ((int)ring.size() != currentWindowSize)
    {
        const int maxLen = (int) std::round(sampleRate * 2.0);
        std::vector<Entry> newRing((size_t) currentWindowSize);
        
        const int toCopy = std::min(currentWindowSize, (int)ring.size());
        for (int i = 0; i < currentWindowSize; ++i)
        {
            auto& e = newRing[(size_t) i];
            e.audio.setSize(2, maxLen, false, false, true);
            e.audio.clear();
            
            if (i < toCopy && i < (int)ring.size())
            {
                e.length = ring[(size_t) i].length;
                e.rms = ring[(size_t) i].rms;
                const int copyLen = std::min(maxLen, ring[(size_t) i].audio.getNumSamples());
                for (int ch = 0; ch < 2; ++ch)
                    e.audio.copyFrom(ch, 0, ring[(size_t) i].audio, ch, 0, copyLen);
            }
            else
            {
                e.length = 0;
                e.rms = 0.0f;
            }
        }
        ring.swap(newRing);
        
        ringWriteIndex = juce::jlimit(0, std::max(0, currentWindowSize - 1), ringWriteIndex);
        countInWindow = std::min(countInWindow, currentWindowSize);
    }
    
    // Resize working arrays
    featuresNorm.resize((size_t) currentWindowSize);
    assignments.resize((size_t) currentWindowSize);
    
    // Resize cluster arrays - critical for the crash fix
    const int kk = std::min(currentK, std::max(1, countInWindow));
    centroids.resize((size_t) kk);
    representatives.assign((size_t) kk, -1);
    
    pending.hasChanges.store(false);
}

const juce::AudioBuffer<float>& KMeansWindowEngine::processWaveset(const juce::AudioBuffer<float>& newWaveset)
{
    applyPendingParams();
        
    if (newWaveset.getNumSamples() <= 0 || newWaveset.getNumChannels() <= 0)
        return lastChosen;

    auto raw = extractFeatures(newWaveset);
    lastProcessedFeatures = normalizeFeature(raw);
    writeEntry(newWaveset, raw);

    wavesetsSinceRefresh++;
    if (wavesetsSinceRefresh >= currentRefreshInterval)
    {
        refreshModel();
        wavesetsSinceRefresh = 0;
    }

    const int repIdx = quantizeIndexFor(raw);
    if (repIdx >= 0 && repIdx < (int)ring.size() && repIdx < countInWindow)
    {
        const auto& src = ring[(size_t) repIdx].audio;
        lastChosen.setSize(src.getNumChannels(), src.getNumSamples(), false, false, true);
        for (int ch = 0; ch < src.getNumChannels(); ++ch)
            lastChosen.copyFrom(ch, 0, src, ch, 0, src.getNumSamples());
    }
    else
    {
        lastChosen.makeCopyOf(newWaveset);
    }

    return lastChosen;
}

std::array<float,2> KMeansWindowEngine::extractFeatures(const juce::AudioBuffer<float> &waveset)
{
    const int len = waveset.getNumSamples();
    const float rms = waveset.getRMSLevel(0, 0, len);
    return { (float) len, rms };
}

void KMeansWindowEngine::ensureWindowCapacity()
{
    const int target = currentWindowSize;
    if ((int)ring.size() != target)
    {
        const int maxLen = (int) std::round(sampleRate * 2.0);
        const int numCh = 2;

        std::vector<Entry> newRing((size_t) target);
        const int toCopy = std::min(target, (int)ring.size());
        for (int i = 0; i < target; ++i)
        {
            auto& e = newRing[(size_t) i];
            e.audio.setSize(numCh, maxLen, false, false, true);
            e.audio.clear();
            if (i < toCopy)
            {
                e.length = ring[(size_t) i].length;
                e.rms = ring[(size_t) i].rms;
                const int copyLen = std::min(maxLen, ring[(size_t) i].audio.getNumSamples());
                const int chs = std::min(numCh, ring[(size_t) i].audio.getNumChannels());
                for (int ch = 0; ch < chs; ++ch)
                    e.audio.copyFrom(ch, 0, ring[(size_t) i].audio, ch, 0, copyLen);
            }
            else
            {
                e.length = 0;
                e.rms = 0.0f;
            }
        }
        ring.swap(newRing);

        ringWriteIndex = juce::jlimit(0, std::max(0, target - 1), ringWriteIndex);
        countInWindow = std::min(countInWindow, target);

        for (auto& ridx : representatives)
        {
            if (ridx < 0 || ridx >= target)
                ridx = -1;
        }
    }

    if ((int)featuresNorm.size() != target) featuresNorm.resize((size_t) target);
    if ((int)assignments.size() != target) assignments.resize((size_t) target);
}

void KMeansWindowEngine::writeEntry(const juce::AudioBuffer<float>& ws, const std::array<float,2>& raw)
{
    ensureWindowCapacity();

    Entry& e = ring[(size_t) ringWriteIndex];
    e.length = (int) raw[0];
    e.rms = raw[1];

    const int copyLen = std::min(e.audio.getNumSamples(), ws.getNumSamples());
    const int chs = std::min(e.audio.getNumChannels(), ws.getNumChannels());
    e.audio.clear();
    for (int ch = 0; ch < chs; ++ch)
        e.audio.copyFrom(ch, 0, ws, ch, 0, copyLen);

    ringWriteIndex = (ringWriteIndex + 1) % std::max(1, currentWindowSize);
    countInWindow = std::min(countInWindow + 1, currentWindowSize);
}

void KMeansWindowEngine::computeWindowStats(float& muLen, float& sdLen, float& muRms, float& sdRms) const
{
    const int n = countInWindow;
    if (n <= 0)
    {
        muLen = 0; sdLen = 1; muRms = 0; sdRms = 1;
        return;
    }

    double sLen = 0, sRms = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto& e = ring[(size_t) i];
        sLen += e.length;
        sRms += e.rms;
    }
    muLen = (float)(sLen / n);
    muRms = (float)(sRms / n);

    double vLen = 0, vRms = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto& e = ring[(size_t) i];
        const double dl = e.length - muLen;
        const double dr = e.rms - muRms;
        vLen += dl * dl;
        vRms += dr * dr;
    }
    sdLen = safeStd((float) std::sqrt(std::max(1e-12, vLen / n)));
    sdRms = safeStd((float) std::sqrt(std::max(1e-12, vRms / n)));
}

std::array<float,2> KMeansWindowEngine::normalizeFeature(const std::array<float,2>& raw) const
{
    float x0 = (raw[0] - meanLen) / stdLen;
    float x1 = (raw[1] - meanRms) / stdRms;

    x0 *= currentLengthWeight;
    return { x0, x1 };
}

int KMeansWindowEngine::nearestCentroid(const std::array<float,2>& x) const
{
    if (centroids.empty()) return -1;
    int best = -1;
    float bestD2 = std::numeric_limits<float>::max();
    for (int i = 0; i < (int)centroids.size(); ++i)
    {
        const float d2 = distance2(x, centroids[(size_t) i]);
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

float KMeansWindowEngine::distance2(const std::array<float,2>& a, const std::array<float,2>& b) const
{
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    return dx*dx + dy*dy;
}

int KMeansWindowEngine::quantizeIndexFor(const std::array<float,2>& raw) const
{
    const int n = countInWindow;
    if (centroids.empty() || n <= 0) return -1;

    std::array<float,2> x = normalizeFeature(raw);
    const int cidx = nearestCentroid(x);
    if (cidx < 0 || cidx >= (int)representatives.size()) return -1;

    const int repRingIdx = representatives[(size_t) cidx];
    if (repRingIdx < 0 || repRingIdx >= n) return -1;
    return repRingIdx;
}

void KMeansWindowEngine::refreshModel()
{
    const int n = countInWindow;
    if (n <= 0) return;

    const int kk = std::min(currentK, n);
    if (kk <= 0) return;
    
    // Ensure arrays match current parameters
    if ((int)centroids.size() != kk) centroids.resize((size_t) kk);
    if ((int)representatives.size() != kk) representatives.assign((size_t) kk, -1);

    // 1) Compute normalization stats
    computeWindowStats(meanLen, stdLen, meanRms, stdRms);

    // 2) Build normalized features
    for (int i = 0; i < n; ++i)
        featuresNorm[(size_t) i] = normalizeFeature({ (float) ring[(size_t) i].length, ring[(size_t) i].rms });

    // 3) Initialize centroids
    centroids[0] = featuresNorm[(size_t)(n / 2)];
    for (int ci = 1; ci < kk; ++ci)
    {
        int farIdx = 0;
        float farDist = -1.0f;
        for (int i = 0; i < n; ++i)
        {
            float d2min = std::numeric_limits<float>::max();
            for (int cj = 0; cj < ci; ++cj)
                d2min = std::min(d2min, distance2(featuresNorm[(size_t) i], centroids[(size_t) cj]));
            if (d2min > farDist) { farDist = d2min; farIdx = i; }
        }
        centroids[(size_t) ci] = featuresNorm[(size_t) farIdx];
    }

    // 4) Lloyd iterations
    for (int it = 0; it < currentIterations; ++it)
    {
        // Assign each point to nearest centroid
        for (int i = 0; i < n; ++i)
        {
            int best = 0;
            float bestD2 = std::numeric_limits<float>::max();
            for (int ci = 0; ci < kk; ++ci)
            {
                const float d2 = distance2(featuresNorm[(size_t) i], centroids[(size_t) ci]);
                if (d2 < bestD2) { bestD2 = d2; best = ci; }
            }
            assignments[(size_t) i] = best;
        }

        // Update centroids
        std::vector<std::array<double,2>> sum((size_t) kk, {0.0, 0.0});
        std::vector<int> cnt((size_t) kk, 0);
        for (int i = 0; i < n; ++i)
        {
            const int a = assignments[(size_t) i];
            if (a >= 0 && a < kk) // Defensive bounds check
            {
                const auto& x = featuresNorm[(size_t) i];
                sum[(size_t) a][0] += x[0];
                sum[(size_t) a][1] += x[1];
                cnt[(size_t) a] += 1;
            }
        }
        for (int ci = 0; ci < kk; ++ci)
        {
            if (cnt[(size_t) ci] > 0)
            {
                centroids[(size_t) ci][0] = (float)(sum[(size_t) ci][0] / cnt[(size_t) ci]);
                centroids[(size_t) ci][1] = (float)(sum[(size_t) ci][1] / cnt[(size_t) ci]);
            }
        }
    }

    // 5) Select representatives
    for (int ci = 0; ci < kk; ++ci)
    {
        int bestIdx = -1;
        float bestD2 = std::numeric_limits<float>::max();
        for (int i = 0; i < n; ++i)
        {
            if (assignments[(size_t) i] != ci) continue;
            const float d2 = distance2(featuresNorm[(size_t) i], centroids[(size_t) ci]);
            if (d2 < bestD2) { bestD2 = d2; bestIdx = i; }
        }
        representatives[(size_t) ci] = (bestIdx >= 0 && bestIdx < n) ? bestIdx : -1;
    }
}

std::vector<std::array<float,2>> KMeansWindowEngine::getVisualizationCentroids() const
{
    return centroids;
}

std::vector<std::array<float,2>> KMeansWindowEngine::getWindowPoints() const
{
    std::vector<std::array<float,2>> points;
    const int n = std::min(countInWindow, (int)featuresNorm.size());
    points.reserve((size_t)n);
    for (int i = 0; i < n; ++i)
        points.push_back(featuresNorm[(size_t)i]);
    return points;
}

std::vector<int> KMeansWindowEngine::getWindowAssignments() const
{
    std::vector<int> assigns;
    const int n = std::min(countInWindow, (int)assignments.size());
    assigns.reserve((size_t)n);
    for (int i = 0; i < n; ++i)
        assigns.push_back(assignments[(size_t)i]);
    return assigns;
}

std::optional<std::array<float,2>> KMeansWindowEngine::getCurrentPoint() const
{
    return lastProcessedFeatures;
}

