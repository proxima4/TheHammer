#pragma once
// Shared AudioProcessor plumbing for The Hammer (drummer) and The Hammer FX.
#include "HammerParams.h"
#include "../Engine/HammerEngine.h"

namespace hammer
{
class HammerPluginBase : public juce::AudioProcessor
{
public:
    HammerPluginBase (const BusesProperties& buses, HammerEngine::Mode mode, ParamSet set);
    ~HammerPluginBase() override = default;

    HammerEngine engine;
    juce::AudioProcessorValueTreeState apvts;
    ParamRefs refs;

    bool isMixerMode() const { return engine.mode == HammerEngine::Mode::Mixer; }
    void loadKitIntoSlot (int slot, const juce::File& source);
    void clearKitSlot (int slot);

    // AudioProcessor boilerplate
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return ! isMixerMode(); }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;
    void releaseResources() override {}

protected:
    TransportInfo readTransport();
};
} // namespace hammer
