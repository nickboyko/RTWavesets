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
    
    juce::Slider radiusSlider, alphaSlider, lengthWeightSlider, clusterDensitySlider, halfLifeSlider;
    juce::ToggleButton autoRadiusToggle { "Auto Radius" };
    juce::TextButton resetClustersButton { "Reset Clusters" };
    juce::TextButton resetAllButton { "Reset All" };

    juce::Label radiusLabel, alphaLabel, lengthWeightLabel, clusterDensityLabel, halfLifeLabel, autoRadiusLabel;
    juce::Label clustersLabel, distanceLabel;

    juce::AudioProcessorValueTreeState::SliderAttachment radiusAtt { audioProcessor.apvts, "radius", radiusSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment alphaAtt { audioProcessor.apvts, "alpha", alphaSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment weightAtt { audioProcessor.apvts, "length_weight", lengthWeightSlider };
    juce::AudioProcessorValueTreeState::SliderAttachment clusterAtt { audioProcessor.apvts, "clusters_per_second", clusterDensitySlider };
    juce::AudioProcessorValueTreeState::SliderAttachment halfLifeAtt { audioProcessor.apvts, "norm_half_life", halfLifeSlider };
    juce::AudioProcessorValueTreeState::ButtonAttachment autoRadiusAtt { audioProcessor.apvts, "auto_radius", autoRadiusToggle };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RTWavesetsAudioProcessorEditor)
};
