#include "PluginProcessor.h"
#include "PluginEditor.h"

MacchinePiselloProcessor::MacchinePiselloProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MacchinePisello", createParameterLayout())
{
    machine.bind (apvts);
}

void MacchinePiselloProcessor::prepareToPlay (double sampleRate, int)
{
    machine.prepare (sampleRate);
    wasBypassed = false;
}

bool MacchinePiselloProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.inputBuses.size() > 0 && ! layouts.getMainInputChannelSet().isDisabled())
        return false;
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void MacchinePiselloProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    if (wasBypassed)
    {
        machine.reset();        // al rientro dal bypass: niente code/gate "congelati"
        wasBypassed = false;
    }

    buffer.clear();
    machine.process (buffer, midi, getPlayHead());
}

void MacchinePiselloProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Uno strumento in bypass tace.
    buffer.clear();
    wasBypassed = true;
}

juce::AudioProcessorEditor* MacchinePiselloProcessor::createEditor()
{
    return new MacchinePiselloEditor (*this);
}

void MacchinePiselloProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MacchinePiselloProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MacchinePiselloProcessor();
}
