/*
  ==============================================================================

    ClusterVisualizationComponent.cpp
    Created: 7 Aug 2025 9:59:22pm
    Author:  Nicholas Boyko

  ==============================================================================
*/

#include "ClusterVisualizationComponent.h"

const std::vector<juce::Colour> ClusterVisualizationComponent::clusterColors = {
    juce::Colours::red, juce::Colours::blue, juce::Colours::green, juce::Colours::orange,
    juce::Colours::purple, juce::Colours::cyan, juce::Colours::yellow, juce::Colours::magenta,
    juce::Colours::lime, juce::Colours::pink, juce::Colours::lightblue, juce::Colours::lightgreen,
    juce::Colours::lightyellow, juce::Colours::lightcyan, juce::Colours::lightgrey, juce::Colours::darkred,
    juce::Colours::darkblue, juce::Colours::darkgreen, juce::Colours::darkorange, juce::Colours::darkviolet
};

ClusterVisualizationComponent::ClusterVisualizationComponent(RTWavesetsAudioProcessor& processor)
    : audioProcessor(processor)
{
    cachedCentroids.reserve(32);
    cachedRecentPoints.reserve(100);
    cachedAssignments.reserve(1024);
    
    setSize(300, 200); // Ensure minimum size
}

ClusterVisualizationComponent::~ClusterVisualizationComponent()
{
    isBeingDestroyed.store(true);
    stopTimer();
}

void ClusterVisualizationComponent::paint(juce::Graphics& g)
{
    // Defensive bounds checking
    const auto bounds = getLocalBounds();
    if (bounds.getWidth() <= 80 || bounds.getHeight() <= 80)
    {
        g.fillAll(juce::Colours::darkgrey);
        g.setColour(juce::Colours::white);
        g.drawText("Too small", bounds, juce::Justification::centred);
        return;
    }
    
    // Background
    g.fillAll(juce::Colours::black);
    
    try
    {
        // Grid and axes
        drawGrid(g);
        drawFeatureAxes(g);
        
        // Mode-specific visualization
        const auto mode = static_cast<EngineMode>(audioProcessor.apvts.getRawParameterValue("engine_mode")->load());
        if (mode == EngineMode::RTEFC)
            drawRTEFCVisualization(g);
        else
            drawKMeansVisualization(g);
        
        // Title
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        const juce::String title = (mode == EngineMode::RTEFC) ? "RTEFC Feature Space" : "K-Means Feature Space";
        g.drawText(title, getLocalBounds().removeFromTop(20), juce::Justification::centred);
    }
    catch (...)
    {
        // Fallback rendering on error
        g.fillAll(juce::Colours::red.withAlpha(0.1f));
        g.setColour(juce::Colours::white);
        g.drawText("Visualization Error", bounds, juce::Justification::centred);
    }
}

void ClusterVisualizationComponent::resized() {}

void ClusterVisualizationComponent::timerCallback()
{
    if (isBeingDestroyed.load())
    {
        stopTimer();
        return;
    }
    
    // Critical: Check if component is still valid
    if (!isShowing() || getWidth() <= 0 || getHeight() <= 0)
        return;
        
    // Additional safety: check parent chain
    if (getTopLevelComponent() == nullptr)
    {
        stopTimer();
        return;
    }
    
    try
    {
        const auto mode = static_cast<EngineMode>(audioProcessor.apvts.getRawParameterValue("engine_mode")->load());
        
        if (mode == EngineMode::RTEFC)
        {
            cachedCentroids = audioProcessor.rtefcEngine.getVisualizationCentroids();
            cachedRecentPoints = audioProcessor.rtefcEngine.getRecentPoints();
            auto currentOpt = audioProcessor.rtefcEngine.getCurrentPoint();
            hasCurrentPoint = currentOpt.has_value();
            if (hasCurrentPoint)
                cachedCurrentPoint = currentOpt.value();
        }
        else
        {
            cachedCentroids = audioProcessor.kmeansEngine.getVisualizationCentroids();
            cachedRecentPoints = audioProcessor.kmeansEngine.getWindowPoints();
            cachedAssignments = audioProcessor.kmeansEngine.getWindowAssignments();
            auto currentOpt = audioProcessor.kmeansEngine.getCurrentPoint();
            hasCurrentPoint = currentOpt.has_value();
            if (hasCurrentPoint)
                cachedCurrentPoint = currentOpt.value();
        }
        
        repaint();
    }
    catch (...)
    {
        // Silently handle any engine access errors
        stopTimer();
        startTimerHz(10); // Restart at lower frequency
    }
}

