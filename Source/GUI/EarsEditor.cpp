#include "EarsEditor.h"

namespace hammer
{
EarsEditor::EarsEditor (EarsProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p),
      linkAtt (*p.apvts.getParameter (pid::link), [this] (float v) { channel = juce::roundToInt (v); repaint(); })
{
    setLookAndFeel (&lnf);
    sens.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    sens.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    sens.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
    addAndMakeVisible (sens);
    sensAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, pid::earsSens, sens);
    linkAtt.sendInitialUpdate();
    setSize (560, 330);
    startTimerHz (30);
}

EarsEditor::~EarsEditor() { setLookAndFeel (nullptr); }

juce::Rectangle<float> EarsEditor::channelCell (int i) const
{
    return { 40.f + (i % 4) * 62.f, 120.f + (i / 4) * 40.f, 54.f, 28.f };
}

void EarsEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 8; ++i)
        if (channelCell (i).contains (e.position)) { linkAtt.setValueAsCompleteGesture ((float) (i + 1)); return; }
}

void EarsEditor::timerCallback()
{
    for (int i = 0; i < 3; ++i) { const float v = proc.meter (i); m[i] = v > m[i] ? v : m[i] * 0.88f + v * 0.12f; }
    flash = proc.onsetFlash();
    repaint();
}

void EarsEditor::paint (juce::Graphics& g)
{
    rc::drawCase (g, getLocalBounds().toFloat(), 18.f);
    rc::drawStencilText (g, "THE HAMMER EARS", { 34, 24, 340, 40 }, 32.f, rc::stencil);
    rc::drawPlate (g, { 380.f, 24.f, 150.f, 48.f }, "APT INDUSTRIAL", "LISTENING POST");
    g.setFont (rc::stencilFont (13.f));
    g.setColour (rc::amber);
    g.drawText ("LINK CHANNEL", 40, 92, 250, 18, juce::Justification::centredLeft);
    for (int i = 0; i < 8; ++i)
        rc::drawTape (g, channelCell (i), juce::String (i + 1), ((i * 3) % 5 - 2) * 0.8f, channel == i + 1, 16.f, 300 + i);

    // meters
    auto mr = juce::Rectangle<float> (304, 104, 120, 150);
    rc::drawRecess (g, mr.expanded (8, 6), 6.f);
    const char* names[3] = { "LOW", "MID", "HIGH" };
    const juce::Colour cols[3] = { rc::stageRed, rc::stageWhite, rc::amber };
    for (int i = 0; i < 3; ++i)
    {
        auto col = juce::Rectangle<float> (mr.getX() + i * 40 + 8, mr.getY(), 24, mr.getHeight() - 18);
        const int segs = 12; const float sh = col.getHeight() / segs;
        for (int s = 0; s < segs; ++s)
        {
            g.setColour (m[i] * segs > s ? cols[i] : juce::Colour (0xff1c1712));
            g.fillRoundedRectangle (col.getX(), col.getBottom() - (s + 1) * sh + 1, col.getWidth(), sh - 2, 1.5f);
        }
        g.setColour (rc::stencil.withAlpha (0.7f)); g.setFont (rc::stencilFont (11.f));
        g.drawText (names[i], (int) (mr.getX() + i * 40), (int) mr.getBottom() - 16, 40, 16, juce::Justification::centred);
    }
    // status
    const bool linked = proc.isLinked(), run = proc.transportRunning();
    rc::drawLed (g, { 40, 222, 14, 14 }, linked ? rc::stageGreen : rc::stageRed, 1.f);
    rc::drawLed (g, { 40, 246, 14, 14 }, rc::amber, flash);
    g.setFont (rc::stencilFont (14.f));
    g.setColour (rc::stencil);
    g.drawText (linked ? (run ? "STREAMING TO THE HAMMER ON CH " + juce::String (channel) : "PRESS PLAY IN FL STUDIO")      : "WAITING FOR AUDIO...",
                62, 220, 230, 18, juce::Justification::centredLeft);
    g.drawText ("HITS HEARD", 62, 244, 230, 18, juce::Justification::centredLeft);
    g.setColour (rc::stencil.withAlpha (0.55f));
    g.setFont (rc::stencilFont (12.f));
    g.drawFittedText ("PUT ME ON A MIXER TRACK WITH YOUR SONG BUT WITHOUT THE DRUMS. AUDIO PASSES THROUGH UNTOUCHED.",
                      40, 272, 380, 34, juce::Justification::centredLeft, 2);
    rc::drawTape (g, { 440.f, 110.f, 90.f, 22.f }, "SENSITIVITY", 1.f, false, 12.f, 400);
}

void EarsEditor::resized()
{
    sens.setBounds (438, 136, 94, 110);
}
} // namespace hammer
