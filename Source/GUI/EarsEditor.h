#pragma once
#include "RoadCaseLookAndFeel.h"
#include "../Plugin/EarsProcessor.h"

namespace hammer
{
class EarsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EarsEditor (EarsProcessor& p);
    ~EarsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> channelCell (int i) const;
    EarsProcessor& proc;
    RoadCaseLookAndFeel lnf;
    juce::Slider sens;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensAtt;
    juce::ParameterAttachment linkAtt;
    int channel = 1;
    float m[3] {}, flash = 0.f;
};
} // namespace hammer
