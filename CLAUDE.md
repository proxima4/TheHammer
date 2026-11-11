# The Hammer - notes for Claude

## Every change must update the version history (owner's standing rule)
1. Bump `project(... VERSION x.y.z ...)` in `CMakeLists.txt`.
2. Add an entry at the top of `CHANGELOG.md`, in plain words for a musician, with the date; the plugin also shows this history.
3. Push to `main`. When the version number is new and pluginval passes, the build publishes `vX.Y.Z` as a TEST VERSION (GitHub pre-release). The owner tries it with the updater's "Try Test Versions" button and approves it with "Approve Tested Versions"; only approved versions reach the automatic updates. Never approve on the owner's behalf. No manual tags are needed.

## Owner's plugin standards
- Stop in FL Studio (or any DAW) while the song plays = silence (fade, then hard stop, tails included). Playing live with the transport stopped lets tails ring. See `Source/HostStop.h`.
- The owner is not a programmer: explain things in plain, step-by-step words.

## Quality gate
A Release is published only after the build passes pluginval (strictness 5, includes Steinberg's VST3 validator) on Windows. If it fails, nothing is published and the owner's updater keeps the last good version. Never bypass this; fix the plugin instead. Test locally first where possible (pluginval Linux build works on the Linux VST3).
