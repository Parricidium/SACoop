# Changelog - Zebra's forked repo

This is the `vcport` line of [Parricidium/SACoop](https://github.com/Parricidium/SACoop). The original's last release
is `0.48.4-prealpha`; everything below was added on top of it. Setting names (`sacoop.ini`) are described in the
README. Technical notes for the VCCoop port are in [`PORT-VCCOOP.md`](PORT-VCCOOP.md).

Versions marked *untested* were written and compiled but not yet played for real by two players.

## 0.54.13-vcport (network version 25: a new message, so 0.54.12 and older cannot join)

Asked by the player on 15/10: the pedestrians' voice lines did not match between host and guest (other line, other
moment), and the players' own lines (a grunt when hurt...) were not heard by the other player. The speech code was
checked in the player's gta_sa.exe 1.0 US (14,383,616 bytes, SHA-1 185b73fbceaa05d66452691fc0d15c8d61b92a7e).

- **Speech sync** (new `speech.cpp`, `MSG_SPEECH`, setting `SyncParole=1`). A ped's line is chosen at random by
  `CAEPedSpeechAudioEntity` (CPed +0x294): `AddSayEvent` (0x4E6550) and the generic voices through the chooser 0x4E5920,
  pain (event 0x35, 0x4E4F70) through its own; all three end in `LoadAndPlaySpeech` (0x4E4840), which loads sound +0xA4 of
  bank +0xA6 into a speech slot. Its three calls (0x4E50D2, 0x4E62EB, 0x4E68B8) now go through `SpeechPlay`: a line of
  the local player, or on the host of a ped it shares with a guest (`MissionPedId`), is sent with its bank, sound, context
  and voice type (relayed by the host). The receiver plays that exact sound on the copy (the player's puppet, the ped's
  copy) the way the game's pain path does: a free slot among the five (0x4E4950, the player's own slot 5 is never used),
  the game's speech or pain volume (+ the player's bonus), then `LoadAndPlaySpeech`. Not played when the copy is more than
  60 m away or still saying the previous line. The copies' own lines (random remarks, pain from a local hit) are dropped,
  and their "talking" flags undone. The three call sites are checked before patching; on another exe nothing is patched
  (`parole : appel inattendu ...`). Log: `parole :` (sent / played / no copy), 30 lines of each at most.

## 0.54.12-vcport (network version 24 unchanged)

Asked by the player on 15/10: three separate tools in F10 > Tools (panel.cpp). Each acts on the local player only.

- **SUICIDE**: armour to 0 and `SET_CHAR_HEALTH` 0 (0223): the game's own death (wasted, hospital), on foot or in a vehicle.
  The menu closes. The 0.54.10 rule applies: the death clears the wanted level of everybody. Log: `menu jeu : suicide`.
- **REMOVE ALL MY WEAPONS**: `REMOVE_ALL_CHAR_WEAPONS` (048F), run from the mod's own script so it is not mirrored to
  the other players. Log: `menu jeu : armes retirees`.
- **Money field**: shows the current money; click it, type digits (Backspace deletes, 9 digits at most), Enter or SET.
  The difference with `CPlayerInfo::m_nMoney` (0xB7CE50) is given with `ADD_SCORE` (0109), so the HUD counts to the new
  amount. While the field has the focus, keys go to it only. Log: `menu jeu : argent a -> b`.
- Note for whoever builds this: with the MinGW C++ headers installed (`g++-mingw-w64-i686`), the files that include
  `<string>` / `<vector>` (panel, ui, mods, savesync, thumbs) can be syntax-checked with clang too.

## 0.54.11-vcport (network version 24 unchanged)

From the player's report of 15/10: on the guest, cutscenes set in an interior show no interior, and cutscenes have no
voices.

- **Interior cutscenes** (mirror.cpp, `CutsceneVisibleArea`). Since 0.54.3 the guest only takes the host's zone
  (`SET_AREA_VISIBLE` 04BB / `LINK_CHAR_WITH_INTERIOR` 0860) together with a scripted move of the player, so that each
  player goes through doors by himself. An interior cutscene sets the visible area without moving anybody: the guest's
  cutscene played in area 0, in the street, and the interior never showed. Now, from the replayed `LOAD_CUTSCENE` to the
  end of the guest's cutscene, the host's 04BB (or the one the script chose up to 15 s before `LOAD_CUTSCENE`) is applied
  to the guest's visible area only; when the cutscene ends the visible area goes back to the guest ped's own zone (a
  scripted move afterwards sets both, as before). The watch ends after 90 s if no cutscene ran. Log:
  `miroir : cinematique, zone visible de l'invite a -> b (...)`, `miroir : fin de cinematique, zone visible ...`; the
  `cinematique de l'invite : debut` line now gives the visible area and the player's zone.
