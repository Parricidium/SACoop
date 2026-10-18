# Handoff notes for a new Claude Code session

Read this first. It is the state of the project as of `0.54.8-vcport` (15/10/2026).

## What this is
Zebra's fork of Parricidium/SACoop (GTA San Andreas 1.0 US co-op mod), branch `vcport`. Two players: the host (desktop PC)
and a guest (his brother, over the internet). Both run the same `dinput8.dll`. Read `README.md`, `docs/CHANGELOG.md` and
`docs/PORT-VCCOOP.md` for the design; the headers of `src/radio.cpp`, `src/trains.cpp` and `src/msnhud.cpp` explain the newer parts.

## Rules
- Commits: the owner is the only contributor. No `Co-Authored-By` or `Claude-Session` lines. Author name `zebra032-cyber`,
  email `339006755+zebra032-cyber@users.noreply.github.com`.
- Never push to Parricidium/SACoop. The owner's fork is `zebra032-cyber/SACoop`; branch `vcport` is also pushed to `main`
  (`git push fork vcport` and `git push fork vcport:main`).
- Every release: bump `SACOOP_VERSION` in `src/sacoop.h`, add a top entry to `docs/CHANGELOG.md`, a `**0.54.x:**` bullet in the
  README, and `NET_VERSION` in `src/net.h` when the protocol changes (both players must run the same dll).
- Keep the final messages to the owner short.

## Build and test
- Windows only: `build-dll.cmd` gives `build\dinput8.dll`; copy it to both PCs. The version on the first line of `sacoop.log`
  must match on both.
- No compiler for the game here: syntax-check with clang (`-target i686-w64-mingw32`, mingw-w64 headers, `-fms-extensions`);
  the files that include `<string>` or `<vector>` (panel, ui, mods, savesync, thumbs) cannot be checked that way.
- The owner plays, then sends `sacoop.log` of the host and of the guest. Fix from those logs.

## Open items (from the owner's tests)
- 0.54.8 is untested: mission HUD (`msnhud.cpp`), vehicle colour / upgrade / paintjob sync (`MSG_VEHMOD`, `vehicles.cpp`),
  F10 STOP MISSION (`mirror.cpp`), the CarFallOut crash patch (`game.cpp`, Robbing Uncle Sam).
- "Objects and NPCs are like brick walls for the guest": the host's mission characters and vehicles are copies placed from the
  host's state. Needs a design where a hit hands them over to the guest for a moment. Ask the owner for a concrete case.
- Sweet's car missing outside his house after cutscenes (game's car generators, explained only).
- The guest's mission text lookups can fail (the menu hook fails on the brother's exe: `crochets impossibles` in his log).
- Older: crates dropped on crouch, left / right movement bug, phone ring tone.
