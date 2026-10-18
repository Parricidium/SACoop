# Changelog - Zebra's forked repo

This is the `vcport` line of [Parricidium/SACoop](https://github.com/Parricidium/SACoop). The original's last release
is `0.48.4-prealpha`; everything below was added on top of it. Setting names (`sacoop.ini`) are described in the
README. Technical notes for the VCCoop port are in [`PORT-VCCOOP.md`](PORT-VCCOOP.md).

Versions marked *untested* were written and compiled but not yet played for real by two players.

## 0.54.92
- Passager : un joueur qui a commence a monter au volant (son etat dit carTask 1 pour ce vehicule) compte deja comme conducteur : G (ou F vers sa voiture) fait monter l'autre joueur en passager tout de suite, a la place la plus proche, sans attendre que le conducteur soit assis. *Non teste.*

## 0.54.91
- Portieres : une voiture occupee dont une portiere reste ouverte plus de 7 s est refermee (CAutomobile::CloseAllDoors), chez chaque joueur pour ses propres copies. Corrige la portiere ouverte chez l'un et fermee chez l'autre sans avoir a reproduire le cas rare. Portieres abimees ou arrachees : on ne touche a rien. *Non teste.* Le journal dit « portiere restee ouverte … refermee ».

## 0.54.90
- Clotures cassees : les journaux de 0.54.89 montrent que le jeu retire un panneau de cloture (modele 1410, type de degat 200) en effacant ses indicateurs « collision » (0x1) et « visible » (0x80) (08000085 -> 08000004) ; il ne bouge pas. Cet etat est maintenant envoye une fois par panneau et applique chez les autres (le panneau est converti en objet reel si besoin). *Non teste.* Les debris (modele 375, sans objet factice) ne sont pas synchronises.

## 0.54.88
- Diagnostic des clotures (2e tour) : le journal liste tous les objets reels a moins de 40 m qui ont un objet factice (meme au repos : modele, modele du factice, vie, effet de degat) et ceux qui disparaissent ou changent de modele (cloture cassee). Les journaux de 0.54.87 ne montrent aucune cloture parmi les objets deplaces.

## 0.54.87
- Diagnostic des clotures : le journal liste (une ligne par modele) les objets reels a moins de 25 m qui bougent ou sont deplaces (modele, modele du factice, factice present ou non). Les journaux de 0.54.86 montrent que ni l'hote ni l'invite n'envoient la cloture : elle n'est pas un objet deplace.

## 0.54.86
- Objets du decor : l'objet factice est aussi cherche au meme endroit avec un autre modele (cloture passee a son modele abime) ; si rien n'existe chez nous a cet endroit, une ligne du journal le dit (une par modele).

## 0.54.85
- Objets du decor (barrieres, clotures...) : un objet de la carte reste un objet factice tant que rien ne le touche, puis le jeu en fait un vrai objet ; une cloture renversee chez un joueur n'existait pas comme vrai objet chez l'autre et n'y bougeait jamais. Le receveur convertit maintenant l'objet factice a la meme place (CPopulation::ConvertToRealObject) puis le place.

## 0.54.84
- Copies de vehicules : une copie dont la place est prise par un vehicule a l'arret et vide attend 0,2 s au lieu de 0,6 s (une voiture en mouvement chez l'hote apparaissait avec retard chez l'invite).

## 0.54.83
- Circulation a l'arret : quand notre vehicule est presque immobile, le plafond de voitures du jeu monte jusqu'a +70 % et leur densite jusqu'a +50 % (progressivement, en 4 s environ), pour que la route se remplisse dans les deux sens.

## 0.54.82
- Voitures copiees : plus de gaz applique a la copie d'une automobile (sa position et sa vitesse viennent du reseau) : les roues motrices ne patinent plus et ne laissent plus de traces de freinage ni de fumee chez le passager a chaque acceleration.

## 0.54.81
- Diagnostic (hote) : toutes les 10 s, voitures de circulation nees (devant/derriere, meme sens/sens inverse que notre vehicule) et retirees par la garde.

## 0.54.80
- Diagnostic (hote) : un resume toutes les 10 s des tirs en voiture rejoues et de ceux qui touchent un occupant (les lignes par tir sont plafonnees).

## 0.54.79
- Population (hote) : meme regle pour les passants que pour les voitures : quand l'invite roule avec l'hote, seuls les passants nes a moins de 28 m de lui sont retires (avant 70 m dans son cone de vue, donc presque tous).

## 0.54.78
- Circulation (hote) : quand l'invite roule avec l'hote, la garde ne retire plus que les voitures apparues a moins de 28 m (le jeu ne cree que hors de la vue de l'hote, donc la ou l'invite regarde par la fenetre : tout etait retire, d'ou la route vide). Les voitures de police ne sont jamais retirees.

## 0.54.77
- Passager : le verrouillage ne vise plus jamais un vehicule, uniquement les personnages (ceux en voiture compris).

## 0.54.76
- Tir de passager sur un occupant de vehicule : test sur le rayon de la balle (arme -> point vise, 3 m de plus derriere la carrosserie, 0,6 m de tolerance) au lieu de la distance au point vise, qui rendait le coup aleatoire a distance ; journal : ecart au rayon et distance du point vise.
- Verrouillage : un vehicule verrouille qui se remplit (quelqu'un y monte) est relache, on retombe sur ses occupants.

## 0.54.75
- Passager : un vehicule occupe n'est plus une cible (le verrouillage prenait la voiture et tirait dans la portiere) : on verrouille ses occupants, visibles a travers la vitre.
- Tir de passager : un coup de pistolet / fusil / mitraillette dont le point vise est a moins de 1,3 m d'un occupant de vehicule le blesse (la balle s'arretait a la carrosserie) ; aussi pour les tirs d'un invite rejoues chez l'hote.

## 0.54.74
- Recherche partagee : l'invite gardait le niveau de l'hote moins d'une minute (journal : 1 etoile recue, retombee a 0 quand l'hote est passe a 2) : le jeu baisse un niveau que personne ne poursuit chez lui, et on ne le posait que quand celui de l'hote changeait. Il est maintenant remis au niveau de l'hote (toutes les 0,5 s au plus) tant qu'il dure, sauf 5 s apres une mort.

## 0.54.73
- Passager : la fonction de tir de la tache (0x627CC0) refuse elle aussi de tirer hors camera 55 / 49 (journal 0.54.72 : animation sans aucun CWeapon::Fire, meme verrouille) ; meme astuce que pour la mise a jour de la tache.

## 0.54.72
- Passager : la visee libre tirait a vide (animation et viseur corrects, aucun coup) : la tache de tir en voiture du joueur n'agit que si la camera active est en mode visee ; elle voit maintenant le mode 55 pendant son seul appel quand notre camera de script est active.
- Passager : verrouille ou libre, c'est la meme camera de script (plus de bascule camera du jeu -> camera de voiture -> camera de visee a chaque verrouillage) ; elle rejoint la cible en douceur et le viseur est dessine sur la cible tant qu'elle n'est pas rattrapee.

## 0.54.71
- Passager : visee libre avec une camera de script qui tourne avec la souris / le stick, viseur au centre de l'ecran, balle sur le rayon camera -> centre (le curseur d'ecran glissait sur la camera du jeu) ; verrouille : camera du jeu rendue.
- Passager : la visee marche aussi quand le conducteur est sorti ou que le conducteur est un PNJ (avant, il fallait un pantin conducteur).
- Passager : l'animation de tir n'est relancee que si la visee tourne de plus de 40 degres (0,8 s min.).

