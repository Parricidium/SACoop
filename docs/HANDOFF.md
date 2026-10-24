# Handoff: SACoop (GTA San Andreas co-op mod) — read this first

State as of **0.54.99-vcport**, 24/10/2026. Written so that any assistant or developer (Claude, ChatGPT, a person) can
pick the project up. A copy lives in the repo at `docs/HANDOFF.md`; keep it up to date when you hand over.

## 1. What this project is
- A fork of [Parricidium/SACoop](https://github.com/Parricidium/SACoop): a co-op mod for **GTA San Andreas 1.0 US**
  (story missions shared, vehicles, combat, saves...). It is a `dinput8.dll` proxy loaded by the game, written in C++
  (MSVC, Win32, no external libraries). Line/branch name: **`vcport`**. Fork on GitHub: `zebra032-cyber/SACoop`
  (owner: cuddlyzebra).
- Players: one **host** (the owner) and up to three **guests** over the internet. Tested with 2 and 3 players. Everyone
  must run the **same `dinput8.dll`**: the first line of each `sacoop.log` shows the version. **Network version is 35**
  (`NET_VERSION` in `src/net.h`); a different one is refused at connection.
- Both/all PCs play at **60 FPS** (`ImagesParSeconde=60`, the original developer's recommendation); they must match.
- Working folder on the owner's PC: **`C:\sacoop`** (the git repo). Sources in `src/`, docs in `docs/`, README at the root.
- Read: `README.md` (features, keys, sacoop.ini), `docs/CHANGELOG.md` (**French**, newest on top: the real history of
  every change and why), `docs/PORT-VCCOOP.md`. The comment headers of each `src/*.cpp` explain each subsystem.

## 2. Rules (the owner's)
- **Commits: the owner is the only contributor.** No `Co-Authored-By`, no AI session/attribution lines in commits or PRs.
  Git identity: `zebra032-cyber` / `339006755+zebra032-cyber@users.noreply.github.com`.
- **Never push to Parricidium/SACoop.** Only the fork remote (`fork`): `git push fork vcport`, then keep `main` identical:
  `git checkout main; git merge vcport; git push fork main; git checkout vcport`.
- Every change: bump `SACOOP_VERSION` in `src/sacoop.h`; add a **French** entry at the top of `docs/CHANGELOG.md`, marked
  `*Non teste.*` until it has been played; bump `NET_VERSION` in `src/net.h` only if the wire protocol changes (then every
  player must update).
- Files on the PC use **CRLF** line endings; keep them.
- The owner builds on Windows with `build-dll.cmd` (Visual Studio, output `build\dinput8.dll`, map file
  `build\dinput8.map`), copies the dll into every game folder and checks the version line. **An assistant cannot compile
  or run the game**: at most a syntax check with clang and stub Windows headers. Write conservatively; a compile error
  costs a round trip.
- Keep messages to the owner short and plain; he does not need the machinery explained. Ask at most one question.
- **Confirm from logs/video before fixing.** Many fixes were done in diagnostic rounds: add a log line, ship, read the
  logs of the next session, then fix. Hypotheses that were not checked against a log were often wrong.

## 3. Working together (two assistants / people)
- One editor at a time on a given file. Before editing, take the **current** files from `C:\sacoop` (or the GitHub fork
  once pushed); the last version number in `src/sacoop.h` and at the top of `docs/CHANGELOG.md` tells you where it is.
- Each delivered change gets its own version number (0.54.100, 0.54.101...). If two people work in parallel, agree who
  takes the next number, or work on a git branch and merge.
- After each test session the owner sends: the host's `sacoop.log`, every guest's log (named `sacoop-<date>.log` on their
  side), and often a video. **Map the logs to one session**: log lines start with `[GetTickCount ms]`, a different clock on
  each PC; line up a common event (for example "reseau : X entre (joueur N)" on the host and "accepte par l'hote, joueur N"
  on that guest) to get the offset. Twice an old log or an old dll was sent by mistake: check the version line.
- Reverse-engineering references used for every game address and struct offset: gta-reversed
  (github.com/gta-reversed/gta-reversed, `source/game_sa`, functions carry their 1.0 US addresses), plugin-sdk
  (github.com/DK22Pac/plugin-sdk, `VALIDATE_OFFSET` lines), and the owner's own `gta_sa.exe` 1.0 US for disassembly
  (file offset = VA − 0x400C00). A crash line `PLANTAGE ... (dinput8.dll, +XXXX)` is resolved with `build\dinput8.map`
  (base 0x10000000) and `objdump -d -M intel` on `build\dinput8.dll`.

## 4. Git state
- GitHub `vcport` and `main` = `41bd3a7` (0.54.92 era). **Everything from 0.54.93 to 0.54.99 is uncommitted on the owner's
  PC.** 0.54.94/95 have been played (crash fix confirmed); 0.54.96-99 are written but not yet built/played as of this
  handoff. Commit once the owner has played them (section 2).

## 5. Architecture in one page (where things live)
- **Network** (`net.cpp`, `net.h`): UDP, the host relays everything between guests. ~30 `MsgState` per second per player
  (position, health, weapon, `carTask`/`carTaskVeh` for getting in/out, `vehicleId`, `seat`, aim, shots, radio...), plus
  `MsgVehicle`, `MsgObject`, `MsgMObject`, `MsgPed`, `MsgPedHit`, `MsgFire`, `MsgRadio`... and a reliable ordered stream
  (mission mirror, pickups, events).
- **Puppets** (`coop.cpp`): each remote player is a ped "puppet" on the other PCs, placed from interpolated states, using
  the game's own tasks to get in/out of cars and to replay shots. `PuppetIndex(ped)` tells whether a ped is a puppet.
- **Vehicles** (`vehicles.cpp`): the driver's PC owns a car and sends it; the others hold a "copy", placed every frame
  (before and after the physics, `VehiclesAfterProcess`) and normally **frozen** (CPhysical +0x40 bit 13). Copies near the
  car we drive are unfrozen for real collisions (`VehiclePushProbe`). Ownership moves to whoever drives (or tows) it.
  Pool 0xB74494, stride 0xA18.
- **Missions** (`script.cpp`, `mirror.cpp`, `conditions.cpp`): story missions run **only on the host**. The host captures
  a whitelist of its mission script's commands (texts, cameras, blips, objects, fires, cutscenes, teleports...) with
  evaluated parameters and the guests replay them in a "ghost script" (`kOps` in mirror.cpp). Since 0.54.97 only story
  missions (2, 11-112) are mirrored; activities (113+: firing range, gyms, odd jobs) stay each player's own.
  `conditions.cpp` makes the host's mission tests ("player in area / in car") also true for a guest's puppet, so any
  player can move the mission on. The host's mission characters are sent to the guests (`entities.cpp`, copies on the
  guest); a guest's hit on a copy goes to the host (`MsgPedHit`), who applies it.
- **Population** (`population.cpp`, `gfx.cpp`): guests near the host use the host's traffic and pedestrians (copies), not
  their own. Guards remove spawns inside a guest's view.
- **Objects** (`objects.cpp`): moved/broken map objects (dummy pool 0xB744A0 stride **0x38**, object pool 0xB7449C stride
  0x19C), mission objects of the host, and fires (gFireManager 0xB71F80, 60 × 0x28).
- **Combat** (`combat.cpp`), **passenger** boarding and drive-by (`passenger.cpp`), **police** and shared wanted level
  (`police.cpp`), **events/pickups** (`events.cpp`), **radio** (`radio.cpp`), **HUD**, **speech**, **trains**, **garages**.
- Namespace gotcha: helpers such as `FindPlayerPed`, `PedVehicle`, `Field<T>(ptr, offset)` live in namespace `game`
  (`game.h`); a file without `using namespace game;` must write `game::FindPlayerPed()`.
- Script commands from C++: `RunScriptCommand(op, n, int args[])` / `RunScriptCommandTyped(op, n, "ifff...", args)` run one
  SCM command in the ghost script (`mirror.cpp`); char/object handles are pool refs (`PedRef`, `ObjRef`).

## 6. What was done recently (0.54.93 → 0.54.99), all in CHANGELOG.md
- 0.54.94: **crash fixed** (both PCs crashed at dinput8 +2C99B: dummy pool read with stride 0x34 instead of 0x38) —
  confirmed. Two players pressing F at the same car both turned passenger and froze: tie-break (first one keeps the wheel,
  else the host). Stuck get-in (> 15 s) aborted.
- 0.54.95 (network 34): player cars no longer frozen near the car you drive (real collisions); fire sync (`MsgFire`: fires
  put out with water, fires lit by a player); cops' arrest-drag (task 703) counted as a jack of the guest.
- 0.54.96 (network 35): radio restart overshoot learnt (was re-syncing every ~6 s); passengers follow the radio of the
  player who drives; police bribes shared; tow truck/trailer takes over the towed copy; bike passenger falls off with the
  rider; jacked guest warped out beside the door; host no longer teleported to a guest for big mission trigger areas;
  no copy of the host's shop seller over the guest's own.
- 0.54.97: only story missions mirrored (the firing range was started on every PC and the host's was replayed over the
  guests': controls lost, stuck in Ammu-Nation); camera-restore guard stops after two tries; fire duplicates.
- 0.54.98: parachute: the parachute script keeps a hidden canopy (model 3131) attached to anyone who owns a parachute;
  the "attached object" copy showed it open over every puppet. Only visible attached objects are copied now.
- 0.54.99: guests' hits on mission targets that are "only damaged by the player" (02A9, CPhysical +0x40 bit 22) are given
  in the host player's name (cemetery boss, motel cops); a guest moved to interior height (z > 900) while still in zone 0
  is held and given the host's zone (he fell 96 m and died); stray canopies left in saves removed.

## 7. Open items (rough priority)
1. **Test 0.54.96-0.54.99** with all players; check the log lines named in each CHANGELOG entry.
2. "Clients keep getting stuck in random positions, usually car related" (24/10): no details yet; need the moment in a
   video and the guest's log (`commandes : le joueur pousse ... mais ne bouge pas` lines list his tasks).
3. Player-car collisions: still reported as not working after 0.54.95; 0.54.96 logs `collision (diagnostic)` when two
   players' cars overlap (in our contact list or not, flags, status). Read those before changing anything.
4. Mission objects the guests never get (firing range targets 3018-3025, mission 23 objects 3061, Burning Desire windows
   so guests' molotovs do not count): diagnostic lines `miroir (diagnostic) : le script X ... utilise 0107` (host) and
   `miroir : objet de l'hote N ... chez nous` (guest) tell which script creates them and whether the guest made a copy.
5. Cops "not angered" by a guest (what exactly is unclear: ask); guest crimes do raise everyone's level.
6. Not shared at all (new features): gang wars (territories), girlfriends (each player's own, the host cannot see a
   guest's), firing range together, puppet parachute animation (only the canopy shows), stunt bonuses (owner wants them
   off for guests), interior -> exterior cutscene black screen for guests, weapon picked when getting in a car (grenades).
7. Older: passenger animation has only 4 poses; muzzle flash position; "bullets stop hitting NPCs after a while" (10 s
   summary line `tirs en voiture rejoues`); fence splinters not synced; host's mission characters are rigid "walls" for
   guests (copies placed from the host's state); diagnostic object lines can be removed once fences are done.

## 8. How to read the logs
- First line: version. Search `PLANTAGE` (crash: code, address, registers, stack). `+XXXX` in dinput8.dll → map file;
  a game address → gta-reversed / disassembly.
- Log lines are mostly French. Useful ones: `miroir : ... rejouee` (guest replayed a host mission command), `copie du
  personnage de mission` / `copie du vehicule` (guest copies), `pantin du joueur N : ...` (puppets), `passager : ...`,
  `choc :` / `choc contact :` (what our car hit), `police : ...`, `radio: ...`, `feu ...`, `commandes : le joueur pousse
  ... mais ne bouge pas` (stuck player: position, zone, controls, tasks), `terrain : ...` (held waiting for ground),
  `relocated:` (local player moved by a script/teleport), `teleportation (...)`, `death:`.
- Task numbers in logs are eTaskType values (gta-reversed `Enums/eTaskType.h`): 208 fall and get up, 240 in air and land,
  275 use sequence, 700/701 enter car as passenger/driver, 702 steal car, 703 drag ped from car, 704 leave car, 1303
  jetpack, 1600 phone.

## 9. Style
- Changelog entries and log lines in **French**; code comments mix French and English (newer ones in English): keep the
  style of the file. In-game and README text is English. `sacoop.ini` key names are French.
- Comments explain *why*, with the date of the report or log that led to the change.
