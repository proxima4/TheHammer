#include "HammerEditor.h"
#include "../Brain/StyleLibrary.h"

namespace hammer
{
using APVTS = juce::AudioProcessorValueTreeState;

// ============================================================================
//  PERFORM page
// ============================================================================
class PerformPage : public juce::Component
{
public:
    PerformPage (HammerPluginBase& p)
        : proc (p),
          styles (*p.apvts.getParameter (pid::style)),
          groove (p.engine), ears (p.engine, p.isMixerMode()), drag (p.engine),
          follow (p.apvts, pid::follow, "FOLLOW", "SUBTLE", "EXACT", 3),
          brain (p.apvts, pid::brain, "BRAIN", "LISTEN", "NEURAL", 4),
          feel (p.apvts, pid::feel, "FEEL", 5), section (p.apvts, pid::section, "SECTION", 6),
          phrase (p.apvts, pid::phrase, "PHRASE", 7), kit (p.apvts, pid::kit, "KIT", 8),
          fill ("FILL"), solo ("SOLO"), crash ("CRASH"), stop ("STOP"), lock ("LOCK")
    {
        addAndMakeVisible (styles);
        addAndMakeVisible (groove);
        addAndMakeVisible (ears);
        addAndMakeVisible (drag);
        const struct { const char* id; const char* label; } knobs[] = {
            { pid::intensity, "INTENSITY" }, { pid::aggression, "AGGRESSION" }, { pid::fills, "FILLS" },
            { pid::solos, "SOLOS" }, { pid::improvise, "IMPROVISE" }, { pid::swing, "SWING" }, { pid::humanize, "HUMANIZE" } };
        int seed = 20;
        for (auto& k : knobs) { auto* t = knobList.add (new TapeKnob (p.apvts, k.id, k.label, seed++)); addAndMakeVisible (t); }
        addAndMakeVisible (follow); addAndMakeVisible (brain);
        for (auto* c : { &feel, &section, &phrase, &kit }) addAndMakeVisible (*c);
        autoKit.setButtonText ("AUTO KIT");
        addAndMakeVisible (autoKit);
        autoKitAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, pid::autoKit, autoKit);

        fill.onPress = [this] { proc.engine.requestFill(); };
        crash.onPress = [this] { proc.engine.requestCrash(); };
        solo.onPress = [this] { proc.engine.setSoloHeld (true); };
        solo.onRelease = [this] { proc.engine.setSoloHeld (false); };
        stop.onPress = [this] { proc.engine.setStopHeld (true); };
        stop.onRelease = [this] { proc.engine.setStopHeld (false); };
        lock.setClickingTogglesState (true);
        lockAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, pid::lock, lock);
        for (auto* b : { (juce::Button*) &fill, (juce::Button*) &solo, (juce::Button*) &crash, (juce::Button*) &stop, (juce::Button*) &lock })
            addAndMakeVisible (b);
        fill.setTooltip ("Play a fill now (MIDI key C0 / note 24)");
        solo.setTooltip ("Hold for a drum solo (note 25)");
        crash.setTooltip ("Crash on the next downbeat (note 26)");
        stop.setTooltip ("Hold to stop the band - one stop hit, then silence (note 27)");
        lock.setTooltip ("Lock the current groove - keeps repeating it (fills still happen)");
        follow.slider.setTooltip ("How strongly the drummer locks to what it hears: SUBTLE keeps the style's groove, EXACT plays with the band's accents");
        brain.slider.setTooltip ("Blend the two brains: LISTEN = style playbook bent by the Ears, NEURAL = the network trained on real drummers");
    }

    void resized() override
    {
        styles.setBounds (30, 102, 1120, 100);
        groove.setBounds (30, 210, 820, 252);
        ears.setBounds (862, 210, 288, 252);
        for (int i = 0; i < knobList.size(); ++i) knobList[i]->setBounds (30 + i * 128, 470, 120, 122);
        drag.setBounds (940, 482, 210, 96);
        follow.setBounds (30, 600, 545, 56);
        brain.setBounds (605, 600, 545, 56);
        feel.setBounds (30, 664, 120, 60);
        section.setBounds (160, 664, 150, 60);
        phrase.setBounds (320, 664, 110, 60);
        kit.setBounds (440, 664, 240, 60);
        autoKit.setBounds (690, 692, 110, 28);
        int x = 810;
        for (auto* b : { (juce::Component*) &fill, (juce::Component*) &solo, (juce::Component*) &crash, (juce::Component*) &stop, (juce::Component*) &lock })
        { b->setBounds (x, 688, 62, 36); x += 68; }
    }

