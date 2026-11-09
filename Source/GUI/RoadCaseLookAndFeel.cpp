#include "RoadCaseLookAndFeel.h"
#include "../Common/HammerTypes.h"

namespace hammer
{
namespace rc
{
juce::Font stencilFont (float h)
{
    return juce::Font (juce::FontOptions ("Impact", h, juce::Font::plain)).withExtraKerningFactor (0.06f);
}
juce::Font markerFont (float h)
{
    return juce::Font (juce::FontOptions ("Segoe Print", h, juce::Font::bold));
}
juce::Font plateFont (float h)
{
    return juce::Font (juce::FontOptions ("Arial", h, juce::Font::bold)).withExtraKerningFactor (0.18f);
}

juce::Colour laneColour (int lane)
{
    switch (laneGroup (lane))
    {
        case GKick: return stageRed;
        case GSnare: return stageWhite;
        case GToms: return stageBlue;
        case GHats: return amber;
        case GCymbals: return stageGold;
        default: return stageGreen;
    }
}

void drawRivet (juce::Graphics& g, float cx, float cy, float r)
{
    juce::ColourGradient cg (juce::Colour (0xffe8ebee), cx - r * 0.5f, cy - r * 0.5f, juce::Colour (0xff4a4e54), cx + r, cy + r, true);
    g.setGradientFill (cg);
    g.fillEllipse (cx - r, cy - r, 2 * r, 2 * r);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (cx - r, cy - r, 2 * r, 2 * r, 0.8f);
}

void drawBallCorner (juce::Graphics& g, float cx, float cy, float r)
{
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (cx - r + 2, cy - r + 3, 2 * r, 2 * r);
    juce::ColourGradient cg (juce::Colour (0xfff3f5f7), cx - r * 0.45f, cy - r * 0.55f, juce::Colour (0xff3d4147), cx + r * 0.9f, cy + r * 0.9f, true);
    cg.addColour (0.35, juce::Colour (0xffc4c9cf));
    g.setGradientFill (cg);
    g.fillEllipse (cx - r, cy - r, 2 * r, 2 * r);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillEllipse (cx - r * 0.55f, cy - r * 0.62f, r * 0.45f, r * 0.3f);
    drawRivet (g, cx + r * 0.15f, cy + r * 0.2f, r * 0.16f);
}

static void brushed (juce::Graphics& g, juce::Rectangle<float> r, bool horizontal)
{
    juce::ColourGradient cg (juce::Colour (0xffd5d9de), r.getX(), r.getY(), juce::Colour (0xff868c94),
                             horizontal ? r.getX() : r.getRight(), horizontal ? r.getBottom() : r.getY(), false);
    cg.addColour (0.45, juce::Colour (0xffbfc4ca));
    cg.addColour (0.55, juce::Colour (0xffa7adb4));
    g.setGradientFill (cg);
    g.fillRect (r);
    juce::Random rnd (1234);
    const int lines = (int) (horizontal ? r.getHeight() : r.getWidth());
    for (int i = 0; i < lines; i += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (rnd.nextFloat() * 0.08f));
        if (horizontal) g.drawHorizontalLine ((int) r.getY() + i, r.getX(), r.getRight());
        else g.drawVerticalLine ((int) r.getX() + i, r.getY(), r.getBottom());
    }
    // extrusion ridges
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    if (horizontal) { g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * 0.3f), r.getX(), r.getRight()); g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * 0.7f), r.getX(), r.getRight()); }
    else { g.drawVerticalLine ((int) (r.getX() + r.getWidth() * 0.3f), r.getY(), r.getBottom()); g.drawVerticalLine ((int) (r.getX() + r.getWidth() * 0.7f), r.getY(), r.getBottom()); }
}

