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
    apvts.addParameterListener("weight", this);
    apvts.addParameterListener("clusters per second", this);
}

RTWavesetsAudioProcessor::~RTWavesetsAudioProcessor()
{
    apvts.removeParameterListener("radius", this);
    apvts.removeParameterListener("alpha", this);
    apvts.removeParameterListener("weight", this);
    apvts.removeParameterListener("clusters per second", this);
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
    
    lastSign = 0;
    isFirstWavesetProcessed = false;
    
    parameterChanged("radius", apvts.getRawParameterValue("radius")->load());
}

void RTWavesetsAudioProcessor::releaseResources()
{
    inputAssemblyBuffer.setSize(0, 0);
    currentOutputWaveset.setSize(0, 0);
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

    auto* leftChannelData = buffer.getWritePointer(0);
    const auto* rightChannelReader = totalNumInputChannels > 1 ? buffer.getReadPointer(1) : buffer.getReadPointer(0);
    
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float leftSample = leftChannelData[i];
        const float rightSample = rightChannelReader[i];
        
        // start assembling waveset buffer
        if (inputAssemblyBufferWritePosition < inputAssemblyBuffer.getNumSamples())
        {
            inputAssemblyBuffer.setSample(0, inputAssemblyBufferWritePosition, leftSample);
            inputAssemblyBuffer.setSample(1, inputAssemblyBufferWritePosition, rightSample);
            inputAssemblyBufferWritePosition++;
        }
        
        // detect zero-crossings using only left channel
        int currentSign = (leftSample > 0.0f) - (leftSample < 0.0f);
        if (currentSign > 0 && lastSign <= 0)
        {
            if (inputAssemblyBufferWritePosition > 1)
            {
                juce::AudioBuffer<float> completedWaveset(2, inputAssemblyBufferWritePosition);
                completedWaveset.copyFrom(0, 0, inputAssemblyBuffer, 0, 0, inputAssemblyBufferWritePosition);
                completedWaveset.copyFrom(1, 0, inputAssemblyBuffer, 1, 0, inputAssemblyBufferWritePosition);
                
                const auto& representative = rtefcEngine.processWaveset(completedWaveset);
                
                currentOutputWaveset.makeCopyOf(representative);
                outputReadPosition = 0;
                isFirstWavesetProcessed = true;
            }
            
            inputAssemblyBuffer.clear();
            inputAssemblyBufferWritePosition = 0;
        }
        lastSign = currentSign;
        
        // write to output buffer
        if (isFirstWavesetProcessed && outputReadPosition < currentOutputWaveset.getNumSamples())
        {
            leftChannelData[i] = currentOutputWaveset.getSample(0, outputReadPosition);
            if (totalNumOutputChannels > 1)
            {
                buffer.getWritePointer(1)[i] = currentOutputWaveset.getSample(1, outputReadPosition);
            }
            outputReadPosition++;
        }
        else
        {
            leftChannelData[i] = leftSample;
            if (totalNumOutputChannels > 1)
            {
                buffer.getWritePointer(1)[i] = rightSample;
            }
        }
    }
    
//    for (int channel = 0; channel < totalNumInputChannels; ++channel)
//    {
//        auto* channelData = buffer.getWritePointer (channel);
//
//        // ..do something to the data...
//    }
}

//==============================================================================
bool RTWavesetsAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* RTWavesetsAudioProcessor::createEditor()
{
//    return new RTWavesetsAudioProcessorEditor (*this);
    return new juce::GenericAudioProcessorEditor(*this);
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
    DBG("Parameter changed: " << parameterID << " to " << newValue);
    auto radius = apvts.getRawParameterValue("radius")->load();
    auto alpha = apvts.getRawParameterValue("alpha")->load();
    auto weight = apvts.getRawParameterValue("weight")->load();
    auto clustersPerSecond = apvts.getRawParameterValue("clusters per second")->load();
        
    auto maxClusters = static_cast<int>(clustersPerSecond * 10.0f);
    
    rtefcEngine.setParameters(radius, alpha, weight, maxClusters);
}

juce::AudioProcessorValueTreeState::ParameterLayout RTWavesetsAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
        
    params.push_back(std::make_unique<juce::AudioParameterFloat>("radius", "radius", 0.1f, 10.f, 1.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("alpha", "alpha", 0.8f, 0.999f, 0.98f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("weight", "weight", 0.1f, 20.f, 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("clusters per second", "clusters", 1.0f, 50.f, 12.8f));
    
    return { params.begin(), params.end() };
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RTWavesetsAudioProcessor();
}