private:
    HammerPluginBase& proc;
    StyleSelector styles;
    GrooveDisplay groove;
    EarsPanel ears;
    DragMidiBox drag;
    juce::OwnedArray<TapeKnob> knobList;
    CrossFader follow, brain;
    TapeChoice feel, section, phrase, kit;
    juce::ToggleButton autoKit;
    std::unique_ptr<APVTS::ButtonAttachment> autoKitAtt, lockAtt;
    HoldButton fill, solo, crash, stop;
    juce::TextButton lock;
};

// ============================================================================
//  KIT & MIX page
// ============================================================================
class KitPage : public juce::Component, private juce::ListBoxModel
{
public:
    KitPage (HammerPluginBase& p) : proc (p), kitChoice (p.apvts, pid::kit, "KIT", 30)
    {
        for (int s = 0; s < KitSlots::kNumSlots; ++s)
        {
            auto* lb = loadButtons.add (new juce::TextButton ("LOAD"));
            lb->onClick = [this, s] { browse (s); };
            addAndMakeVisible (lb);
            auto* cb = clearButtons.add (new juce::TextButton ("X"));
            cb->onClick = [this, s] { proc.clearKitSlot (s); };
            addAndMakeVisible (cb);
            auto* sel = slotSelect.add (new juce::ToggleButton ("SLOT " + juce::String (s + 1)));
            sel->setRadioGroupId (77);
            sel->onClick = [this, s] { targetSlot = s; };
            addAndMakeVisible (sel);
        }
        slotSelect[0]->setToggleState (true, juce::dontSendNotification);
        openFolder.onClick = [] { auto f = SampleKitLoader::defaultKitsFolder(); f.createDirectory(); f.startAsProcess(); };
        rescan.onClick = [this] { refreshList(); };
        getKits.onClick = [] { juce::URL ("https://github.com/proxima4/TheHammer/releases").launchInDefaultBrowser(); };
        for (auto* b : { &openFolder, &rescan, &getKits }) addAndMakeVisible (*b);
        list.setModel (this);
        list.setRowHeight (24);
        addAndMakeVisible (list);
        addAndMakeVisible (kitChoice);
        autoKit.setButtonText ("AUTO KIT FOR STYLE");
        addAndMakeVisible (autoKit);
        autoAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, pid::autoKit, autoKit);

        // mixer faders
        for (int g = 0; g < NumGroups; ++g) addFader (pid::groupVol (g), juce::String (groupName (g)).toUpperCase());
        addFader (pid::master, "MASTER");
        const struct { const char* id; const char* label; } fx[] = {
            { pid::room, "ROOM" }, { pid::roomSize, "SIZE" }, { pid::drive, "DRIVE" }, { pid::glue, "GLUE" },
            { pid::tune, "TUNE" }, { pid::velCurve, "VEL CURVE" }, { pid::dynamics, "DYNAMICS" }, { pid::earsSens, "EARS SENS" } };
        int seed = 40;
        for (auto& k : fx) { auto* t = knobs.add (new TapeKnob (p.apvts, k.id, k.label, seed++)); addAndMakeVisible (t); }

