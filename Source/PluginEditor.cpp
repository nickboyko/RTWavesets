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
    setSize (720, 460);
    
    engineModeCombo.addItem("RTEFC", 1);
    engineModeCombo.addItem("K-Means", 2);
    addAndMakeVisible(engineModeCombo);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (audioProcessor.apvts, "engine_mode", engineModeCombo);
    modeLabel.setText("Engine Mode", juce::dontSendNotification);
    addAndMakeVisible(modeLabel);
    
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
    
    radiusLabel.setText("radius: distance thresh", juce::dontSendNotification);
    alphaLabel.setText("alpha", juce::dontSendNotification);
    lengthWeightLabel.setText("weight", juce::dontSendNotification);
    clusterDensityLabel.setText("cluster density (cps)", juce::dontSendNotification);
    halfLifeLabel.setText("normalization half-life (wavesets)", juce::dontSendNotification);
    autoRadiusLabel.setText("auto radius", juce::dontSendNotification);
    
    
    for (auto* l : { &radiusLabel, &alphaLabel, &lengthWeightLabel, &clusterDensityLabel, &halfLifeLabel, &autoRadiusLabel })
    {
        l->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    }
    
    for (auto* s : { &kmKSlider,&kmWindowSlider,&kmRefreshSlider,&kmItersSlider,&kmLenWeightSlider })
        configureSlider(*s);
    addAndMakeVisible(kmKSlider);
    addAndMakeVisible(kmWindowSlider);
    addAndMakeVisible(kmRefreshSlider);
    addAndMakeVisible(kmItersSlider);
    addAndMakeVisible(kmLenWeightSlider);

    kmKLabel.setText("K (clusters)", juce::dontSendNotification);
    kmWindowLabel.setText("Window (wavesets)", juce::dontSendNotification);
    kmRefreshLabel.setText("Refresh Interval", juce::dontSendNotification);
    kmItersLabel.setText("Iterations/Refresh", juce::dontSendNotification);
    kmLenWeightLabel.setText("KMeans Length Weight", juce::dontSendNotification);
    
    for (auto* l : { &kmKLabel, &kmWindowLabel, &kmRefreshLabel, &kmItersLabel, &kmLenWeightLabel })
    {
        l->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    }
    
    addAndMakeVisible(resetClustersButton);
    addAndMakeVisible(resetAllButton);
    
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
    windowCountLabel.setText("windows: 0", juce::dontSendNotification);
    addAndMakeVisible(clustersLabel);
    addAndMakeVisible(distanceLabel);
    addAndMakeVisible(windowCountLabel);
    
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

    // Mode
    auto top = r.removeFromTop(40);
    modeLabel.setBounds(top.removeFromLeft(120));
    engineModeCombo.setBounds(top.removeFromLeft(200));

    // RTEFC row
    auto row1 = r.removeFromTop(150);
    auto colW = row1.getWidth() / 3;

    {
        auto b = row1.removeFromLeft(colW).reduced(6);
        radiusLabel.setBounds(b.removeFromTop(18));
        radiusSlider.setBounds(b);
    }
    {
        auto b = row1.removeFromLeft(colW).reduced(6);
        alphaLabel.setBounds(b.removeFromTop(18));
        alphaSlider.setBounds(b);
    }
    {
        auto b = row1.removeFromLeft(colW).reduced(6);
        lengthWeightLabel.setBounds(b.removeFromTop(18));
        lengthWeightSlider.setBounds(b);
    }

    // RTEFC second row
    auto row2 = r.removeFromTop(150);
    colW = row2.getWidth() / 3;
    {
        auto b = row2.removeFromLeft(colW).reduced(6);
        clusterDensityLabel.setBounds(b.removeFromTop(18));
        clusterDensitySlider.setBounds(b);
    }
    {
        auto b = row2.removeFromLeft(colW).reduced(6);
        halfLifeLabel.setBounds(b.removeFromTop(18));
        halfLifeSlider.setBounds(b);
    }
    {
        auto b = row2.removeFromLeft(colW).reduced(6);
        autoRadiusLabel.setBounds(b.removeFromTop(18));
        autoRadiusToggle.setBounds(b.removeFromTop(24));
        resetClustersButton.setBounds(b.removeFromTop(28).removeFromLeft(140));
        resetAllButton.setBounds(b.removeFromTop(28).removeFromLeft(120));
    }

    // KMeans row
    auto row3 = r.removeFromTop(150);
    colW = row3.getWidth() / 5;
    {
        auto b = row3.removeFromLeft(colW).reduced(6);
        kmKLabel.setBounds(b.removeFromTop(18));
        kmKSlider.setBounds(b);
    }
    {
        auto b = row3.removeFromLeft(colW).reduced(6);
        kmWindowLabel.setBounds(b.removeFromTop(18));
        kmWindowSlider.setBounds(b);
    }
    {
        auto b = row3.removeFromLeft(colW).reduced(6);
        kmRefreshLabel.setBounds(b.removeFromTop(18));
        kmRefreshSlider.setBounds(b);
    }
    {
        auto b = row3.removeFromLeft(colW).reduced(6);
        kmItersLabel.setBounds(b.removeFromTop(18));
        kmItersSlider.setBounds(b);
    }
    {
        auto b = row3.removeFromLeft(colW).reduced(6);
        kmLenWeightLabel.setBounds(b.removeFromTop(18));
        kmLenWeightSlider.setBounds(b);
    }

    // Telemetry
    auto bottom = r.removeFromTop(40);
    clustersLabel.setBounds(bottom.removeFromLeft(200));
    distanceLabel.setBounds(bottom.removeFromLeft(220));
    windowCountLabel.setBounds(bottom.removeFromLeft(200));
}

void RTWavesetsAudioProcessorEditor::timerCallback()
{
    clustersLabel.setText("clusters: " + juce::String(audioProcessor.rtefcEngine.getNumClusters()), juce::dontSendNotification);
    distanceLabel.setText("mean d: " + juce::String(audioProcessor.rtefcEngine.getDistanceEMA(), 2), juce::dontSendNotification);
    windowCountLabel.setText("Windowed count: " + juce::String(audioProcessor.kmeansEngine.getWindowCount()), juce::dontSendNotification);
}
