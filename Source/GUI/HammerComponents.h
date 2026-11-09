#pragma once
#include "RoadCaseLookAndFeel.h"
#include "../Engine/HammerEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace hammer
{
// 12 gaffer-tape style labels -------------------------------------------------
class StyleSelector : public juce::Component
{
public:
    explicit StyleSelector (juce::RangedAudioParameter& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
private:
    int hit (juce::Point<float> p) const;
    juce::Rectangle<float> cell (int i) const;
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attach;
    int value = 0, hover = -1;
};

// Knob with a tape label ------------------------------------------------------
class TapeKnob : public juce::Component
{
public:
    TapeKnob (juce::AudioProcessorValueTreeState& s, const char* paramId, const juce::String& label, int seed);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider slider;
private:
    juce::String text; int seed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// Long horizontal crossfader with end labels ----------------------------------
class CrossFader : public juce::Component
{
public:
    CrossFader (juce::AudioProcessorValueTreeState& s, const char* paramId, const juce::String& title,
                const juce::String& left, const juce::String& right, int seed);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider slider;
private:
    juce::String title, leftText, rightText; int seed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// Labelled combo bound to a choice parameter -----------------------------------
class TapeChoice : public juce::Component
{
public:
    TapeChoice (juce::AudioProcessorValueTreeState& s, const char* paramId, const juce::String& label, int seed);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::ComboBox box;
private:
    juce::String text; int seed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

// Live LED groove matrix -------------------------------------------------------
class GrooveDisplay : public juce::Component, private juce::Timer
{
public:
    explicit GrooveDisplay (HammerEngine& e);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    HammerEngine& engine;
    DisplaySnapshot snap;
    float playTick = 0.f;
};

// Ears meters + status ---------------------------------------------------------
class EarsPanel : public juce::Component, private juce::Timer
{
public:
    explicit EarsPanel (HammerEngine& e, bool mixerMode);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    HammerEngine& engine;
    bool mixer;
    float m[3] {}, energy = 0, intensity = 0;
    bool linked = false, listening = false, model = false;
};

// Drag-out MIDI ----------------------------------------------------------------
class DragMidiBox : public juce::Component
{
public:
    explicit DragMidiBox (HammerEngine& e) : engine (e) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    int bars = 16;
private:
    HammerEngine& engine;
    bool dragging = false;
};

// Momentary / trigger button ---------------------------------------------------
class HoldButton : public juce::TextButton
{
public:
    explicit HoldButton (const juce::String& t) : juce::TextButton (t) {}
    std::function<void()> onPress, onRelease;
    void buttonStateChanged() override
    {
        const bool d = isDown();
        if (d != wasDown) { wasDown = d; if (d) { if (onPress) onPress(); } else if (onRelease) onRelease(); }
        juce::TextButton::buttonStateChanged();
    }
private:
    bool wasDown = false;
};
} // namespace hammer