        drummerOn.setButtonText ("DRUMMER ON");
        addAndMakeVisible (drummerOn);
        toggleAtts.add (new APVTS::ButtonAttachment (p.apvts, pid::drummerOn, drummerOn));
        if (! p.isMixerMode())
        {
            midiOut.setButtonText ("MIDI OUT (GM)");
            addAndMakeVisible (midiOut);
            toggleAtts.add (new APVTS::ButtonAttachment (p.apvts, pid::midiOut, midiOut));
            link.setSliderStyle (juce::Slider::IncDecButtons);
            link.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 26);
            addAndMakeVisible (link);
            linkAtt = std::make_unique<APVTS::SliderAttachment> (p.apvts, pid::link, link);
        }
        else
        {
            look = std::make_unique<TapeChoice> (p.apvts, pid::lookahead, "LOOKAHEAD", 60);
            addAndMakeVisible (*look);
            passThru.setButtonText ("PASS INPUT THROUGH");
            addAndMakeVisible (passThru);
            toggleAtts.add (new APVTS::ButtonAttachment (p.apvts, pid::passThrough, passThru));
        }
        refreshList();
    }

    void paint (juce::Graphics& g) override
    {
        rc::drawRecess (g, { 30, 104, 560, 620 }, 8.f);
        rc::drawRecess (g, { 610, 104, 540, 620 }, 8.f);
        rc::drawStencilText (g, "KIT SLOTS", { 46, 112, 300, 30 }, 24.f, rc::stencil);
        rc::drawStencilText (g, "MIXER", { 626, 112, 300, 30 }, 24.f, rc::stencil);
        g.setFont (rc::stencilFont (14.f));
        for (int s = 0; s < KitSlots::kNumSlots; ++s)
        {
            const int y = 150 + s * 54;
            g.setColour (proc.engine.kits.isLoading (s) ? rc::amber : rc::stencil.withAlpha (0.85f));
            g.drawFittedText (proc.engine.kits.statusOf (s), 150, y, 300, 34, juce::Justification::centredLeft, 2);
        }
        g.setColour (rc::stencil.withAlpha (0.6f));
        g.setFont (rc::stencilFont (13.f));
        g.drawText ("FOUND IN " + SampleKitLoader::defaultKitsFolder().getFullPathName().toUpperCase(), 46, 420, 530, 18, juce::Justification::centredLeft);
        g.drawText ("DOUBLE-CLICK A KIT TO LOAD IT INTO THE SELECTED SLOT", 46, 604, 530, 18, juce::Justification::centredLeft);
        g.setColour (rc::amber);
        g.drawText ("NOW PLAYING: " + proc.engine.kitDisplayName().toUpperCase(), 46, 624, 530, 18, juce::Justification::centredLeft);
        if (! proc.isMixerMode())
        {
            g.setColour (rc::stencil);
            g.setFont (rc::stencilFont (14.f));
            g.drawText ("EARS LINK CH", 626, 668, 120, 26, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        for (int s = 0; s < KitSlots::kNumSlots; ++s)
        {
            const int y = 150 + s * 54;
            slotSelect[s]->setBounds (46, y + 4, 100, 26);
            loadButtons[s]->setBounds (460, y + 2, 70, 30);
            clearButtons[s]->setBounds (536, y + 2, 34, 30);
        }
        openFolder.setBounds (46, 380, 160, 32);
        rescan.setBounds (214, 380, 110, 32);
        getKits.setBounds (332, 380, 238, 32);
        list.setBounds (46, 442, 524, 156);
        kitChoice.setBounds (46, 648, 260, 62);
        autoKit.setBounds (322, 684, 240, 26);
        for (int i = 0; i < faders.size(); ++i) faders[i]->setBounds (630 + i * 72, 150, 66, 250);
        for (int i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (626 + (i % 4) * 128, 410 + (i / 4) * 124, 120, 120);
        drummerOn.setBounds (980, 668, 160, 26);
        if (! proc.isMixerMode()) { link.setBounds (740, 668, 110, 26); midiOut.setBounds (980, 694, 160, 26); }
        else { look->setBounds (626, 660, 150, 56); passThru.setBounds (790, 690, 190, 26); }
    }

    void refreshStatus() { repaint(); }

private:
    void addFader (const char* idv, const juce::String& name)
    {
        auto* f = faders.add (new FaderStrip (proc.apvts, idv, name, 50 + faders.size()));
        addAndMakeVisible (f);
    }

    struct FaderStrip : public juce::Component
    {
        FaderStrip (APVTS& s, const char* idv, const juce::String& n, int sd) : name (n), seed (sd)
        {
            slider.setSliderStyle (juce::Slider::LinearVertical);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
            addAndMakeVisible (slider);
            att = std::make_unique<APVTS::SliderAttachment> (s, idv, slider);
        }
        void paint (juce::Graphics& g) override { rc::drawTape (g, { 0.f, 0.f, (float) getWidth(), 20.f }, name, ((seed * 5) % 5 - 2) * 0.7f, false, 11.f, seed); }
        void resized() override { slider.setBounds (getLocalBounds().withTrimmedTop (24)); }
        juce::Slider slider; juce::String name; int seed;
        std::unique_ptr<APVTS::SliderAttachment> att;
    };

    void browse (int slot)
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a kit folder or .sfz file", SampleKitLoader::defaultKitsFolder(), "*.sfz");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                              [this, slot] (const juce::FileChooser& fc) {
                                  const auto f = fc.getResult();
                                  if (f.exists()) proc.loadKitIntoSlot (slot, f);
                              });
    }

    void refreshList()
    {
        found = SampleKitLoader::scan (SampleKitLoader::defaultKitsFolder());
        list.updateContent();
        list.repaint();
    }

    int getNumRows() override { return (int) found.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) found.size()) return;
        if (selected) g.fillAll (rc::amberDim);
        const auto& k = found[(size_t) row];
        g.setColour (rc::stencil);
        g.setFont (rc::stencilFont (15.f));
        g.drawText (k.name.toUpperCase(), 8, 0, w / 2, h, juce::Justification::centredLeft);
        g.setColour (rc::stencil.withAlpha (0.55f));
        g.setFont (rc::stencilFont (12.f));
        g.drawText ((k.styles.joinIntoString (" / ") + (k.license.isNotEmpty() ? "   " + k.license : juce::String())).toUpperCase(),
                    w / 2, 0, w / 2 - 8, h, juce::Justification::centredRight);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < (int) found.size()) proc.loadKitIntoSlot (targetSlot, juce::File (found[(size_t) row].path));
    }

    HammerPluginBase& proc;
    juce::OwnedArray<juce::TextButton> loadButtons, clearButtons;
    juce::OwnedArray<juce::ToggleButton> slotSelect;
    juce::TextButton openFolder { "OPEN KITS FOLDER" }, rescan { "RESCAN" }, getKits { "GET THE FREE KIT PACK" };
    juce::ListBox list;
    std::vector<KitInfo> found;
    int targetSlot = 0;
    TapeChoice kitChoice;
    juce::ToggleButton autoKit, drummerOn, midiOut, passThru;
    std::unique_ptr<APVTS::ButtonAttachment> autoAtt;
    juce::OwnedArray<APVTS::ButtonAttachment> toggleAtts;
    juce::OwnedArray<FaderStrip> faders;
    juce::OwnedArray<TapeKnob> knobs;
    juce::Slider link;
    std::unique_ptr<APVTS::SliderAttachment> linkAtt;
    std::unique_ptr<TapeChoice> look;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ============================================================================