- **Radio sync paused during cutscenes** (radio.cpp). The speech of a cutscene is played on the same audio stream as the
  radio; a radio restart by the sync while a cutscene was loaded or playing could cut it. The guest's follow now waits
  while `CCutsceneMgr` has a cutscene loaded (0xB5F84C) or running (0xB5F851). This may not be the whole story of the
  missing voices: the guest's log of a cutscene without voices is needed.

## 0.54.10-vcport (network version 24 unchanged: plays with 0.54.8 and 0.54.9, but both players need it for the fix)

From the player's report of 15/10: the wanted level did not follow deaths.

- **A death clears the wanted level of everybody** (`SomeoneDied`, police.cpp, with `RecherchePartagee=1`). Before: a guest
  who died came back with no stars (his game clears them) while the host kept his level, and nothing told the guest's
  game about it again; when the host died, the guest stayed chased until the host respawned. Now, on the step any
  player goes from alive to dead (the local player's health, or a remote player's state), the host clears its level
  (CLEAR_WANTED_LEVEL, which also calls the police off) and ignores the guests' old levels for 5 s, and a guest clears his
  at once instead of waiting for the host's new level. Log: `police : ... est mort, plus de recherche pour personne`.
- The crash probe of 0.54.9 stays in.

## 0.54.9-vcport (diagnostic, network version 24 unchanged: plays with 0.54.8)

From the player's report of 15/10: away from the host, when the guest drives his own car into a lamp post, a pedestrian
or another car at high speed, the car stops dead and the thing it hit does not move (no lamp post knocked down, nobody
thrown). Fine while the host rides with him. The cause is not visible in the code: the host's copies (frozen with
`GelerCopies`) only exist near the host, and far from him the guest's traffic and street furniture are his own game's.

- **Crash probe** (`VehicleHitProbe`, vehicles.cpp, called right after `CGame::Process` in interp.cpp). When the local
  player's car loses more than half its speed in one physics step above ~70 km/h (40 lines at most, 1 per second), the log
  gets `choc : notre vehicule ...` (speed before / after, place, interior, distance to the host, its flags and mass,
  whether it is a network vehicle or a copy) and one `choc degats/contact :` line per entity touched (CPhysical +0xDC and
  the collided list +0xBC): type, model, `statique` (bIsStatic), `ATTEND-COLLISION` (bIsStaticWaitingForCollision: the
  game freezes things whose collision is not loaded), `GELE` (bDontApplySpeed: a frozen copy), mass, and whether it is a
  copy. The next fix depends on what these lines show.

## 0.54.8-vcport (untested, network version 24: two new messages, so 0.54.7 and older cannot join a 0.54.8 game)

Written from the host and client logs of the 0.54.7 test (Robbing Uncle Sam) and the player's list.

- **Crash of the guest when a box thrown by Ryder hit a car and exploded (Robbing Uncle Sam).** The guest's log ends in
  `CTaskSimpleCarFallOut` (the animation "fall out of the car"): the explosion makes the car's occupants fall out, the mod's
  mission-character copies get their event responses cleared every few frames (`ClearEventResponses`), which deletes the
  task in the middle of its animation, and the animation's finish callback (`FinishAnimFallOutCB`, `0x648EE0`) was left
  pointing at the freed task. `InstallTaskFixes` (game.cpp) patches that task's destructor (vtable `0x86EFD0`, slot 0): before
  the task dies it clears the callback of its animation if the callback still points at it. The patch checks the vtable
  and logs `correctif : CTaskSimpleCarFallOut ne laisse plus sa fin d'animation pointer sur elle`; if the game's code is not
  what it expects, nothing is patched.
- **Mission HUD for the guest (new file `msnhud.cpp`, `MSG_HUD`).** The timer and counters of a mission (Ryder's health bar,
  the crates left, the time left...) are filled in by the host's mission script, which the guest never runs, so he saw
  none. The host sends the game's `COnscreenTimer` (one clock, four counters: type, label key, colour, maximum and the value of
  the script variable each one points at) 4 times a second; the guest puts the same slots on his own timer, each pointing at a
  small buffer of the mod (a variable id is only an offset from `ScriptSpace`), so the game draws and ticks them itself.
  Slots his own game uses are left alone; they are emptied when the host's go off or after 1.5 s of silence. The label is
  the host's GXT key, shown when the guest's mission text has it.
