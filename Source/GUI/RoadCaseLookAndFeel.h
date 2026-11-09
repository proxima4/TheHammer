#pragma once
// ============================================================================
//  Road-case look: aluminium extrusion, ball corners, rivets, black laminate,
//  gaffer-tape labels, stencil lettering, stage-light glows.
// ============================================================================
#include <juce_gui_basics/juce_gui_basics.h>

namespace hammer
{
namespace rc
{
    // palette
    const juce::Colour laminate      { 0xff151516 };
    const juce::Colour laminateHi    { 0xff222224 };
    const juce::Colour alu           { 0xffb9bec4 };
    const juce::Colour aluDark       { 0xff6c727a };
    const juce::Colour tape          { 0xffe6dfcc };
    const juce::Colour tapeShadow    { 0xffb8b09a };
    const juce::Colour marker        { 0xff1c1c1c };
    const juce::Colour markerRed     { 0xffb3261e };
    const juce::Colour stencil       { 0xffeeeeea };
    const juce::Colour amber         { 0xffffa21f };
    const juce::Colour amberDim      { 0xff7a4a0a };
    const juce::Colour stageRed      { 0xffff3b2f };
    const juce::Colour stageBlue     { 0xff3fa9ff };
    const juce::Colour stageGreen    { 0xff4fe08a };
    const juce::Colour stageGold     { 0xffffd34d };
    const juce::Colour stageWhite    { 0xfff4f1e8 };
    const juce::Colour stageMagenta  { 0xffe25cff };
    const juce::Colour ledOff        { 0xff2a1a12 };

    juce::Font stencilFont (float h);
    juce::Font markerFont (float h);
    juce::Font plateFont (float h);

    void drawCase (juce::Graphics& g, juce::Rectangle<float> r, float frame);
    void drawRivet (juce::Graphics& g, float cx, float cy, float rad);
    void drawBallCorner (juce::Graphics& g, float cx, float cy, float rad);
    void drawTape (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float angleDeg,
                   bool lit, float fontH, int seed, juce::Colour ink = marker);
    void drawStencilText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float h,
                          juce::Colour c, juce::Justification j = juce::Justification::centredLeft);
    void drawPlate (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& line1, const juce::String& line2);
    void drawLed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float level);
    void drawRecess (juce::Graphics& g, juce::Rectangle<float> r, float corner = 6.f);
    juce::Colour laneColour (int lane);
}

class RoadCaseLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RoadCaseLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
};
} // namespace hammer
