#pragma once
// ============================================================================
//  Version history - shown inside the plugin (Road Crew page).
//  APT Industrial rule: every change adds an entry here AND in CHANGELOG.md,
//  and bumps project(VERSION) in CMakeLists.txt.
// ============================================================================
namespace hammer
{
struct VersionEntry { const char* version; const char* date; const char* notes; };

inline constexpr VersionEntry kVersionHistory[] = {
    { "0.1.1", "2026-11-10",
      "Stop in a playing track now silences everything within 50 ms (voices, room reverb, MIDI out); "
      "loading a project silences the old one; live hits while FL is stopped still ring out. "
      "Windows build fixes (newer Visual Studio on GitHub, self-test tool memory). Version history added." },
    { "0.1.0", "2026-11-09",
      "First build. Drummer instrument, mixer FX and Ears listener. 12 styles, Listen/Neural brain blend, "
      "fills and solos, modeled 808/909/CR-78/Linn/acoustic/cajon/orchestral kits, sample kit loader, "
      "road-case GUI, neural training and free kit pack workflows." },
};
} // namespace hammer