juce::Point<float> ClusterVisualizationComponent::featureToScreen(const std::array<float,2>& feature) const
{
    const auto totalBounds = getLocalBounds();
    if (totalBounds.getWidth() <= 80 || totalBounds.getHeight() <= 80)
        return {0.0f, 0.0f};
        
    const auto bounds = totalBounds.toFloat().reduced(40.0f).removeFromBottom(getHeight() - 40);
    
    // Clamp feature values to prevent extreme coordinates
    const float clampedX = juce::jlimit(minX - 1.0f, maxX + 1.0f, feature[0]);
    const float clampedY = juce::jlimit(minY - 1.0f, maxY + 1.0f, feature[1]);
    
    const float x = bounds.getX() + (clampedX - minX) / (maxX - minX) * bounds.getWidth();
    const float y = bounds.getBottom() - (clampedY - minY) / (maxY - minY) * bounds.getHeight();
    
    return {x, y};
}

std::array<float,2> ClusterVisualizationComponent::screenToFeature(const juce::Point<float>& screen) const
{
    const auto bounds = getLocalBounds().toFloat().reduced(40.0f).removeFromBottom(getHeight() - 40);
    
    const float fx = minX + (screen.x - bounds.getX()) / bounds.getWidth() * (maxX - minX);
    const float fy = maxY - (screen.y - bounds.getY()) / bounds.getHeight() * (maxY - minY);
    
    return {fx, fy};
}

void ClusterVisualizationComponent::drawGrid(juce::Graphics& g)
{
    g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));
    
    const auto bounds = getLocalBounds().toFloat().reduced(40.0f).removeFromBottom(getHeight() - 40);
    
    // Vertical lines
    for (int i = 0; i <= 6; ++i)
    {
        const float x = bounds.getX() + i * bounds.getWidth() / 6.0f;
        g.drawVerticalLine((int)x, bounds.getY(), bounds.getBottom());
    }
    
    // Horizontal lines
    for (int i = 0; i <= 4; ++i)
    {
        const float y = bounds.getY() + i * bounds.getHeight() / 4.0f;
        g.drawHorizontalLine((int)y, bounds.getX(), bounds.getRight());
    }
}

void ClusterVisualizationComponent::drawFeatureAxes(juce::Graphics& g)
{
    g.setColour(juce::Colours::lightgrey);
    g.setFont(10.0f);
    
    const auto bounds = getLocalBounds().toFloat().reduced(40.0f).removeFromBottom(getHeight() - 40);
    
    // X-axis labels (Length Weight)
    g.drawText("Length (weighted)", bounds.getX(), bounds.getBottom() + 5, bounds.getWidth(), 15, juce::Justification::centred);
    for (int i = 0; i <= 6; ++i)
    {
        const float val = minX + i * (maxX - minX) / 6.0f;
        const float x = bounds.getX() + i * bounds.getWidth() / 6.0f;
        g.drawText(juce::String(val, 1), x - 15, bounds.getBottom() + 20, 30, 12, juce::Justification::centred);
    }
    
    // Y-axis labels (RMS)
    g.drawText("RMS", 5, bounds.getY(), 30, bounds.getHeight(), juce::Justification::centredLeft);
    for (int i = 0; i <= 4; ++i)
    {
        const float val = minY + i * (maxY - minY) / 4.0f;
        const float y = bounds.getBottom() - i * bounds.getHeight() / 4.0f;
        g.drawText(juce::String(val, 1), 5, y - 6, 30, 12, juce::Justification::centredRight);
    }
}

