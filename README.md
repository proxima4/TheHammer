# THE HAMMER — intelligent session drummer
### by APT Industrial · for FL Studio (VST3, Windows)

The Hammer is a drummer that **listens to your track** and plays along like a real session player. It follows the tempo, accents, energy and sections of the song in 12 styles, with drum fills and solos, on real sampled kits or modeled classic drum machines.

```
 Jazz · Rock · Metalcore · Death Metal · Indie · Fusion Jazz · Funk · Groove · Blues · Orchestral · Minimal · Cajón
```

---

## How the drummer thinks

| Part | What it does |
|---|---|
| **Ears** | Splits your music into LOW (kick/bass), MID (snare/guitars/keys) and HIGH (hats/cymbals/picking), detects accents in each, tracks energy per bar and spots section changes. |
| **Listening brain** | A hand-written *playbook* of grooves per style (5 intensity levels each, from verse brushes to blast beats). It bends them toward what the Ears hear: kicks lock to the bass, snare and crash catch band stabs, hats follow the subdivision. Repeating bars are predicted from 1, 2, 4 and 8 bars back. |
| **Neural brain** | A 2-layer GRU network trained on four sources: the **Groove MIDI Dataset** (13.6 h of pro drummers, for human feel), bars from The Hammer's own **Playbook** (for metal, orchestral, cajón, minimal and the other styles the dataset lacks), **your own MIDI** in `training/my_midi`, and **drum tabs** in `training/my_tabs` (for note choice only). It generates one 16th at a time, conditioned on style, intensity, what the Ears hear, bar position and fill mode, and keeps a memory of what it just played. Pure C++, uses under 1 ms per bar. |
| **Conductor** | Plans phrases (fills at 4/8/16-bar ends, solo spots), blends both brains with the **BRAIN** fader, adds crashes after fills and section changes, and enforces **two hands + two feet** at human speeds. Then it humanises timing and velocity with each style's feel (laid-back snare in Groove, pushed hats in punk, tight triggers in death metal). |

### The controls you asked for
- **STYLE**: 12 drummers, each with their own grooves, fills, swing, feel and kit.
- **INTENSITY**: how much it plays. **AGGRESSION**: how hard (velocity, china, double kick, bashing).
- **FILLS / SOLOS**: how often and how long.
- **FOLLOW** (Subtle ↔ Exact): keep the style's groove, or lock to the band's every accent.
- **BRAIN** (Listen ↔ Neural): playbook-plus-ears, the neural drummer, or any mix of the two.
- **IMPROVISE**: from "repeat like a loop" to "keeps inventing".
- Also **SWING, HUMANIZE, DYNAMICS, FEEL** (auto half/double time), **SECTION** (Auto or forced) and **LOCK**.
- **DRAG MIDI**: drag the last 16 bars into FL's playlist or piano roll.

---

## Three plugins, two ways to use them in FL Studio

FL Studio doesn't let instrument plugins hear other tracks, so The Hammer comes in two setups:

### A) Drummer + Ears (recommended)
1. **The Hammer**: load it in the **Channel Rack** and route it to its own mixer track.
2. **The Hammer Ears**: insert it on a mixer track that carries your song **without the drums** (for example a "band" bus). Audio passes through untouched.
3. Set the same **LINK CHANNEL** (1–8) on both and press play. The **EARS** LED goes green.
   Extras: 6 multi-outs (Kick/Snare/Toms/Hats/Cymbals/Perc), and **MIDI OUT** to drive FPC or another drum plugin.

### B) The Hammer FX (one plugin)
Insert **The Hammer FX** on the track/bus with your song. It listens to that audio and adds drums. You can also put it on an empty track and *sidechain* the song into it (turn off *Pass input through*).
**Lookahead** (50/100/200 ms; FL compensates the latency) lets it hear the band early. With FOLLOW above 75% it then lands exactly on their hits.

### MIDI control keys
`C0` fill now · `C#0` solo (hold) · `D0` crash next · `D#0` stop (hold) · `E0` intro · `F0` verse · `F#0` chorus · `G0` bridge · `G#0` breakdown · `A0` outro · `A#0` half-time · `B0` auto.
Notes 35 and up play the kit directly (General MIDI drum map).

