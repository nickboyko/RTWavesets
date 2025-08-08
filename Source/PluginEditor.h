/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class ClusterVisualizationComponent;

class RTWavesetsAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    RTWavesetsAudioProcessorEditor (RTWavesetsAudioProcessor&);
    ~RTWavesetsAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    RTWavesetsAudioProcessor& audioProcessor;
    
    //mode
    juce::ComboBox engineModeCombo;
    
    // rtefc
    juce::Slider radiusSlider, alphaSlider, lengthWeightSlider, clusterDensitySlider, halfLifeSlider;
    juce::ToggleButton autoRadiusToggle { "Auto Radius" };
    
    //kmeans
    juce::Slider kmKSlider, kmWindowSlider, kmRefreshSlider, kmItersSlider, kmLenWeightSlider;
    
    // general
    juce::TextButton resetClustersButton { "Reset Clusters" };
    juce::TextButton resetAllButton { "Reset All" };

    //labels
    juce::Label modeLabel;
    juce::Label radiusLabel, alphaLabel, lengthWeightLabel, clusterDensityLabel, halfLifeLabel, autoRadiusLabel;
    juce::Label kmKLabel, kmWindowLabel, kmRefreshLabel, kmItersLabel, kmLenWeightLabel;
    
    //telemetry
    juce::Label clustersLabel, distanceLabel, windowCountLabel;

    //attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::unique_ptr<ClusterVisualizationComponent> visualizationComponent;
    juce::AudioProcessorValueTreeState::SliderAttachment radiusAtt { audioProcessor.apvts, "radius", radiusSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment alphaAtt { audioProcessor.apvts, "alpha", alphaSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment weightAtt { audioProcessor.apvts, "length_weight", lengthWeightSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment clusterAtt { audioProcessor.apvts, "clusters_per_second", clusterDensitySlider };
    
    juce::AudioProcessorValueTreeState::SliderAttachment halfLifeAtt { audioProcessor.apvts, "norm_half_life", halfLifeSlider };
    juce::AudioProcessorValueTreeState::ButtonAttachment autoRadiusAtt { audioProcessor.apvts, "auto_radius", autoRadiusToggle };
    
    juce::AudioProcessorValueTreeState::SliderAttachment kmKAtt { audioProcessor.apvts, "km_k", kmKSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment kmWindowAtt { audioProcessor.apvts, "km_window", kmWindowSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment kmRefreshAtt { audioProcessor.apvts, "km_refresh", kmRefreshSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment kmItersAtt { audioProcessor.apvts, "km_iters", kmItersSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment kmLenWeightAtt { audioProcessor.apvts, "km_length_weight", kmLenWeightSlider };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RTWavesetsAudioProcessorEditor)
};
