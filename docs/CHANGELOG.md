# Changelog - Zebra's forked repo

This is the `vcport` line of [Parricidium/SACoop](https://github.com/Parricidium/SACoop). The original's last release
is `0.48.4-prealpha`; everything below was added on top of it. Setting names (`sacoop.ini`) are described in the
README. Technical notes for the VCCoop port are in [`PORT-VCCOOP.md`](PORT-VCCOOP.md).

Versions marked *untested* were written and compiled but not yet played for real by two players.

## 0.54.2-vcport (untested, network version 23, same protocol as 0.54.0)

Written from the host and client logs of the 14/10 test of 0.54.1 (Home Invasion again).

- **Radio**: the station a passenger chose reached the driver's game and changed the station *number* there, but the music
  never changed. The call used (`0x4E8330`, `InitialiseRadioStationID`) only overwrites the station; the radio keys and
  `SET_RADIO_CHANNEL` use `CAERadioTrackManager::RetuneRadio` (`0x4E8290`), which queues the station for the game's radio
  service. Now used, with a 3 s window after each retune in which the station change is not taken for a choice of our
  own, and no second call for the same station within 2 s.
- **Radar icons on the guest during a host mission**: the host's list had 104 blips (all contact points included), the
  guest's reconciliation found them all in place and so kept them. On the host they are hidden by the engine because the
  script variable ONMISSION (`ScriptSpace[CTheScripts::OnAMissionFlag]`, `OnAMissionFlag` at `0xA476AC`) is 1; the guest
  never ran the mission start. While the host is in a mission the guest's ONMISSION is forced to 1 (log
  `miroir : ... drapeau MISSION ...`) and put back to its old value when the mission ends or the game state changes.
- **Alt+Tab, mouse**: the loop that gave the cursor back (`ShowCursor(TRUE)`) gave up after 32 calls, but the game's hide
  counter sinks by one every frame it is not in front (the log said `curseur +32` each time): it stayed hidden. Now the
  counter is raised to 0 whatever it takes, every frame while away; `ShowCursor(FALSE)` calls from the game are ignored
  while away; the cursor over the game window is an arrow (`WM_SETCURSOR`). On return the same number of calls is undone.
- **Landing watchdog** leaves the player alone while the ground wait holds him (his speed is zero on purpose then; the
  1.2 s tasks-cleared line in the client log came from that).
- **Fall rescue**: a guest in an interior while the host is outside (the host left the house, the guest had already been
  teleported to outside coordinates but kept zone 8 and fell) is put next to the host, outside - not back into the interior
  where nothing is loaded.
- **Mission people in the air pose**: the host told the guests a mission ped was "in the air" whenever the game's standing
  flag was 0, but the game only refreshes that flag for peds it runs through the collision code, not for a ped at rest
  that a script placed. The old man of Home Invasion (model 62) most likely hung in the air pose on the guest because of it.
  Now "in the air" needs real vertical speed, a jump task or swimming (same rule as for players since 0.53).
- **Text diagnostic** reads the game's 8-bit text (it printed `????` for every key).
- **New log line `commandes :`** when the player pushes a direction for 0.8 s and does not move (pad and keyboard state,
  `DisablePlayerControls`, tasks, standing flag; 25 lines at most): the "cannot move left or right after the cutscene"
  bug has no explanation in the 14/10 logs (the host log only shows the host stayed at x = 2807.6 for over a minute).
- **Launcher**: the "Zebra's forked repo" label overlapped the status line (`Choose your gta_sa.exe`, `SACoop 0.54.x - up
  to date`); it now has its own line above the footer. A launcher run straight from `build\` (no game folder next to it)
  looks for its pictures in `..\dist\files\SACoop\interface` too, instead of drawing the plain fallback card.
- Still open: the old man and the crates (needs the "commandes" lines and a clip of what is seen), Alt+Enter and the
  window size (the log shows the game resets to the window size, 1904x958 -> 1280x644 and back; what is expected beyond
  that is to be clarified).

## 0.54.1-vcport (untested, network version 23, same protocol as 0.54.0)

Written from the host and client logs of the 14/10 Home Invasion test (both were 0.53.0).

- **Alt+Tab away**: the mouse goes back to Windows. The window stays blind to its own deactivation (the game must not
  pause), so the DLL now watches the foreground itself: when the game loses it, DirectInput is unacquired, the cursor
  clip and capture are released and the cursor is shown; on return the cursor state and the clip are put back and the
  devices are acquired again. `IDirectInputDevice8::Acquire` is refused for the mouse and keyboard while the game is not in
  front (the game retries blindly). The cooperative level the game asks for is logged (`entrees : niveau de cooperation`).
- **Ground wait (Home Invasion guest)**: the client log shows the guest teleported by the mirrored script to the interior
  at z 1025, `chute sans fin` 40 m in 2 s (collisions not streamed in), then the fall rescue put them back outside (zone 0)
  while the host's interior scene went on: the guest saw only the sky, the old man "fell". Now, for 8 s after a relocation
  into an interior, a player with no ground under him (`GET_GROUND_Z_FOR_3D_COORD`) is held in place while
  `REQUEST_COLLISION` is repeated and `LOAD_SCENE` is called once after 1.5 s (`terrain : ...` lines).
- **Fall rescue inside a mission**: a guest falling in an interior while the host is in an interior is put next to the host,
  in the host's zone, not back outside.
- **Zone regroup**: during a host mission, a guest whose zone (ped area or game area) differs from the host's for 4 s is
  put next to the host (3 tries in a row at most, 12 s apart; `miroir : l'invite n'est pas dans la zone de l'hote`).
