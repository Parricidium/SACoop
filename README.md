<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/img/logo.png">
    <img src="docs/img/logo-noir.png" width="200" alt="SACoop">
  </picture>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-PRE--ALPHA-red?style=for-the-badge" alt="Pre-alpha">
  <a href="https://github.com/zebra032-cyber/SACoop/releases"><img src="https://img.shields.io/github/v/release/zebra032-cyber/SACoop?include_prereleases&label=Download&style=for-the-badge" alt="Download"></a>
</p>

<p align="center">
  <a href="https://store.rockstargames.com/fr/game/buy-grand-theft-auto-the-trilogy"><img src="https://img.shields.io/badge/Buy%20GTA%20San%20Andreas%20legitimately-Rockstar%20Store-FCAF17?style=for-the-badge&logo=rockstargames&logoColor=black" alt="Buy GTA San Andreas on the Rockstar Store"></a>
</p>
<p align="center">
  <b>This mod needs a legitimately owned copy of Grand Theft Auto: San Andreas.</b>
  <a href="https://store.rockstargames.com/fr/game/buy-grand-theft-auto-the-trilogy">Buy it on the Rockstar Store</a> (also on Steam). No game data is included here.
</p>

> [!WARNING]
> **PRE-ALPHA.** Shared story missions, vehicles, combat and saves already work, but the mod has not been played
> for real between friends for long. Expect bugs, and send your logs (`sacoop.log`, `logs` folder).

# SACoop: the San Andreas story in co-op — Zebra's forked repo

A co-op mod for **Grand Theft Auto: San Andreas** (PC, version 1.0 US). One player hosts and plays the story, friends
join the same world and the same missions, with modern options on top.

