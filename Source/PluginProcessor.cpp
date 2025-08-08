/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
RTWavesetsAudioProcessor::RTWavesetsAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    apvts.addParameterListener("radius", this);
    apvts.addParameterListener("alpha", this);
    apvts.addParameterListener("length_weight", this);
    apvts.addParameterListener("clusters_per_second", this);
    apvts.addParameterListener("norm_half_life", this);
    apvts.addParameterListener("auto_radius", this);
    apvts.addParameterListener("reset_clusters", this);
    apvts.addParameterListener("reset_all", this);
}

RTWavesetsAudioProcessor::~RTWavesetsAudioProcessor()
{
    apvts.removeParameterListener("radius", this);
    apvts.removeParameterListener("alpha", this);
    apvts.removeParameterListener("length_weight", this);
    apvts.removeParameterListener("clusters_per_second", this);
    apvts.removeParameterListener("norm_half_life", this);
    apvts.removeParameterListener("auto_radius", this);
    apvts.removeParameterListener("reset_clusters", this);
    apvts.removeParameterListener("reset_all", this);
}

//==============================================================================
const juce::String RTWavesetsAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool RTWavesetsAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool RTWavesetsAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool RTWavesetsAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double RTWavesetsAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int RTWavesetsAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int RTWavesetsAudioProcessor::getCurrentProgram()
{
    return 0;
}

void RTWavesetsAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String RTWavesetsAudioProcessor::getProgramName (int index)
{
    return {};
}

void RTWavesetsAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void RTWavesetsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rtefcEngine.prepare(sampleRate);
    
    const int numChannels = 2;
    const int bufferSize = static_cast<int>(sampleRate * 2.0);
    
    inputAssemblyBuffer.setSize(numChannels, bufferSize);
    inputAssemblyBuffer.clear();
    inputAssemblyBufferWritePosition = 0;
    
    currentOutputWaveset.setSize(numChannels, bufferSize);
    currentOutputWaveset.clear();
    outputReadPosition = 0;
    
    scratchWaveset.setSize(numChannels, bufferSize);
    scratchWaveset.clear();
    
    lastSign = 0;
    isFirstWavesetProcessed = false;
    
    parameterChanged("radius", apvts.getRawParameterValue("radius")->load());
}

void RTWavesetsAudioProcessor::releaseResources()
{
    inputAssemblyBuffer.setSize(0, 0);
    currentOutputWaveset.setSize(0, 0);
    scratchWaveset.setSize(0, 0);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool RTWavesetsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void RTWavesetsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    auto* leftOut = buffer.getWritePointer(0);
    const auto* leftIn = buffer.getReadPointer(0);
    const auto* rightIn = totalNumInputChannels > 1 ? buffer.getReadPointer(1) : buffer.getReadPointer(0);
    
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float leftSample = leftIn[i];
        const float rightSample = rightIn[i];
        
        // start assembling waveset buffer
        if (inputAssemblyBufferWritePosition < inputAssemblyBuffer.getNumSamples())
        {
            inputAssemblyBuffer.setSample(0, inputAssemblyBufferWritePosition, leftSample);
            inputAssemblyBuffer.setSample(1, inputAssemblyBufferWritePosition, rightSample);
            inputAssemblyBufferWritePosition++;
        }
        
        // detect zero-crossings on left channel
        int currentSign = (leftSample > 0.0f) - (leftSample < 0.0f);
        if (currentSign > 0 && lastSign <= 0)
        {
            const int wsLen = inputAssemblyBufferWritePosition;
            if (wsLen > 1 && wsLen <= scratchWaveset.getNumSamples())
            {
                // copy into scratch
                scratchWaveset.clear();
                scratchWaveset.copyFrom(0, 0, inputAssemblyBuffer, 0, 0, wsLen);
                scratchWaveset.copyFrom(1, 0, inputAssemblyBuffer, 1, 0, wsLen);

                // create a view buffer of exact length without realloc
                juce::AudioBuffer<float> wsView (scratchWaveset.getArrayOfWritePointers(), 2, wsLen);

                const auto& representative = rtefcEngine.processWaveset(wsView);

                const int copyLen = std::min(representative.getNumSamples(), currentOutputWaveset.getNumSamples());
                if (copyLen > 0)
                {
                    currentOutputWaveset.clear();
                    currentOutputWaveset.copyFrom(0, 0, representative, 0, 0, copyLen);
                    if (currentOutputWaveset.getNumChannels() > 1 && representative.getNumChannels() > 1)
                        currentOutputWaveset.copyFrom(1, 0, representative, 1, 0, copyLen);

                    outputReadPosition = 0;
                    isFirstWavesetProcessed = true;
                }
            }
            
            inputAssemblyBuffer.clear();
            inputAssemblyBufferWritePosition = 0;
        }
        lastSign = currentSign;
        
        // write to output buffer
        if (isFirstWavesetProcessed && outputReadPosition < currentOutputWaveset.getNumSamples())
        {
            leftOut[i] = currentOutputWaveset.getSample(0, outputReadPosition);
            if (totalNumOutputChannels > 1)
            {
                buffer.getWritePointer(1)[i] = currentOutputWaveset.getSample(1, outputReadPosition);
            }
            outputReadPosition++;
        }
        else
        {
            // pass through until representative waveset is ready
            leftOut[i] = leftSample;
            if (totalNumOutputChannels > 1)
            {
                buffer.getWritePointer(1)[i] = rightSample;
            }
        }
    }
}

