// Renders the plugin editors to PNG files (used to review the GUI without a DAW).
#include "../Source/Plugin/HammerPluginBase.h"
#include "../Source/Plugin/EarsProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static void snap (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshots");
    dir.createDirectory();
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    p->setPlayConfigDetails (0, 2, 44100.0, 512);
    p->prepareToPlay (44100.0, 512);
    // play a few bars so the display has something to show
    auto* hp = dynamic_cast<hammer::HammerPluginBase*> (p.get());
    hp->engine.setInternalPlay (true);
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    for (int i = 0; i < 400; ++i) { buf.clear(); p->processBlock (buf, midi); midi.clear(); }
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->setSize (1180, 760);
    snap (*ed, dir.getChildFile ("perform.png"));
    // other tabs
    for (auto* c : ed->getChildren())
        for (auto* b : c->getChildren())
            if (auto* tb = dynamic_cast<juce::TextButton*> (b))
            {
                if (tb->getButtonText() == "KIT & MIX") { tb->setToggleState (true, juce::dontSendNotification); tb->onClick(); snap (*ed, dir.getChildFile ("kits.png")); }
            }
    for (auto* c : ed->getChildren())
        for (auto* b : c->getChildren())
            if (auto* tb = dynamic_cast<juce::TextButton*> (b))
                if (tb->getButtonText() == "ROAD CREW") { tb->setToggleState (true, juce::dontSendNotification); tb->onClick(); snap (*ed, dir.getChildFile ("crew.png")); }
    ed.reset();
    hammer::EarsProcessor ears;
    ears.prepareToPlay (44100.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ee (ears.createEditor());
    snap (*ee, dir.getChildFile ("ears.png"));
    return 0;
}
