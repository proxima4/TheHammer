#include "StyleLibrary.h"
#include <cmath>

namespace hammer
{
const char* kitName (int k)
{
    static const char* n[NumKits] = {
        "Studio Kit (Modeled)", "TR-808 (Modeled)", "TR-909 (Modeled)", "CR-78 (Modeled)", "Linn-Style (Modeled)",
        "Cajon (Modeled)", "Orchestral (Modeled)",
        "Sample Kit 1", "Sample Kit 2", "Sample Kit 3", "Sample Kit 4"
    };
    return (k >= 0 && k < NumKits) ? n[k] : "?";
}

// Groove MIDI Dataset primary styles (conditioning for the neural brain)
const char* gmdStyleName (int i)
{
    static const char* n[kNumGmdStyles] = {
        "rock", "funk", "jazz", "latin", "hiphop", "soul", "pop", "afrobeat", "reggae",
        "blues", "country", "punk", "neworleans", "gospel", "dance", "middleeastern", "afrocuban", "highlife"
    };
    return (i >= 0 && i < kNumGmdStyles) ? n[i] : "?";
}
//                       rock funk jazz latin hiph soul pop  afro regg blue cntr punk nola gosp danc mide afcu high

bool StyleLibrary::decodeStep (char c, float& p, float& v)
{
    switch (c)
    {
        case 'X': p = 1.f;  v = 1.0f;  return true;
        case 'x': p = 1.f;  v = 0.82f; return true;
        case 'o': p = 1.f;  v = 0.62f; return true;
        case 'f': p = 1.f;  v = 0.45f; return true;
        case 'g': p = 1.f;  v = 0.28f; return true;
        case '?': p = 0.4f; v = 0.62f; return true;
        case 'h': p = 0.5f; v = 0.28f; return true;
        default: return false;
    }
}

int StyleLibrary::grooveForIntensity (const StyleDef& s, float in)
{
    int best = 0; float bestD = 10.f;
    for (int i = 0; i < s.numGrooves; ++i)
    {
        const float d = std::abs (s.grooves[i].intensity - in);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// Helper to make HumanFeel with a few lane biases
static HumanFeel feel (float jitter, float vj, float kick, float snare, float hats, float ride, float accent = 0.25f)
{
    HumanFeel f;
    f.jitterMs = jitter; f.velJitter = vj; f.accentHats = accent;
    f.laneBiasMs[Kick] = kick;
    f.laneBiasMs[Snare] = snare; f.laneBiasMs[Rim] = snare; f.laneBiasMs[Clap] = snare;
    f.laneBiasMs[HatClosed] = hats; f.laneBiasMs[HatOpen] = hats; f.laneBiasMs[HatPedal] = hats;
    f.laneBiasMs[Ride] = ride; f.laneBiasMs[RideBell] = ride;
    return f;
}

static Ornaments orn (float ghost, float pickup, float openHat, float subdiv, float comp, float bombs,
                      float bell, float splash, float dk, float china)
{
    Ornaments o;
    o.ghostSnare = ghost; o.kickPickup = pickup; o.openHatEnd = openHat; o.hatSubdiv = subdiv;
    o.comping = comp; o.bombs = bombs; o.rideBell = bell; o.splash = splash; o.doubleKick = dk;
    o.chinaWithAggression = china;
    return o;
}

static const StyleDef kStyles[NumStyles] =
{
    // ------------------------------------------------------------------ JAZZ
    { "Jazz", true, 1.0f, 0.f, KitStudio, "jazz",
      {
        { "Brushes / two-feel", 0.15f, 12, { { Ride, "x..o..x..o.." }, { HatPedal, "...x.....x.." }, { Kick, "g.....g....." }, { Snare, "........h..." } } },
        { "Medium swing", 0.35f, 12, { { Ride, "x..X.ox..X.o" }, { HatPedal, "...x.....x.." }, { Kick, "g..g..g..g.." }, { Snare, ".....h.....h" } } },
        { "Up-tempo swing", 0.6f, 12, { { Ride, "x..X.ox..X.o" }, { HatPedal, "...x.....x.." }, { Kick, "g..g..g..g.." }, { Snare, "..h..h.h..h." } } },
        { "Hard bop", 0.8f, 12, { { Ride, "X..X.oX..X.o" }, { HatPedal, "...x.....x.." }, { Kick, "?....h?....." }, { Snare, "..h.hh..h.h." } } },
        { "Burning", 0.95f, 12, { { Ride, "X..X.oX..X.o" }, { Crash2, "x..........." }, { HatPedal, "...x.....x.." }, { Kick, "h....?h..h.." }, { Snare, "..hhh...hh.h" } } },
      }, 5,
      { 0.1f, 0.f, 0.f, 0.4f, 0.f, 0.5f, 0.f, 0.f, 0.f, 0.f },
      feel (6.f, 0.08f, 2.f, 4.f, 0.f, -3.f, 0.35f),
      orn (0.f, 0.f, 0.f, 0.f, 0.8f, 0.5f, 0.15f, 0.05f, 0.f, 0.f),
      55.f, 320.f,
      { 0, 0, 1.f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
      0.35f, 0.7f },

    // ------------------------------------------------------------------ ROCK
    { "Rock", false, 0.f, 0.f, KitStudio, "rock",
      {
        { "Verse side-stick", 0.2f, 16, { { Kick, "x.......x.x....." }, { Rim, "....x.......x..." }, { HatClosed, "x.o.x.o.x.o.x.o." } } },
        { "Basic 8ths", 0.4f, 16, { { Kick, "x.......x.x....." }, { Snare, "....X.......X..." }, { HatClosed, "x.o.x.o.x.o.x.o." } } },
        { "Driving", 0.6f, 16, { { Kick, "x.....x.x.x....." }, { Snare, "....X.......X..." }, { HatClosed, "x.x.x.x.x.x.x.x." }, { HatOpen, "..............?." } } },
        { "Chorus ride", 0.78f, 16, { { Kick, "x.....x.x.x....." }, { Snare, "....X.......X..." }, { Ride, "x.o.x.o.x.o.x.o." }, { RideBell, "x...x...x...x..." } } },
        { "Crash-ride anthem", 0.93f, 16, { { Kick, "x.x...x.x.x...x." }, { Snare, "....X.......X..." }, { Crash, "x...x...x...x..." } } },
      }, 5,
      { 0.5f, 0.3f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.2f },
      feel (4.f, 0.06f, 0.f, 3.f, -2.f, -1.f),
      orn (0.15f, 0.35f, 0.3f, 0.3f, 0.f, 0.f, 0.f, 0.1f, 0.f, 0.f),
      70.f, 185.f,
      { 0.8f, 0, 0, 0, 0, 0, 0.2f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
      0.8f, 0.85f },

    // ------------------------------------------------------------- METALCORE
    { "Metalcore", false, 0.f, 0.f, KitStudio, "metal",
      {
        { "Clean verse", 0.2f, 16, { { Kick, "x.....x...x....." }, { Snare, "....x.......x..." }, { HatClosed, "x.x.x.x.x.x.x.x." } } },
        { "Driving 8ths", 0.42f, 16, { { Kick, "x.x.x.x.x.x.x.x." }, { Snare, "....X.......X..." }, { Ride, "x.x.x.x.x.x.x.x." } } },
        { "Breakdown", 0.62f, 16, { { Kick, "x..x..x.x..x...." }, { Snare, "........X......." }, { China, "x...x...x...x..." } }, true },
        { "Double-kick chorus", 0.8f, 16, { { Kick, "xxxxxxxxxxxxxxxx" }, { Snare, "....X.......X..." }, { Crash, "x...x...x...x..." } } },
        { "Blast", 0.96f, 16, { { Kick, "x.x.x.x.x.x.x.x." }, { Snare, ".x.x.x.x.x.x.x.x" }, { Crash, "x.x.x.x.x.x.x.x." } } },
      }, 5,
      { 0.35f, 0.2f, 0.f, 0.f, 0.2f, 0.f, 0.f, 0.f, 0.f, 0.25f },
      feel (2.5f, 0.04f, 0.f, 0.f, -1.f, -1.f, 0.15f),
      orn (0.f, 0.3f, 0.1f, 0.2f, 0.f, 0.f, 0.3f, 0.3f, 0.7f, 0.6f),
      70.f, 240.f,
      { 0.5f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0, 0 },
      0.95f, 0.95f },

    // ----------------------------------------------------------- DEATH METAL
    { "Death Metal", false, 0.f, 0.f, KitStudio, "metal",
      {
        { "Doom half-time", 0.18f, 16, { { Kick, "x.......x.x....." }, { Snare, "........X......." }, { Ride, "x...x...x...x..." } }, true },
        { "Skank beat", 0.4f, 16, { { Kick, "x...x...x...x..." }, { Snare, "..X...X...X...X." }, { Crash, "x.x.x.x.x.x.x.x." } } },
        { "Double-kick groove", 0.6f, 16, { { Kick, "xxxxxxxxxxxxxxxx" }, { Snare, "....X.......X..." }, { Ride, "x.x.x.x.x.x.x.x." } } },
        { "Traditional blast", 0.8f, 16, { { Kick, "x.x.x.x.x.x.x.x." }, { Snare, ".x.x.x.x.x.x.x.x" }, { Ride, "x.x.x.x.x.x.x.x." } } },
        { "Hammer blast", 0.97f, 16, { { Kick, "xxxxxxxxxxxxxxxx" }, { Snare, "xoxoxoxoxoxoxoxo" }, { Crash, "x.x.x.x.x.x.x.x." } } },
      }, 5,
      { 0.35f, 0.f, 0.f, 0.f, 0.45f, 0.f, 0.f, 0.f, 0.f, 0.2f },
      feel (2.f, 0.03f, 0.f, 0.f, 0.f, 0.f, 0.1f),
      orn (0.f, 0.2f, 0.f, 0.f, 0.f, 0.f, 0.2f, 0.2f, 0.9f, 0.7f),
      60.f, 270.f,
      { 0.3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.7f, 0, 0, 0, 0, 0, 0 },
      0.95f, 0.97f },

    // ----------------------------------------------------------------- INDIE
    { "Indie", false, 0.f, 0.f, KitStudio, "rock",
      {
        { "Floor-tom pulse", 0.15f, 16, { { Kick, "x.......x......." }, { TomFloor, "x...x...x...x..." }, { Rim, "....o.......o..." } } },
        { "Motorik", 0.35f, 16, { { Kick, "x...x...x...x..." }, { Snare, "....x.......x..." }, { HatClosed, "x.o.x.o.x.o.x.o." } } },
        { "Dance-punk", 0.55f, 16, { { Kick, "x...x...x...x..." }, { Snare, "....X.......X..." }, { HatClosed, "x...x...x...x..." }, { HatOpen, "..x...x...x...x." } } },
        { "Tom-heavy", 0.74f, 16, { { Kick, "x..x..x...x..x.." }, { TomFloor, "x.x.x.x.x.x.x.x." }, { Snare, "....X.......X..." } } },
        { "Wall of sound", 0.92f, 16, { { Kick, "x.x.x.x.x.x.x.x." }, { Snare, "....X.......X..." }, { Crash, "x...x...x...x..." }, { Tambourine, "....x.......x..." } } },
      }, 5,
      { 0.4f, 0.35f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.25f, 0.f },
      feel (5.f, 0.08f, 1.f, 2.f, 0.f, 0.f),
      orn (0.1f, 0.2f, 0.25f, 0.2f, 0.f, 0.f, 0.f, 0.1f, 0.f, 0.f),
      75.f, 180.f,
      { 0.5f, 0, 0, 0, 0, 0, 0.3f, 0, 0, 0, 0, 0, 0, 0, 0.2f, 0, 0, 0 },
      0.65f, 0.8f },

    // ----------------------------------------------------------- FUSION JAZZ
    { "Fusion Jazz", false, 0.f, 0.1f, KitStudio, "jazz",
      {
        { "Ghosted 16ths", 0.25f, 16, { { Kick, "x.....x...x....." }, { Snare, ".g.gX.g..g.gX.g." }, { HatClosed, "xoxoxoxoxoxoxoxo" } } },
        { "Linear fusion", 0.45f, 16, { { Kick, "x..x...x..x....." }, { Snare, "....X..g.g..X..g" }, { HatClosed, "x.o.x.o.x.o.x.o." } } },
        { "Bell fusion", 0.65f, 16, { { Kick, "x..x..x...x..x.." }, { Snare, "....X.g..g..X.g." }, { RideBell, "x.o.x.o.x.o.x.o." }, { HatPedal, "....x.......x..." } } },
        { "Songo-ish", 0.83f, 16, { { Kick, "...x..x....x..x." }, { Snare, "..g.X..g.g.gX..g" }, { HatClosed, "x.xxx.xxx.xxx.xx" }, { Ride, "x..x..x...x.x..." } } },
        { "Full-on", 0.95f, 16, { { Kick, "x.xx..x.x.xx..x." }, { Snare, "....X..g.g.gX.gg" }, { Crash, "x.......x......." }, { Ride, "x.x.x.x.x.x.x.x." } } },
      }, 5,
      { 0.3f, 0.f, 0.45f, 0.25f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f },
      feel (3.5f, 0.07f, 0.f, 1.f, -1.f, -1.f, 0.3f),
      orn (0.6f, 0.4f, 0.2f, 0.5f, 0.2f, 0.f, 0.3f, 0.2f, 0.f, 0.f),
      70.f, 200.f,
      { 0, 0.5f, 0.5f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
      0.5f, 0.78f },

    // ------------------------------------------------------------------ FUNK
    { "Funk", false, 0.f, 0.15f, KitStudio, "funk",
      {
        { "Pocket", 0.25f, 16, { { Kick, "x......x..x....." }, { Snare, "....X..g.g..X..." }, { HatClosed, "x.o.x.o.x.o.x.o." } } },
        { "16th hats", 0.45f, 16, { { Kick, "x.x....x..x....." }, { Snare, "....X..g.g..X..g" }, { HatClosed, "xoxoxoxoxoxoxoxo" } } },
        { "Open-hat funk", 0.62f, 16, { { Kick, "x..x...x..x..x.." }, { Snare, "....X..g.g..X..." }, { HatClosed, "x.x.x.x.x.x.x..." }, { HatOpen, "..............x." } } },
        { "Linear", 0.78f, 16, { { Kick, "x.....x...x....." }, { Snare, "...gX..g.g..X..g" }, { HatClosed, "x.x...x.x...x.x." } } },
        { "Breakbeat", 0.92f, 16, { { Kick, "x.x...x...x..x.." }, { Snare, "....X..g.g.gX..g" }, { HatClosed, "xoxoxoxoxoxoxo.o" }, { HatOpen, "..............x." } } },
      }, 5,
      { 0.3f, 0.2f, 0.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f },
      feel (3.f, 0.07f, 0.f, 2.f, -2.f, -1.f, 0.3f),
      orn (0.7f, 0.45f, 0.35f, 0.5f, 0.f, 0.f, 0.f, 0.05f, 0.f, 0.f),
      78.f, 135.f,
      { 0, 1.f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
      0.45f, 0.8f },

    // ---------------------------------------------------------------- GROOVE
    { "Groove", false, 0.f, 0.35f, Kit808, "groove",
      {
        { "Laid-back pocket", 0.2f, 16, { { Kick, "x......x.x......" }, { Snare, "....X.......X..." }, { HatClosed, "x.o.x.o.x.o.x.o." } } },
        { "Boom bap", 0.4f, 16, { { Kick, "x......x..x....." }, { Snare, "....X.......X..." }, { HatClosed, "x.o.x.o.x.o.x.o." }, { HatOpen, ".......?........" } } },
        { "Neo-soul", 0.6f, 16, { { Kick, "x.....x.x..x...." }, { Snare, "....X..g....X..g" }, { HatClosed, "x.ox.ox.ox.ox.ox" } } },
        { "Gospel chops", 0.78f, 16, { { Kick, "x..x..x...x..x.." }, { Snare, "....X..g.g..X.gg" }, { HatClosed, "xoxoxoxoxoxoxoxo" } } },
        { "Big groove", 0.92f, 16, { { Kick, "x..x..x...x..x.." }, { Snare, "....X.......X..g" }, { Ride, "x.o.x.o.x.o.x.o." }, { Crash, "x..............." }, { Clap, "....x.......x..." } } },
      }, 5,
      { 0.3f, 0.3f, 0.3f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.4f, 0.f },
      feel (5.f, 0.08f, 3.f, 12.f, 4.f, 3.f, 0.35f),
      orn (0.45f, 0.35f, 0.25f, 0.3f, 0.f, 0.f, 0.f, 0.05f, 0.f, 0.f),
      60.f, 125.f,
      { 0, 0.2f, 0, 0, 0.4f, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
      0.4f, 0.78f },

    // ----------------------------------------------------------------- BLUES
    { "Blues", true, 1.0f, 0.f, KitStudio, "jazz",
      {
        { "Slow 12/8", 0.2f, 12, { { Kick, "x.....x....." }, { Snare, "...X.....X.." }, { HatClosed, "x.ox.ox.ox.o" } } },
        { "Texas shuffle", 0.4f, 12, { { Kick, "x.....x....." }, { Snare, "..gX.g..gX.g" }, { HatClosed, "x.ox.ox.ox.o" } } },
        { "Ride shuffle", 0.6f, 12, { { Kick, "x..x..x..x.." }, { Snare, "...X.....X.." }, { Ride, "x.ox.ox.ox.o" } } },
        { "Double shuffle", 0.8f, 12, { { Kick, "x..x.ox..x.o" }, { Snare, "..gX.g..gX.g" }, { Ride, "x.ox.ox.ox.o" } } },
        { "Rocking blues", 0.93f, 12, { { Kick, "x..x..x..x.." }, { Snare, "...X.....X.." }, { Crash, "x..x..x..x.." } } },
      }, 5,
      { 0.3f, 0.1f, 0.f, 0.6f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f },
      feel (6.f, 0.08f, 1.f, 6.f, 1.f, 0.f, 0.3f),
      orn (0.3f, 0.3f, 0.f, 0.f, 0.15f, 0.1f, 0.1f, 0.05f, 0.f, 0.f),
      50.f, 170.f,
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.8f, 0, 0, 0.2f, 0, 0, 0, 0, 0 },
      0.6f, 0.78f },

    // ------------------------------------------------------------ ORCHESTRAL
    //  Kit mapping: Kick = concert bass drum, Snare = orchestral snare,
    //  toms = timpani (high/mid/low), Crash = clash cymbals,
    //  Ride = suspended cymbal, China = gong, Splash = triangle.
    { "Orchestral", false, 0.f, 0.f, KitOrchestralModel, "orchestral",
      {
        { "Pianissimo pulse", 0.15f, 16, { { TomFloor, "x.......f......." } } },
        { "March", 0.35f, 16, { { Snare, "x.ggx.g.x.ggx.x." }, { Kick, "x.......x......." } } },
        { "Ostinato", 0.55f, 16, { { TomMid, "x..x..x.x..x..x." }, { TomFloor, "x.......x......." }, { Kick, "f...f...f...f..." } } },
        { "Epic hits", 0.78f, 16, { { Kick, "x.....x.x......." }, { TomFloor, "x..x..x...x....." }, { Snare, "....x.......x..." }, { Crash, "x..............." } } },
        { "Battle", 0.95f, 16, { { Kick, "x..x..x.x..x..x." }, { TomFloor, "x.o.x.o.x.o.x.o." }, { TomMid, ".o.o.o.o.o.o.o.o" }, { Snare, "xggg....xggg...." }, { Crash, "x.......x......." } } },
      }, 5,
      { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f },
      feel (8.f, 0.1f, 0.f, 0.f, 0.f, 0.f, 0.2f),
      orn (0.1f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.2f, 0.f, 0.f),
      50.f, 170.f,
      { 0.4f, 0, 0, 0.3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.3f, 0 },
      0.6f, 0.75f },

    // --------------------------------------------------------------- MINIMAL
    { "Minimal", false, 0.f, 0.2f, Kit909, "electronic",
      {
        { "Heartbeat", 0.15f, 16, { { Kick, "x.......x......." }, { Shaker, "..o...o...o...o." } } },
        { "Tick", 0.35f, 16, { { Kick, "x.......x.x....." }, { Rim, "....x.......x..." }, { HatClosed, "..x...x...x...x." } } },
        { "Four-floor minimal", 0.55f, 16, { { Kick, "x...x...x...x..." }, { Clap, "....x.......x..." }, { HatClosed, "..x...x...x...x." }, { Shaker, "gggggggggggggggg" } } },
        { "Minimal tech", 0.75f, 16, { { Kick, "x...x...x...x..." }, { Rim, "...x..x....x..x." }, { HatOpen, "..x...x...x...x." }, { Perc, "......x.......x." } } },
        { "Peak", 0.92f, 16, { { Kick, "x...x...x...x..." }, { Clap, "....x.......x..." }, { HatClosed, "xoxoxoxoxoxoxoxo" }, { HatOpen, "..x...x...x...x." }, { Ride, "x.x.x.x.x.x.x.x." } } },
      }, 5,
      { 0.f, 0.2f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.8f, 0.f },
      feel (1.5f, 0.03f, 0.f, 0.f, 0.f, 0.f, 0.2f),
      orn (0.f, 0.15f, 0.2f, 0.3f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f),
      90.f, 140.f,
      { 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 0, 0, 0 },
      0.3f, 0.8f },

    // ----------------------------------------------------------------- CAJON
    //  Kit mapping: Kick = bass tone, Snare = slap, Rim = finger tip,
    //  toms = bongo / conga, Crash = tambourine hit, Ride/Hats = shaker.
    { "Cajon", false, 0.f, 0.1f, KitCajonModel, "cajon",
      {
        { "Ballad", 0.2f, 16, { { Kick, "x.......x......." }, { Snare, "....x.......x..." }, { Rim, "..g...g...g...g." } } },
        { "Pop cajon", 0.4f, 16, { { Kick, "x.....x.x......." }, { Snare, "....X.......X..." }, { Rim, "..g...g...g.g..." }, { Shaker, "x.o.x.o.x.o.x.o." } } },
        { "Rumba", 0.6f, 16, { { Kick, "x..x..x...x..x.." }, { Snare, "....X..g....X..." }, { Rim, "gg.ggg.ggg.ggg.g" } } },
        { "Driving 16ths", 0.78f, 16, { { Kick, "x.....x.x.....x." }, { Snare, "....X.......X..." }, { Rim, "gggg.ggggggg.ggg" }, { Tambourine, "....x.......x..." } } },
        { "Full band", 0.92f, 16, { { Kick, "x..x..x.x..x..x." }, { Snare, "....X..g....X.g." }, { Rim, "g.g.gg.gg.g.gg.g" }, { Tambourine, "x.o.x.o.x.o.x.o." }, { TomHigh, "........x.x....." } } },
      }, 5,
      { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f },
      feel (5.f, 0.1f, 0.f, 1.f, 0.f, 0.f, 0.2f),
      orn (0.6f, 0.3f, 0.f, 0.2f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f),
      60.f, 160.f,
      { 0, 0, 0, 0.4f, 0, 0, 0.3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.3f, 0 },
      0.25f, 0.75f },
};

const StyleDef& StyleLibrary::get (int s)
{
    if (s < 0 || s >= NumStyles) s = Rock;
    return kStyles[s];
}
} // namespace hammer
