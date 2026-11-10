# The Hammer — version history

Every change to The Hammer is listed here and inside the plugin (Road Crew page).

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
