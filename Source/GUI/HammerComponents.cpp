#include "HammerComponents.h"
#include "../Brain/StyleLibrary.h"

namespace hammer
{
// ============================================================================
StyleSelector::StyleSelector (juce::RangedAudioParameter& p)
    : param (p), attach (p, [this] (float v) { value = juce::roundToInt (v); repaint(); })
{
    attach.sendInitialUpdate();
}

juce::Rectangle<float> StyleSelector::cell (int i) const
{
    const float w = getWidth() / 6.f, h = getHeight() / 2.f;
    return { (i % 6) * w + 6, (i / 6) * h + 6, w - 12, h - 12 };
}

int StyleSelector::hit (juce::Point<float> p) const
{
    for (int i = 0; i < NumStyles; ++i) if (cell (i).expanded (4).contains (p)) return i;
    return -1;
}

void StyleSelector::paint (juce::Graphics& g)
{
    for (int i = 0; i < NumStyles; ++i)
    {
        const float ang = ((i * 37) % 7 - 3) * 0.45f;
        auto r = cell (i);
        if (hover == i && value != i) r = r.translated (0, -1.5f);
        rc::drawTape (g, r, juce::String (styleName (i)).toUpperCase(), ang, value == i, r.getHeight() * 0.5f, 100 + i);
    }
}

void StyleSelector::mouseDown (const juce::MouseEvent& e)
{
    const int i = hit (e.position);
    if (i >= 0) attach.setValueAsCompleteGesture ((float) i);
}

void StyleSelector::mouseMove (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h != hover) { hover = h; repaint(); }
}

// ============================================================================
TapeKnob::TapeKnob (juce::AudioProcessorValueTreeState& s, const char* pidv, const juce::String& label, int sd)
    : text (label), seed (sd)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, pidv, slider);
}

void TapeKnob::paint (juce::Graphics& g)
{
    rc::drawTape (g, { 8.f, 2.f, getWidth() - 16.f, 22.f }, text, ((seed * 13) % 5 - 2) * 0.6f, false, 14.f, seed);
}

void TapeKnob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (28));
}

// ============================================================================
CrossFader::CrossFader (juce::AudioProcessorValueTreeState& s, const char* pidv, const juce::String& t,
                        const juce::String& l, const juce::String& r, int sd)
    : title (t), leftText (l), rightText (r), seed (sd)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, pidv, slider);
    slider.onValueChange = [this] { repaint(); };
}

void CrossFader::paint (juce::Graphics& g)
{
    rc::drawTape (g, { 0.f, 4.f, 150.f, 26.f }, title, -1.2f, false, 15.f, seed);
    g.setFont (rc::stencilFont (15.f));
    const float v = (float) slider.valueToProportionOfLength (slider.getValue());
    g.setColour (rc::stencil.withAlpha (0.45f + 0.55f * (1.f - v)));
    g.drawText (leftText, juce::Rectangle<float> (160.f, 0.f, 120.f, 18.f), juce::Justification::centredLeft);
    g.setColour (rc::stencil.withAlpha (0.45f + 0.55f * v));
    g.drawText (rightText, juce::Rectangle<float> (getWidth() - 130.f, 0.f, 120.f, 18.f), juce::Justification::centredRight);
    g.setColour (rc::amber);
    g.setFont (rc::stencilFont (14.f));
    g.drawText (juce::String (juce::roundToInt ((1.f - v) * 100)) + " : " + juce::String (juce::roundToInt (v * 100)),
                juce::Rectangle<float> (160.f, 0.f, getWidth() - 290.f, 18.f), juce::Justification::centred);
}

void CrossFader::resized()
{
    slider.setBounds (160, 16, getWidth() - 170, getHeight() - 16);
}

// ============================================================================
TapeChoice::TapeChoice (juce::AudioProcessorValueTreeState& s, const char* pidv, const juce::String& label, int sd)
    : text (label), seed (sd)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (pidv)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, pidv, box);
}

void TapeChoice::paint (juce::Graphics& g)
{
    rc::drawTape (g, { 4.f, 0.f, std::min (120.f, getWidth() - 8.f), 20.f }, text, ((seed * 7) % 5 - 2) * 0.5f, false, 13.f, seed);
}

