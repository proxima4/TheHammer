// ============================================================================
//  The Hammer - drummer instrument (FL Studio Channel Rack).
//  Hears the song through "The Hammer Ears" (link channel), plays its kit,
//  and can also send MIDI to FPC / other drum plugins.
//  Outputs: main stereo + 6 optional stereo outs (Kick, Snare, Toms, Hats,
//  Cymbals, Perc) for mixing on separate FL mixer tracks.
// ============================================================================
#include "HammerPluginBase.h"

namespace hammer
{
class DrummerProcessor final : public HammerPluginBase
{
public:
    DrummerProcessor()
        : HammerPluginBase (makeBuses(), HammerEngine::Mode::Drummer, ParamSet::Drummer) {}

    static BusesProperties makeBuses()
    {
        auto b = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
        for (int g = 0; g < NumGroups; ++g)
            b = b.withOutput (groupName (g), juce::AudioChannelSet::stereo(), false);
        return b;
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
        for (int i = 1; i < layouts.outputBuses.size(); ++i)
        {
            const auto& set = layouts.outputBuses.getReference (i);
            if (! set.isDisabled() && set != juce::AudioChannelSet::stereo()) return false;
        }
        return layouts.inputBuses.isEmpty();
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        engine.prepare (sampleRate, samplesPerBlock);
        midiOut.ensureSize (4096);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        buffer.clear();

        EngineSettings es; SoundSettings snd; int kit = 0; bool autoKit = true, pass = true; int link = 1;
        refs.read (es, snd, kit, autoKit, link, pass);

        auto main = getBusBuffer (buffer, false, 0);
        float* auxL[NumGroups] {}; float* auxR[NumGroups] {};
        bool anyAux = false;
        for (int g = 0; g < NumGroups; ++g)
        {
            if (g + 1 < getBusCount (false) && getBus (false, g + 1)->isEnabled())
            {
                auto aux = getBusBuffer (buffer, false, g + 1);
                if (aux.getNumChannels() >= 2) { auxL[g] = aux.getWritePointer (0); auxR[g] = aux.getWritePointer (1); anyAux = true; }
            }
        }

        midiOut.clear();
        HammerEngine::IO io;
        io.outL = main.getWritePointer (0);
        io.outR = main.getNumChannels() > 1 ? main.getWritePointer (1) : main.getWritePointer (0);
        io.auxL = anyAux ? auxL : nullptr;
        io.auxR = anyAux ? auxR : nullptr;
        io.midiIn = &midi;
        io.midiOut = &midiOut;
        io.numSamples = n;
        engine.process (readTransport(), es, snd, kit, autoKit, link, io);
        midi.swapWith (midiOut);
    }

private:
    juce::MidiBuffer midiOut;
};
} // namespace hammer

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hammer::DrummerProcessor();
}