- **Colours, paintjobs and upgrades follow the owner (new `MSG_VEHMOD`).** The colours were copied once, when a copy was
  made, so a car sprayed at Pay'n'Spray kept its old colour for the other player, and upgrades or paintjobs never travelled.
  The owner of a vehicle now sends its four colours, paintjob (`GetRemapIndex`) and upgrades (`m_anUpgrades`) when they
  change and every 3 s (6 s when idle, only when another player is within 350 m); a copy applies them one change a turn
  (`SetRemap`, `AddVehicleUpgrade`, `RemoveVehicleUpgrade`, loading the upgrade model first). A state the game refuses is
  not retried until the owner says something different. Log lines: `vehicle ...: colours / paintjob / upgrades changed`,
  `upgrade N added`.
- **F10 > Tools > STOP MISSION.** For a mission that is hopelessly stuck. Two clicks within 6 s (a guest's click asks the
  host). It does what the game's own `FAIL_CURRENT_MISSION` does: `CTheScripts::FailCurrentMission = 2`, which throws the
  mission script back to its own clean-up (`MISSION_HAS_FINISHED` / `MISSION_END`: mission entities released, controls and
  camera back); a running cutscene is ended first. No pass, no reward, the contact can start it again. Nothing happens
  (and a toast says so) when no mission is running.
- Not changed: "objects and characters are like brick walls for the guest". The host's mission characters and vehicles are
  copies placed from the host's state every frame, so the guest's own collisions cannot move them; making a hit move them
  needs the guest to take them over for a moment (as a vehicle already does when a player drives it). That is a larger change
  that cannot be tested without two players; to aim it, the exact case is needed (which object or character, what the
  guest did, what he expected).
- `SACoop\version.txt` of each PC is what the launcher shows (an old copy says `0.48.4-prealpha`, the original's last
  release); the first line of `sacoop.log` is the dll's real version.

## 0.54.7-vcport (untested, network version 23, same protocol as 0.54.0)

