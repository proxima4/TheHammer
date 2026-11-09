#include "EarsProcessor.h"
#include "../GUI/EarsEditor.h"

namespace hammer
{
EarsProcessor::EarsProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HAMMER_EARS", createParameterLayout (ParamSet::Ears))
{
    for (auto& m : meters) m.store (0.f);
    linkParam = apvts.getRawParameterValue (pid::link);
    sensParam = apvts.getRawParameterValue (pid::earsSens);
}

bool EarsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void EarsProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    analyzer.prepare (sr);
}

void EarsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int n = buffer.getNumSamples();
    const int ch = juce::jlimit (1, 8, (int) linkParam->load());
    if (! link.isOpen() || link.channel() != ch) linkOpen = link.open (ch);
    currentChannel = ch;
    analyzer.setSensitivity (sensParam->load());

    double ppq = 0.0, bpm = 120.0; bool playing = false; int bpb = 4;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p; else playing = false;
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ts = pos->getTimeSignature()) bpb = juce::jlimit (1, 8, ts->numerator * 4 / std::max (1, ts->denominator));
        }
    running = playing;

    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;
    const int ne = analyzer.process (L, R, n, events, OnsetAnalyzer::kMaxEventsPerBlock);
    const double pps = bpm / 60.0 / sr;
    for (int i = 0; i < 3; ++i) meters[i] = analyzer.bandLevel (i);
    float fl = flash.load() * std::pow (0.5f, (float) n / (float) (sr * 0.06));
    if (playing)
    {
        for (int i = 0; i < ne; ++i)
        {
            LinkEvent e {};
            e.ppq = ppq + events[i].sampleOffset * pps;
            for (int k = 0; k < 3; ++k) e.band[k] = events[i].band[k];
            e.strength = events[i].strength;
            e.kind = 0;
            link.publishEvent (e);
            fl = std::max (fl, events[i].strength);
        }
        energyCountdown -= n;
        if (energyCountdown <= 0)
        {
            energyCountdown = (int) (sr * 0.02);
            LinkEvent e {};
            e.ppq = ppq; e.kind = 1;
            for (int k = 0; k < 3; ++k) e.band[k] = analyzer.bandEnergyDb (k);
            link.publishEvent (e);
        }
    }
    flash = fl;
    float m[3] = { meters[0].load(), meters[1].load(), meters[2].load() };
    link.publishTransport (ppq, bpm, playing, bpb, m);
    // audio passes through untouched
}

juce::AudioProcessorEditor* EarsProcessor::createEditor() { return new EarsEditor (*this); }

void EarsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void EarsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
} // namespace hammer

#ifndef HAMMER_SNAPSHOT_TOOL
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hammer::EarsProcessor();
}
#endif