void drawCase (juce::Graphics& g, juce::Rectangle<float> r, float f)
{
    // laminate
    g.setColour (laminate);
    g.fillRect (r);
    juce::Random rnd (42);
    for (int i = 0; i < (int) (r.getWidth() * r.getHeight() / 90.f); ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (rnd.nextFloat() * 0.035f));
        g.fillRect (r.getX() + rnd.nextFloat() * r.getWidth(), r.getY() + rnd.nextFloat() * r.getHeight(), 1.5f, 1.5f);
    }
    // aluminium frame
    brushed (g, { r.getX(), r.getY(), r.getWidth(), f }, true);
    brushed (g, { r.getX(), r.getBottom() - f, r.getWidth(), f }, true);
    brushed (g, { r.getX(), r.getY(), f, r.getHeight() }, false);
    brushed (g, { r.getRight() - f, r.getY(), f, r.getHeight() }, false);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRect (r.reduced (f), 2.f);
    // rivets
    for (float x = r.getX() + 70; x < r.getRight() - 60; x += 72)
    {
        drawRivet (g, x, r.getY() + f * 0.5f, 3.2f);
        drawRivet (g, x, r.getBottom() - f * 0.5f, 3.2f);
    }
    for (float y = r.getY() + 70; y < r.getBottom() - 60; y += 72)
    {
        drawRivet (g, r.getX() + f * 0.5f, y, 3.2f);
        drawRivet (g, r.getRight() - f * 0.5f, y, 3.2f);
    }
    const float br = f * 1.25f;
    drawBallCorner (g, r.getX() + f * 0.6f, r.getY() + f * 0.6f, br);
    drawBallCorner (g, r.getRight() - f * 0.6f, r.getY() + f * 0.6f, br);
    drawBallCorner (g, r.getX() + f * 0.6f, r.getBottom() - f * 0.6f, br);
    drawBallCorner (g, r.getRight() - f * 0.6f, r.getBottom() - f * 0.6f, br);
}

void drawTape (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float angleDeg, bool lit, float fontH, int seed, juce::Colour ink)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (angleDeg), r.getCentreX(), r.getCentreY()));
    if (lit)
    {
        // stage light pooling behind the tape
        juce::ColourGradient glow (amber.withAlpha (0.55f), r.getCentreX(), r.getCentreY(), amber.withAlpha (0.f), r.getCentreX() + r.getWidth() * 0.75f, r.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillEllipse (r.expanded (r.getWidth() * 0.25f, r.getHeight() * 1.1f));
    }
    // torn edges
    juce::Path p;
    juce::Random rnd (seed);
    const int teeth = 7;
    p.startNewSubPath (r.getX(), r.getY());
    p.lineTo (r.getRight(), r.getY());
    for (int i = 1; i <= teeth; ++i) p.lineTo (r.getRight() + (rnd.nextFloat() - 0.5f) * 3.5f, r.getY() + r.getHeight() * i / teeth);
    p.lineTo (r.getX(), r.getBottom());
    for (int i = teeth - 1; i >= 1; --i) p.lineTo (r.getX() + (rnd.nextFloat() - 0.5f) * 3.5f, r.getY() + r.getHeight() * i / teeth);
    p.closeSubPath();
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillPath (p, juce::AffineTransform::translation (1.5f, 2.f));
    juce::ColourGradient tg (lit ? tape.brighter (0.08f) : tape, r.getX(), r.getY(), lit ? tape : tapeShadow, r.getX(), r.getBottom(), false);
    g.setGradientFill (tg);
    g.fillPath (p);
    // cloth weave
    g.setColour (juce::Colours::black.withAlpha (0.04f));
    for (float x = r.getX() + 2; x < r.getRight(); x += 3) g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
    g.setColour (lit ? markerRed : ink);
    g.setFont (markerFont (fontH));
    g.drawFittedText (text, r.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
}

void drawStencilText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float h, juce::Colour c, juce::Justification j)
{
    g.setFont (stencilFont (h));
    // slight overspray
    g.setColour (c.withAlpha (0.12f));
    g.drawText (text, r.translated (0.8f, 0.6f), j, false);
    g.setColour (c.withAlpha (0.92f));
    g.drawText (text, r, j, false);
    // stencil bridges: thin cuts through the letters
    juce::GlyphArrangement ga;
    ga.addLineOfText (stencilFont (h), text, 0, 0);
    const float tw = ga.getBoundingBox (0, -1, true).getWidth();
    float x0 = r.getX();
    if (j.testFlags (juce::Justification::horizontallyCentred)) x0 = r.getCentreX() - tw * 0.5f;
    if (j.testFlags (juce::Justification::right)) x0 = r.getRight() - tw;
    g.setColour (laminate);
    for (int i = 0; i < ga.getNumGlyphs(); ++i)
    {
        const auto& gl = ga.getGlyph (i);
        if (gl.isWhitespace() || gl.getBounds().getWidth() < h * 0.32f) continue; // narrow glyphs (I, 1, !) stay whole
        const float cx = x0 + gl.getLeft() + gl.getBounds().getWidth() * 0.5f;
        g.fillRect (cx - h * 0.025f, r.getCentreY() - h * 0.06f, h * 0.05f, h * 0.12f);
    }
}

