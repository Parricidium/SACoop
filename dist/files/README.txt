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
- Same time, same weather. Merged street life: the host populates around
  them (same pedestrians and traffic for all), further out the nearest player.
- Police: one wanted level for everybody, and the host's police also
  chases wanted guests (RecherchePartagee, PoliceHote).

Keys
- G: get in another player's car as a passenger (F too).
- T: chat (Enter to send, Escape to cancel). In the chat, /join brings
  you next to the host (or as his passenger if he is driving).
- F5 (held): players board.
- F6: first-person view on foot (F6 again to go back).
- F10: co-op panel (go to a player, friendly fire, outfit; time and weather
  for the host). Arrows and Enter to choose, Escape to close.

You need
- GTA San Andreas for PC, version 1.0 US. The current Steam version (3.0)
  must be downgraded to 1.0 US first (a downgrader tool does that). The mod
  refuses to start on any other version.
- The same SACoop zip for every player (the launcher takes care of it).

Installation
1. Copy the contents of the zip into the game folder (next to gta_sa.exe).
2. Run SACoop.exe (the launcher). It updates itself at every start: no need
   to download the zip again.
3. The host clicks HOST (lobby). The others type the host's IP address,
   JOIN, then READY. The host picks the game (new or a save) and clicks
   START: every game starts. Arriving after the start: JOIN IN GAME.
   ("SACoop - Heberger.cmd" and "SACoop - Rejoindre.cmd" also work, without
   updates.) In the game too: main or pause menu > COOP (Host, Join with
   the address, Name).
4. The host starts their game (new or a save). Guests still in the menu
   follow automatically (same save if they received it). Over the
   Internet, the host opens port 7800 (TCP and UDP) on their router (or use a virtual
   network such as Radmin VPN).

Settings: sacoop.ini (or the launcher tabs). Name, address and port:
sacoop-joueur.ini (created at first start). Logs: sacoop.log and the logs
folder (attach them to a bug report).

Uninstall: delete dinput8.dll, SACoop.exe, the SACoop folder, sacoop*.ini,
sacoop.log and logs.

Unofficial fan project, not affiliated with Rockstar Games or Take-Two.
No game files are included: you must own the game.
https://github.com/Parricidium/SACoop