## 0.54.70
- Passager : la tache de tir par la fenetre n'est plus relancee quand le viseur bouge (l'animation repartait de zero a chaque tir, avec un long delai) ; les balles suivent le viseur par CWeapon::Fire.

## 0.54.69
- Passager : viseur libre moins sensible (souris 0,6 px par unite au lieu de 1,5 ; stick 0,45 ecran/s au lieu de 0,9).

## 0.54.68
- Passager : un seul viseur (croix dessinee en rectangles au pixel pres sur le curseur) ; celui du jeu est cache pendant CHud::Draw.

## 0.54.67
- Passager : CWeapon::Fire recoit le point vise comme cible (la tache de tir ne sert plus qu'a l'animation) ; la cible n'est plus donnee a la tache (journal 0.54.66 : cible a 4000 m avec un personnage verrouille).

## 0.54.66
- Passager : visee par curseur d'ecran (souris / stick) au lieu d'un cap libre ; le viseur du jeu est dessine sur le curseur et les balles partent vers le premier obstacle sur le rayon camera -> curseur, donc ou le joueur pense viser.

## 0.54.65
- Passager : la tache de tir suit la cible verrouillee (personnage ou vehicule) au lieu d'un point fige ; en visee libre elle est relancee des que le point bouge de 2,5 m (250 ms min.), les balles restaient sur l'ancien point.
- Passager : seules les cibles visibles (a l'ecran, sans obstacle, ProcessLineOfSight) sont verrouillables ou reperees ; une cible cachee plus de 1,2 s est deverrouillee.

## 0.54.64
- Passager : reperes "v" au-dessus des personnages a moins de 50 m pendant la visee (rouge = verrouille, jaune = le plus proche du viseur, gris = autres).

## 0.54.63
- Passager : le viseur du jeu est deplace sur le point vise (fractions 0xB6EC14/0xB6EC10) au lieu d'afficher un second "+" decale ; le "+" ne reste que hors camera 53/55/65.

## 0.54.62-vcport (network version 33 unchanged, untested)

- Passenger drive-by: a "+" is drawn where the shots go (white free aim, red locked target). The game's own crosshair stays on the four fixed views of the in-car aim camera and does not show the aim the mouse sets (0.54.60 log: the shot targets followed the mouse, the game's crosshair did not).

## 0.54.61-vcport (network version 33 unchanged, untested)

- A player's puppet getting into a car (passenger seat) is given 4.8 s instead of 3 s before the game's get-in task is cut short and the puppet is placed in the seat. 18/10 client log: the cut came exactly 3 s after the start, the door the puppet had opened stayed open on the guest's screen while it closed on the host's. If the door still stays open, the next step is closing it directly when the puppet is seated.

## 0.54.60-vcport (network version 33 unchanged, untested)

- Passenger free aim reads the raw DirectInput mouse counts (dllmain.cpp `MouseRawTake`): the game's own mouse state never showed horizontal movement while aiming from a car (0.54.59 log: heading stuck at 132 deg, pitch jumping to its limits), so the aim could not turn.

## 0.54.59-vcport (network version 33 unchanged, untested)

- Passenger lock-on keys moved off the chat key (T): backslash locks / releases, ] next target, [ previous target.

## 0.54.58-vcport (network version 33 unchanged, untested)

- Passenger lock-on keys changed: the middle mouse button (look behind) and the wheel (radio) are taken by the game. T locks / releases, Y next target, U previous target (moved in 0.54.59) (pad: d-pad up / right / left). A drive-by order is only re-issued when the aim moved more than 12 m and at most every 600 ms (0.54.57 log: the gun restarted every half second while locked). Log lines `passager : visee libre ...` show the mouse and stick values read.

## 0.54.57-vcport (network version 33 unchanged, untested)

- Passenger drive-by: free aim and lock-on. The camera in drive-by mode only ever showed four points (ahead, behind, left, right), so the aim now comes from the mouse movement (or the right stick of a pad) and moves the target of the drive-by order and the aim sent to the other player. Locks the target nearest the crosshair (pedestrians and vehicles within 70 m) or releases it, and switches targets (keys changed in 0.54.58). Fire: left mouse button or R2, aim: right mouse button or L2. The pad part is untested.

## 0.54.56-vcport (network version 33 unchanged, untested)

- Passenger drive-by: a drive-by order already running keeps the target it was given (0.54.55 log: bursts of shots at one point whatever the crosshair did). When the aim has moved more than 6 m (at most every 350 ms) the running order is taken off and given again with the new point.

## 0.54.55-vcport (network version 33 unchanged, untested)

- Passenger drive-by aim: the point given to the drive-by order now comes from the view really drawn (RenderWare camera frame), not TheCamera's matrix, which did not follow the free-aim crosshair (0.54.54 log: every shot at the same point).

## 0.54.54-vcport (network version 33 unchanged, untested)

- A passenger's drive-by animation is 72:237 (group 72) with 0:64 for the seat: the puppet of the passenger replays group 72 too (before only the driver's 0:74 / 75 passed the filter, the other player saw the passenger seated).
- Diagnostics: `tir du joueur en vehicule : cible ...` (sender) and `tir rejoue depuis un vehicule : cible ...` (receiver) to see where passenger shots are aimed (reported: they leave the front of the car whatever the crosshair).

## 0.54.53-vcport (network version 33 unchanged, untested)

- The seated puppet that turned into a runner while the guest fired: the host log (0.54.52) shows the old per-puppet TASK_DRIVE_BY (`pantin du joueur 1 : tir par la fenetre`) starting in every gap of the drive-by animations (0:62 alone between 0:74 and 0:75). That fallback is removed; the animations alone show the drive-by.

## 0.54.52-vcport (network version 33 unchanged, untested)

- Drive-by firing seen by the other player works (0.54.51 test: sound and firing animation). The gun task given to the puppet for each replayed shot is now taken off at once, and the puppet is seated again 300 ms after its last shot (before, it turned into a runner while the shots went on).
- Passenger drive-by: the game starts none for a passenger of another player's car. While the fire button is held with a gun, the passenger gets TASK_DRIVE_BY (0713) at the camera's aim point, taken off when released (`passager : tir par la fenetre lance`).

## 0.54.51-vcport (network version 33 unchanged, untested)

- Drive-by shots: the host's log (0.54.50) shows that not one shot of the guest's drive-by was counted, though its bullets hurt. Shots fired from a vehicle are now also counted from the weapon's ammunition going down when CWeapon::Fire did not count them (`tir du joueur en vehicule compte par les munitions`). The host replays them (flash, sound) from that.
- Guest log line `police : l'hote est a N etoile(s), nous a M` each time the host's level changes, to follow the clearing of the wanted level.
- Not done: fences/breakable map objects not synced, damage to parked cars.

## 0.54.50-vcport (network version 33 unchanged, untested)

- Diagnostics only, for the drive-by shots that the host still does not see or hear (0.54.49 log: `tir rejoue depuis un vehicule` never appeared): `tirs recus d'un pantin en vehicule` (shots counted from the guest, weapon sent and weapon in hand) and `tir rejoue ignore` (weapon in hand differs from the weapon sent).

## 0.54.49-vcport (network version 33 unchanged, untested)

- Drive-by animations confirmed working (0.54.48 test). Shots replayed from a vehicle now also give the puppet the gun task for that frame only (`AimFireCar` / `AimCarDone`) so the game plays the muzzle flash, recoil and sound. Log: `tir rejoue depuis un vehicule`.
- "Clear wanted level" of the tools menu now clears it for everybody (`PoliceClearAll`): the host's guests' old levels are ignored for 5 s, before the host took them back at once as "a crime of a guest".