void drawPlate (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& l1, const juce::String& l2)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (2, 3), 4.f);
    juce::ColourGradient cg (juce::Colour (0xffdcdfe3), r.getX(), r.getY(), juce::Colour (0xff8d939b), r.getRight(), r.getBottom(), false);
    g.setGradientFill (cg);
    g.fillRoundedRectangle (r, 4.f);
    juce::Random rnd (7);
    for (float y = r.getY() + 1; y < r.getBottom(); y += 1.5f)
    {
        g.setColour (juce::Colours::white.withAlpha (rnd.nextFloat() * 0.12f));
        g.drawHorizontalLine ((int) y, r.getX() + 2, r.getRight() - 2);
    }
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 4.f, 1.f);
    for (auto p : { r.getTopLeft().translated (8, 8), r.getTopRight().translated (-8, 8), r.getBottomLeft().translated (8, -8), r.getBottomRight().translated (-8, -8) })
        drawRivet (g, p.x, p.y, 3.f);
    g.setColour (juce::Colour (0xff23262a));
    g.setFont (plateFont (r.getHeight() * 0.3f));
    g.drawText (l1, r.withTrimmedBottom (r.getHeight() * 0.42f).withTrimmedTop (r.getHeight() * 0.12f), juce::Justification::centred);
    g.setFont (plateFont (r.getHeight() * 0.16f));
    g.setColour (juce::Colour (0xff3a3e44));
    g.drawText (l2, r.withTrimmedTop (r.getHeight() * 0.56f).withTrimmedBottom (r.getHeight() * 0.12f), juce::Justification::centred);
}

void drawLed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float level)
{
    level = juce::jlimit (0.f, 1.f, level);
    if (level > 0.05f)
    {
        juce::ColourGradient glow (c.withAlpha (0.55f * level), r.getCentreX(), r.getCentreY(), c.withAlpha (0.f), r.getCentreX() + r.getWidth() * 1.6f, r.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillEllipse (r.expanded (r.getWidth() * 1.4f));
    }
    g.setColour (juce::Colours::black);
    g.fillEllipse (r.expanded (1.5f));
    g.setColour (ledOff.interpolatedWith (c, level));
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (0.25f + 0.4f * level));
    g.fillEllipse (r.getX() + r.getWidth() * 0.22f, r.getY() + r.getHeight() * 0.15f, r.getWidth() * 0.35f, r.getHeight() * 0.28f);
}

void drawRecess (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setColour (juce::Colour (0xff0a0a0b));
    g.fillRoundedRectangle (r, corner);
    juce::ColourGradient sh (juce::Colours::black.withAlpha (0.8f), r.getX(), r.getY(), juce::Colours::transparentBlack, r.getX(), r.getY() + 10, false);
    g.setGradientFill (sh);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colour (0xff3a3d42));
    g.drawRoundedRectangle (r, corner, 1.2f);
}
} // namespace rc

