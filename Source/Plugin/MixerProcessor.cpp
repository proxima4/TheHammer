// ============================================================================
//  The Hammer FX - the drummer as a mixer insert.
//  Put it on the bus/track carrying your song (or on an empty track and
//  sidechain the song into it). It hears the input, adds drums to the output.
//  Optional lookahead (reported as plugin latency, FL compensates) lets it
//  hear the band slightly before it plays - so FOLLOW = EXACT really lands
//  on their hits.
// ============================================================================
#include "HammerPluginBase.h"

namespace hammer
{
class MixerProcessor final : public HammerPluginBase
{
public:
    MixerProcessor()
        : HammerPluginBase (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                            HammerEngine::Mode::Mixer, ParamSet::Mixer) {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
        const auto in = layouts.getMainInputChannelSet();
        if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
        if (layouts.inputBuses.size() > 1)
        {
            const auto sc = layouts.getChannelSet (true, 1);
            if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono()) return false;
        }
        return true;
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        engine.prepare (sampleRate, samplesPerBlock);
        scratch.setSize (4, samplesPerBlock);
        lastLatency = -1;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        EngineSettings es; SoundSettings snd; int kit = 0; bool autoKit = true, pass = true; int link = 1;
        refs.read (es, snd, kit, autoKit, link, pass);

        const int lat = engine.latencySamples (es);
        if (lat != lastLatency) { setLatencySamples (lat); lastLatency = lat; }

        if (scratch.getNumSamples() < n) scratch.setSize (4, n, false, false, true);
        auto in = getBusBuffer (buffer, true, 0);
        // copy the input (it shares memory with the output)
        const int inCh = in.getNumChannels();
        scratch.copyFrom (0, 0, in, 0, 0, n);
        scratch.copyFrom (1, 0, in, inCh > 1 ? 1 : 0, 0, n);

        // listen to the sidechain if it's connected and carrying audio, else the main input
        const float* aL = scratch.getReadPointer (0);
        const float* aR = scratch.getReadPointer (1);
        if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0 && sc.getMagnitude (0, n) > 1.0e-5f)
            {
                scratch.copyFrom (2, 0, sc, 0, 0, n);
                scratch.copyFrom (3, 0, sc, sc.getNumChannels() > 1 ? 1 : 0, 0, n);
                aL = scratch.getReadPointer (2); aR = scratch.getReadPointer (3);
            }
        }

        auto out = getBusBuffer (buffer, false, 0);
        out.clear();
        float* oL = out.getWritePointer (0);
        float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : oL;
        if (pass) engine.processPassThrough (scratch.getReadPointer (0), scratch.getReadPointer (1), oL, oR, n, lat);

        HammerEngine::IO io;
        io.analysisL = aL; io.analysisR = aR;
        io.outL = oL; io.outR = oR;
        io.midiIn = &midi;
        io.numSamples = n;
        engine.process (readTransport(), es, snd, kit, autoKit, link, io);
        midi.clear();
    }

private:
    juce::AudioBuffer<float> scratch;
    int lastLatency = -1;
};
} // namespace hammer

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hammer::MixerProcessor();
}
