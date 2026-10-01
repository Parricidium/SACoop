SACoop - GTA San Andreas co-op - PRE-ALPHA
==========================================

WARNING: early version, still being tested. There will be bugs.

What already works
- Up to 4 players in the same city. Everybody sees the others as CJ with
  their own clothes (or the chosen character), their name and radar blip.
- Shared vehicles (getting in and out, damage, explosions, sirens),
  weapons, shots, damage between players (Friendly fire option), death
  and hospital.
- Story missions played together: they run on the host, the others see the
  cutscenes, texts, markers, mission characters and vehicles, and can move
  the mission on. Story progress, mission money and the host's save are
  shared.
- Same time, same weather, same pedestrians and traffic near the host.

Keys
- T: chat (Enter to send, Escape to cancel).
- F5 (held): players board.

You need
- GTA San Andreas for PC, version 1.0 US. The current Steam version (3.0)
  must be downgraded to 1.0 US first (a downgrader tool does that). The mod
  refuses to start on any other version.
- The same SACoop zip for every player (the launcher takes care of it).

Installation
1. Copy the contents of the zip into the game folder (next to gta_sa.exe).
2. Run SACoop.exe (the launcher). It updates itself at every start: no need
   to download the zip again.
3. The host clicks HOST. The others type the host's IP address, then JOIN.
   ("SACoop - Heberger.cmd" and "SACoop - Rejoindre.cmd" also work, without
   updates.)
4. The host starts their game (new or a save). Guests still in the menu
   follow automatically (same save if they received it). Over the
   Internet, the host opens UDP port 7800 on their router (or use a virtual
   network such as Radmin VPN).

Settings: sacoop.ini (or the launcher tabs). Name, address and port:
sacoop-joueur.ini (created at first start). Logs: sacoop.log and the logs
folder (attach them to a bug report).

Uninstall: delete dinput8.dll, SACoop.exe, the SACoop folder, sacoop*.ini,
sacoop.log and logs.

Unofficial fan project, not affiliated with Rockstar Games or Take-Two.
No game files are included: you must own the game.
https://github.com/Parricidium/SACoop