## 0.54.48-vcport (network version 33 unchanged, untested)

- Traffic copies within 7 m of the car we drive are left to our physics before the first touch (the contact list only filled after the car had already stopped against a frozen copy: "too static").
- Diagnostic line on the receiving side for a drive-by (`tir par la fenetre recu`), to see which animations arrive. The 18/10 host log still showed the old per-task path, so check both PCs run the same, new dll (version in the first log line).

## 0.54.47-vcport (network version 33 unchanged, untested)

From the logs of 18/10 (0.54.45).

- **NPCs stuck holding a gun on the guest after a fight:** when the host's person stopped aiming, the copy's gun task was
  taken out of its slot without being aborted, so its aim animation kept playing for good (the same mistake as the
  drive-by pose of 16/10). The task is aborted first now (`AimMirror`).
- **Jacking a car with several people inside:** the copy of the driver had the reaction the game gives a jacked driver
  (being dragged out) wiped at every frame, so the guest's character jolted at the door until the host's state moved the
  driver. The copies' reactions are left alone while the local get-in is aimed at their vehicle (and 4 s after), as the
  players' puppets already were.
- **Drive-by not seen either way:** the logs of 18/10 show no sign of the drive-by test (task 1022) succeeding, though
  shots from a car did land. A player in a vehicle with a gun-class weapon now counts as shooting from the window when
  the attack-slot task is the gun task or the drive-by task, or a drive-by animation of the PED block is playing. Log:
  `tir par la fenetre : tache d'attaque ...` (what the game has, each time it changes), to settle the real task type if
  this is not enough.
- **Drive-by, second attempt (logs of 18/10 again show no sign of the task or animation):** a gun-class weapon fired from a
  vehicle in the last second now counts as a drive-by whatever the game's tasks say, with the shots' own aim point, so the
  other player's puppet leans out and the replayed shots fire.
- **Drive-by, third attempt (logs of 18/10, second test):** the player's own drive-by is no task at all, it is the animations
  CAR_SIT_WEAK (0:62) plus DRIVEBY_L / DRIVEBY_R (0:74 / 0:75) over the seat. The puppet of a player in a car now replays those
  drive-by animations (the others, seat and steering, still not: they dragged the body through the floor); the drive-by task
  of the first attempt, which restarted for every shot and sat the puppet again each time, is only the fallback now.
- **Puppet stuck at the car door** (the guest's character took 12 s to get in, the host saw it run on the spot): once his state
  says he is seated and the puppet's own get-in has lasted 3 s, it is aborted and the puppet seated.
- **The host's traffic was a wall for the guest:** the host marks all its traffic as "mission" vehicles, and the push code
  skipped those (the `choc :` line of 18/10: a frozen copy, 56 -> 22 km/h). Fixed above (mission vehicles pushed locally).
- **Host hitting the guest's traffic (report of 18/10):** the push of a copy starts from a 3.6 km/h difference now (9 before),
  and `contact :` lines log what our car touches (not a network vehicle / which copy) to find why one stays a wall.
- **Hitting the host's mission vehicles:** pushed by the guest's car like any copy (the guest's copy only, it follows the
  host again after 0.7 s). The `choc :` log now starts from 18 km/h and a 40 % loss of speed, to see what a slow hit finds.

## 0.54.46-vcport (network version 33 unchanged, untested)

From the test of 16/10 (0.54.45).

- **Cars stopping dead near the other player:** copies of another player's vehicles are frozen so our physics cannot
  push them, which made each one an unmovable wall: the guest's car stopped dead on the host's traffic (only near the
  host, away from him the traffic is the guest's own). Copies within 10 m of the car you drive are no longer frozen;
  they are still placed every frame, they now only weigh in a collision.
- **People stuck holding a gun after a shoot-out:** the mirrored aiming task was deleted without being aborted, which
  left its arm pose on the host's people (and on puppets). It is aborted first now.
- Log: a get-in at a copy that stops with you still on foot (`vol : montee interrompue`), with its occupants, door
  lock, status and speed, for "can't jack a car with several NPCs inside".

## 0.54.45-vcport (network version 33 unchanged, untested)

From the logs of 16/10 (0.54.44).

- **NPCs jacking a guest, for real this time:** the host never saw the jacker (its get-in task sits under its event
  response, only the top task was checked), so the guest's puppet was put back every 3 s and the jack looped. The
  whole task tree is searched now, a jack that already ended (one of the host's people at the wheel the guest just
  held) counts too, and the puppet's "being dragged out" reaction is no longer cleared during it. Once the host's
  person sits at the wheel, the vehicle becomes the host's (it was a frozen copy, they could not drive off) and the
  guest's late states for it are ignored for 6 s.
- **Jacking a car with several people inside:** pressing F (or Enter) by a copy of the host's vehicle with people in
  it, when the game starts no get-in at all, gives the game's "get in at the wheel" task anyway (it picks the door,
  pulls the driver out, the others flee). Logged as `vol :`.
- **Drive-by bullets:** replayed shots from a vehicle now pass the game's drive-by origin (outside the window) instead
  of starting inside the car, where they hit the car itself (2 bullets of a clip got through). The host's people's
  drive-bys are replayed on the guest too.
- **Drive-by pose left after getting out:** the drive-by task lives in the secondary attack slot, which the cleanup
  never looked at. It is aborted (with its animation) and removed there, when the drive-by ends and when a puppet is
  on foot.
- Log: siren changes of the vehicle you drive (`sirene :`), for the "cannot turn the ambulance siren off" report.

## 0.54.44-vcport (network version 33 unchanged, untested)

- **The host's people can jack a guest** (out of a car, off a bike), request of 16/10: the jack only plays on the host
  (on the guest's puppet), and the puppet was put back in its seat at once because the guest still drove on his side.
  Now the host tells the guest's game (RL_JACKED) when one of his people is getting in at the wheel the guest's puppet
  holds; the guest gets out, and his puppet is not put back in its seat meanwhile.

## 0.54.43-vcport (network version 33 unchanged, untested)

From the logs of 16/10 (0.54.42).

- **Get-in animation seen twice:** a puppet's get-in was started again when the game's task was cut short (the copy
  moving, a driver being pulled out); it now starts once per get-in of the player. The car leaving during the second
  one dropped the puppet on the ground before it was put in its seat.
- **Getting in by a door where the host's people sit:** while the local player gets in (at the wheel or as a
  passenger), the copies in that vehicle are left to the game's task (it pulls them out) and none is put back in.
- **G picks the seat on your side:** front or back right when standing on the right, back left on the left (G always
  took the back left seat).
- **Drive-by pose stuck** ("holding the gun behind the head" after a drive-by): when a puppet's or a copy's drive-by ends
  it is seated again, which resets the arm.
- Log: what the game has while the guest aims as a passenger (tasks, drive-by permission, camera mode, weapon), for the
  "the guest cannot drive-by" report.

## 0.54.42-vcport (network version 33 unchanged, untested)

From the logs of 16/10 (0.54.40).

- **Guest crash at the second cutscene of Big Smoke's mission** (again, no crash line this time): the host's script
  cleared the area and 17 copies went at once. Removed entities are now also taken off the list of peds whose weapon
  is drawn after them (CVisibilityPlugins::RenderWeaponPedsForPC, 0x732F30), the last render list a deleted ped could
  stay on.
- **Getting into the other side's traffic:** the copy of the owner's driver was warped back into the driver's seat on
  every state while a player's puppet was jacking him (the guest log has the host's puppet warped into the seat 60
  times a second; "the driver stood up in the car"). A copy is no longer put back at a wheel a player is getting in at
  or driving, and is left alone while the puppet's jack pulls him out. The host no longer teleports his own driver out
  the moment the guest takes the car: the guest's puppet jacks him with the game's animation, and he is only put out if
  still at the wheel 4 s later.

