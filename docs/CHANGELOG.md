# Changelog - Zebra's forked repo

This is the `vcport` line of [Parricidium/SACoop](https://github.com/Parricidium/SACoop). The original's last release
is `0.48.4-prealpha`; everything below was added on top of it. Setting names (`sacoop.ini`) are described in the
README. Technical notes for the VCCoop port are in [`PORT-VCCOOP.md`](PORT-VCCOOP.md).

Versions marked *untested* were written and compiled but not yet played for real by two players.

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