void TapeChoice::resized() { box.setBounds (getLocalBounds().withTrimmedTop (24)); }

// ============================================================================
GrooveDisplay::GrooveDisplay (HammerEngine& e) : engine (e) { startTimerHz (30); }

void GrooveDisplay::timerCallback()
{
    engine.copyDisplay (snap);
    playTick = engine.playheadTick();
    repaint();
}

void GrooveDisplay::paint (juce::Graphics& g)
{
    engine.copyDisplay (snap);
    playTick = engine.playheadTick();
    auto r = getLocalBounds().toFloat();
    rc::drawRecess (g, r, 8.f);
    const auto& st = engine.conductor.status;
    const int style = st.style.load();

    // header strip
    auto header = r.removeFromTop (30).reduced (12, 4);
    static const char* roles[] = { "GROOVE", "FILL", "SOLO", "STOP", "SECTION" };
    const auto& sd = StyleLibrary::get (style);
    const int gi = juce::jlimit (0, sd.numGrooves - 1, st.groove.load());
    const int phrase = st.barInPhrase.load() + 1;
    const juce::String feelTxt = st.feel.load() == 1 ? "HALF-TIME" : (st.feel.load() == 3 ? "DOUBLE-TIME" : "");
    g.setFont (rc::stencilFont (15.f));
    g.setColour (rc::amber);
    g.drawText ("BAR " + juce::String (st.bar.load() + 1) + "   PHRASE " + juce::String (phrase), header, juce::Justification::centredLeft);
    g.setColour (rc::stencil);
    g.drawText (juce::String (sectionName (st.section.load())).toUpperCase() + "  /  " + juce::String (sd.grooves[gi].name).toUpperCase()
                    + (feelTxt.isNotEmpty() ? "  /  " + feelTxt : juce::String()),
                header, juce::Justification::centred);
    const int role = juce::jlimit (0, 4, snap.role);
    g.setColour (role == RoleFill ? rc::stageBlue : role == RoleSolo ? rc::stageMagenta : role == RoleStop ? rc::stageRed : rc::stageGreen);
    g.drawText (roles[role], header, juce::Justification::centredRight);

    // rows
    struct Row { const char* name; int lanes[4]; };
    Row rows[] = {
        { "CYMB", { Crash, Crash2, China, Splash } }, { "RIDE", { Ride, RideBell, -1, -1 } },
        { "HATS", { HatClosed, HatOpen, HatPedal, -1 } }, { "TOM 1", { TomHigh, -1, -1, -1 } },
        { "TOM 2", { TomMid, -1, -1, -1 } }, { "FLOOR", { TomFloor, -1, -1, -1 } },
        { "SNARE", { Snare, Rim, Clap, -1 } }, { "KICK", { Kick, -1, -1, -1 } },
        { "PERC", { Cowbell, Shaker, Tambourine, Perc } } };
    if (style == Cajon) { rows[6].name = "SLAP/TIP"; rows[7].name = "BASS"; rows[3].name = "BONGO"; rows[4].name = "CONGA"; rows[2].name = "SHAKER"; }
    if (style == Orchestral) { rows[0].name = "CLASH/GONG"; rows[1].name = "SUS CYM"; rows[3].name = "TIMP HI"; rows[4].name = "TIMP MID"; rows[5].name = "TIMP LO"; rows[7].name = "BASS DRM"; }
    const int nRows = 9;
    auto body = r.reduced (10, 6);
    auto labels = body.removeFromLeft (74);
    const float rowH = body.getHeight() / nRows;
    const int tpb = std::max (12, snap.ticksPerBar);
    const bool trip = sd.tripletGrid;
    const int colTicks = trip ? 4 : 3;
    const float tickW = body.getWidth() / (float) tpb;

    // grid
    for (int t = 0; t <= tpb; t += colTicks)
    {
        const float x = body.getX() + t * tickW;
        g.setColour (t % kTicksPerBeat == 0 ? juce::Colour (0xff3b3324) : juce::Colour (0xff1d1b18));
        g.drawVerticalLine ((int) x, body.getY(), body.getBottom());
    }
    for (int i = 0; i < nRows; ++i)
    {
        const float y = body.getY() + i * rowH;
        float flash = 0.f;
        for (int l : rows[i].lanes) if (l >= 0) flash = std::max (flash, engine.drums.laneMeter (l));
        g.setColour (rc::stencil.withAlpha (0.45f + 0.55f * flash));
        g.setFont (rc::stencilFont (std::min (14.f, rowH * 0.7f)));
        g.drawText (rows[i].name, labels.withY (y).withHeight (rowH), juce::Justification::centredLeft);
        // unlit LEDs
        for (int t = 0; t < tpb; t += colTicks)
        {
            g.setColour (juce::Colour (0xff1b1410));
            g.fillEllipse (body.getX() + (t + colTicks * 0.5f) * tickW - 3.f, y + rowH * 0.5f - 3.f, 6.f, 6.f);
        }
    }
    // playhead
    const float px = body.getX() + juce::jlimit (0.f, (float) tpb, playTick) * tickW;
    juce::ColourGradient ph (rc::amber.withAlpha (0.35f), px, body.getY(), rc::amber.withAlpha (0.f), px - 40, body.getY(), false);
    g.setGradientFill (ph);
    g.fillRect (px - 40, body.getY(), 40.f, body.getHeight());
    g.setColour (rc::amber.withAlpha (0.8f));
    g.drawVerticalLine ((int) px, body.getY(), body.getBottom());

    // hits
    for (int i = 0; i < snap.numHits; ++i)
    {
        const Hit& h = snap.hits[(size_t) i];
        int row = -1;
        for (int k = 0; k < nRows && row < 0; ++k) for (int l : rows[k].lanes) if (l == h.lane) { row = k; break; }
        if (row < 0) continue;
        const float x = body.getX() + ((float) h.tick + h.offsetTicks + 0.5f * colTicks) * tickW;
        const float y = body.getY() + row * rowH + rowH * 0.5f;
        const bool past = (float) h.tick <= playTick;
        const juce::Colour c = rc::laneColour (h.lane).withMultipliedBrightness (past ? 1.f : 0.55f);
        const float s = 5.f + 5.f * h.velocity;
        if (past)
        {
            juce::ColourGradient glow (c.withAlpha (0.6f * h.velocity), x, y, c.withAlpha (0.f), x + s * 1.8f, y, true);
            g.setGradientFill (glow);
            g.fillEllipse (x - s * 1.8f, y - s * 1.8f, s * 3.6f, s * 3.6f);
        }
        g.setColour (c.withAlpha (0.35f + 0.65f * h.velocity));
        if (h.lane == HatOpen || h.lane == China) g.drawEllipse (x - s * 0.5f, y - s * 0.5f, s, s, 2.f);
        else g.fillEllipse (x - s * 0.5f, y - s * 0.5f, s, s);
    }
}