## 0.54.41-vcport (network version 33: both players need 0.54.41, untested)

From the Robbing Uncle Sam test of 15/10.

- **Forklift forks** (and every vehicle's moving part: Dumper bed, tow truck hook...): the owner's angle now goes with
  the vehicle (CAutomobile +0x86C) and the copy shows it. The game only moves that part from the driver's pad, and a
  copy's driver has none, so the host never saw the guest's forks rise.
- **Ryder in the back of the van, on the guest:** his attached copy now also plays the host's animations (throwing the
  boxes); without them it stood with its arm twisted behind its head. When the mission lets him go, his tasks and pose
  are flushed (the twisted arm lasted into the final cutscene).

## 0.54.40-vcport (network version 32 unchanged, untested)

- **F10 > MISSIONS names:** the title now comes from the key the game builds from the mission's script name (`RYDER2` ->
  `RYDER_2`, "Robbing Uncle Sam"), then from the mission's own big texts other than "mission passed / failed"; it used to
  be the first big text of the mission, which for many is "Mission failed". Each name found is in the log
  (`missions : 38 RYDER2 -> Robbing Uncle Sam`); the script name shows when no title is found.

## 0.54.39-vcport (network version 32: both players need 0.54.39, untested)

Reports of 15/10: "the client looking behind sees people pop in and vanish", "after a teleport, massive desync that
does not re-sync", "it should be synced at all times".

- **The guests' view on the host's population:** each guest now sends where his camera looks. The host's game no longer
  removes people or cars a guest sees (within 45-50 m of him, or within 150-200 m in his view), and a person or car of
  the host's that appears in a guest's view (within 70 m) is removed at once, before it is ever sent: no more pop-ins
  and vanishings behind the host when travelling together. What a guest sees is also sent to him even a little outside
  the host's zone. (Indoors the crowd is left alone.)
- **Re-sync after a teleport:** when either player's position jumps more than 40 m at once (go to player, teleport, a
  script move, respawn), for 3 s the guest's own people and cars in the host's zone go, on screen or not. Otherwise
  what has stayed in the host's zone more than 6 s goes even on screen, unless within 25 m (it used to stay as long as
  it was on screen: a guest looking at his own cars kept them for good, next to the host's).
- Log: `visee :` lines (gun task, aim flag, camera mode) for the "still seen aiming" report; the population line gives
  how many removals were avoided for a guest's view and how many births in his view were removed.

Not yet: the guest's own population (around him, away from the host) is not sent to the host.

## 0.54.38-vcport (network version 31 unchanged)

To find why a guest out of the host's sight (short to medium range) has little traffic and sees other cars than the
host (report of 15/10). Nothing changes by default; new switches and log lines:

- `sacoop.ini` switches for each feature added since 0.54.29 that runs all the time, to switch them off one at a time:
  `SyncMissionObjects` (mission objects placed on the guests), `PushCopies` (a copy hit by a car is pushed),
  `JackTakeover` (a copy being jacked becomes the guest's at the door), `RenderListGuard` (removed entities leave the
  render lists). All `1` by default.
- `TrafficGuardRange` (default `90`): the host removes a car of his traffic that the game makes appear closer than this
  in front of a guest (so it does not pop in under his eyes). The game only makes traffic where the host is not looking,
  which is often where a guest near him looks: lower it (e.g. `40`) for more traffic around the guest, with some cars
  appearing at a distance; `0` never removes any.
- Log: the population line (every 10 s) also gives the guest's density multipliers, his mode (0 next to the host, 1 in
  his zone, 2 his own) and the host's mission densities; a guest away from the host logs it every 30 s too. The network
  line (every 30 s) is followed by the busiest message types sent and received.

## 0.54.37-vcport (network version 31 unchanged, untested)

- **F10 > MISSIONS** (debug tool): every story mission of `main.scm`, by number and title (or script name). Two clicks
  start one. The host's game starts it the way the main script does (START_MISSION), and the guests follow it as any
  other mission (regroup next to the host, mirrored commands). A guest's click asks the host. Refused while a mission is
  already running (stop it first with Tools > Stop mission), during a cutscene, or when the host is dead; everybody gets a
  line on screen saying who started which mission, or why not. It counts as played: saving afterwards can upset the story
  order, so use a spare save.

## 0.54.36-vcport (network version 31 unchanged, untested)

Review of everything since 0.54.9 for what could get in the way of play.

- **A guest riding in the host's car could not be hurt** by mission enemies or police (since 0.54.18): the rule that
  drops hits between two players in the same vehicle also dropped the host's people's hits on that guest. It now only
  drops the local player's own hits.
- **Garage doors:** between two checks of who is nearest, any other player's door state was applied, even to a garage
  this PC is in charge of: two PCs could flap a door, or cut into a Pay'n'Spray. The last check is kept, the reference's
  state only is followed, and a passenger is never the reference (the driver's game runs the doors).
- **Phone calls:** a guest's contact call waits for the host's 15 s at most (it could be held until the host's next call,
  a mission later), and only the contacts' call scripts are held.
- **Interior cutscenes:** the mission-start regroup no longer puts area 0 back while a host cutscene is loading on the
  guest; a cutscene that never starts (dropped, mission stopped, host gone) gives the guest his own visible area back after
  40 s, at the mission's end, or at once without a host.
- **Mission objects only the player may damage:** only mission objects are opened to the guests' shots, and the
  protection is put back once no guest is within 80 m (it was lifted for good on any object).
- **Replay queue:** mission sounds at a ped or vehicle the guest has no copy of are dropped instead of holding the queue
  3 s; the host's phone-call audio is not waited for while the guest's own call says it; the host's "cutscene skipped"
  goes through the queue, in order with the CLEAR_CUTSCENE it is about.
- **Passenger drive-by permission:** put back as a script set it during the ride (a mission starting or ending), not
  always as it was before the ride (it is saved with the game).
- **Y / N vehicle offer:** keys only read with the game in front and its pause menu closed.
- **Waypoint teleport at the wheel inside an interior:** the car comes out of the interior too (it was left invisible).

## 0.54.35-vcport (network version 31 unchanged, untested)

- **Jacking the host's traffic on the guest** (report of 15/10: "reset to where it was, the driver back in, the guest
  outside, again and again", worst on moving cars): the host's driver kept driving on the host, so every state of his
  said "at the wheel" and the guest's copy of him was warped straight back in, out went the guest, and the car went
  back to following the host's moving car. Now:
  - the guest at the door of such a car (getting in at the wheel, within 4.5 m) takes it over at once: it runs on his
    physics, its copied driver has no orders and brakes, the jack plays as in the game;
  - the guest never warps a copy back into the driver's seat of a car he took over, nor touches the one his jack is
    pulling out;
  - the host, told by the guest's next state, takes his own driver out on the driver's side, and keeps the car (it is
    no longer a random car the game may drop while the guest drives it; it becomes traffic again if the host takes it
    back).

## 0.54.34-vcport (network version 31 unchanged, untested)

Review of the 0.54.28 to 0.54.33 changes for anything that could get in the way of play.

- An attached mission character's state (Ryder in the van) no longer goes into his copy's movement track: it carries the
  attach offset where the speed usually is, and the copy could be thrown for a frame when the mission let him go.
- Boxes moved by the guest: only light objects that really moved are sent to the host (a fixed mission object - gate,
  ramp, barrier - brushed by the guest could be made movable on the host).
- Vehicle pushes: a car another player drives and the host's mission vehicles are no longer taken over (the first would
  bounce and snap back; the mission may freeze the second or drive it on a recorded path). The owner's push handler is
  set even when the after-physics hook is off.

