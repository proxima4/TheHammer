#include "HammerParams.h"
#include "../Brain/StyleLibrary.h"

namespace hammer
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using API = juce::AudioParameterInt;

static juce::ParameterID id (const char* s) { return { s, 1 }; }

static std::unique_ptr<APF> pct (const char* pidv, const char* name, float def)
{
    return std::make_unique<APF> (id (pidv), name, juce::NormalisableRange<float> (0.f, 1.f, 0.001f), def,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.f)) + "%"; }));
}

static std::unique_ptr<APF> db (const char* pidv, const char* name, float lo, float hi, float def)
{
    return std::make_unique<APF> (id (pidv), name, juce::NormalisableRange<float> (lo, hi, 0.1f), def,
                                  juce::AudioParameterFloatAttributes().withLabel ("dB"));
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (ParamSet set)
{
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    if (set == ParamSet::Ears)
    {
        L.add (std::make_unique<API> (id (pid::link), "Link Channel", 1, 8, 1));
        L.add (pct (pid::earsSens, "Ears Sensitivity", 0.5f));
        return L;
    }
    juce::StringArray styles; for (int i = 0; i < NumStyles; ++i) styles.add (styleName (i));
    juce::StringArray kits; for (int i = 0; i < NumKits; ++i) kits.add (kitName (i));
    juce::StringArray sections; for (int i = 0; i < (int) Section::NumSections; ++i) sections.add (sectionName (i));

    L.add (std::make_unique<APC> (id (pid::style), "Style", styles, Rock));
    L.add (pct (pid::intensity, "Intensity", 0.55f));
    L.add (pct (pid::aggression, "Aggression", 0.4f));
    L.add (pct (pid::fills, "Fills", 0.4f));
    L.add (pct (pid::solos, "Solos", 0.05f));
    L.add (pct (pid::follow, "Follow (Subtle-Exact)", 0.5f));
    L.add (pct (pid::improvise, "Improvise", 0.4f));
    L.add (pct (pid::brain, "Brain (Listen-Neural)", 0.35f));
    L.add (std::make_unique<APF> (id (pid::swing), "Swing", juce::NormalisableRange<float> (0.f, 2.f, 0.01f), 1.f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.f)) + "% of style"; })));
    L.add (pct (pid::humanize, "Humanize", 0.6f));
    L.add (pct (pid::dynamics, "Dynamics", 0.6f));
    L.add (std::make_unique<APC> (id (pid::feel), "Feel", juce::StringArray { "Auto", "Half-time", "Normal", "Double-time" }, 0));
    L.add (std::make_unique<APC> (id (pid::section), "Section", sections, 0));
    L.add (std::make_unique<APC> (id (pid::phrase), "Phrase Length", juce::StringArray { "4 bars", "8 bars", "16 bars" }, 1));
    L.add (std::make_unique<APC> (id (pid::kit), "Kit", kits, KitStudio));
    L.add (std::make_unique<APB> (id (pid::autoKit), "Auto Kit", true));
    L.add (std::make_unique<APB> (id (pid::drummerOn), "Drummer On", true));
    L.add (std::make_unique<APB> (id (pid::lock), "Lock Groove", false));
    L.add (pct (pid::earsSens, "Ears Sensitivity", 0.5f));
    if (set == ParamSet::Drummer)
    {
        L.add (std::make_unique<APB> (id (pid::midiOut), "MIDI Out", false));
        L.add (std::make_unique<API> (id (pid::link), "Link Channel", 1, 8, 1));
        L.add (std::make_unique<APF> (id (pid::tempo), "Standalone Tempo", juce::NormalisableRange<float> (40.f, 300.f, 0.1f), 120.f));
    }
    if (set == ParamSet::Mixer)
    {
        L.add (std::make_unique<APC> (id (pid::lookahead), "Lookahead", juce::StringArray { "Off", "50 ms", "100 ms", "200 ms" }, 0));
        L.add (std::make_unique<APB> (id (pid::passThrough), "Pass Input Through", true));
    }
    // sound
    for (int g = 0; g < NumGroups; ++g)
        L.add (db (pid::groupVol (g), (juce::String (groupName (g)) + " Level").toRawUTF8(), -30.f, 6.f, 0.f));
    L.add (db (pid::master, "Master", -36.f, 6.f, -3.f));
    L.add (pct (pid::room, "Room", 0.2f));
    L.add (pct (pid::roomSize, "Room Size", 0.45f));
    L.add (pct (pid::drive, "Hammer Drive", 0.15f));
    L.add (pct (pid::glue, "Glue", 0.3f));
    L.add (std::make_unique<APF> (id (pid::tune), "Tune", juce::NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f,
                                  juce::AudioParameterFloatAttributes().withLabel ("st")));
    L.add (pct (pid::velCurve, "Velocity Curve", 0.5f));
    return L;
}

