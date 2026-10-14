# Port of VCCoop's robustness layer to SACoop (0.50.0-vcport, NET_VERSION 20)

Both players must run the **same build** (the wire format of vehicle and ped messages gained a `time` field).

## Why

VCCoop is smooth because remote entities are *interpolated from timestamped states* and *placed right after the
game's physics step*. SACoop used to place them after the whole frame was rendered and extrapolate/snap them, ran at
60 FPS with framerate corrections, and mirrored fewer mission opcodes. This port brings the VC design over.

## Modules (each has a kill-switch in `sacoop.ini`, section `[SACoop]`)

| Key (default)             | What it does | If it misbehaves |
|---------------------------|--------------|------------------|
| `ImagesParSeconde=30`     | 30 FPS: the original game timing, the Framerate-Vigilante-style corrections in `fps.cpp` are skipped (`maxFps <= 30`). | try 60 again |
| `Interpolation=1`         | Hooks the `CGame::Process` call inside `CIdle` (0x53E920 → 0x53BEE0, found at run time, must be unique). Adds clock offsets per sender, a track of the last 10 states per entity, and Hermite/linear sampling `DelaiInterpolation` ms (110) in the past. | `0` = old behaviour (nothing hooked unless `GardeFou=1`) |
| `PlacementPietons=1`      | Remote players and mission peds (guest side) are placed after physics: position + heading written, then `CEntity::UpdateRwMatrix`/`UpdateRwFrame` (0x446F90 / 0x532B00). The GoToPoint task stays, so the walk animation still plays. | `0` = glide towards target as before |
| `PlacementVehicules=1`    | Remote vehicle copies are placed before **and** after the physics step (occupants and passenger camera stay in step). | `0` = old `UpdateCopy` |
| `GelerCopies=1`           | Sets `bDontApplySpeed` (CPhysical flags +0x40, bit 13) on copies so local physics cannot push them; cleared when the local player drives it or updates go stale (1.5 s). | `0` |
| `GardeFou=1`              | Before each `CGame::Process`, vehicles and peds with NaN / absurd matrix, position or speed get their last sane pose back (or are laid flat), speeds zeroed, logged as `sanitizer: ...` (first 60). Targets the world-sector crash seen on VC guests. | `0` |
| `MiroirEtendu=1`          | ~120 more mission opcodes mirrored from host to guest (texts, blips, camera/fade, mission audio, cutscene helpers, roads, garages, objects, pickups). Blip modifications no longer overwrite the creation entry kept for late joiners. | `0` |
| `CacherEnCinematique=1`   | Pantin de l'autre joueur, copies de PNJ de mission et copies de vehicules sont caches (bIsVisible) pendant une cinematique (la notre ou la sienne, drapeau `gear & 0x10` du message d'etat) : sinon ils chevauchent les acteurs de la scene et scintillent. | `0` |
| `MarqueursRadar=1`        | L'hote envoie l'etat de ses marqueurs du radar de type coordonnees / point de contact (`MSG`/`RL_BLIPS`, 2 fois par seconde au plus, toutes les 5 s sinon) ; l'invite cree, retire et met a jour les siens (miroir.cpp, `BlipsReconcile`). Couvre les marqueurs poses par le script principal, que le miroir de commandes (scripts de mission seulement) ne voyait pas. | `0` |
| `SecoursChute=1`          | Un joueur sous le monde (z < -150 m pendant 0,5 s) est remis a sa derniere position sure (ou aupres de l'hote). Journal : `chute :`. | `0` |

## Bisect order if the build is worse than before

1. `ImagesParSeconde=30` alone (copy the old DLL back) — is it already better?
2. New DLL with all new keys `=0` → must behave like the old build (except the 30 FPS default).
3. Turn on one key at a time in this order: `GardeFou`, `Interpolation` + `PlacementVehicules`, `GelerCopies`,
   `Interpolation` + `PlacementPietons`, `MiroirEtendu`.
4. Send `sacoop.log` from **both** players. Relevant lines: `interpolation: hooked ...` (or `not found ...`),
   `sanitizer: ...`, mirror warnings.

## Not ported (on purpose)

Ragdoll sync (VC bone tags do not map to SA), on-screen timers/counters (need script variables), model-loading ops,
`CLEAR_AREA`, trains, roadblocks, per-frame text-draw commands, VC cutscene-object opcodes (NOPs in SA).

## Build

`build-dll.cmd` finds Visual Studio (2019–2026), builds `build\dinput8.dll` (Win32, /MT) and writes `build\build.log`.
Copy that DLL over `dinput8.dll` in the San Andreas folder, on both machines.


## Ajouts du 14/10 (retours du test Home Invasion)

- **Marqueurs du radar** (`MarqueursRadar`) : voir ci-dessus. Journal : `radar : ...` (hote : nombre de marqueurs partages ; invite : crees / retires / mis a jour).
- **Meteo** : `WeatherWatch` (charge.cpp) journalise chaque changement de meteo (`meteo :`) ; l'invite ne reprend plus la meteo/l'heure de l'hote pendant une cinematique.
- **Vehicules poses** : un vehicule pose par le menu F10 (`VehicleSpawnedHere`) ou par un code de triche (detecte : apparu a moins de 12 m du joueur a pied, sans conducteur) devient un vehicule reseau envoye tant qu'il existe. Un vehicule gare reste envoye (1 fois par seconde) tant qu'un autre joueur est a moins de 350 m, au lieu de disparaitre chez lui 10 s plus tard.
- **Circulation de l'invite** : quand une copie de vehicule de l'hote va naitre, une voiture ordinaire de chez l'invite deja a cet endroit (a 6 m) est retiree avec ses occupants (journal : `circulation : voiture locale retiree`) ; un vehicule d'un joueur ou de mission fait attendre la copie (4 s au plus).
- **Menu pause** : `gear & 0x20` dans l'etat du joueur. Chez les autres, son pantin passe a l'allure immobile (vitesse nulle, pas de visee, rien a extrapoler) et son nom affiche `(paused)` / `(en pause)`.
- **Affinite** (`AffiniteCoeur=0`) : le processus du jeu est limite au coeur 0 des le lancement (comme Gestionnaire des taches > Definir l'affinite) : corrige le curseur de souris qui derape. `-1` = aucune restriction. Journal : `affinite :`.

## Ajouts du 14/10 (suite : sauvegarde, chute, mort)

- **Disquette de sauvegarde** (modele 1277) : jamais partagee (`events.cpp`, `h_PickupUpdate` / `ApplyPickup`). Avant, celui qui la ramassait ouvrait aussi le menu de sauvegarde des autres. Journal : `pickup : disquette de sauvegarde ...`.
- **Teleportation** (`TeleportPed`, conditions.cpp) : interieur choisi et lie au joueur (04BB, 0860), decor charge sur place (LOAD_SCENE 03CB), sortie du vehicule, vitesse nulle. Utilisee par F10 > ALLER VERS (l'hote arrivait dans la maison de l'invite sans l'interieur : chute de 1014 a 936, mort), "Ou je suis mort", le suivi d'invite en mission et le secours de chute. Journal : `teleportation (...)`.
- **Secours de chute** : en plus de z < -150, une chute sans fin (en l'air depuis 1,2 s, plus de 20 m dans un interieur ou plus de 40 m depuis 500+ d'altitude, sans vehicule / jetpack / parachute) remet le joueur a sa derniere position sure. Journal : `chute : chute sans fin ...`.
- **Armes a la mort** (`ArmesMort=1`) : les armes tenues sont retenues a chaque image ; a la mort elles sont posees en cercle (0,9 m) autour du corps par CREATE_PICKUP_WITH_AMMO (032B, type 3) du script fantome : pas rejoue chez les autres, et `h_PickupUpdate` n'annonce pas leur ramassage. Retirees apres 10 minutes. Journal : `mort : ...`.
- **Menu F10 > JOUEURS > "Ou je suis mort"** : place le joueur sur son dernier deces (interieur compris).
- **Touche F10 / F1 maintenue** : seul le premier appui compte (le menu s'ouvrait et se fermait toutes les 30 ms).
- **Journal** : `menu du jeu : ouvert/ferme (joueur en x y z, zone, vie, etat)` a chaque ouverture/fermeture du menu pause ou sauvegarde.

## Additions of 14/10 (evening, 0.52.0-vcport)

- **Weather colours** (`GuestWeatherGuard=1`): `h_WeatherUpdate` (coop.cpp) replaces the call to `CWeather::Update` (0x72B850) in `CGame::Process`. On a guest it zeroes `CWeather::InterpolationValue` (0xC8130C) first, so the game's own hour-wrap logic never starts a weather change by itself; the weather and hour still come from the host (`MsgWorld`). `OnWorld` no longer writes the blend value. Log: `weather guard installed ...`.
- **Guest mission blips**: `BlipsMissionState` (mirror.cpp) - while the host is in a mission the guest strictly mirrors the host's blip list (the guest's own mission letters are removed).
- **Ping**: a dedicated reception thread (net.cpp `RxThread`) reads the UDP socket, answers `MSG_PING` at once and times `MSG_PONG` with `QueryPerformanceCounter` (before: handled once per game frame, +33 ms on each side at the 30 FPS cap, and `GetTickCount` has a 15 ms step). Packets go into a ring that `NetPoll` drains on the game thread in order. The host pings every guest once a second and shares the numbers in `MsgWorld.ping[]` (NET_VERSION 21). Shown in the F5 board (`DrawPlayers`) and the F10 Players tab; `NetPing(player)`. Log: `network: ping a b c d ms ...`.
- **Falling animation after interiors / teleports**: `LocalAirState` (coop.cpp). `CPed::bIsStanding` (+0x46C bit 0) is not refreshed for a ped at rest, so after a teleport it stays 0 and the other players' copy played the fall animation until the player moved. Being "in the air" now needs real vertical speed (> 0.03, kept 400 ms), the ped is told he is standing again for 2 s after a relocation (teleport, area change, > 4 m jump) and the game's in-air reaction (`CTaskComplexInAirAndLand`, type 240) is dropped. On the receiving side the copy's standing flag is set every frame on foot (the game raises `CEventInAir` whenever `!bIsStanding`; the response task exists only in that case) and when the air state ends. Log: `relocated: ...`.
- **Host mission vehicle copy**: the guest's own copy (`GuestMissionVehicle`) is registered as a network vehicle the moment it is created (`LocalVehicleId`), so the host sees it appear at once.
- **Radio sync** (`RadioSync=1`, radio.cpp, `MSG_RADIO`): see the header of radio.cpp. Host sends the track being played, the position and the queue twice a second; a guest on the same station restarts its radio on the host's queue and position when it differs for 1.5 s. Custom "User Tracks": only when both libraries have the same signature (hash of ordered file names + sizes from `sa-utrax.dat` / `sa-ufiles.dat`). Log lines start with `radio:`.
- **Weapons kept on death** (`ArmesMort=1`, events.cpp, replaces the 0.51.0 weapon drop): the weapons held are remembered on every frame; at death the list is kept, and 1.5 s after the player is alive again (no cutscene) `RestoreWeapons` gives back what is missing with GIVE_WEAPON_TO_CHAR (01B2, ghost script: not mirrored). A weapon of the same type is only topped up with the missing ammo. F10 > Players > "Where I died" still goes to the death spot. Log: `death: ...`.
- **Fork**: `SACOOP_FORK` ("Zebra's forked repo") in the log's first line, the F10 header and the launcher; the launcher updates from `zebra032-cyber/SACoop` (an empty release list counts as "up to date", not "offline"); `dist/make-release.ps1 -Repo` (default the fork); `build-exe.cmd` builds only the launcher.
