#pragma once
// The Hammer Ears - put it on your master (or a bus without the drums).
// It passes audio through untouched and streams what it hears to the
// drummer instrument over a shared-memory link channel.
#include "HammerParams.h"
#include "../Ears/OnsetAnalyzer.h"
#include "../Ears/SharedLink.h"

namespace hammer
{
class EarsProcessor final : public juce::AudioProcessor
{
public:
    EarsProcessor();
    juce::AudioProcessorValueTreeState apvts;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;

    float meter (int b) const { return meters[b].load(); }
    float onsetFlash() const { return flash.load(); }
    bool isLinked() const { return linkOpen.load(); }
    int channel() const { return currentChannel.load(); }
    bool transportRunning() const { return running.load(); }

private:
    OnsetAnalyzer analyzer;
    SharedLink link;
    OnsetAnalyzer::Event events[OnsetAnalyzer::kMaxEventsPerBlock];
    std::atomic<float> meters[3];
    std::atomic<float> flash { 0.f };
    std::atomic<bool> linkOpen { false }, running { false };
    std::atomic<int> currentChannel { 1 };
    std::atomic<float>* linkParam = nullptr;
    std::atomic<float>* sensParam = nullptr;
    double sr = 44100.0;
    int energyCountdown = 0;
};
} // namespace hammer