// ===========================================================================
RoadCaseLookAndFeel::RoadCaseLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, rc::stencil);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, rc::stencil);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0d0d0e));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff4a4d52));
    setColour (juce::ComboBox::arrowColourId, rc::amber);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff141415));
    setColour (juce::PopupMenu::textColourId, rc::stencil);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, rc::amberDim);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, rc::stencil);
    setColour (juce::TextButton::textColourOffId, rc::stencil);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ToggleButton::textColourId, rc::stencil);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0b0b0c));
    setColour (juce::TextEditor::textColourId, rc::stencil);
    setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3a3d42));
    setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0b0b0c));
    setColour (juce::ScrollBar::thumbColourId, rc::aluDark);
}

void RoadCaseLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const float size = (float) std::min (w, h) - 4.f;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, r = size * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    // value arc (stage amber)
    juce::Path arcBg, arc;
    arcBg.addCentredArc (cx, cy, r, r, 0, a0, a1, true);
    arc.addCentredArc (cx, cy, r, r, 0, a0, ang, true);
    g.setColour (juce::Colour (0xff2b2216));
    g.strokePath (arcBg, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (s.isEnabled() ? rc::amber : rc::aluDark);
    g.strokePath (arc, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // tick marks
    g.setColour (rc::stencil.withAlpha (0.35f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * i / 10.f;
        const float r1 = r + 3, r2 = r + (i % 5 == 0 ? 8.f : 5.5f);
        g.drawLine (cx + r1 * std::sin (a), cy - r1 * std::cos (a), cx + r2 * std::sin (a), cy - r2 * std::cos (a), 1.f);
    }
    // knurled skirt
    const float kr = r * 0.82f;
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (cx - kr + 2, cy - kr + 4, kr * 2, kr * 2);
    juce::ColourGradient sk (juce::Colour (0xff3b3c3f), cx, cy - kr, juce::Colour (0xff0e0e0f), cx, cy + kr, false);
    g.setGradientFill (sk);
    g.fillEllipse (cx - kr, cy - kr, kr * 2, kr * 2);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (int i = 0; i < 36; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * i / 36.f + ang;
        g.drawLine (cx + kr * 0.86f * std::sin (a), cy - kr * 0.86f * std::cos (a), cx + kr * std::sin (a), cy - kr * std::cos (a), 1.2f);
    }
    // metal cap
    const float cr = kr * 0.62f;
    juce::ColourGradient cap (juce::Colour (0xffe9ecef), cx - cr * 0.4f, cy - cr * 0.6f, juce::Colour (0xff5d6168), cx + cr, cy + cr, true);
    g.setGradientFill (cap);
    g.fillEllipse (cx - cr, cy - cr, cr * 2, cr * 2);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (cx - cr, cy - cr, cr * 2, cr * 2, 1.f);
    // indicator
    juce::Path ind;
    ind.addRoundedRectangle (-1.8f, -kr * 0.98f, 3.6f, kr * 0.5f, 1.5f);
    g.setColour (s.isEnabled() ? rc::amber : rc::aluDark);
    g.fillPath (ind, juce::AffineTransform::rotation (ang).translated (cx, cy));
}

void RoadCaseLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                            juce::Slider::SliderStyle style, juce::Slider& s)
{
    const bool horiz = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    juce::Rectangle<float> track = horiz ? juce::Rectangle<float> ((float) x, y + h * 0.5f - 4.f, (float) w, 8.f)
                                         : juce::Rectangle<float> (x + w * 0.5f - 4.f, (float) y, 8.f, (float) h);
    rc::drawRecess (g, track.expanded (2.f), 4.f);
    // lit portion
    g.setColour ((s.isEnabled() ? rc::amber : rc::aluDark).withAlpha (0.7f));
    if (horiz) g.fillRoundedRectangle (track.withWidth (pos - x).reduced (1.5f), 3.f);
    else g.fillRoundedRectangle (track.withTop (pos).reduced (1.5f), 3.f);
    // scale ticks
    g.setColour (rc::stencil.withAlpha (0.25f));
    for (int i = 0; i <= 10; ++i)
    {
        if (horiz) { const float tx = x + w * i / 10.f; g.drawVerticalLine ((int) tx, track.getBottom() + 4, track.getBottom() + (i % 5 == 0 ? 11.f : 8.f)); }
        else { const float ty = y + h * i / 10.f; g.drawHorizontalLine ((int) ty, track.getRight() + 4, track.getRight() + (i % 5 == 0 ? 11.f : 8.f)); }
    }
    // fader cap
    juce::Rectangle<float> cap = horiz ? juce::Rectangle<float> (0, 0, 22, (float) std::min (h, 34)).withCentre ({ pos, y + h * 0.5f })
                                       : juce::Rectangle<float> (0, 0, (float) std::min (w, 40), 20).withCentre ({ x + w * 0.5f, pos });
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (cap.translated (2, 3), 3.f);
    juce::ColourGradient cg (juce::Colour (0xff2e2f32), cap.getX(), cap.getY(), juce::Colour (0xff0c0c0d), cap.getX(), cap.getBottom(), false);
    g.setGradientFill (cg);
    g.fillRoundedRectangle (cap, 3.f);
    g.setColour (juce::Colour (0xff55585e));
    g.drawRoundedRectangle (cap, 3.f, 1.f);
    g.setColour (s.isEnabled() ? rc::amber : rc::aluDark);
    if (horiz) g.fillRect (cap.getCentreX() - 1.f, cap.getY() + 4, 2.f, cap.getHeight() - 8);
    else g.fillRect (cap.getX() + 4, cap.getCentreY() - 1.f, cap.getWidth() - 8, 2.f);
}

void RoadCaseLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (1.5f, 2.f), 4.f);
    juce::ColourGradient cg (on ? rc::amber : juce::Colour (0xff2c2d30), r.getX(), r.getY(),
                             on ? rc::amberDim : juce::Colour (0xff111112), r.getX(), r.getBottom(), false);
    g.setGradientFill (cg);
    g.fillRoundedRectangle (down ? r.translated (0, 1) : r, 4.f);
    g.setColour (over ? rc::amber.withAlpha (0.8f) : juce::Colour (0xff4a4d52));
    g.drawRoundedRectangle (r, 4.f, 1.2f);
}

void RoadCaseLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? juce::Colours::black : rc::stencil);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2).translated (0, down ? 1 : 0), juce::Justification::centred, 1);
}

juce::Font RoadCaseLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return rc::stencilFont (std::min (17.f, h * 0.55f));
}

void RoadCaseLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float d = std::min (14.f, r.getHeight() - 6.f);
    rc::drawLed (g, { r.getX() + 4, r.getCentreY() - d * 0.5f, d, d }, rc::stageGreen, b.getToggleState() ? 1.f : 0.f);
    g.setColour (over ? rc::amber : rc::stencil);
    g.setFont (rc::stencilFont (std::min (16.f, r.getHeight() * 0.62f)));
    g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (d + 12).toNearestInt(), juce::Justification::centredLeft, 1);
}

void RoadCaseLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.f);
    rc::drawRecess (g, r, 4.f);
    juce::Path tri;
    const float ax = r.getRight() - 14, ay = r.getCentreY();
    tri.addTriangle (ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
    g.setColour (c.isEnabled() ? rc::amber : rc::aluDark);
    g.fillPath (tri);
}

juce::Font RoadCaseLookAndFeel::getComboBoxFont (juce::ComboBox& c) { return rc::stencilFont (std::min (17.f, c.getHeight() * 0.58f)); }

void RoadCaseLookAndFeel::positionComboBoxText (juce::ComboBox& c, juce::Label& l)
{
    l.setBounds (8, 1, c.getWidth() - 28, c.getHeight() - 2);
    l.setFont (getComboBoxFont (c));
}

juce::Font RoadCaseLookAndFeel::getPopupMenuFont() { return rc::stencilFont (17.f); }

void RoadCaseLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff121213));
    g.setColour (rc::amberDim);
    g.drawRect (0, 0, w, h, 1);
}

juce::Font RoadCaseLookAndFeel::getLabelFont (juce::Label& l) { return rc::stencilFont (std::min (16.f, l.getHeight() * 0.7f)); }
} // namespace hammer
