SACoop - GTA San Andreas co-op - PRE-ALPHA
==========================================

WARNING: very early version. For now, players see each other walk and run
in the same city, and that is all: no vehicles, shared missions, combat or
co-op menu yet. Do not expect a full game. Everything else comes in the
next versions.

You need
- GTA San Andreas for PC, version 1.0 US. The current Steam version (3.0)
  must be downgraded to 1.0 US (a downgrader tool does it). The mod refuses
  to run on any other version.
- The same SACoop zip for every player.

Installation
1. Copy the zip contents into the game folder (next to gta_sa.exe).
2. The host runs "SACoop - Heberger.cmd" (host).
   The others run "SACoop - Rejoindre.cmd" (join) and type the host's IP address.
3. Everybody starts a new game. Over the Internet, the host opens UDP port
   7800 on their router (or use a virtual LAN such as Radmin VPN).

Settings: sacoop.ini. Nickname, address and port: sacoop-joueur.ini (created
on first run). Logs: sacoop.log and the logs folder.

Uninstall: delete dinput8.dll, sacoop*.ini, sacoop.log and logs.

Fan project, unofficial, not affiliated with Rockstar Games or Take-Two.
No game files are included: you must own the game.
https://github.com/Parricidium/SACoop