//  ROAD CREW page (help + credits)
// ============================================================================
class CrewPage : public juce::Component
{
public:
    CrewPage (HammerPluginBase& p) : proc (p)
    {
        text.setMultiLine (true);
        text.setReadOnly (true);
        text.setScrollbarsShown (true);
        text.setFont (juce::Font (juce::FontOptions ("Consolas", 15.f, juce::Font::plain)));
        text.setText (helpText());
        addAndMakeVisible (text);
    }
    void resized() override { text.setBounds (30, 104, 1120, 620); }
    void paint (juce::Graphics&) override {}

private:
    juce::String helpText() const
    {
        juce::String s;
        s << "THE HAMMER  v" << HAMMER_VERSION_STRING << "  -  intelligent session drummer by APT Industrial\n"
          << proc.engine.neural.status() << "\n\n";
        if (proc.isMixerMode())
            s << "YOU ARE USING: THE HAMMER FX (mixer insert)\n"
              << "  * Put it on the mixer track/bus that carries your song (NOT the master if other FX follow).\n"
              << "    It listens to that audio and ADDS drums to it.  Or: put it on an empty mixer track and\n"
              << "    route your song into it as a SIDECHAIN (right-click the arrow at the bottom of the source\n"
              << "    track > 'Sidechain to this track'). Turn PASS INPUT THROUGH off if you only want drums.\n"
              << "  * LOOKAHEAD (Kit & Mix page): the drummer hears the band up to 200 ms early (FL compensates\n"
              << "    the latency). With FOLLOW above 75% it then lands EXACTLY on the band's hits.\n\n";
        else
            s << "YOU ARE USING: THE HAMMER (Channel Rack instrument)\n"
              << "  1. Load 'The Hammer' as a generator in the Channel Rack and route it to its own mixer track.\n"
              << "  2. Put 'The Hammer Ears' on a mixer track that carries your song WITHOUT the drums\n"
              << "     (e.g. a 'Band' bus, or the master if the drums aren't on it). Same LINK CHANNEL on both.\n"
              << "  3. Press play. The EARS LED turns green when the drummer can hear the band.\n"
              << "  * Multi-outs: enable outputs 2-7 in FL's wrapper (Kick/Snare/Toms/Hats/Cymbals/Perc).\n"
              << "  * MIDI OUT: sends GM drum notes so you can drive FPC or another drum plugin.\n\n";
        s << "CONTROLS\n"
          << "  STYLE        12 drummers: Jazz, Rock, Metalcore, Death Metal, Indie, Fusion Jazz, Funk,\n"
          << "               Groove, Blues, Orchestral, Minimal, Cajon.\n"
          << "  INTENSITY    how much the drummer plays (verse brushes ... chorus crash-ride / blasts).\n"
          << "  AGGRESSION   how hard: velocity, china instead of crash, double kick, bashing.\n"
          << "  FILLS/SOLOS  how often and how long. Fills land on phrase ends (PHRASE = 4/8/16 bars).\n"
          << "  IMPROVISE    0 = repeats like a loop, 100 = keeps inventing.\n"
          << "  FOLLOW       SUBTLE = keeps the style's groove ... EXACT = kicks/stabs lock to the band.\n"
          << "  BRAIN        LISTEN = style playbook bent by the Ears ... NEURAL = network trained on\n"
          << "               13.6 hours of real drummers. Anything in between blends both.\n"
          << "  SECTION      Auto (follows the song's energy) or force Intro/Verse/Chorus/Breakdown/...\n"
          << "  FEEL         Auto picks half/double time from the tempo. Breakdown forces half-time.\n"
          << "  LOCK         freezes the current groove.   DRAG MIDI: drag the last 16 bars into FL.\n\n"
          << "MIDI CONTROL KEYS (piano roll / keyboard)\n"
          << "  C0 (24) Fill now   C#0 (25) Solo (hold)   D0 (26) Crash next   D#0 (27) Stop (hold)\n"
          << "  E0 Intro  F0 Verse  F#0 Chorus  G0 Bridge  G#0 Breakdown  A0 Outro  A#0 Half-time  B0 Auto\n"
          << "  Notes 35-82 play the kit directly (General MIDI drum map) - program your own parts too.\n\n"
          << "KITS\n"
          << "  Built in (modeled, no samples needed): Studio Kit (tunes itself per style, brushes on quiet\n"
          << "  jazz), TR-808, TR-909, CR-78, Linn-style, Cajon, Orchestral.\n"
          << "  Sample kits: drop a kit folder or .sfz into  Documents/APT Industrial/The Hammer/Kits\n"
          << "  (one sub-folder per drum: kick, snare, hihat_closed ... or any SFZ). The free kit pack\n"
          << "  from the GitHub releases page installs real acoustic, metal, orchestral and cajon kits.\n\n"
          << "CREDITS\n"
          << "  Groove MIDI Dataset - Google Magenta, CC BY 4.0 (Gillick et al., 'Learning to Groove with\n"
          << "    Inverse Sequence Transformations', ICML 2019).\n"
          << "  Free kit pack: DrumGizmo DRSKit & MuldjordKit (CC BY 4.0, DrumGizmo team / DRS Drums / Lars\n"
          << "    Muldjord), Big Rusty Drums (Karoryfer Samples, CC0), VSCO 2 Community Edition & VCSL\n"
          << "    (Versilian Studios, CC0).\n"
          << "  Built with JUCE.  The Hammer is a non-commercial APT Industrial project.\n";
        return s;
    }
    HammerPluginBase& proc;
    juce::TextEditor text;
};