std::atomic<float>* ParamRefs::p (const char* idv) const
{
    return state != nullptr ? state->getRawParameterValue (idv) : nullptr;
}

void ParamRefs::attach (juce::AudioProcessorValueTreeState& s)
{
    state = &s;
    style = p (pid::style); intensity = p (pid::intensity); aggression = p (pid::aggression); fills = p (pid::fills);
    solos = p (pid::solos); follow = p (pid::follow); improvise = p (pid::improvise); brain = p (pid::brain);
    swing = p (pid::swing); humanize = p (pid::humanize); dynamics = p (pid::dynamics); feel = p (pid::feel);
    section = p (pid::section); phrase = p (pid::phrase); kit = p (pid::kit); autoKit = p (pid::autoKit);
    drummerOn = p (pid::drummerOn); midiOut = p (pid::midiOut); lock = p (pid::lock); lookahead = p (pid::lookahead);
    earsSens = p (pid::earsSens); link = p (pid::link); tempo = p (pid::tempo); passThrough = p (pid::passThrough);
    master = p (pid::master); room = p (pid::room); roomSize = p (pid::roomSize); drive = p (pid::drive);
    glue = p (pid::glue); tune = p (pid::tune); velCurve = p (pid::velCurve);
    for (int g = 0; g < NumGroups; ++g) groupVol[g] = p (pid::groupVol (g));
}

float ParamRefs::lookaheadMsFor (int i)
{
    static const float ms[4] = { 0.f, 50.f, 100.f, 200.f };
    return ms[juce::jlimit (0, 3, i)];
}

void ParamRefs::read (EngineSettings& e, SoundSettings& snd, int& kitOut, bool& autoKitOut, int& linkOut, bool& passOut) const
{
    auto v = [] (std::atomic<float>* a, float def) { return a != nullptr ? a->load (std::memory_order_relaxed) : def; };
    e.style = juce::jlimit (0, NumStyles - 1, (int) v (style, Rock));
    e.intensity = v (intensity, 0.55f); e.aggression = v (aggression, 0.4f); e.fills = v (fills, 0.4f);
    e.solos = v (solos, 0.05f); e.follow = v (follow, 0.5f); e.improvise = v (improvise, 0.4f);
    e.brain = v (brain, 0.35f); e.swing = v (swing, 1.f); e.humanize = v (humanize, 0.6f); e.dynamics = v (dynamics, 0.6f);
    e.feel = (Feel) juce::jlimit (0, 3, (int) v (feel, 0));
    e.section = (Section) juce::jlimit (0, (int) Section::NumSections - 1, (int) v (section, 0));
    static const int phraseBars[3] = { 4, 8, 16 };
    e.phraseBars = phraseBars[juce::jlimit (0, 2, (int) v (phrase, 1))];
    e.drummerOn = v (drummerOn, 1.f) > 0.5f;
    e.midiOut = v (midiOut, 0.f) > 0.5f;
    e.lockGroove = v (lock, 0.f) > 0.5f;
    e.lookaheadMs = lookaheadMsFor ((int) v (lookahead, 0));
    e.earsSensitivity = v (earsSens, 0.5f);
    e.internalBpm = v (tempo, 120.f);
    kitOut = juce::jlimit (0, NumKits - 1, (int) v (kit, KitStudio));
    autoKitOut = v (autoKit, 1.f) > 0.5f;
    linkOut = juce::jlimit (1, 8, (int) v (link, 1));
    passOut = v (passThrough, 1.f) > 0.5f;
    for (int g = 0; g < NumGroups; ++g) snd.groupGain[g] = juce::Decibels::decibelsToGain (v (groupVol[g], 0.f), -30.f);
    snd.master = juce::Decibels::decibelsToGain (v (master, -3.f), -36.f);
    snd.room = v (room, 0.2f); snd.roomSize = v (roomSize, 0.45f); snd.drive = v (drive, 0.15f);
    snd.glue = v (glue, 0.3f); snd.tuneSemis = v (tune, 0.f); snd.velCurve = v (velCurve, 0.5f);
}
} // namespace hammer