## 0.54.33-vcport (network version 31 unchanged, untested)

From the logs of the 15/10 test (0.54.32, Big Smoke's mission: the guest crashed twice at the same place).

- **Guest crash when the mission reached its objective** (jump to `0x74756F52` from the renderer's alpha list,
  `0x5539BF`): the host's script cleared a 300 m area and the guest's copies of the people and cars there went at once;
  a deleted ped was still on the list of things to draw that frame. Every entity taken out of the world is now also taken
  off the renderer's lists (the four visible-entity arrays and the alpha entity list).
- **Jacked car put back where it was jacked:** the guest took over one of the host's cars, and the host's last state for
  it (sent before the host knew) arrived 15 ms later and turned it back into a copy at the host's place. A state from
  another player is now ignored while we drive the vehicle and for 2 s after we took it over.

## 0.54.32-vcport (network version 31 unchanged)

- **Hitting a police vehicle** of the host (the copies a guest sees near the host: police bikes, cars, Enforcer, FBI)
  above ~20 km/h now gives at least one star, as the game does for damaging a cop car; the shared wanted level takes it
  to everybody. (The push itself, so the car no longer stops dead, came in 0.54.31.)

## 0.54.31-vcport (network version 31: both players need 0.54.31, untested)

From the logs and screenshots of the 15/10 test (0.54.30, Robbing Uncle Sam).

- **Ryder in the back of the van, on the guest:** 0.54.30 attached his copy at the van's centre with no weapon (the
  attach offset of a ped is not where an object keeps it): he was out of sight and his empty arm stuck out. The host now
  sends the ped's own attach data (offset, direction, turn limit, checked in `CPed::AttachPedToEntity`), and the copy is
  attached again when the weapon arrives (the script hands it over after attaching him). No aim is replayed on top of
  the game's own attached-ped aiming.
- **Boxes the guest moves:** the guest's copies were held at the host's place, so the guest's forklift fought them and
  shook the van, and the host never saw the guest's boxes move. While the guest (or his vehicle) touches a box, his
  physics moves it and its place goes to the host, whose box follows: a box the guest loads counts for the mission.
- **Hitting another player's vehicle (or the host's traffic and police):** the copy stood still like a wall and the
  car stopped dead; the original was never touched (no wanted level for hitting a police bike). Now a copy hit by our car
  is pushed by our physics for 0.7 s and its place and speed go to its owner, who moves the original there (unless a
  player drives it).
- **Mission vehicle offer:** 10 s to answer (with a countdown), then it is a no.
- Log: what the game has when the guest rides as a passenger (vehicle model, weapon, drive-by permission), for the
  Hotknife drive-by report.

## 0.54.30-vcport (network version 30: both players need 0.54.30, untested)

From the host and client logs of the 15/10 test (0.54.29, Robbing Uncle Sam). The mission objects now reached the guest
(he saw the host pick the boxes up with the forklift).

- **Ryder in the back of the van:** the mission attaches him to the van (he stands in it with his gun). The guest's copy
  used to walk after his position with his aim and animations replayed, on a moving van, and stayed stuck in a pose with
  the gun at an odd angle. The host now says when a mission character is attached to a vehicle (and where on it); the
  guest attaches the copy to his copy of that vehicle the same way (the game's own `AttachPedToEntity`), with the same
  weapon, and lets it go when the host's is let go.
- **Boxes flying out of the van on the guest:** the guest's copies of mission objects are now placed on every frame
  (carried along their speed between messages, 10 a second while they move), with their own physics off while the host
  moves them; an object attached on the host (a box loaded in the van) has no collision on the guest.
- **Host crash after the mission** (`0x59FE47`, drawing an object whose 3D model was gone): such an object is now skipped
  instead of drawn.
- The client's crash in this log was while closing the game (`0x72FDBA`, in the game's own shutdown), after the host had
  left; nothing in the mod's code was on the stack.
- Log: each animation replayed on a copy is now logged the first time it is seen (only the first 20 of the session were),
  to find what put the players' copies in the stuck pose.

## 0.54.29-vcport (network version 29: both players need 0.54.29, untested)

From the host and client logs of the 15/10 test (0.54.28, Just Business and Robbing Uncle Sam).

- **Host crash while driving the van in Robbing Uncle Sam** (`0x465CB4`): fixed. The mod's ghost script (which replays
  commands) had its "external script type" byte at 0 instead of the game's -1, so a task command it ran took its first
  local variable (a coordinate, 1013.7) as the handle of "the ped this script belongs to" and read far outside the ped
  pool. The byte is now -1, as the game sets it for every ordinary script.
- **Mission objects for the guest:** the host now sends each mission object near a guest (crates, boxes, anything the
  mission created) with its place, rotation, area and visibility, 5 times a second while it moves and every 2 s
  otherwise. The guest's copy is put there, so crates carried, pushed by a forklift or shown later look the same on both
  screens. The log says when a copy was wrong (`objet de mission ... -> corrige`) or missing (`pas de copie chez
  l'invite`).
- **Weapon pickups for each player:** a gun picked up by one player stays for the others; each player takes their own.
  A mission waiting for that pickup still sees it collected. Setting `WeaponPickupsEach` (default `1`; `0` = one for
  the group, as before).
- **Text lookups on the guest** always use the game's text table (they returned nothing when the guest had not opened a
  menu yet, so the mission title / help checks in the log said "no text").
- The `choc :` log line (car stopping dead) now also fires from 40 km/h (was 70), to catch the guest's car stopping on
  another car and sinking into the road.

## 0.54.28-vcport (network version 28 unchanged, untested)

Request of 15/10: aiming at an enemy kept locking on to the other player.

- **The other players can no longer be locked on to.** Their characters carry the game's own "never target this ped"
  flag (the one the script command `SET_CHAR_NEVER_TARGETTED` sets), which only the player's lock-on reads: the
  first-target search and the next-target switch skip them, so lock-on picks the enemies around them. Police, gangs and
  mission enemies still go after the players as before. Free aim still hits another player when friendly fire is on.
- New setting `TargetPlayers` (default `0`). `1` lets you lock on to the other players again.
- Only the player aiming needs 0.54.28; the network messages did not change.

## 0.54.27-vcport (network version 28 unchanged)

From the host and client logs of the 15/10 test (0.54.26, Robbing Uncle Sam). The mission title now shows on the guest
(`texte : 00BA cle "RYDER_2" -> Robbing Uncle Sam`).

- **In-car dialogue of a mission** (mirror.cpp). During the drive the guest got 20 `LOAD_MISSION_AUDIO` (03CF) and no
  `PLAY_MISSION_AUDIO` (03D1) at all (not a log limit: 330 replay lines). Checked in gta_sa.exe 1.0 US: 03D1 is the only
  command that plays a mission line (0x48521C, the one caller of `CAudioEngine` 0x5072B0). The play is said by a helper
  script the mission started, which is not flagged as a mission script, and only the mission scripts were mirrored. While
  a story mission runs on the host (its ONMISSION flag), the mission audio and text commands go to the guests whichever
  script says them (`ligne de mission ... dite par le script ...`, 10 lines at most). Ryder's own lines (character speech,
  banks 650-654) were already heard through the speech sync.