**This is Zebra's fork of [Parricidium/SACoop](https://github.com/Parricidium/SACoop).** It is the `vcport` line: the
same mod, plus the robustness and sync work that made [VCCoop](https://github.com/Parricidium/VCCoop) (the Vice City
co-op by the same author) so smooth, plus a long list of fixes and new features (see
[What this fork adds](#what-this-fork-adds)).

## Thank you, Parricidium

**A very big thank you to [Parricidium](https://github.com/Parricidium)** for creating SACoop and VCCoop. Everything in
this repository stands on their work: the networking, the mission mirroring, the launcher, the in-game menu and the
whole design of the mod are theirs. This fork only exists because they built something worth building on, and a
brother and I got to play San Andreas together because of it. Thank you for making it, and for the years of
reverse-engineering behind it.

If you like this, go and star the original: **<https://github.com/Parricidium/SACoop>**.

<p align="center">
  <img src="docs/img/cinematique-invite.jpg" width="100%" alt="A guest watching the host's mission cutscene">
</p>
<p align="center"><i>A guest watches the cutscene of the host's mission (Big Smoke), at the same time as the host.</i></p>

<p align="center">
  <img src="docs/img/mission-invite.jpg" width="49%" alt="Mission camera on the guest, with Sweet and the host">
  <img src="docs/img/grand-ecran.jpg" width="49%" alt="Guest in the mission, widescreen HUD">
</p>
<p align="center"><i>On the guest: the mission camera with Sweet and the host, then the mission's own texts, in widescreen.</i></p>

<p align="center">
  <img src="docs/img/police-invite.jpg" width="100%" alt="The host's police going after a wanted guest">
</p>
<p align="center"><i>A wanted guest: the host's cops come for them too (Grove Street).</i></p>

<p align="center">
  <img src="docs/img/rendu-avec.jpg" width="49%" alt="Grove Street with ambient occlusion and FXAA">
  <img src="docs/img/rendu-occlusion.jpg" width="49%" alt="The ambient occlusion alone">
</p>
<p align="center"><i>Optional modern rendering: ambient occlusion and FXAA (right: the occlusion alone).</i></p>

## Contents

1. [Quick start](#quick-start)
2. [Updating to the latest version](#updating-to-the-latest-version)
3. [What this fork adds](#what-this-fork-adds)
4. [What the original mod does](#what-the-original-mod-does)
5. [Keys](#keys)
6. [Settings (`sacoop.ini`)](#settings-sacoopini)
7. [The launcher](#the-launcher)
8. [Testing checklist and sending logs](#testing-checklist-and-sending-logs)
9. [Building from source](#building-from-source)
10. [Publishing your own build to GitHub](#publishing-your-own-build-to-github)
11. [Known gaps and roadmap](#known-gaps-and-roadmap)
12. [Credits and license](#credits-and-license)

## Quick start

1. You need GTA San Andreas **1.0 US**. The current Steam version (3.0) must be downgraded first (a downgrader tool
   does it). The mod refuses to run on any other version.
2. **Both players need the same build** of `dinput8.dll`. A mismatched pair is refused. Either:
   - download the zip from this repository's **Releases** page and copy its contents next to `gta_sa.exe`, or
   - build it yourself (see [Building from source](#building-from-source)) and copy `build\dinput8.dll` into the game
     folder **on both machines**.
3. Start the game with **`SACoop.exe`** (the launcher), or with the shortcuts `SACoop - Heberger.cmd` (host) and
   `SACoop - Rejoindre.cmd` (join).
4. The host clicks **HOST**. The others type the host's IP address, click **JOIN**, then **READY**.
5. The host picks a new game or a save and clicks **START**. Everybody's game starts together.
   - Over the Internet, the host opens port **7800** (TCP and UDP) on their router, or you use a virtual LAN.
   - A player who arrives late uses **JOIN IN GAME**.
6. Check that the first line of `sacoop.log` says `SACoop 0.54.34-vcport (Zebra's forked repo) loaded`. If it does
   not, that machine is running another DLL.

> [!IMPORTANT]
> The launcher updates the mod by itself on every start. Use the `SACoop.exe` from **this fork** (its zip, or
> `build-exe.cmd` / `build.cmd`). One built from the original repository checks the original's releases and could
> replace the fork's `dinput8.dll`.

Uninstall: delete `dinput8.dll`, `SACoop.exe`, the `SACoop` folder, `sacoop*.ini`, `sacoop.log` and `logs\`.

## Updating to the latest version

From your clone (for example `C:\sacoop`), in PowerShell, with this fork as the `fork` remote:

```powershell
cd C:\sacoop
git pull fork vcport
.\build-dll.cmd
```

Then copy `build\dinput8.dll` into the game folder **on both PCs**.

## What this fork adds

The original is Parricidium's mod at `0.48.4-prealpha`. Everything here was added on top of it, in `0.49.0-vcport` to
`0.54.34-vcport`. Per-version detail is in [`docs/CHANGELOG.md`](docs/CHANGELOG.md), and technical notes are in
[`docs/PORT-VCCOOP.md`](docs/PORT-VCCOOP.md).

> [!NOTE]
> **New in 0.54.9 to 0.54.34** is marked with 🆕. Most of it has been tried by two players; the newest items
> (Ryder in the van, boxes moved by the guest, pushing other players' cars) still need a real test.

### 🎬 Missions and cutscenes

- **Only the host starts story missions.** A guest who walks into a marker starts nothing, but still sees the marker.
- **The guest sees the whole mission:**
  - 🆕 The **mission title** at the start.
  - 🆕 The **subtitles and dialogue**: what the host and their passengers say, including lines from the mission's helper
    scripts and phone calls.
  - The mission **timer, counters and health bars** (Ryder's health, crates left), 🆕 without blinking.
  - The objective blips, matching the host's map (no stray mission letters or contact icons).
- **Cutscenes:**
  - 🆕 Interior cutscenes show the interior, and a guest inside sees an outdoor cutscene.
  - 🆕 The scenery loads where the camera looks, and the cutscene waits for it before starting.
  - 🆕 The guest's cutscene is not cut short when the host lets theirs play to the end.
  - Doubles of the other players are hidden during the scene.
- 🆕 **Phone calls:** the host's call rings for the guest too, and the guest's call starts when the host's does.
- 🆕 **"Mission cancelled by \<name\>"** pops up for everybody.
- 🆕 **Mission objects match:** crates and boxes the mission creates are in the same place, and seen, on every screen;
  boxes a guest moves (forklift, car, on foot) move for the host too.
- 🆕 **Mission objects:** a guest's shots count, even from far away (the control panel and switches of Robbing Uncle Sam).
- 🆕 **Mission characters** no longer fall over on the spot in scripted scenes, and ride where the mission puts them
  (Ryder standing in the back of the van).
- **F10 > Tools > SKIP CUTSCENE** and **STOP MISSION** for when something gets stuck.
- Fixes for Home Invasion, new games and interiors.

### 🚗 Vehicles

- 🆕 **Mission vehicle offer:** instead of appearing at once, the guest's copy of the mission vehicle is offered at the
  top of the screen. Press **Y** to spawn it where you stand, **N** to decline (10 s to choose, then it is a no).
- 🆕 **Cars push each other:** hitting another player's car, or the host's traffic and police, pushes it instead of
  stopping you dead; ramming the police gives a star.
- 🆕 **Passenger drive-bys:** passengers can shoot from the car, and never hurt the others inside.
- 🆕 **Nitro flame** shows on the other players' screens.
- 🆕 **Extras** match (van adverts and other optional parts).
- **Colours, paintjobs and upgrades** follow the owner (Pay'n'Spray, TransFender), 🆕 including hydraulics.
- 🆕 **Getting in:** no more double entry, no more both players jacking each other, no more feet through the floor.
- 🆕 A passenger follows the driver into **garages and TransFender**.
- **Radio:** any player in a shared car can change the station, and everybody ends on the same one.
- Spawned and parked vehicles stay for everybody; copies no longer appear inside a player or on top of local traffic.
- Helicopter rotors and plane propellers turn.

### 🔫 Combat and voices

- 🆕 **No lock-on to other players:** aiming picks the enemies, not your friend standing next to them (free aim still
  hits them with friendly fire on). Setting `TargetPlayers`.
- 🆕 **Firing animation and muzzle flash** for the other players' shots.
- 🆕 **Exact hits:** shots are replayed where they really landed, so small targets take one bullet.
- 🆕 **Speech sync:** what players and shared pedestrians say (remarks, pain grunts, dialogue) is the same line, at the
  same moment, at the same volume, on every PC.
- 🆕 **Wanted level clears for everybody** when any player dies.
- **Weapons come back** after a death, with their ammo.
- 🆕 **Weapon pickups for each player:** a gun picked up by one player stays there for the others.

### 🛠️ F10 > Tools

| Button | What it does |
|--------|--------------|
| 🆕 **TELEPORT TO WAYPOINT** | Takes you to the map waypoint, held in the air until the ground loads (your car comes too) |
| 🆕 **SUICIDE** | Kills your own character |
| 🆕 **REMOVE ALL MY WEAPONS** | Empties your weapon slots |
| 🆕 **Money field** | Click it, type an amount, press Enter or SET |
| **SKIP CUTSCENE** | The game's own skip; a second click forces the end and gives the controls back |
| **STOP MISSION** | Two clicks within 6 s; the mission ends cleanly, as if failed (a guest's click asks the host) |
| **REMOVE MY LAST VEHICLE / ALL MY VEHICLES** | Removes the cars you spawned, on every screen |

Each player only changes their own character, weapons and money.

### 🌍 A world that matches

- 🆕 **Garage doors** open and close the same for everybody.
- **Trains** run the same on every PC: same place, speed and stations. Whoever drives one owns it.
- **Weather and sky** no longer flicker on the guest.
- **Radio** follows the host's station, song, position and adverts (custom User Tracks too, if both libraries match).
- **Radar blips** of the host are copied to the guests.
- 🆕 The pause-menu **map** lines up with its blips on wide screens.

### 👥 Players

- **Ping** on the F5 board and in F10 > Players.
- **Go to a player** from anywhere, interiors included, and **go back to where you died**.
- **Endless-fall rescue** for a player under the world.
- A **pause indicator** next to the name of a player in the pause menu.
- No more players stuck in the falling animation after a teleport or leaving a safe house.

### ⚡ Smoother and more stable

- **Interpolation:** other players, cars and mission characters are shown between real received states (about 110 ms
  in the past), placed after the game's physics, so they no longer jitter.
- **Frozen vehicle copies:** your physics can no longer push someone else's car.
- **Precise clock** for what is sent and played back (driving no longer looks jerky).
- **World sanitizer:** absurd positions are caught before they crash the game.
- **30 FPS by default** (stable); 60 FPS with frame-rate fixes is still an option.
- **Crashes fixed:** vehicle model pool filling up (Sweet's Girl), a crate exploding next to a car and 🆕 a host crash in the van (Robbing Uncle Sam),
  the Display Setup > Advanced menu, a host address typed as `address:port`.
- **Alt+Tab** gives the mouse back at once; **Alt+Enter** switches between borderless and windowed.

### 🔧 Fork tooling and diagnostics

- The launcher updates from **this** repository's releases and shows "Zebra's forked repo".
- `build-dll.cmd` and `build-exe.cmd` find Visual Studio 2019 to 2026 by themselves.
- `dist\make-release.ps1` publishes to this fork by default.
- Many log lines (`parole :`, `garage :`, `radio:`, `death:`, `network:`, `teleportation`...) so a problem can be traced
  from the two `sacoop.log` files, plus a 10 s load report and an optional profiler.

### 📜 Version history

| Versions | Highlights |
|----------|------------|
| 0.49 | Smoothness layer from VCCoop: interpolation, frozen copies, sanitizer, extended mission mirror, 30 FPS |
| 0.50 to 0.53 | Radar blips, vehicles that stay, pause indicator, go to player, where I died, fall rescue, ping, radio sync, weapons kept on death, trains |
| 0.54.0 to 0.54.8 | Host-only missions, skip cutscene, stop mission, mission HUD, colours / upgrades, Home Invasion fixes, crash fixes |
| 0.54.9 to 0.54.12 | Wanted level cleared on death, interior cutscenes, suicide / weapons / money tools |
| 0.54.13 to 0.54.18 | Speech sync, waypoint teleport, extras, entry fixes, phone calls, garage doors, drive-bys, nitro |
| 0.54.19 to 0.54.23 | Mission objects for the guest, HUD without blinking, mission title, vehicle offer (Y/N), exact hit points |
| 0.54.24 to 0.54.34 | Firing animation, cutscene fixes, helper-script dialogue, no lock-on to players, mission objects synced, weapon pickups for each player, Ryder in the van, pushing copies, crash fixes |

**Network versions:** both players must use the same build. The last network changes were in 0.54.13, 0.54.15, 0.54.18,
0.54.26, 0.54.29, 0.54.30 and **0.54.31** (network version 31).

## What the original mod does

Everything below comes from Parricidium's original SACoop and still works.

- Host and join over UDP (default port 7800), up to 4 players.
- Each player sees the others as **CJ wearing that player's own clothes** (shops and wardrobe included), walking,
  running and sprinting with the game's animations, in the right place. A pedestrian model can be picked instead.
- **Shared vehicles:** whoever drives a car sends it to the others, who see it move with that player's CJ at the
  wheel (or as a passenger). Getting into someone else's car and driving off hands it over to you.
  Everybody sees the others open the door and get in or out, and the car's damage (doors, bumpers, lights,
  tyres, smoke, fire, explosion), siren and repairs.
- Each player's **name above their head** and a **radar blip** in their colour.
- **Same time and weather** for everybody (the host's).
- **Mission vehicles for everybody:** when the host takes a mission vehicle (the bikes of Big Smoke's mission...),
  each guest nearby with no free seat in it gets the same vehicle next to them (or a reminder to ride along with G).
- **Police for everybody:** one wanted level for the group (the highest; losing the police clears it for all), and
  the host's cops also hunt down wanted guests near the host (their hits land on the guest).
- **Merged street life:** the host populates the streets around them (110 m) and everybody sees the same
  pedestrians and traffic there; further out, the nearest player populates. A guest a bit away from the host keeps
  live streets on their side, with nothing doubled or vanishing in front of anyone.
- **Weapons and shots:** everybody sees the weapon in each player's hand and their shots. Players can hurt each
  other (bullets, punches, cars; *Friendly fire* option, each player chooses), and a player who dies falls for
  everybody, then comes back at the hospital.
- **Shared story missions:** missions run on the host, everybody plays them. The other players see the mission
  characters (Sweet, Big Smoke... with their weapons, in their cars), vehicles and objects, the **cutscenes** (skipped
  together when the host skips), mission texts, help boxes, radar markers, dialogues, fades and mission cameras. They
  can fight alongside the host: their hits on mission characters count, and mission enemies hurt them. **Any player
  can move the mission on**: reaching a checkpoint or getting into the mission car counts for everybody, and
  checkpoint markers show for all. At mission start, far-away players are brought behind the host. **Shared story
  progress:** what the host's missions unlock is sent to everybody, and money earned in missions goes to each player.
  A player joining in the middle of a mission gets its markers and texts. **Shared save:** when the host saves, the
  save goes to every player's same slot. Side activities stay local to each player.
- The pause menu no longer freezes the world while other players are connected, and switching windows does not
  pause the game.
- **Shared mods:** put car, weapon or character mods (`.dff`, `.txd`, handling lines...) in `SACoop\mods`; the host
  sends them to the guests in the launcher lobby, and everybody plays with the host's mods.
- **COOP menu in the game** (main and pause menus): host, join an address, change your name, see the session.
- **Original game, further** (launcher VIDEO tab or F10 > DISPLAY, applied right away): draw distance up to 250%
  (detailed scenery, horizon and fog further away), population area (how far pedestrians and cars appear) and density,
  anisotropic filtering.
- **Modern rendering** (optional): ambient occlusion (corners, wall bases, under cars darker) and FXAA anti-aliasing.
- **Widescreen:** the 3D, the HUD (radar, icons, texts) and the menus keep their proportions on wide screens, with a
  wider field of view (Hor+). Borderless mode picks your screen's resolution by itself.
- **60 FPS option:** the original game's frame-rate bugs (swimming, braking, doors, helicopter rotors, pushing cars...)
  are fixed with the fixes of [Framerate Vigilante](https://github.com/GTAmodding/FramerateVigilante) (MIT), ported
  into SACoop, with automatic 30/50 FPS caps where the game still needs them (pool, girlfriend, DRUGS1 indoors).
  Set `ImagesParSeconde=60` to use it; the default is 30.
- Windowed or borderless play, logos and intro videos skipped.
- Settings and saves kept in the game folder, apart from your solo saves.
- Two copies of the game can run on the same PC (the game normally refuses).

## Keys

| Key | What it does |
|-----|--------------|
| **G** (also **F**) | Ride as a passenger in another player's car |
| **Y** / **N** | Accept or decline the mission vehicle offered to a guest |
| **T** | Chat (Enter to send; `/join` brings you back next to the host) |
| **F5** (hold) | Players board: name, health, distance and ping |
| **F6** | First-person view on foot |
| **F10** | In-game menu (mouse): **Players**, **Vehicles**, **Tools**, **Outfit**, and for the host **World** and **Host** |
| **F1** | Help (keys, tips) |
| **Alt+Enter** | Borderless full screen <-> window |

## Settings (`sacoop.ini`)

The file is next to `gta_sa.exe`, section `[SACoop]`. Both players should use the same values for the network and
sync options. **The key names are still the original French ones**; they are explained here.

| Key (default) | What it does |
|---------------|--------------|
| `Fenetre=2` | Window mode: 2 = borderless fullscreen (recommended), 1 = windowed, 0 = the game's own fullscreen |
| `ImagesParSeconde=30` | Frame rate. 30 = smooth and stable (like VCCoop); 60 turns the frame-rate corrections back on (more fragile). Use the same value on both PCs |
| `SansIntro=1` | Skip the logos and the intro video |
| `GrandEcran=1` | Widescreen: 3D with the screen's proportions and a wider field of view |
| `SauvegardesLocales=1` | Settings and saves in the game folder, apart from your solo saves |
| `Tenue=0` | Your outfit: 0 = CJ with your clothes, 1 to 299 = a pedestrian of the game |
| `TirAmi=1` | Friendly fire: 1 = other players can hurt you, 0 = no (each player chooses for themselves) |
| `WeaponPickupsEach=1` | 1 = a weapon pickup taken by one player stays for the others, 0 = one for the whole group |
| `TargetPlayers=0` | Lock-on: 0 = the other players can never be locked on to (aiming picks the enemies), 1 = like the original game |
| `VuePremierePersonne=1`, `ToucheVue=F6` | First-person view on foot, and its key |
| `OcclusionAmbiante=1`, `Anticrenelage=1` | Modern rendering: ambient occlusion and FXAA |
| `ModsPartages=1`, `MemoireStreaming=256` | Shared mods (folder `SACoop\mods`), and the streaming memory in MB when mods are loaded |
| `RecherchePartagee=1` | One wanted level for everybody (the highest) |
| `PoliceHote=1` | The host's police also chase wanted guests |
| `Port=7800` | UDP port of the host (open it on the router to play over the Internet) |
| `Interpolation=1`, `DelaiInterpolation=110` | Show other players and vehicles ~110 ms in the past, between received states |
| `PlacementPietons=1`, `PlacementVehicules=1` | Place remote characters / vehicle copies right after the game's physics |
| `GelerCopies=1` | Local physics cannot push vehicle copies |
| `GardeFou=1` | World sanitizer: fixes absurd (NaN) vehicles and characters before they crash the game |
| `MiroirEtendu=1` | ~120 more mission commands repeated on the guest |
| `CacherEnCinematique=1` | Hide the network doubles during cutscenes |
| `MarqueursRadar=1` | The host's coordinate and contact blips are copied to the guests |
| `SecoursChute=1` | Fall rescue: a player under the world, or falling endlessly, is put back on a safe spot |
| `ArmesMort=1` | Weapons are kept on death (given back after the respawn). 0 = lost, like the original game |
| `GuestWeatherGuard=1` | The guest never changes the weather by itself (no more sky colour flicker) |
| `RadioSync=1` | Guests follow the host's radio (station, track, position, adverts) |
| `SyncGarages=1` | Garage doors open and close the same for everybody (the nearest player leads) |
| `SyncParole=1` | What players and shared pedestrians say (remarks, pain) is the same line at the same moment on every PC |
| `GuestMissionLock=1` | A guest cannot start a story mission by walking into its marker (the marker is still drawn); only the host starts missions. 0 = like before |
| `TrainSync=1` | The host's trains run the same on every guest's rails; the guest's own random trains are off. 0 = every PC runs its own trains |
| `Profil=0` | 1 = sampling profiler for stutter hunting (the log always carries the 10 s load report) |
| `AffiniteCoeur=0` | CPU core the game is limited to at launch (0 = first core; fixes the drifting mouse cursor), -1 = no restriction |

If the build is worse than before, switch the new keys off one at a time (set them to `0`), starting with
`GardeFou`, then `Interpolation` with `PlacementVehicules`, `GelerCopies`, `PlacementPietons`, `MiroirEtendu`.
`docs/PORT-VCCOOP.md` has the details.

## The launcher

`SACoop.exe` updates the mod by itself from this repository's Releases page on every start, checks your game
version, and starts the game as host or guest. Its tabs hold the lobby, your outfit (3D preview; CJ with his clothes
or any of the game's ~265 pedestrians: gangs, police, everyone in the streets), your mods (on/off, 3D preview), the
options and the release notes; the round button at the top right of the left panel opens the logs of your last 50
sessions (to send when something goes wrong).

**Lobby:** HOST opens a lobby; the others JOIN it with the host's address and click READY. The host picks a new game
or one of their saves, then START launches everybody's game at once (sounds when a player joins, leaves or gets
ready).

<p align="center">
  <img src="docs/img/lanceur.png" width="49%" alt="SACoop launcher">
  <img src="docs/img/salon.png" width="49%" alt="SACoop launcher lobby">
</p>
## Testing checklist and sending logs

Play a session and tick these off. Newest first:

- [ ] Both `sacoop.log` files start with the same version (`0.54.34-vcport (Zebra's forked repo)`).
- [ ] **Mission dialogue in a car:** the guest hears the host's passengers and the mission's lines.
- [ ] **Cutscenes:** the guest sees the scenery loaded (indoors and outdoors) and the cutscene is not cut short.
- [ ] **Mission objects:** the guest sees the crates of Home Invasion and the boxes of Robbing Uncle Sam, in the same place.
- [ ] **Weapon pickups:** one player takes a gun; it is still there for the other.
- [ ] **Lock-on:** aim at an enemy standing next to the other player: the lock goes to the enemy, never to the player.
- [ ] **Firing:** the other player's shots show the firing animation and muzzle flash.
- [ ] **Mission title and subtitles** show on the guest.
- [ ] **Vehicle offer:** the guest gets the Y/N prompt at the top of the screen; Y spawns the car, N declines.
- [ ] **Garage doors** open for both players; **drive-bys** from the passenger seat work; **nitro** flames show.
- [ ] **Phone calls** ring for both, and the guest's call waits for the host's.
- [ ] **F10 > Tools:** waypoint teleport, suicide, remove weapons, money; skip cutscene, stop mission, remove vehicles.
- [ ] **Speech:** pain grunts and remarks are heard the same on both PCs.
- [ ] **Death:** the wanted level clears for everybody, and your weapons come back after the respawn.
- [ ] **Trains**, **radio** (station, then a song change), **sky** at dusk, **ping** in F5.

When something goes wrong, send **both players' `sacoop.log`** (next to `gta_sa.exe`; older sessions are in `logs\`,
or use the launcher's logs button) and say roughly when it happened.

## Building from source

You need Visual Studio 2019 to 2026 with the **Desktop development with C++** workload (x86, `/MT`), nothing else.

| Script | What it builds | Output |
|--------|----------------|--------|
| `build-dll.cmd` | the mod only (finds Visual Studio itself; log in `build\build.log`) | `build\dinput8.dll` |
| `build-exe.cmd` | the launcher only (log in `build\build-exe.log`) | `build\SACoop.exe` |
| `build.cmd` | both | the two files above |

Copy `build\dinput8.dll` over `dinput8.dll` in the game folder, **on both machines**, and `build\SACoop.exe` next to it
if you changed the launcher. `launcher\make-art.ps1` draws the launcher's background and icon. `run\` holds the
two-instance test scripts, `re\` the command-line Ghidra tools used to read the game.

The game executable and any decompiled code are **not** in this repository.

## Publishing your own build to GitHub

From the folder of your clone (for example `C:\sacoop`), with the fork as the `fork` remote:

```bat
git remote -v
git add -A
git commit -m "Describe what changed"
git push -u fork vcport
```

To make this README and the code the default view of the fork, also update its `main` branch:

```bat
git checkout main
git merge vcport
git push fork main
git checkout vcport
```

To publish a release that the launcher can download (zip with `dinput8.dll`, `SACoop.exe` and the config files),
with the [GitHub CLI](https://cli.github.com/) installed and logged in (`gh auth login`):

```powershell
.\dist\make-release.ps1 -Version 0.54.34-vcport -Publier -Notes "What is new in this version"
```

Without the GitHub CLI, run the script without `-Publier`, then on GitHub use **Releases > Draft a new release**, tag
`v0.54.34-vcport`, tick *pre-release*, and attach `dist\out\SACoop-0.54.34-vcport.zip`.

## Known gaps and roadmap

1. Story missions: testing every mission with real players.
2. Police cars ramming guests' cars (cops on foot already chase them).
3. More modern rendering (sun shadows, reflections, as in VCCoop), all optional.
4. Radio: the track and its position follow the host's radio, so two guests in the same car while the host is on foot
   are on the same station but not necessarily on the same song. Radio sync is a first version: expect a small hiccup now and then when a song changes.
5. Train sync is a first version: a player inside a train is not seated, and the game only builds trains near the host.
6. The `sacoop.ini` key names, the in-game log lines and some code comments are still in French; the in-game menus
   and the launcher show French or English by your system language.
7. Not ported from VCCoop on purpose: ragdoll sync, model-loading commands, `CLEAR_AREA`, roadblocks.
8. The host's mission characters are copies on the guest, placed from the host's state: the guest cannot push them.
   (Vehicles and mission objects can be pushed since 0.54.31.)
9. Ryder attached in the van, boxes moved by the guest, pushing copies and weapon pickups for each player (0.54.29-31)
   still need a real test.

## Credits and license

- **[Parricidium](https://github.com/Parricidium)**: creator of SACoop and VCCoop. Thank you!
- Zebra and their brother: testing, bug reports and the `vcport` fork.
- San Andreas 1.0 US addresses: read with Ghidra in the game executable, cross-checked with
  [plugin-sdk](https://github.com/DK22Pac/plugin-sdk) (DK22Pac) and the
  [gta-reversed](https://github.com/gta-reversed/gta-reversed) project.
- 60 FPS fixes: [Framerate Vigilante](https://github.com/GTAmodding/FramerateVigilante) (MIT).
- Grand Theft Auto and San Andreas are trademarks of Rockstar Games / Take-Two Interactive. This is an unofficial,
  non-commercial fan project, not affiliated with them. You must own the game.
- License: not chosen yet.