---

## Kits
**Built in, no samples needed:** Studio Kit (modeled acoustic; it re-tunes itself per style and uses brushes on quiet jazz), TR-808, TR-909, CR-78, Linn-style, Cajón, Orchestral. The drum machines are synthesised from circuit-style models, not sampled.

**Free kit pack**, built by GitHub Actions from freely licensed libraries:

| Kit | Source | Licence |
|---|---|---|
| DRS Kit / DRS Kit Brushes | DrumGizmo DRSKit | CC BY 4.0 |
| Muldjord Metal Kit (double kick, china) | DrumGizmo MuldjordKit | CC BY 4.0 |
| Big Rusty Drums | Karoryfer Samples | CC0 |
| VSCO Orchestral Percussion (timpani, concert bass drum, clash, gong) | Versilian VSCO 2 CE | CC0 |
| VCSL Cajón & Hand Percussion | Versilian VCSL | CC0 |

Unzip into `Documents\APT Industrial\The Hammer\Kits`. **Auto Kit** picks the right kit for each style.
**Your own samples:** add a folder with sub-folders `kick`, `snare`, `hihat_closed` … (velocity layers and round robins are detected automatically), or any `.sfz` file.

---

## Building it (GitHub does all the work)

1. Create a repository **`proxima4/TheHammer`** and upload this folder (or `git push` it).
2. **Actions → "Train the neural brain" → Run workflow.** This takes about 30–60 minutes on GitHub's servers. It downloads the Groove MIDI Dataset, trains, checks that the C++ engine matches PyTorch, and commits `model/hammer_brain.json`.
3. That commit triggers **"Build The Hammer"**. When it's green, download **TheHammer-Windows** from the run's *Artifacts*.
4. **Actions → "Build the free kit pack" → Run workflow.** It publishes `TheHammer-Kits.zip` under **Releases**.
5. Copy the three `.vst3` folders to `C:\Program Files\Common Files\VST3`. In FL Studio, open *Options → Manage plugins → Find plugins*.

Until step 2 has run, the Neural side uses a stand-in "Playbook improviser", and the UI says so.

### Teaching it your own grooves
Drop MIDI drum files into `training/my_midi/<style>/`, for example `training/my_midi/metalcore/breakdown_ideas.mid` (General MIDI drum notes, 4/4). Commit them and run **"Train the neural brain"** again. Your files count four times as much as the dataset, so the drummer picks up your habits. Good sources: your own e-kit recordings, parts you've programmed, or transcriptions of your own recordings. Avoid ripping other people's performances from videos. Name a file with "fill" in it (`fill_tomrun.mid`) and it trains the fill mode.

### Teaching it from drum tab
Ordinary text drum tab works too. Put `.txt` files in `training/my_tabs/<style>/` (cheat sheet: `training/my_tabs/README.txt`). Tabs teach the brain **which notes** to play. The **feel** (micro-timing, dynamics) is learned only from real drummers, so tab-trained grooves still sound human. Check a tab was read correctly with `python training/tab_reader.py <file> --midi check.mid`. The Funky Drummer break is included as the first example.

Local build: `cmake -B build && cmake --build build --config Release`. Engine self-test: `HammerRender --selftest`. Demo renders: `HammerRender --render demos`.

## Project layout
```
Source/Ears      onset analyser, listening memory, shared-memory link
Source/Brain     style playbook, listening + neural brains, fills/solos, conductor
Source/Sound     modeled voices (808/909/CR-78/Linn/acoustic/cajón/orchestral), sample kits, mixer
Source/Engine    timing, scheduling, MIDI in/out, MIDI recorder
Source/Plugin    The Hammer / The Hammer FX / The Hammer Ears processors
Source/GUI       road-case look & feel and editors
training/        neural trainer (PyTorch)      kits/  free kit pack builder
Tools/           HammerRender (self-test + demos), HammerSnapshot (GUI screenshots)
```

The Hammer is a non-commercial APT Industrial project. See CREDITS.md.
