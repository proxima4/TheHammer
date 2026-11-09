#pragma once
#include "HammerComponents.h"
#include "../Plugin/HammerPluginBase.h"

namespace hammer
{
class HammerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HammerEditor (HammerPluginBase& p);
    ~HammerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kW = 1180, kH = 760;

private:
    void timerCallback() override;
    void showPage (int p);

    HammerPluginBase& proc;
    RoadCaseLookAndFeel lnf;
    std::unique_ptr<juce::Component> content;
    juce::ComponentBoundsConstrainer constrainer;
    juce::TooltipWindow tooltips { this, 600 };
};
} // namespace hammer