- **Cutscene without textures for a while** (mirror.cpp). A guest outside, brought by the mission start from far away
  (2702 -2392 to 2461 -1686 in the log), saw the cutscene start 0.5 s later with the scenery still streaming; one already
  inside did not. The guest's `START_CUTSCENE` now also waits, 3 s at most, for `CStreaming::ms_numModelsRequested`
  (0x8E4CB8) to reach 0, loading meanwhile (`cinematique de l'invite : decor charge en ... ms avant le debut`).

## 0.54.26-vcport (network version 28: the speech message grew)

From the host and client logs of the 15/10 test (0.54.25, Robbing Uncle Sam, cutscene watched to its end).

- **Cutscene subtitles work on the guest**: 14 of 14 shown on both (the guest's cutscene now ends by itself when the host
  watches his to the end). The players have the subtitles option off, hence nothing on screen.
- **Mission title for the guest** (mirror.cpp). The 0.54.25 diagnostic shows it: `message en grand : style 1, 1000 ms,
  "Robbing Uncle Sam" (appele de 004680B7)`: a `PRINT_BIG` of the contact's script, 0.5 s before the mission is launched,
  so the 20 s window that began at the launch (0.54.21) missed it. Every `PRINT_BIG` of the host's scripts outside the
  missions now goes to the guests (the mission scripts' were already mirrored).
- **Passenger dialogue in a mission car** (speech.cpp). The guest log shows Ryder's lines arriving and played on his copy
  (`son 294 banque 650 ... joue sur sa copie`), but not heard: the receiver played them as an ordinary 3D sound at the
  copy, with its own volume. Checked in gta_sa.exe 1.0 US: `AddSayEvent` makes a ped's line "always audible" (+0x9B) when
  the ped sits in the player's vehicle (0x4E684E), and lines can be "front end" (+0x9C, heard as in the cabin). The sender
  now sends the line's volume (+0xAC) and both flags; the receiver uses them (and no distance limit for such lines).
- **Diagnostic**: for the whole mission, each command of the mission script that the guests do not get is logged once on
  the host (`mission : debut, ... (pas envoyee)`): during the drive the host's script loads lines (`03CF`, seen replayed on
  the guest) but plays them with a command that is not mirrored; this names it.

## 0.54.25-vcport (network version 27 unchanged; both players need it)

From the host and client logs of the 15/10 test (0.54.24, Robbing Uncle Sam). The 0.54.23 switch fix works: one bullet
from the guest moves the mission on (the guest's copy of the switch does not break, cosmetic).

- **Firing seen by the other player** (combat.cpp, anims.cpp `AimFire`). The other player was seen aiming, but no shot, no
  recoil, no flash: the replayed shot (`CWeapon::Fire`) only makes the bullet. Now, on a replayed shot of an on-foot puppet
  that is aiming (pistols to rifles, 22-34), its gun task (`CTaskSimpleUseGun`, given by `AimMirror`) gets the FIRE command
  for that frame (`ControlGun` 0x61E040, command 2; the per-frame AIM refresh of `AimMirror` leaves that frame alone): the
  game plays the shot, the recoil and the muzzle flash. The bullet the task fires itself is dropped (`h_Fire`: a puppet's
  shot outside a replay); the bullet is the replayed one, to the exact point its player hit (0.54.23).
- **Guest's cutscene not cut short** (mirror.cpp, `RL_CUT_END`). The 0.54.24 logs: the host skipped at 4.6 s (2 of 14
  subtitles shown), the guest's cutscene, started 0.3 s later, was ended with it at 4.3 s, before its first subtitle. The
  host now says whether he skipped (`CCutsceneMgr` 0xB5F854) just before his `CLEAR_CUTSCENE`; if he did not, the guest's
  cutscene is left to end by itself (6 s at most) instead of being skipped.
- **Guest inside an interior** (mirror.cpp): an outdoor mission cutscene showed nothing on a guest standing in an interior.
  At the host's `LOAD_CUTSCENE`, with no area chosen by the host's script just before, the guest's visible area now
  takes the host's area for the cutscene (given back at its end, 0.54.11).
- **Diagnostic**: every big on-screen message (`CMessages::AddBigMessage` 0x69F2B0) is logged with its style, text and
  caller (`message en grand : ...`, 40 at most), on both PCs: the mission title is not a script text (0.54.24: the mission
  script uses no `PRINT_BIG` in its first 30 s), this shows what puts it on the host's screen.

## 0.54.24-vcport (diagnostics, network version 27 unchanged)