// ============================================================================
//  Editor shell: road case, header, tabs, scaling
// ============================================================================
class HammerContent : public juce::Component
{
public:
    HammerContent (HammerPluginBase& p) : proc (p), perform (p), kits (p), crew (p)
    {
        addAndMakeVisible (perform);
        addChildComponent (kits);
        addChildComponent (crew);
        const char* names[3] = { "PERFORM", "KIT & MIX", "ROAD CREW" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = tabs.add (new juce::TextButton (names[i]));
            b->setRadioGroupId (11);
            b->setClickingTogglesState (true);
            b->onClick = [this, i] { show (i); };
            addAndMakeVisible (b);
        }
        tabs[0]->setToggleState (true, juce::dontSendNotification);
        if (juce::JUCEApplicationBase::isStandaloneApp())
        {
            play.setClickingTogglesState (true);
            play.onClick = [this] { proc.engine.setInternalPlay (play.getToggleState()); };
            addAndMakeVisible (play);
        }
    }

    void show (int i)
    {
        perform.setVisible (i == 0);
        kits.setVisible (i == 1);
        crew.setVisible (i == 2);
    }

    void paint (juce::Graphics& g) override
    {
        rc::drawCase (g, getLocalBounds().toFloat(), 22.f);
        rc::drawStencilText (g, "THE HAMMER", { 40, 26, 420, 56 }, 54.f, rc::stencil);
        g.setFont (rc::stencilFont (14.f));
        g.setColour (rc::amber);
        g.drawText (proc.isMixerMode() ? "INTELLIGENT SESSION DRUMMER  -  MIXER FX" : "INTELLIGENT SESSION DRUMMER",
                    44, 78, 400, 18, juce::Justification::centredLeft);
        // decals
        g.setColour (rc::markerRed.withAlpha (0.55f));
        g.setFont (rc::stencilFont (12.f));
        g.drawText ("FRAGILE  //  THIS WAY UP  //  ROAD CREW ONLY", 470, 82, 330, 14, juce::Justification::centredLeft);
        rc::drawPlate (g, { 930.f, 30.f, 220.f, 60.f }, "APT INDUSTRIAL", "BUILT FOR THE ROAD");
        // status LEDs
        const bool ears = proc.isMixerMode() || proc.engine.linkConnected();
        const bool model = proc.engine.neural.hasModel();
        auto led = [&] (float x, const char* t, juce::Colour c, float on) {
            rc::drawLed (g, { x, 40.f, 11.f, 11.f }, c, on);
            g.setColour (rc::stencil.withAlpha (0.75f)); g.setFont (rc::stencilFont (11.f));
            g.drawText (t, (int) x - 20, 54, 52, 14, juce::Justification::centred);
        };
        led (826.f, "EARS", ears ? rc::stageGreen : rc::stageRed, 1.f);
        led (870.f, "NEURAL", model ? rc::stageGreen : rc::amber, 1.f);
    }

