#include "HammerPluginBase.h"
#include "../GUI/HammerEditor.h"

namespace hammer
{
HammerPluginBase::HammerPluginBase (const BusesProperties& buses, HammerEngine::Mode mode, ParamSet set)
    : juce::AudioProcessor (buses),
      engine (mode),
      apvts (*this, nullptr, "HAMMER", createParameterLayout (set))
{
    refs.attach (apvts);
    // auto-load kits from the default folder into empty slots on first run
    auto folder = SampleKitLoader::defaultKitsFolder();
    if (folder.isDirectory())
    {
        auto found = SampleKitLoader::scan (folder);
        // one kit per slot: acoustic (jazz/rock), metal, orchestral, cajon
        const char* wanted[KitSlots::kNumSlots] = { "jazz", "metal", "orchestral", "cajon" };
        for (int s = 0; s < KitSlots::kNumSlots; ++s)
            for (auto& k : found)
                if (k.styles.contains (wanted[s])) { engine.kits.requestLoad (s, juce::File (k.path)); break; }
    }
}

void HammerPluginBase::loadKitIntoSlot (int slot, const juce::File& source)
{
    engine.kits.requestLoad (slot, source);
}

void HammerPluginBase::clearKitSlot (int slot)
{
    engine.kits.clearSlot (slot);
}

juce::AudioProcessorEditor* HammerPluginBase::createEditor()
{
    return new HammerEditor (*this);
}

void HammerPluginBase::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    auto* kitsXml = xml->createNewChildElement ("KITS");
    for (int s = 0; s < KitSlots::kNumSlots; ++s)
        kitsXml->setAttribute ("slot" + juce::String (s), engine.kits.pathOf (s));
    copyXmlToBinary (*xml, dest);
}

void HammerPluginBase::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    engine.requestSilence(); // new project / preset: nothing from the old one keeps ringing
    if (xml == nullptr) return;
    if (auto* kitsXml = xml->getChildByName ("KITS"))
    {
        for (int s = 0; s < KitSlots::kNumSlots; ++s)
        {
            const auto path = kitsXml->getStringAttribute ("slot" + juce::String (s));
            if (path.isNotEmpty() && juce::File (path).exists()) engine.kits.requestLoad (s, juce::File (path));
        }
        xml->removeChildElement (kitsXml, true);
    }
    if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

TransportInfo HammerPluginBase::readTransport()
{
    TransportInfo t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            t.playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) { t.ppq = *ppq; t.valid = true; }
            if (auto bpm = pos->getBpm()) t.bpm = *bpm;
            if (auto ts = pos->getTimeSignature()) { t.numerator = ts->numerator; t.denominator = ts->denominator; }
        }
    return t;
}
} // namespace hammer