// ============================================================================
EarsPanel::EarsPanel (HammerEngine& e, bool mixerMode) : engine (e), mixer (mixerMode) { startTimerHz (30); }

void EarsPanel::timerCallback()
{
    float v[3]; engine.earsMeters (v);
    for (int i = 0; i < 3; ++i) m[i] = v[i] > m[i] ? v[i] : m[i] * 0.88f + v[i] * 0.12f;
    energy = engine.conductor.status.energy.load();
    intensity = engine.conductor.status.intensity.load();
    linked = mixer ? true : engine.linkConnected();
    listening = engine.conductor.status.listening.load();
    model = engine.conductor.status.neuralModel.load() || engine.neural.hasModel();
    repaint();
}

void EarsPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    rc::drawRecess (g, r, 8.f);
    auto top = r.removeFromTop (28).reduced (10, 4);
    g.setFont (rc::stencilFont (15.f));
    g.setColour (rc::stencil);
    g.drawText ("EARS", top, juce::Justification::centredLeft);
    rc::drawLed (g, { top.getRight() - 12, top.getCentreY() - 6, 12, 12 }, linked ? rc::stageGreen : rc::stageRed, 1.f);
    g.setFont (rc::stencilFont (12.f));
    g.setColour (rc::stencil.withAlpha (0.7f));
    g.drawText (mixer ? "INPUT" : (linked ? "LINKED" : "NO EARS"), top.withTrimmedRight (18), juce::Justification::centredRight);

    auto meters = r.removeFromTop (r.getHeight() - 92).reduced (14, 6);
    const char* names[3] = { "LOW", "MID", "HIGH" };
    const juce::Colour cols[3] = { rc::stageRed, rc::stageWhite, rc::amber };
    const float w = meters.getWidth() / 3.f;
    for (int i = 0; i < 3; ++i)
    {
        auto col = juce::Rectangle<float> (meters.getX() + i * w + w * 0.2f, meters.getY(), w * 0.6f, meters.getHeight() - 18);
        const int segs = 14;
        const float sh = col.getHeight() / segs;
        for (int s = 0; s < segs; ++s)
        {
            const bool on = m[i] * segs > s;
            auto seg = juce::Rectangle<float> (col.getX(), col.getBottom() - (s + 1) * sh + 1, col.getWidth(), sh - 2);
            g.setColour (on ? cols[i].withAlpha (0.4f + 0.6f * (float) s / segs) : juce::Colour (0xff1c1712));
            g.fillRoundedRectangle (seg, 1.5f);
        }
        g.setColour (rc::stencil.withAlpha (0.7f));
        g.setFont (rc::stencilFont (12.f));
        g.drawText (names[i], juce::Rectangle<float> (meters.getX() + i * w, meters.getBottom() - 16, w, 16), juce::Justification::centred);
    }
    // bars: song energy, drummer intensity, status
    auto bar = [&] (juce::Rectangle<float> b, const juce::String& t, float v, juce::Colour c) {
        g.setFont (rc::stencilFont (12.f));
        g.setColour (rc::stencil.withAlpha (0.75f));
        g.drawText (t, b.removeFromTop (14), juce::Justification::centredLeft);
        auto tr = b.reduced (0, 2);
        g.setColour (juce::Colour (0xff1c1712)); g.fillRoundedRectangle (tr, 2.f);
        g.setColour (c); g.fillRoundedRectangle (tr.withWidth (tr.getWidth() * juce::jlimit (0.f, 1.f, v)), 2.f);
    };
    r.reduce (12, 2);
    bar (r.removeFromTop (28), listening ? "SONG ENERGY" : "SONG ENERGY (NOT HEARING)", energy, rc::stageGreen);
    bar (r.removeFromTop (28), "DRUMMER INTENSITY", intensity, rc::amber);
    g.setFont (rc::stencilFont (12.f));
    g.setColour (model ? rc::stageGreen : rc::amber);
    g.drawText (model ? "NEURAL: TRAINED MODEL" : "NEURAL: PLAYBOOK IMPROVISER", r, juce::Justification::centredLeft);
}

