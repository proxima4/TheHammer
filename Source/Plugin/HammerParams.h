#pragma once
// Parameter layout shared by the Drummer, the mixer FX and Ears.
#include "../Common/HammerTypes.h"
#include "../Sound/DrumKitEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace hammer
{
namespace pid
{
    inline constexpr const char* style = "style";
    inline constexpr const char* intensity = "intensity";
    inline constexpr const char* aggression = "aggression";
    inline constexpr const char* fills = "fills";
    inline constexpr const char* solos = "solos";
    inline constexpr const char* follow = "follow";
    inline constexpr const char* improvise = "improvise";
    inline constexpr const char* brain = "brain";
    inline constexpr const char* swing = "swing";
    inline constexpr const char* humanize = "humanize";
    inline constexpr const char* dynamics = "dynamics";
    inline constexpr const char* feel = "feel";
    inline constexpr const char* section = "section";
    inline constexpr const char* phrase = "phrase";
    inline constexpr const char* kit = "kit";
    inline constexpr const char* autoKit = "autokit";
    inline constexpr const char* drummerOn = "drummer";
    inline constexpr const char* midiOut = "midiout";
    inline constexpr const char* lock = "lock";
    inline constexpr const char* lookahead = "lookahead";
    inline constexpr const char* earsSens = "earssens";
    inline constexpr const char* link = "link";
    inline constexpr const char* tempo = "tempo";
    inline constexpr const char* passThrough = "passthru";
    inline constexpr const char* master = "master";
    inline constexpr const char* room = "room";
    inline constexpr const char* roomSize = "roomsize";
    inline constexpr const char* drive = "drive";
    inline constexpr const char* glue = "glue";
    inline constexpr const char* tune = "tune";
    inline constexpr const char* velCurve = "velcurve";
    inline const char* groupVol (int g)
    {
        static const char* ids[NumGroups] = { "volkick", "volsnare", "voltoms", "volhats", "volcymbals", "volperc" };
        return ids[g];
    }
}

enum class ParamSet { Drummer, Mixer, Ears };

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (ParamSet set);

struct ParamRefs
{
    void attach (juce::AudioProcessorValueTreeState& s);
    void read (EngineSettings& es, SoundSettings& snd, int& kit, bool& autoKit, int& linkCh, bool& passThrough) const;
    static float lookaheadMsFor (int index);

private:
    std::atomic<float>* p (const char* id) const;
    juce::AudioProcessorValueTreeState* state = nullptr;
    std::atomic<float> *style = nullptr, *intensity = nullptr, *aggression = nullptr, *fills = nullptr, *solos = nullptr,
        *follow = nullptr, *improvise = nullptr, *brain = nullptr, *swing = nullptr, *humanize = nullptr, *dynamics = nullptr,
        *feel = nullptr, *section = nullptr, *phrase = nullptr, *kit = nullptr, *autoKit = nullptr, *drummerOn = nullptr,
        *midiOut = nullptr, *lock = nullptr, *lookahead = nullptr, *earsSens = nullptr, *link = nullptr, *tempo = nullptr,
        *passThrough = nullptr, *master = nullptr, *room = nullptr, *roomSize = nullptr, *drive = nullptr, *glue = nullptr,
        *tune = nullptr, *velCurve = nullptr;
    std::atomic<float>* groupVol[NumGroups] {};
};
} // namespace hammer