    void resized() override
    {
        for (int i = 0; i < tabs.size(); ++i) tabs[i]->setBounds (470 + i * 112, 40, 106, 34);
        if (play.isVisible()) play.setBounds (370, 84, 70, 18);
        for (auto* c : { (juce::Component*) &perform, (juce::Component*) &kits, (juce::Component*) &crew })
            c->setBounds (getLocalBounds());
    }

    void tick() { if (kits.isVisible()) kits.refreshStatus(); repaint (800, 30, 120, 50); proc.engine.kits.freeRetired(); }

private:
    HammerPluginBase& proc;
    PerformPage perform;
    KitPage kits;
    CrewPage crew;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::TextButton play { "PLAY" };
};

// ============================================================================
HammerEditor::HammerEditor (HammerPluginBase& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<HammerContent> (p);
    content->setBounds (0, 0, kW, kH);
    addAndMakeVisible (*content);
    constrainer.setFixedAspectRatio ((double) kW / kH);
    constrainer.setSizeLimits (kW / 2, kH / 2, kW * 2, kH * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (kW, kH);
    startTimerHz (4);
}

HammerEditor::~HammerEditor()
{
    setLookAndFeel (nullptr);
}

void HammerEditor::paint (juce::Graphics& g) { g.fillAll (rc::laminate); }

void HammerEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    content->setTransform (juce::AffineTransform::scale (s));
}

void HammerEditor::timerCallback()
{
    static_cast<HammerContent*> (content.get())->tick();
}

void HammerEditor::showPage (int p) { static_cast<HammerContent*> (content.get())->show (p); }
} // namespace hammer
