/*
  ==============================================================================

    ClusterVisualizationComponent.h
    Created: 7 Aug 2025 9:59:22pm
    Author:  Nicholas Boyko

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ClusterVisualizationComponent : public juce::Component,
                                      private juce::Timer
{
public:
    ClusterVisualizationComponent(RTWavesetsAudioProcessor& processor);
    ~ClusterVisualizationComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void setVisible(bool shouldBeVisible) override;
    
private:
    void timerCallback() override;
    std::atomic<bool> isBeingDestroyed { false };
    juce::Point<float> featureToScreen(const std::array<float,2>& feature) const;
    std::array<float,2> screenToFeature(const juce::Point<float>& screen) const;
    
    void drawRTEFCVisualization(juce::Graphics& g);
    void drawKMeansVisualization(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawFeatureAxes(juce::Graphics& g);
    
    RTWavesetsAudioProcessor& audioProcessor;
    
    float minX = -3.0f, maxX = 3.0f;
    float minY = -2.0f, maxY = 2.0f;
    
    // cache data for smooth updates
    std::vector<std::array<float,2>> cachedCentroids;
    std::vector<std::array<float,2>> cachedRecentPoints;
    std::vector<int> cachedAssignments;
    std::array<float,2> cachedCurrentPoint = {0.0f, 0.0f};
    bool hasCurrentPoint = false;
    
    static const std::vector<juce::Colour> clusterColors;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClusterVisualizationComponent)
};