Written from the host and guest logs of the 0.54.6 test (Home Invasion, Sweet's Girl) and the player's report.

- **Crash in `CVehicleModelInfo::PreprocessHierarchy` (Sweet's Girl, "for either player").** Every loaded vehicle model
  takes one of the 50 `CVehicleStructure` of the game's pool; the 51st gets a null structure and the game dies (address
  `004C8F24`, access `00000030`, in the guest's log). The mod asked for each vehicle model with the "game required" flag, which
  keeps it in memory for good, and mirrored the host's traffic and mission cars, one copy per vehicle: the guest had 48
  different models loaded when it crashed. `RequestModel` now remembers the vehicle models it asked for; `VehiclesFrame`
  lets them go again (`CStreaming::SetModelIsDeletable`) 3 s after the request, when the copy exists (a model in use is
  never unloaded). A model that is not loaded yet waits (`VehicleModelRoom`) when 42 or more are, after asking the game to
  drop an unused one; the F10 spawner says so in a toast. A host crash in a cutscene that was not skipped may have had the
  same cause (cutscenes load vehicles too).
- **A vehicle spawned with F10 could not be removed once another player had got in.** The one who takes the wheel becomes
  the owner and goes on sending the vehicle; the spawner's "gone" notice was ignored by the owner, and the spawner's copy
  came back from the owner's next state message. A "gone" notice from the player who created the id (first byte of the id)
  is now obeyed by the owner too, and removed ids are remembered for 8 s so late state messages do not raise them again.
- **Death at the end of Home Invasion.** The guest was a passenger in the host's van; the host's clean-up of the garage
  forecourt removed the van, the guest's copy had no news for 3 s and was deleted with the guest inside (log: ten copies
  retired in the same millisecond, the player dead 15 ms later). The local player is now put out of a copy before it is
  deleted, and the cars cleared from a copy's spot never take the local player or a puppet with them as passengers.
- **Jerky driving, both ways (host driving seen by the guest, and the other way round).** The game's `GetTickCount` moves in
  steps of 15.6 ms (the log stamps are multiples of 15.625). The 33 ms send pace of the player and of vehicles therefore
  became every second frame at 30 images/s (15 per second, 20 at 60), and the timestamps used to interpolate were rounded
  to the same step. `NowMs()` (QueryPerformanceCounter) now stamps and paces the player, vehicle, mission character and
  train messages and drives the interpolation clock; the pace is 30 ms so that a frame at 30 or 60 images/s always passes.
- The black screen a guest may be left with after a cutscene (the host already sees clear) is undone after 1 s, not 2.
- Not changed: Sweet's Greenwood outside his house. The game's parked-car generators only re-create a car when the player is
  140 m or more away (except for 20 frames after a cutscene), and the mission scripts clear that yard (the guest's log shows
  the host's `CLEAR_AREA` on it); no coop code removes it. If it is still missing after a cutscene, the two logs are needed.

## 0.54.6-vcport (untested, network version 23, same protocol as 0.54.0)

Written from the log of the first 0.54.5 test on the brother's PC (internet join, host's public address).

- **`address:port` as the host address.** The launcher's lobby accepts `ip:port` and uses the port for its own connection,
  but starts the game with the text as typed (`-sacoop invite 61.245.130.116:7800`); the game gave it whole to
  `getaddrinfo`, which cannot resolve it: `reseau : adresse '61.245.130.116:7800' introuvable`. The guest's game then sat
  at the main menu (nothing connected, nothing said); "Start game" made a solo game started as a guest (the host's story is
  not run on a guest) and a black screen. `NetStart` now splits one `:port` off the address (the port after the colon wins
  over `Port` of the settings), the log line says the real name and port, and the error code is logged.
- An address that cannot be found now shows a message box (own thread, the game goes on) saying so and not to press "Start
  game"; the toast used for other network errors cannot show in the main menu.
- The in-game main-menu "join" box accepts `:` too.
- `SACoop\version.txt` is what the launcher shows and what the lobby compares between host and guest (not the dll's own
  version); the first line of `sacoop.log` is the real version of the dll.

## 0.54.5-vcport (untested, network version 23, same protocol as 0.54.0)

Written from the client log of the first 0.54.4 test, a new game (client joins while the host loads a new save: the client
cannot get out of the opening cutscene, then ends up "in the middle of nowhere"), and the host log of the same session.

- **Guest teleported to x = 0 (the void) by the host's scripted move.** `SetGuestArea` (0.54.3) runs the game's 04BB and
  0860 commands through the ghost script, which uses the same command buffer (`g_code`) as the replay in progress; it was
  called after the host's `SET_CHAR_COORDINATES` had been built there and before it ran, so the move ran with the area
  number as its x (client log: `... 0.0 -1261.9 23.9`, then falling, then only the F10 "go to player" helped). The buffer is
  now saved and restored around those commands. The same was true of the mission-text reload (`TextCheck`, 054C): fixed too.
- **"Could not skip the first cutscene".** The guest's own skip (Enter, Space, click, cross, or F10 > skip) only brings the
  cutscene to its end (`CCutsceneMgr::SkipCutscene`); it stays "running", screen frozen and controls cut, until
  `CLEAR_CUTSCENE` (02EA), which on the guest only comes with the host's script, in the order of the replay queue. The
  host had skipped 1.3 s after the guest's cutscene started, but the guest's queue was held up for 21 s by seven
  `CREATE_PICKUP_WITH_AMMO` (032B) commands whose model never loaded, 3 s each before being given up. Fixed twice over:
  a cutscene that the guest has skipped himself is cleared 0.3 s later (controls, camera and screen given back,
  `DO_FADE`), the host's `CLEAR_CUTSCENE` arriving later finds nothing to do; and a creation command whose model does not
  load waits 0.8 s (was 3 s), after which that model is skipped for 30 s (the log says which one: `modele N jamais charge`).
- New log lines `cinematique de l'invite :` when a cutscene starts on the guest (loading state, queue length), every 5 s
  (time, finished, skipped, camera mode, queue length, Enter/Space/mouse state) and at its end, to see what the guest's
  cutscene is doing if skipping still fails.

## 0.54.4-vcport (untested, network version 23, same protocol as 0.54.0)

Written from the client log of the first 0.54.3 test (client leaves the Johnson house: no textures at all).

- **Removed the 0.54.3 "zone coherence" check.** It forced the guest's zone back to the ped's area (3, the house) when the
  game's visible area (0) and the ped's area disagreed for 2 s. That mismatch is the game's own interior exit: it sets the
  visible area first and the ped's area when the fade ends, and on the guest's machine (3.5 s frame stalls while the street
  streamed in) it lasted long enough to trigger the check, which put the interior zone back at the moment the exit
  finished: the log shows `relocated: ... 1014.7 area 3 -> ... 14.8 area 3` (0.54.2: `area 0`), then nothing loads. Both
  players start inside the house on the exit marker of this save, so this hit the first seconds of every session.
- The "put back in the host's zone" step (only during the 15 s after a scripted move of the host) now also waits while the
  game's own entry/exit transition runs (`CEntryExitManager::ms_exitEnterState`, `0x96A7CC`).

## 0.54.3-vcport (untested, network version 23, same protocol as 0.54.0)

Written from the host and client logs of the 14/10 test of 0.54.2 (Home Invasion, third time).

- **Guest in a vehicle when the host's script moves the player**: the replay of `SET_PLAYER_COORDINATES` (0055) /
  `SET_CHAR_COORDINATES` (00A1) skipped the move for a guest in a vehicle (moving a ped in a vehicle moves the vehicle),
  but the `SET_AREA_VISIBLE` (04BB) that came with it was applied: the guest stayed in the van outside while the view
  switched to the interior (void, the interior never streamed in). Now the move becomes `WARP_CHAR_FROM_CAR_TO_COORD`
  (0362, same four parameters, as `TeleportPed` does): the guest leaves the vehicle at once, no exit animation, no
  damage, the vehicle stays; the log says `l'invite quitte son vehicule sans animation`.
- **Zones follow the script, not the doors**: `SET_AREA_VISIBLE` (04BB) and `LINK_CHAR_WITH_INTERIOR` (0860) of the host's
  script are noted and applied to the guest together with the scripted move (before it, when they came first - taken
  before the move, as `TeleportPed` does -, or within 3 s after it), always as one step (`SetGuestArea`: 04BB, link, ped
  area field) so the guest's visible area and his ped's area cannot disagree any more (the replayed exit left the guest
  "inside" in the street: `zone 8` at z 25, then the fall rescue). They are not applied on their own any more (the old rule
  "host puppet within 40 m" made the view switch for a guest standing near the door).
- **Separate interiors after the scripted move**: `AreaRegroupFrame` put the guest in the host's zone whenever it differed
  for 4 s during a host mission (client log: dragged out at +3637 s, in again at +3649 s, both seconds after the host used
  a door himself). It now only does so in the 15 s after a scripted move of the host (10 s after a mission start): a guest
  who fell or was held up still comes back, a guest who just took a different door is left alone. (A second check making
  the guest's two zone fields agree was in 0.54.3 and removed in 0.54.4, see above.)
- **Extra pedestrians**: the host's `CLEAR_AREA` (0395), `CLEAR_AREA_OF_CARS` (03BA) and `CLEAR_AREA_OF_CHARS` (042B) are now
  mirrored; on the guest they remove only the guest's own ordinary passers-by and parked cars in that area (not the
  copies of the host's characters and vehicles, not networked vehicles, not his own vehicle, not his police while he is
  wanted). The client log of the Home Invasion start shows 3-4 guest-local pedestrians in the host's zone kept all along
  by the "on screen or within 40 m" rule (`population fusionnee: ... 4 passants (4 dans la zone d'un autre)`).
- **Phone calls**: when the host's player has the phone out (task `CTaskComplexUseMobilePhone`, already in his state as
  gear bit 4) the guest's CJ takes his phone out too (`TASK_USE_MOBILE_PHONE` 0729 on himself) and puts it away when the
  host does (`telephone :` log lines). The commands that carry a call's dialogue (mission audio 03CF/03D1/040D, text
  054C, prints) are mirrored from **any** script while the host's phone is out - until now only mission threads were -
  so a call in free roam is heard and read by both at the same time. `0729` / `052B` seen in any script are logged with
  the script name to learn which scripts start calls. The ring tone itself is not mirrored (unknown command).
- **Launcher lobby ping**: (1) length and data of every lobby message left in two small sends - the second waited for
  the other side's delayed acknowledgement (up to ~200 ms) - now one send, and `TCP_NODELAY` on both ends; (2) the
  clock was `GetTickCount` (15 ms steps; a LAN read 0 or 16 ms), now `QueryPerformanceCounter`, in tenths of a ms on the
  wire (`0xFFFF` = not measured); (3) the answer's time was taken after waiting for the lobby lock, now before; (4) a
  number per measurement, so a late answer to an old ping is ignored; (5) a new guest was listed at "0 ms" for two
  seconds, now measured on arrival and shown as "..." until then; (6) measured every second instead of two, smoothed
  over the last few answers; (7) not measured while the guest downloads the host's mods (its answer waits behind the file
  data on the same connection).
- **Diagnostic**: `objet porte :` lines (object attached to the player, "hold entity" task 307, crouch tasks 415/1306,
  active task) each time one of them changes while carrying: for the carried crate that is dropped when CJ crouches (no
  trace of it in the 14/10 logs).

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