void ClusterVisualizationComponent::drawRTEFCVisualization(juce::Graphics& g)
{
    if (cachedCentroids.empty())
        return;
        
    const float radius = audioProcessor.apvts.getRawParameterValue("radius")->load();
    const bool autoRadius = audioProcessor.apvts.getRawParameterValue("auto_radius")->load() > 0.5f;
    
    // Draw radius circles with bounds checking
    if (autoRadius)
    {
        const float adaptiveRadius = std::max(radius, 1.25f * audioProcessor.rtefcEngine.getDistanceEMA());
        g.setColour(juce::Colours::yellow.withAlpha(0.3f));
        for (size_t i = 0; i < cachedCentroids.size(); ++i)
        {
            const auto center = featureToScreen(cachedCentroids[i]);
            const float screenRadius = juce::jlimit(1.0f, 100.0f, adaptiveRadius * getWidth() / (maxX - minX) * 0.15f);
            if (center.x >= 0 && center.y >= 0 && center.x < getWidth() && center.y < getHeight())
            {
                g.drawEllipse(center.x - screenRadius, center.y - screenRadius,
                             screenRadius * 2, screenRadius * 2, 2.0f);
            }
        }
    }
    
    // Draw base radius circles
    g.setColour(juce::Colours::cyan.withAlpha(0.5f));
    for (size_t i = 0; i < cachedCentroids.size(); ++i)
    {
        const auto center = featureToScreen(cachedCentroids[i]);
        const float screenRadius = juce::jlimit(1.0f, 50.0f, radius * getWidth() / (maxX - minX) * 0.15f);
        if (center.x >= 0 && center.y >= 0 && center.x < getWidth() && center.y < getHeight())
        {
            g.drawEllipse(center.x - screenRadius, center.y - screenRadius,
                         screenRadius * 2, screenRadius * 2, 1.0f);
        }
    }
    
    // Draw recent points with size limit
    g.setColour(juce::Colours::lightgrey.withAlpha(0.6f));
    const size_t maxPoints = std::min(cachedRecentPoints.size(), size_t(100)); // Limit for performance
    for (size_t i = 0; i < maxPoints; ++i)
    {
        const auto pos = featureToScreen(cachedRecentPoints[i]);
        if (pos.x >= 0 && pos.y >= 0 && pos.x < getWidth() && pos.y < getHeight())
            g.fillEllipse(pos.x - 2, pos.y - 2, 4, 4);
    }
    
    // Draw centroids with validation
    for (size_t i = 0; i < cachedCentroids.size() && i < 32; ++i) // Limit clusters drawn
    {
        const auto color = clusterColors[i % clusterColors.size()];
        g.setColour(color);
        const auto pos = featureToScreen(cachedCentroids[i]);
        if (pos.x >= 0 && pos.y >= 0 && pos.x < getWidth() && pos.y < getHeight())
        {
            g.fillEllipse(pos.x - 4, pos.y - 4, 8, 8);
            g.setColour(juce::Colours::white);
            g.drawEllipse(pos.x - 4, pos.y - 4, 8, 8, 1.0f);
        }
    }
    
    // Draw current processing point
    if (hasCurrentPoint)
    {
        const auto pos = featureToScreen(cachedCurrentPoint);
        if (pos.x >= 0 && pos.y >= 0 && pos.x < getWidth() && pos.y < getHeight())
        {
            g.setColour(juce::Colours::white);
            g.fillEllipse(pos.x - 3, pos.y - 3, 6, 6);
            g.setColour(juce::Colours::black);
            g.drawEllipse(pos.x - 3, pos.y - 3, 6, 6, 2.0f);
        }
    }
}

void ClusterVisualizationComponent::drawKMeansVisualization(juce::Graphics& g)
{
    // Validate data sizes match
    const size_t maxPoints = std::min({cachedRecentPoints.size(), cachedAssignments.size(), size_t(200)});
    
    // Draw window points with cluster assignments
    for (size_t i = 0; i < maxPoints; ++i)
    {
        const int assignment = cachedAssignments[i];
        const auto color = (assignment >= 0 && assignment < (int)clusterColors.size())
                          ? clusterColors[(size_t)assignment].withAlpha(0.7f)
                          : juce::Colours::grey;
        
        g.setColour(color);
        const auto pos = featureToScreen(cachedRecentPoints[i]);
        if (pos.x >= 0 && pos.y >= 0 && pos.x < getWidth() && pos.y < getHeight())
            g.fillEllipse(pos.x - 3, pos.y - 3, 6, 6);
    }
    
    // Draw centroids with bounds
    for (size_t i = 0; i < cachedCentroids.size() && i < 32; ++i)
    {
        const auto color = clusterColors[i % clusterColors.size()];
        g.setColour(color);
        const auto pos = featureToScreen(cachedCentroids[i]);
        if (pos.x >= 0 && pos.y >= 0 && pos.x < getWidth() && pos.y < getHeight())
        {
            g.fillEllipse(pos.x - 5, pos.y - 5, 10, 10);
            g.setColour(juce::Colours::white);
            g.drawEllipse(pos.x - 5, pos.y - 5, 10, 10, 2.0f);
            
            // Draw cluster number
            g.setColour(juce::Colours::white);
            g.setFont(10.0f);
            g.drawText(juce::String((int)i), pos.x - 10, pos.y - 15, 20, 12, juce::Justification::centred);
        }
    }
    
    // Draw current processing point
    if (hasCurrentPoint)
    {
        const auto pos = featureToScreen(cachedCurrentPoint);
        if (pos.x >= 0 && pos.y >= 0 && pos.x < getWidth() && pos.y < getHeight())
        {
            g.setColour(juce::Colours::yellow);
            g.fillEllipse(pos.x - 4, pos.y - 4, 8, 8);
            g.setColour(juce::Colours::black);
            g.drawEllipse(pos.x - 4, pos.y - 4, 8, 8, 2.0f);
        }
    }
}

void ClusterVisualizationComponent::setVisible(bool shouldBeVisible)
{
    Component::setVisible(shouldBeVisible);
    
    if (shouldBeVisible && !isBeingDestroyed.load())
    {
        startTimerHz(20); // Start timer when becoming visible
    }
    else
    {
        stopTimer(); // Stop timer when becoming invisible
    }
}


