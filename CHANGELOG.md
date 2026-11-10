# The Hammer — version history

Every change to The Hammer is listed here and inside the plugin (Road Crew page).

## 0.1.2 — 2026-11-10
- **The neural brain can learn from drum tab.** Put text tabs in `training/my_tabs/<style>/`. Tabs teach which notes to play; timing feel and dynamics are learned only from real drummers, so the result stays human.
- New tab reader (`training/tab_reader.py`) understands standard drum tab: crash, hi-hat, ride, china, splash, toms, snare, kick, cowbell, tambourine and shaker lines; accents, ghost notes, open hats, flams, drags, buzz rolls, cross-stick, ride bell; 8ths, 16ths, triplets and 32nds; "x4" repeats. It can write a MIDI file to check what it read.
- First tab in the library: the Funky Drummer break (Clyde Stubblefield), funk.
- Training also leans less on the Playbook for feel: Playbook bars now teach mostly note choice.

## 0.1.1 — 2026-11-10
- **Stop means stop:** pressing Stop in FL Studio while a track is playing now silences everything within 50 ms: drum voices, cymbal tails, room reverb, and any MIDI notes sent out. Loading a project also silences the old one. Playing the kit live while FL is stopped still rings out naturally (handy for demoing).
- Windows build fixes: uses whatever Visual Studio GitHub's machines have installed; the self-test tool no longer runs out of stack memory on Windows.
- Version history added, on GitHub (this file) and inside the plugin.
- Training the neural brain now starts the plugin build automatically when it finishes.

## 0.1.0 — 2026-11-09
- First build: The Hammer (drummer instrument), The Hammer FX (mixer insert) and The Hammer Ears (listener).
- 12 styles, Listen/Neural brain blend, Intensity, Aggression, Fills, Solos, Follow, Improvise, Swing, Humanize, Feel, Section and Lock.
- Modeled kits (Studio, TR-808, TR-909, CR-78, Linn-style, Cajón, Orchestral), sample kit loader (kit folders and SFZ), road-case GUI, Drag MIDI.
- GitHub Actions: Windows build, neural brain training, free kit pack builder.
