#include "HammerTypes.h"

namespace hammer
{
const char* laneName (int lane)
{
    static const char* names[NumLanes] = {
        "Kick", "Snare", "Rim / Side Stick", "Hi-Hat Closed", "Hi-Hat Open", "Hi-Hat Pedal",
        "Tom High", "Tom Mid", "Tom Floor",
        "Crash", "Crash 2", "Ride", "Ride Bell", "China", "Splash",
        "Clap", "Cowbell", "Shaker", "Tambourine", "Perc"
    };
    return (lane >= 0 && lane < NumLanes) ? names[lane] : "?";
}

const char* laneShortName (int lane)
{
    static const char* names[NumLanes] = {
        "KICK", "SNR", "RIM", "HH-C", "HH-O", "HH-P",
        "TOM1", "TOM2", "FLR",
        "CRSH", "CRS2", "RIDE", "BELL", "CHNA", "SPLS",
        "CLAP", "COWB", "SHKR", "TAMB", "PERC"
    };
    return (lane >= 0 && lane < NumLanes) ? names[lane] : "?";
}

int laneToGmNote (int lane)
{
    static const int notes[NumLanes] = {
        36, 38, 37, 42, 46, 44,
        48, 45, 43,
        49, 57, 51, 53, 52, 55,
        39, 56, 70, 54, 75
    };
    return (lane >= 0 && lane < NumLanes) ? notes[lane] : 36;
}

int gmNoteToLane (int n)
{
    switch (n)
    {
        case 35: case 36: return Kick;
        case 38: case 40: return Snare;
        case 37: return Rim;
        case 42: return HatClosed;
        case 46: return HatOpen;
        case 44: return HatPedal;
        case 48: case 50: return TomHigh;
        case 45: case 47: return TomMid;
        case 41: case 43: return TomFloor;
        case 49: return Crash;
        case 57: return Crash2;
        case 51: case 59: return Ride;
        case 53: return RideBell;
        case 52: return China;
        case 55: return Splash;
        case 39: return Clap;
        case 56: return Cowbell;
        case 69: case 70: case 82: return Shaker;
        case 54: return Tambourine;
        case 75: case 76: case 77: case 60: case 61: case 62: case 63: case 64: return Perc;
        default: return -1;
    }
}

int laneGroup (int lane)
{
    switch (lane)
    {
        case Kick: return GKick;
        case Snare: case Rim: case Clap: return GSnare;
        case TomHigh: case TomMid: case TomFloor: return GToms;
        case HatClosed: case HatOpen: case HatPedal: return GHats;
        case Crash: case Crash2: case Ride: case RideBell: case China: case Splash: return GCymbals;
        default: return GPerc;
    }
}

const char* groupName (int g)
{
    static const char* n[NumGroups] = { "Kick", "Snare", "Toms", "Hats", "Cymbals", "Perc" };
    return (g >= 0 && g < NumGroups) ? n[g] : "?";
}

bool laneIsFoot (int lane) { return lane == Kick || lane == HatPedal; }
bool laneIsHand (int lane) { return ! laneIsFoot (lane); }

const char* styleName (int s)
{
    static const char* n[NumStyles] = {
        "Jazz", "Rock", "Metalcore", "Death Metal", "Indie", "Fusion Jazz",
        "Funk", "Groove", "Blues", "Orchestral", "Minimal", "Cajon"
    };
    return (s >= 0 && s < NumStyles) ? n[s] : "?";
}

const char* sectionName (int s)
{
    static const char* n[(int) Section::NumSections] = {
        "Auto", "Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Breakdown", "Solo", "Outro", "Stop"
    };
    return (s >= 0 && s < (int) Section::NumSections) ? n[s] : "?";
}
} // namespace hammer
