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
    apvts.addParameterListener("engine_mode", this);
    
    // rtefc params
    apvts.addParameterListener("radius", this);
    apvts.addParameterListener("alpha", this);
    apvts.addParameterListener("length_weight", this);
    apvts.addParameterListener("clusters_per_second", this);
    apvts.addParameterListener("norm_half_life", this);
    apvts.addParameterListener("auto_radius", this);
    apvts.addParameterListener("reset_clusters", this);
    apvts.addParameterListener("reset_all", this);
    
    // kmeans params
    apvts.addParameterListener("km_k", this);
    apvts.addParameterListener("km_window", this);
    apvts.addParameterListener("km_refresh", this);
    apvts.addParameterListener("km_iters", this);
    apvts.addParameterListener("km_length_weight", this);
}

RTWavesetsAudioProcessor::~RTWavesetsAudioProcessor()
{
    for (auto id : { "radius","alpha","length_weight","clusters_per_second","norm_half_life","auto_radius","reset_clusters","reset_all",
                         "engine_mode","km_k","km_window","km_refresh","km_iters","km_length_weight" })
            apvts.removeParameterListener(id, this);
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
    kmeansEngine.prepare(sampleRate);
    
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
    parameterChanged("engine_mode", apvts.getRawParameterValue("engine_mode")->load());
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
                
                const EngineMode m = mode.load();
                const juce::AudioBuffer<float>* rep = nullptr;
                
                if (m == EngineMode::RTEFC)
                {
                    rep = &rtefcEngine.processWaveset(wsView);
                }
                else
                {
                    rep = &kmeansEngine.processWaveset(wsView);
                }
                
                if (rep != nullptr)
                {
                    const int copyLen = std::min(rep->getNumSamples(), currentOutputWaveset.getNumSamples());
                    if (copyLen > 0)
                    {
                        currentOutputWaveset.clear();
                        currentOutputWaveset.copyFrom(0, 0, *rep, 0, 0, copyLen);
                        if (currentOutputWaveset.getNumChannels() > 1 && rep->getNumChannels() > 1)
                            currentOutputWaveset.copyFrom(1, 0, *rep, 1, 0, copyLen);

                        outputReadPosition = 0;
                        isFirstWavesetProcessed = true;
                    }
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
            kmeansEngine.resetAll();
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
            kmeansEngine.resetAll();
            isFirstWavesetProcessed = false;
            
            juce::MessageManager::callAsync([this]() {
                if (auto* p = apvts.getParameter("reset_clusters")) p->setValueNotifyingHost(0.0f);
            });
        }
        return;
    }
    
    if (parameterID == "engine_mode")
    {
        const int m = (int)newValue;
        mode.store(m == 0 ? EngineMode::RTEFC : EngineMode::WindowedKMeans);
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
    
    const int kmK        = (int) apvts.getRawParameterValue("km_k")->load();
    const int kmWin      = (int) apvts.getRawParameterValue("km_window")->load();
    const int kmRefresh  = (int) apvts.getRawParameterValue("km_refresh")->load();
    const int kmIters    = (int) apvts.getRawParameterValue("km_iters")->load();
    const float kmLW     = apvts.getRawParameterValue("km_length_weight")->load();

    kmeansEngine.setParameters(kmK, kmWin, kmRefresh, kmIters, kmLW);
}

juce::AudioProcessorValueTreeState::ParameterLayout RTWavesetsAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"engine_mode", 1},
            "Engine Mode", juce::StringArray{ "RTEFC", "Windowed K-Means" }, 0));
            
    //rtefc
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"radius", 1}, "Radius", juce::NormalisableRange<float>(0.1f, 10.f, 0.0f, 0.4f), 1.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"alpha", 1}, "Alpha", juce::NormalisableRange<float>(0.85f, 0.995f, 0.0f, 0.6f), 0.98f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"length_weight", 1}, "Length Weight",
        juce::NormalisableRange<float>(0.5f, 12.f, 0.0f, 0.5f), 5.0f));
    
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"clusters_per_second", 1}, "Cluster Density",
        juce::NormalisableRange<float>(1.0f, 50.f, 0.0f, 0.45f), 12.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"norm_half_life", 1}, "Normalization Half-Life",
        juce::NormalisableRange<float>(16.f, 256.f, 0.0f, 0.6f), 64.f));
    
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"auto_radius", 1}, "Auto Radius", false));
    
    
    //k-means
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"km_k", 1}, "K (clusters)", 2, 32, 8)); // avoid degenerate k=1[1]

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"km_window", 1}, "Window (wavesets)", 64, 1024, 256)); // per-window stats[1]

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"km_refresh", 1}, "Refresh Interval (wavesets)", 8, 128, 32));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"km_iters", 1}, "Iterations/Refresh", 1, 8, 3));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"km_length_weight", 1}, "KMeans Length Weight",
        juce::NormalisableRange<float>(0.5f, 12.f, 0.0f, 0.5f), 5.0f));

    //general
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