- **Stuck landing**: the watchdog no longer needs the "standing" flag, also looks at the fall / jump tasks (207, 210, 211)
  and reacts after 1.2 s in the 6 s after a relocation (2.5 s otherwise). In the host log the settle code had just removed
  the "in the air" reaction (`relocated: in-air reaction dropped`) at the moment the script moved CJ into the house.
- **Help text**: PRINT_HELP_FOREVER was replayed on the guest (four times, 4 s apart) but no popup showed. Every text
  command's key is now looked up in the guest's game text and logged (`texte : ...`); if the key comes back empty the last
  mission text table (`LOAD_MISSION_TEXT`) is loaded again. The replay log keeps the first 20 of each command (700 in
  all) instead of the first 300, which `CLEAR_HELP` every frame used up.
- Still open: crates that do not follow CJ's hands (nothing in either log; the carry is an object attached by the host's
  script - needs a test of the same mission without the mod to tell vanilla behaviour from ours).

## 0.54.0-vcport (untested, network version 23)

### New features
- **Guests cannot start story missions.** Walking into a mission-start marker does nothing on a guest, but the marker is
  still drawn. Only the host starts missions. Setting `GuestMissionLock`.
- **F10 > Tools > SKIP CUTSCENE**: the game's own skip (`CCutsceneMgr::SkipCutscene`); a second click, or a click with no
  cutscene running, ends it by force and gives the controls and camera back.
- **F10 > Tools > REMOVE MY LAST VEHICLE / REMOVE ALL MY VEHICLES**: removes the vehicles the player spawned (menu or
  cheat); the other players' copies disappear at once (a new "gone" flag in the vehicle message).
- **Alt+Tab**: the DirectInput mouse and keyboard are acquired again when the game window gets the foreground back
  (the game never heard about the deactivation, so it never did it).
- **Alt+Enter**: borderless full screen <-> window. Same game resolution, the picture is scaled into the window; saved in
  `sacoop.ini` (`Fenetre`). The game does not see the Enter key of the combination.
- **Helicopter rotors and plane propellers** are synced: the vehicle message carries the rotor / propeller speed, and the
  copy's engine is switched on while it turns.
- **Radio in a shared vehicle**: every occupant sends the station he listens to and a counter of his own changes. The
  vehicle's leader (its driver) takes the stations the others choose; the others follow the leader, except for 2.5 s after
  a change of their own. Track and position still come from the host's radio.

### Fixes
- **Blips during a host mission**: every 100 ms the guest removes any coordinate / contact blip that the host does not have
  (the guest's own main script re-creates mission-start letters and contact icons such as OG and BS). Blips made by the
  mission mirror are kept.
- **Home Invasion** (14/10 test, to be confirmed):
  - the guest brought into the host's interior (mirrored `SET_PLAYER_COORDINATES`, `LINK_CHAR_WITH_INTERIOR`, or the
    regrouping at the start of a mission) now gets the scenery and collisions loaded around the new spot (`LOAD_SCENE`), as
    `TeleportPed` does; the mission start regroups into the host's interior too;
  - `LOAD_SCENE` and `REQUEST_COLLISION` of the host's scripts are repeated on the guest (cutscene place);
  - the local ped is no longer told he is "standing" in the middle of his own jump (the jump apex has zero vertical speed
    too), which could leave the host stuck in the landing animation;
  - a landing watchdog frees a player who stands still on the ground for 2.5 s with the in-air / landing tasks still running.
- Still open from that test: the guest's help-text popups and the crates that do not follow CJ's hands (need both logs).

## 0.53.0-vcport (untested, network version 22)

### New features
- **Train sync**: the host's trains (the random ones and the mission ones) run on every guest's rails: same place,
  same speed, same direction, and they stop at the stations together. The guest's own random trains are switched off
  while the host shares its trains. Whoever drives a train owns it: a guest who gets in the cab drives it for
  everybody, and it goes back to the host when they get out. Setting `TrainSync`.
  Limits: the game only builds trains near the host (a guest far away sees none), a player inside a train is shown
  standing in it, and a derailed train is not synced.

### Fixes
- Trains no longer go through the car sync: the street-life cleanup could delete a single carriage of a train, and a
  copy of a train carriage could be built as a free vehicle.

## 0.52.1-vcport (untested)