//==============================================================================
bool RTWavesetsAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* RTWavesetsAudioProcessor::createEditor()
{
    return new RTWavesetsAudioProcessorEditor (*this);
//    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void RTWavesetsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary(*xml, destData);
}

void RTWavesetsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

void RTWavesetsAudioProcessor::parameterChanged(const juce::String &parameterID, float newValue)
{
    if (parameterID == "reset_all")
    {
        if (newValue > 0.5f)
        {
            DBG("reset all triggered");
            rtefcEngine.resetAll();
            isFirstWavesetProcessed = false;
            
            juce::MessageManager::callAsync([this]() {
                if (auto* p = apvts.getParameter("reset_all")) p->setValueNotifyingHost(0.0f);
            });
        }
        return;
    }
    
    if (parameterID == "reset_clusters")
    {
        if (newValue > 0.5f)
        {
            DBG("reset clusters triggered");
            rtefcEngine.resetClustersOnly();
            isFirstWavesetProcessed = false;
            
            juce::MessageManager::callAsync([this]() {
                if (auto* p = apvts.getParameter("reset_clusters")) p->setValueNotifyingHost(0.0f);
            });
        }
        return;
    }
    
    DBG("Parameter changed: " << parameterID << " to " << newValue);
    const float radius    = apvts.getRawParameterValue("radius")->load();
    const float alpha     = apvts.getRawParameterValue("alpha")->load();
    const float lenWeight = apvts.getRawParameterValue("length_weight")->load();
    const float cps       = apvts.getRawParameterValue("clusters_per_second")->load();
    const float halfLife  = apvts.getRawParameterValue("norm_half_life")->load();
    const bool  autoRad   = apvts.getRawParameterValue("auto_radius")->load() > 0.5f;

    const float maxClusters = cps;
    
    // detect large parameter changes to trigger reset
    const bool bigRadiusChange = std::abs(prevRadius - radius) / std::max(0.001f, prevRadius) > 0.25f;
    const bool bigWeightChange = std::abs(prevLengthWeight - lenWeight) / std::max(0.001f, prevLengthWeight) > 0.25f;
    if (bigRadiusChange || bigWeightChange)
        rtefcEngine.resetClustersOnly();
    
    rtefcEngine.setParameters(radius, alpha, lenWeight, maxClusters, halfLife, autoRad);
    
    prevRadius = radius;
    prevLengthWeight = lenWeight;
}

juce::AudioProcessorValueTreeState::ParameterLayout RTWavesetsAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
            
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"radius", 1}, "Radius", 0.1f, 10.f, 1.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"alpha", 1}, "Alpha", 0.80f, 0.999f, 0.98f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"length_weight", 1}, "Length Weight", 0.1f, 20.f, 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"clusters_per_second", 1}, "Cluster Density", 1.0f, 50.f, 12.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"norm_half_life", 1}, "Normalization Half-Life", 8.0f, 256.f, 64.f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"auto_radius", 1}, "Auto Radius", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"reset_clusters", 1}, "Reset Clusters", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"reset_all", 1}, "Reset All", false));
    
    return { params.begin(), params.end() };
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RTWavesetsAudioProcessor();
}
