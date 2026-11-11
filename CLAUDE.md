# The Hammer - notes for Claude

## Every change must update the version history (owner's standing rule)
1. Bump `project(... VERSION x.y.z ...)` in `CMakeLists.txt`.
2. Add an entry at the top of `CHANGELOG.md`, in plain words for a musician, with the date; the plugin also shows this history.
3. Push to `main`. The build workflow publishes GitHub Release `vX.Y.Z` automatically when the version number is new, and the owner's APT Industrial updater installs it into FL Studio. No manual tags are needed.

## Owner's plugin standards
- Stop in FL Studio (or any DAW) while the song plays = silence (fade, then hard stop, tails included). Playing live with the transport stopped lets tails ring. See `Source/HostStop.h`.
- The owner is not a programmer: explain things in plain, step-by-step words.