### New features
- **Weapons kept on death**: you get your weapons back (with their ammo) about 1.5 s after you respawn, instead of
  losing them. Only what is missing is given back. Setting `ArmesMort` (0 = lost, like the original game).
  This replaces the 0.51 weapon-drop-on-the-ground behaviour.

## 0.52.0-vcport (untested)

### New features
- **Ping display**: every player's ping to the host shows on the F5 players board and in the F10 Players tab.
- **Radio sync**: players tuned to the same station hear the same song at the same point, and the same adverts.
  Custom "User Tracks" radio syncs too when both players have an identical track library (checked automatically).
  Setting `RadioSync`.
- **Fork branding**: the launcher, the F10 menu and the first line of the log say "Zebra's forked repo"; the launcher
  updates from this repository's releases, and a repository with no release yet counts as "up to date".
- `build-exe.cmd` builds the launcher on its own; `dist/make-release.ps1` publishes to the fork by default.

### Fixes
- Ping no longer reads far too high: it is now timed on a separate network thread, with a high-resolution clock,
  instead of including the game's frame delay.
- The sky no longer flickers between colours at dawn and dusk: the guest never starts a weather change by itself.
  Setting `GuestWeatherGuard`.
- The joining player no longer keeps mission-letter blips that the host does not have (strict blip sync during
  missions).
- The falling animation no longer sticks on other players after leaving an interior or teleporting.
- The host now sees a vehicle that a mission spawns for the joining player as soon as it appears (the guest's copy
  is registered at once).

## 0.51.0-vcport (untested)

### New features
- **Where I died**: F10 > Players > "GO THERE" takes you back to your last death spot.
- **Go to player** works from anywhere, including inside interiors: the right interior is selected, the scenery
  is loaded on the spot, and you leave your vehicle first.
- **Endless-fall rescue**: a player who falls under the world, or falls for a long time without a parachute or
  jetpack, is put back on their last safe position (or next to the host). Setting `SecoursChute`.
- Weapons dropped on death (replaced in 0.52.1 by keeping the weapons).

### Fixes
- Picking up a save disk no longer opens the save menu for the other players too (the save disk is not shared).
- Vehicle copies wait less when a player stands next to the spawn spot (a car dropped next to someone appeared
  after 4 s; now 0.6 s for a still, empty car).
- Holding F10 or F1 no longer makes the menu flicker open and shut.

## 0.50.0-vcport (untested, network version 20)

### New features
- **Radar blip sync**: the host's coordinate and contact blips are copied to the guests. Setting `MarqueursRadar`.
- **Vehicles stay**: cars spawned from the F10 menu or by a cheat code are shared, and parked vehicles keep being
  sent while another player is within 350 m (they used to vanish for the other player 10 s later).
- **Pause indicator**: a player in the pause menu stands still for the others and shows "(paused)".
- **Fall rescue** (first version, `SecoursChute`).
- **Core-0 CPU affinity** at launch, setting `AffiniteCoeur`: fixes the mouse cursor that drifted.

### Fixes
- Traffic copies no longer overlap: when a copy of the host's vehicle is about to appear, an ordinary car of the
  guest's own traffic already standing there is removed (with its occupants); a copy no longer pops into a player
  standing next to it.
- Weather and the host's weather changes are logged to help trace sky problems.

## 0.49.x-vcport (untested)

### Smoother play: the VCCoop robustness layer (0.49.0)
- **Interpolation**: other players, vehicles and mission characters are shown about 110 ms in the past, between
  two real received states (the last 10 kept per entity). Every vehicle and character message now carries a
  timestamp. Settings `Interpolation`, `DelaiInterpolation`.
- **Placement after the physics step** for remote players, mission characters on the guest and vehicle copies
  (`PlacementPietons`, `PlacementVehicules`).
- **Frozen vehicle copies**: the local physics can no longer push or bounce the copy of someone else's car
  (`GelerCopies`).
- **World sanitizer**: a vehicle or character with an absurd position, matrix or speed (NaN...) gets its last sane
  pose back before it can crash the game (`GardeFou`).
- **Extended mission mirror**: about 120 more mission commands are repeated on the guest (texts, blips, camera and
  fades, mission audio, cutscene helpers, roads, garages, objects, pickups) (`MiroirEtendu`).
- **30 FPS by default** (as VCCoop does); 60 FPS with the frame-rate corrections is still an option
  (`ImagesParSeconde`).
- `build-dll.cmd` finds Visual Studio 2019 to 2026 by itself.

### Fixes (0.49.1 to 0.49.x)
- Fixed a crash when opening Display Setup > Advanced (a missing null check on a text conversion).
- **Load diagnostics** every 10 s in the log (CPU per thread, process and machine, disk reads, the heaviest
  processes), an optional sampling profiler (`Profil=1`), and a Windows power-throttling opt-out for the game.
- **Cutscenes**: the other players' doubles (player copies, mission character copies, vehicle copies) are hidden
  while a cutscene plays (`CacherEnCinematique`), and the clock and weather are not applied during cutscenes.