// ============================================================================
void DragMidiBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (1.5f, 2.f), 5.f);
    g.setColour (juce::Colour (0xff1b1c1e));
    g.fillRoundedRectangle (r, 5.f);
    const float dash[] = { 6.f, 4.f };
    juce::Path p; p.addRoundedRectangle (r.reduced (3), 4.f);
    juce::Path d; juce::PathStrokeType (1.5f).createDashedStroke (d, p, dash, 2);
    g.setColour (dragging ? rc::amber : rc::stencil.withAlpha (0.6f));
    g.fillPath (d);
    rc::drawStencilText (g, "DRAG MIDI", r.withTrimmedBottom (r.getHeight() * 0.35f), 18.f, rc::stencil, juce::Justification::centred);
    g.setFont (rc::stencilFont (12.f));
    g.setColour (rc::amber);
    g.drawText ("LAST " + juce::String (bars) + " BARS > FL PLAYLIST", r.withTrimmedTop (r.getHeight() * 0.55f), juce::Justification::centred);
}

void DragMidiBox::mouseDrag (const juce::MouseEvent&)
{
    if (dragging) return;
    dragging = true;
    repaint();
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("The Hammer " + juce::Time::getCurrentTime().formatted ("%H%M%S") + ".mid");
    if (engine.exportMidi (f, bars))
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; repaint(); });
    else
        dragging = false;
}
} // namespace hammer
