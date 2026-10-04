// SACoop : mod coop de GTA San Andreas (1.0 US). Reglages et fonctions partagees entre les modules.
#pragma once
#include <windows.h>
#include <stdint.h>

#define SACOOP_VERSION "0.48.3-prealpha"

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
void InstallGamePatches();
void InstallPuppetRender();   // coop.cpp : rendu des pantins (faces arriere)
void StartWatchdog();
void WatchdogFrame();
void OnFrame();             // une fois par image, sur le fil du jeu (window.cpp, juste avant Present)
void CoopFrame(bool inGameLoop);
bool RequestGameStart(int slot);   // game.cpp : depuis le menu, nouvelle partie (-1) ou chargement (0-7)
int MenuLoadingSlot();             // game.cpp : emplacement en cours de chargement depuis le menu, sinon -1   // chaque tour de la boucle du jeu (game.cpp) : menu (false) ou partie (true)
