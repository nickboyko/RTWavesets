/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
RTWavesetsAudioProcessorEditor::RTWavesetsAudioProcessorEditor (RTWavesetsAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (560, 360);
    
    auto configureSlider = [] (juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    };
    
    configureSlider(radiusSlider);
    configureSlider(alphaSlider);
    configureSlider(lengthWeightSlider);
    configureSlider(clusterDensitySlider);
    configureSlider(halfLifeSlider);

    addAndMakeVisible(radiusSlider);
    addAndMakeVisible(alphaSlider);
    addAndMakeVisible(lengthWeightSlider);
    addAndMakeVisible(clusterDensitySlider);
    addAndMakeVisible(halfLifeSlider);

    addAndMakeVisible(autoRadiusToggle);

    addAndMakeVisible(resetClustersButton);
    addAndMakeVisible(resetAllButton);
    
    radiusLabel.setText("radius: distance threshold for new clusters", juce::dontSendNotification);
    alphaLabel.setText("alpha: centroid smoothing (higher = slower)", juce::dontSendNotification);
    lengthWeightLabel.setText("weight", juce::dontSendNotification);
    clusterDensityLabel.setText("cluster density (cps)", juce::dontSendNotification);
    halfLifeLabel.setText("normalization half-life (wavesets)", juce::dontSendNotification);
    autoRadiusLabel.setText("auto radius", juce::dontSendNotification);
    
    
    for (auto* l : { &radiusLabel, &alphaLabel, &lengthWeightLabel, &clusterDensityLabel, &halfLifeLabel, &autoRadiusLabel })
    {
        l->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    }
    resetClustersButton.onClick = [this]()
    {
        if (auto* p = audioProcessor.apvts.getParameter("reset_clusters"))
            p->setValueNotifyingHost(1.0f);
    };
    resetAllButton.onClick = [this]()
    {
        if (auto* p = audioProcessor.apvts.getParameter("reset_all"))
            p->setValueNotifyingHost(1.0f);
    };
    
    clustersLabel.setText("clusters: 0", juce::dontSendNotification);
    distanceLabel.setText("mean d: 0.00", juce::dontSendNotification);
    addAndMakeVisible(clustersLabel);
    addAndMakeVisible(distanceLabel);
    
    startTimerHz(10);
}

RTWavesetsAudioProcessorEditor::~RTWavesetsAudioProcessorEditor() = default;

//==============================================================================
void RTWavesetsAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void RTWavesetsAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(10);

    // Top row: three knobs + labels
    auto top = r.removeFromTop(140);
    auto col = top.getWidth() / 3;
    {
        auto b = top.removeFromLeft(col).reduced(6);
        radiusLabel.setBounds(b.removeFromTop(18));
        radiusSlider.setBounds(b);
    }
    {
        auto b = top.removeFromLeft(col).reduced(6);
        alphaLabel.setBounds(b.removeFromTop(18));
        alphaSlider.setBounds(b);
    }
    {
        auto b = top.removeFromLeft(col).reduced(6);
        lengthWeightLabel.setBounds(b.removeFromTop(18));
        lengthWeightSlider.setBounds(b);
    }

    // Second row: three knobs/toggle + labels
    auto mid = r.removeFromTop(140);
    col = mid.getWidth() / 3;
    {
        auto b = mid.removeFromLeft(col).reduced(6);
        clusterDensityLabel.setBounds(b.removeFromTop(18));
        clusterDensitySlider.setBounds(b);
    }
    {
        auto b = mid.removeFromLeft(col).reduced(6);
        halfLifeLabel.setBounds(b.removeFromTop(18));
        halfLifeSlider.setBounds(b);
    }
    {
        auto b = mid.removeFromLeft(col).reduced(6);
        autoRadiusLabel.setBounds(b.removeFromTop(18));
        autoRadiusToggle.setBounds(b.removeFromTop(24));
        resetClustersButton.setBounds(b.removeFromTop(28).removeFromLeft(140));
        resetAllButton.setBounds(b.removeFromTop(28).removeFromLeft(120));
    }

    // Telemetry
    auto bottom = r.removeFromTop(40);
    clustersLabel.setBounds(bottom.removeFromLeft(200));
    distanceLabel.setBounds(bottom.removeFromLeft(200));
}

void RTWavesetsAudioProcessorEditor::timerCallback()
{
    clustersLabel.setText("clusters: " + juce::String(audioProcessor.rtefcEngine.getNumClusters()), juce::dontSendNotification);
    distanceLabel.setText("mean d: " + juce::String(audioProcessor.rtefcEngine.getDistanceEMA(), 2), juce::dontSendNotification);
}
