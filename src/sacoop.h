// SACoop : mod coop de GTA San Andreas (1.0 US). Reglages et fonctions partagees entre les modules.
#pragma once
#include <windows.h>
#include <stdint.h>

#define SACOOP_VERSION "0.54.2-vcport"
#define SACOOP_FORK "Zebra's forked repo"   // shown in the log, the F10 menu and the launcher: tells this build from the original

struct Config {
    bool windowed;          // Fenetre (1 = fenetre, 2 = sans bordure, 0 = plein ecran du jeu)
    bool borderless;
    int winX, winY, winW, winH;
    bool background;        // ArrierePlan : instance de test hors ecran, ne vole jamais le premier plan
    bool skipIntro;         // SansIntro : logos et video d'ouverture sautes
    bool localUserFiles;    // SauvegardesLocales : reglages et sauvegardes dans le dossier du jeu
    int maxFps;             // ImagesParSeconde
    bool autoStart;         // AutoDemarrer : nouvelle partie lancee toute seule (instances de test)
    bool netAuto;           // Reseau : demarre le reseau des le lancement (instances de test, ligne de commande)
    bool host;              // Role = hote | invite
    char address[64];
    int port;
    char playerName[24];
    int testMission;        // TestMission : mission lancee par l'autotest "mission"
    int testLoadSlot;       // ChargerEmplacement : le demarrage auto charge cet emplacement (1-8, tests)
    int testMenuCoop;       // TestMenuCoop : 1 la page COOP s'ouvre et heberge, 2 elle rejoint (captures)
    bool testBoard;         // TestTableau : tableau des joueurs toujours affiche (captures)
    bool testSkip;          // TestPasser : l'autotest "mission" passe la cinematique
    bool widescreen;        // GrandEcran : 3D au format de l'ecran, champ de vision elargi (defaut 1)
    bool shareWanted;       // RecherchePartagee : un seul niveau de recherche pour tous (le plus haut)
    bool hostPolice;        // PoliceHote : la police de l'hote poursuit aussi les invites recherches
    bool fpsView;           // VuePremierePersonne : la touche ToucheVue (F6) bascule la vue a la premiere personne
    int fpsKey;             // ToucheVue : code de touche virtuelle (F6 par defaut)
    bool testFirstPerson;   // TestPremierePersonne : vue a la premiere personne des l'arrivee (captures)
    bool ao;                // OcclusionAmbiante : rendu moderne, coins et dessous assombris (render.cpp)
    bool fxaa;              // Anticrenelage : FXAA sur la scene 3D
    int drawDistance;       // DistanceAffichage : 100-300 % (gfx.cpp)
    int zonePop;            // ZonePopulation : 100-200 %, distance d'apparition des pietons et voitures
    int popDensity;         // DensitePopulation : 50-300 %
    int gameLang;           // LangueJeu : -1 = celle de Windows, 0 anglais, 1 francais, 2 allemand, 3 italien, 4 espagnol
    bool invertMouseY;      // SourisInverseeY : axe vertical de la souris inverse (0 par defaut)
    bool aniso;             // FiltrageAnisotrope : textures nettes de biais (render.cpp)
    bool friendlyFire;      // TirAmi : les joueurs peuvent se blesser entre eux (chacun decide pour lui)
    int skin;               // Tenue : sur soi et chez les autres (0 = CJ avec ses vetements, par defaut ; 1-299 = un pieton du jeu)
    char autotest[32];      // Autotest : scenario de test (autotest.cpp)
    bool logScripts;        // JournalScripts : releves periodiques du fil du jeu
    // --- Modules portes de VCCoop (chacun a son interrupteur dans sacoop.ini, pour trouver une regression par bissection) ---
    bool interp;            // Interpolation : les autres joueurs / vehicules / PNJ sont places apres la physique, dans le passe (interp.cpp)
    int interpDelay;        // DelaiInterpolation : retard d'affichage en ms (110)
    bool placeFoot;         // PlacementPietons : pantins et copies de PNJ places par interpolation
    bool placeVeh;          // PlacementVehicules : copies de vehicules placees par interpolation
    bool freezeCopies;      // GelerCopies : la physique du jeu ne deplace plus les copies de vehicules (CPhysical bDontApplySpeed)
    bool guard;             // GardeFou : vehicules / personnages aberrants (NaN...) remis a leur derniere position saine
    bool mirrorExtra;       // MiroirEtendu : commandes de mission supplementaires rejouees chez les invites (mirror.cpp)
    bool fallRescue;        // SecoursChute : joueur tombe sous le monde (collisions absentes) remis a sa derniere position sure (coop.cpp)
    bool radarSync;         // MarqueursRadar : marqueurs du radar de l'hote (coordonnees, points de contact) reproduits chez l'invite (mirror.cpp)
    bool radioSync;         // RadioSync: the guests follow the host's radio (station, track, position, adverts) - radio.cpp
    bool syncWeather;       // GuestWeatherGuard: the guest never changes weather by itself (coop.cpp h_WeatherUpdate)
    bool lockGuestMissions; // GuestMissionLock: a guest cannot start a story mission (its start marker is still drawn) (conditions.cpp)
    bool syncTrains;        // TrainSync: the host's trains (random and mission) run the same on every guest's rails (trains.cpp)
    bool deathDrop;         // ArmesMort: weapons are kept on death (given back after the respawn, events.cpp); 0 = vanilla, they are lost
    bool hideCut;           // CacherEnCinematique : pantins, copies de PNJ de mission et de vehicules caches pendant une cinematique
};
extern Config g_cfg;

namespace sa {
    enum : uintptr_t {
        gGameState = 0xC8D4C0,   // 0-4 logos et videos, 5-6 initialisation, 7 menu, 8 chargement, 9 partie
    };
}

HWND GameWindow();
bool GameHasFocus();

void InstallCrashLog();
void InstallFileHooks();
void InstallWindowHooks();
void ReacquireInput();          // dllmain.cpp: DirectInput mouse and keyboard acquired again (after Alt+Tab)
void ReleaseInput();            // dllmain.cpp: DirectInput mouse and keyboard let go (Alt+Tab away): the mouse goes back to Windows
void WindowToggleFullscreen();  // window.cpp: Alt+Enter, borderless full screen <-> window
void InstallGamePatches();
void InstallPuppetRender();   // coop.cpp : rendu des pantins (faces arriere)
void StartWatchdog();
void StartCharge();   // charge.cpp : processeur, disque, processus gourmands, echantillonnage (Profil=1)
void WatchdogFrame();
void OnFrame();             // une fois par image, sur le fil du jeu (window.cpp, juste avant Present)
void CoopFrame(bool inGameLoop);
void InstallWeatherGuard();
void RadioSyncFrame();   // radio.cpp: host sends what its radio plays, guests follow it
void NoteLocalRelocated();   // the local ped was moved (teleport...): his "standing" state is settled for 2 s (coop.cpp)   // coop.cpp : the guest's weather only changes when the host says so
bool RequestGameStart(int slot);   // game.cpp : depuis le menu, nouvelle partie (-1) ou chargement (0-7)
int MenuLoadingSlot();             // game.cpp : emplacement en cours de chargement depuis le menu, sinon -1   // chaque tour de la boucle du jeu (game.cpp) : menu (false) ou partie (true)