The guest sees neither the mission title nor the cutscene subtitles; the host sees both. Checked in gta_sa.exe 1.0 US:
the subtitles are shown by `CCutsceneMgr::Update` itself (0x5B1843: when its clock 0xBC3F08, which runs on the frame time,
passes a line's start time, `CMessages::AddMessageJumpQ` 0x69F1E0 with the text of key 0xBC2FC0 + 8 x n); nothing there
depends on the host or on the network, and the title is not a `PRINT_BIG` of any script (0.54.23). Log only:

- End of each cutscene, on the host (new) and on the guest: `... sous-titres a affiches sur b` (CCutsceneMgr text count
  0xBC3FEC, next one to show 0xBC3FF0). Same numbers on both and nothing on the guest's screen: the guest's game hides
  them (its subtitles option). Fewer on the guest: his cutscene stopped early.
- Host, the first 30 s of a story mission: each command its mission script uses, once, and whether the guests get it
  (`mission : debut, le script ... utilise XXXX (envoyee aux invites / pas envoyee)`): the title's command is among them.

## 0.54.23-vcport (network version 27 unchanged)

From the host and client logs of the 15/10 test (0.54.22, Robbing Uncle Sam).

- **Shots replayed at the point they hit** (combat.cpp). The guest could shoot the switches now, but it took many bullets:
  a player's shot was sent as a point 80 m along his camera, and his puppet, firing from where it stands, passed beside a
  small target. The point the shot really hits is now traced (`CWorld::ProcessLineOfSight` 0x56BA00, checked in
  gta_sa.exe 1.0 US: buildings, vehicles, peds, objects; the shooter and his vehicle ignored through
  `CWorld::pIgnoreEntity` 0xB7CD68) and sent; the puppet's replayed shot goes to that exact point.
- **A guest's shot on a mission object counts for the host** (mirror.cpp). The host log has no "plus seulement
  endommageable" line: the switches do not carry the player-only flag of 0.54.22, the gates opened through the puppet's
  replayed shots, and the second switch (by the far garage) did not open it. The 0.54.19 watch never fired either: the
  guest's copy does not get the "damaged" bit. Now a drop of the copy's health (CObject +0x154, lowered by
  `CObject::ObjectDamage` at 0x5A0E07) also counts, but only within 0.5 s of a shot by the guest (a crate knocked by his
  forklift must not count); the host then marks its own object damaged, wherever the host is.
- **Scenery where the guest's cutscene camera looks** (mirror.cpp). The far garage of the second shot of the cutscene was
  half loaded on the guest, textures out of place. `LOAD_SCENE` at the cutscene camera when the cutscene starts and
  whenever the camera jumps more than 80 m (`cinematique de l'invite : decor charge autour de la camera`).
- **The mission vehicle offer at the top of the screen** (hud.cpp, `HudPrompt`), in yellow, out of the way of the mission
  texts at the bottom; it goes away when answered.
- Mission title at the start of the first cutscene: neither log has a `PRINT_BIG` (00BA) from any script around the
  mission start, so it is not a script text; still open.

## 0.54.22-vcport (network version 27 unchanged)

From the host and client logs of the 15/10 test (0.54.20, Robbing Uncle Sam).

- **Switches a guest could not shoot, the cause** (mirror.cpp, `HostObjectsForGuests`). The 0.54.19 watch never fired (no
  `objet de mission` line in the client log). Checked in gta_sa.exe 1.0 US, `CObject::ObjectDamage`: an object with CPhysical
  flag 0x400000 (+0x40), which mission scripts set on such switches, drops any damage that does not come from the player or
  his vehicle (0x5A0E34). The guest's shots reach the host as his puppet's replayed shots, so the host's switch ignored
  them. The host now lifts that flag on any object within 80 m of a guest (every 0.5 s); the puppet's replayed shots then
  damage it as the host's would, and the mission goes on. Log: `objet de mission (modele ...) : plus seulement
  endommageable par l'hote`. (The 0.54.19 watch stays as a second way.)
- **Mission HUD still blinking a few times a second** (msnhud.cpp, interp.cpp). With 0.54.20 the counters were still put
  back every 0.3 to 1 s (client log: claims 1 to 20 in 5 s), so something clears them during the frame. They used to be
  put back only once per loop turn, after the frame was drawn: one frame without them each time, the blink. They are now
  also put back right after `CGame::Process`, before the frame is drawn. The claim lines now give what the slot held
  (`the slot held id ...`) to find what clears it.

## 0.54.21-vcport (network version 27 unchanged)

From the players' test of 15/10.

- **Mission title for the guest** (mirror.cpp). The title at the bottom right at the start of a mission's first cutscene
  only showed for the host: the client log of Robbing Uncle Sam has no `PRINT_BIG` (00BA) at all, so it is printed by the
  script that launches the mission, not by the mission script (the only one mirrored). For 20 s after a story mission is
  launched, `PRINT_BIG` from any script of the host now goes to the guests. Log: `texte en grand du script ...`.
- **Mission vehicle offered, not dropped on the guest** (vehicles.cpp, `GuestVehicleOffer`). When the host takes the wheel
  of a mission vehicle and has no free seat for the guest, the guest used to get a copy at once next to him, sometimes in a
  wall or on a car. Now a toast offers it for 30 s: Y spawns it next to the guest wherever he is then (he can walk to an
  open spot first), N declines; no answer, or the mission ending, drops the offer. Keys read only while the chat and the
  F10 menu are closed. (A free passenger seat still gives the "G: ride with the host" hint instead.)
- **Mission dialogue heard by the guest** (mirror.cpp). The host's script loads a line (`LOAD_MISSION_AUDIO` 03CF), waits
  for it (`HAS_MISSION_AUDIO_LOADED` 03D0) and plays it (03D1); the guest got the load and the play back to back, his own
  load was not done and the play did nothing: no dialogue between the host and his passenger. Checked in gta_sa.exe 1.0
  US: 03D0 asks `CAudioEngine::GetMissionAudioLoadingStatus` (0x5072A0 on AudioEngine 0xB6BC90, slot - 1), 1 = loaded. A
  mirrored play now waits for that, in order, 3 s at most.

## 0.54.20-vcport (network version 27 unchanged)

From the host and client logs of the 15/10 night test (0.54.17, Robbing Uncle Sam).

- **Mission HUD blinking, the cause** (msnhud.cpp). The client log has 550 "counter ... type 1, value 100" lines: the
  counters were put back on the guest again and again, always at multiples of the 265 ms message interval. One message
  from the host now and then says the counter is off (its script clears and shows it again), the guest dropped it at once
  and the next message put it back. A counter (or the clock) is now dropped on the guest only after the host has said
  "off" for 1 s. (0.54.19's steady flash byte stays.)
- **"upgrade 1087 removed" 4700 times** (vehicles.cpp, `ApplyMods`). Hydraulics (1087), which the game fits by itself on
  some models, are not in the owner's list; the copy removed them, the game kept them, and this was tried again every
  frame. A removal the game refuses is now noted once and not retried (`upgrade ... stays (the game keeps it)`).
- Checked in the same logs: the waypoint hold of 0.54.17 found the ground every time; the passenger followed the host's
  car into the garage interior and back out (`passager : interieur 0 -> 2`, `2 -> 0`); the shared radio followed the
  driver without fighting. Phone calls (0.54.16 / 0.54.17): working, the players confirmed it.

## 0.54.19-vcport (network version 27 unchanged: plays with 0.54.18, both need it for the object fix)

From the players' test of 15/10.

- **Mission objects damaged by a guest** (mirror.cpp, `GuestObjectDamageWatch`, `RL_OBJECT_DAMAGED`). Robbing Uncle Sam:
  the guest could not shoot the control panel, the host had to. The mission runs on the host and asks
  `HAS_OBJECT_BEEN_DAMAGED` (0366) about its own object; checked in gta_sa.exe 1.0 US, the case at 0x4831F8 calls 0x46A2F0,
  true when the object's CEntity flags (+0x1C) have bit 0x200 (damaged) or have lost bit 0x80 (visible). The guest now
  watches his copies of the host's mission objects; the first time one gets the damaged bit, the host's handle is sent,
  and the host sets the same bit on its object: the script goes on as if the host had shot it. Log: `objet de mission`.
- **Guest's mission HUD blinking** (msnhud.cpp). The counters and the timer blinked on the guest at the update rate. The
  guest no longer copies the host's "flash when first displayed" byte (+0x3F of each counter) nor the timer's +0x151
  byte; the counters are drawn steady and only their values change. Each time a counter has to be put back on the guest
  is now logged (`mission HUD: counter ... (claim n)`, 20 at most): if it still blinks, these lines show what resets it.

## 0.54.18-vcport (network version 27: new garage message, the vehicle message grew)

Asked by the players on 15/10. Addresses checked in gta_sa.exe 1.0 US.

- **Garage doors** (new `garages.cpp`, `MSG_GARAGE`, setting `SyncGarages=1`). `CGarages::aGarages` (0x96C048, 0xD8 bytes,
  count 0x96C024): +0 position, +0x38 door position, +0x4C type, +0x4D door state. Each PC ran its own garages for its own
  player. Now the player nearest a garage (30 m, outside) sends its door state and position (on every open/close change,
  and twice a second); the others set their door opening (3) or closing (2) when theirs is the other way, and the game
  moves the door. Log: `garage :`.
- **Passenger drive-by** (coop.cpp, `PassengerDriveByFrame`). Both places that start a player's drive-by (0x5E63AB,
  0x74200C) test `CPlayerInfo::m_bCanDoDriveBy` (0xB7CD98 + 0x153, set by `SET_PLAYER_CAN_DO_DRIVE_BY` 0501 at 0x48C856),
  which the story scripts leave off. It is switched on while the local player rides as another player's passenger, and
  restored when he gets out. A shot from inside a vehicle ignores that vehicle (0x73FD47), and hits between two players
  in the same vehicle are now dropped on both sides (combat.cpp).
- **Nitro flame** (vehicles.cpp, `MsgVehicle.nitro`). `CAutomobile::ProcessControl` runs `NitrousControl` (0x6A3EA0) for
  every car with nitro fitted (+0x38C bit 0x80000); for a car not driven locally it draws the flame (`DoNitroEffect`
  0x6A3BD0) from the charge (+0x8A4) and the gas pedal. The owner sends the charge, the boosts left (+0x48A) and the
  fitted flag; the copy gets them every frame. Cars and the types built on them only (not bikes, boats, trains).
- **Passenger interior, follow-up of 0.54.17**: the passenger took the interior the host had walked into (his puppet still
  seated for a moment after he got out): the street vanished and the guest fell. Now only while the driver's own state says
  he is at the wheel of that vehicle; each player goes through doors by himself.
- **Mission characters falling on the spot** in the host's scripted scenes (entities.cpp): the copies, placed every
  frame, had no ground of their own and the game played its in-air reaction. Like the players' puppets (0.52.0): standing
  flag set and `CTaskComplexInAirAndLand` dropped unless the original is jumping or falling.

## 0.54.17-vcport (network version 26 unchanged: plays with 0.54.15 and 0.54.16)

From the host and client logs of the 15/10 evening test.

- **Waypoint teleport, again** (panel.cpp). The host log shows the first teleport of the session: no ground after 6 s
  (`sol 0.0 introuvable`), then a fall from 50 m and a death; the later ones found the ground at once (already loaded).
  The game only streams collisions around the player, so asking for them from afar did nothing. Now: if the ground at
  the waypoint is already there, straight onto it; otherwise the player (with his vehicle at the wheel) is put 150 m
  above the spot and held there (position kept, no speed) while `REQUEST_COLLISION` / `LoadAllRequestedModels` run and
  the ground is probed every frame; he is put on it as soon as it is found. If it has not loaded after 10 s he goes back
  where he started (never a drop).
- **The host's call rings for the guest** (mirror.cpp). The guest's own call now waits for the host's (0.54.16), and the
  host's call (script `mob_la1` in the log) only reached the guest once the host had answered: the guest heard no ring.
  The sound and text commands of the contact-call scripts (`mob_*`) are now sent from the first ring (mission audio,
  one-off sounds, audio events at a character or a position, texts). Each command those scripts use is logged once on
  the host (`telephone : le script ... utilise la commande ...`) to see what rings if it is still silent.
- **Passenger follows the driver's interior** (coop.cpp, `PassengerAreaFrame`). The guest, passenger of the host, drove
  into a TransFender with him: the garage moved the host's car into interior 2, the guest's game got there late and
  never came back out (client log: `relocated: ... area 2`, then nothing outside loaded). A player in a vehicle driven by
  another player now takes the driver's interior (visible area, link, ped and vehicle area, scenery) once they have
  differed for 300 ms, going in and coming out.
- **Shared-vehicle radio** (coop.cpp, `RadioFrame`). The guest teleported into the host's vehicle: his radio was stopped,
  a retune to the host's station did not take, the old station came back and was counted as the guest's own choice, sent
  to the driver, and the host was retuned to it every few seconds: no sound, could not change station. A station change
  now counts as the player's only while the radio is playing and if it is not the station our own last retune replaced.

## 0.54.16-vcport (network version 26 unchanged: plays with 0.54.15)

Asked by the player on 15/10: a guest outside before the host got a waiting contact's call first, out of step.

- **Guest's calls wait for the host's** (script.cpp, `GuestPhoneHold`). The contacts' calls are made by the guest's own
  scripts as well. When one of them puts the phone to the guest's ear (`TASK_USE_MOBILE_PHONE` 0729 with on = 1, from any
  script but the mod's own) while the host is connected and not on the phone, the script is held on that command: its
  instruction pointer goes back on the opcode and it yields, trying again every frame. Once the host's CJ is on the phone
  (gear bit 4 of his state) the script goes on, so both calls start together. While the guest's own call runs, the
  host's mirrored lines of the call (mission audio and text commands) are dropped, so the dialogue is not heard twice.
  Log: `telephone : appel du script ... retenu` / `libere`. The ring itself is the guest's script's and keeps ringing
  while it waits.

## 0.54.15-vcport (network version 26: the vehicle message grew, 0.54.14 and older cannot join)

From the players' test of 15/10. Addresses checked in gta_sa.exe 1.0 US.

- **Vehicle extras** (vehicles.cpp, `MsgVehicle.comps`). A van with a "Toy Corner" advert for the guest had a plain side
  for the host: the colours travel, the extras did not. `CVehicle::SetModelIndex` keeps the extras the game picked in
  +0x438 / +0x439 (from `CVehicleModelInfo::ms_compsUsed` 0xB4E478); the owner sends them, and the copy is built after
  writing them into `CVehicleModelInfo::ms_compsToUse` (0x8A6458 / 0x8A6459), which `ChooseComponent` /
  `ChooseSecondComponent` take when it is not -2 and then set back to -2 (the script command for a car's components does
  the same).
- **Waypoint teleport waits for the ground** (panel.cpp, `WaypointFrame`). 0.54.14 moved the player and probed the ground
  at once: never loaded yet, so he fell from 50 m. Now the player stays where he is while `REQUEST_COLLISION` at the
  waypoint and `CStreaming::LoadAllRequestedModels` run every 250 ms and the ground is probed every frame; he goes as soon
  as it is found (6 s at most, then `LOAD_SCENE` and one last probe). Toast "Loading the waypoint...". Log gives the time
  it took.
- **"Mission cancelled by <name>"** (mirror.cpp). The host's stop (its own click or a guest's request) shows the name of
  the player who asked, on the host and on every guest (`RL_MISSION_STOPPED` with the name).
- **Pause-menu map on wide screens** (widescreen.cpp). The map image is drawn by the menu code (squeezed to 4:3), its
  blips, waypoint cursor and zoom by `CRadar`, which also had the narrower HUD scale: the blips drifted from the map,
  more with the zoom, and the map cursor was not under the mouse. On the map page (`FrontEndMenuManager` +0x15D == 5,
  compared with 5 in eight places in the exe) neither correction applies: the game's own map, stretched but consistent.
- **Feet through the car floor** (coop.cpp). The player's animations were replayed on his puppet inside any vehicle
  (meant for bunny hops and standing pedalling); in a car his seat and steering animations, on top of the puppet's own,
  dragged its body through the floor. Now only on bikes, BMX and quads.
- **Entering vehicles** (coop.cpp).
  - "The host watches the client hop into the car twice": the puppet's own entry finished before its player's "in the
    vehicle" state arrived, so it was pulled out and put back. A puppet that has just got into the vehicle it was walking
    to stays seated for up to 4 s while the state catches up.
  - Both players jacking each other: a puppet walking to the driver's seat where the local player already sits no longer
    gets the "enter as driver" task (in the game that means pulling the driver out). If both end up at the wheel of the
    same vehicle, the player with the lower number keeps it (the host first) and the other one is moved by his own game
    to a free passenger seat (`WARP_CHAR_INTO_CAR_AS_PASSENGER` 0430), with a toast. Same rule on every PC, no message.

## 0.54.14-vcport (network version 25 unchanged: plays with 0.54.13)

Asked by the player on 15/10.

- **F10 > Tools > TELEPORT TO WAYPOINT** (panel.cpp, `TeleportToWaypoint`). Checked in gta_sa.exe 1.0 US: the map sets its
  waypoint with `CRadar::SetCoordBlip` (0x583820, sprite 41) and keeps the blip handle in `FrontEndMenuManager` +0x2C
  (0xBA6774); handle = counter << 16 | index into `CRadar::ms_RadarTrace` (0xBA86F0, 0x28 bytes: position +8, counter
  +0x14, flags +0x25, bit 2 = in use). The handle is checked against the table (no waypoint: a toast says so). The blip
  has no height: out of any interior first, `LOAD_SCENE` and `REQUEST_COLLISION` there, then
  `GET_GROUND_Z_FOR_3D_COORD` from 1000 m gives the ground (1 m above it; 50 m if the ground did not load). On foot:
  `TeleportPed`; at the wheel: `SET_CAR_COORDINATES` (00AB) moves the vehicle with the player in it. The menu closes. Log:
  `menu jeu : point de passage x y, sol z (...)`.

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
