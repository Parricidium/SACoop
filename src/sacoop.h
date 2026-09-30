// SACoop : mod coop de GTA San Andreas (1.0 US). Reglages et fonctions partagees entre les modules.
#pragma once
#include <windows.h>
#include <stdint.h>

#define SACOOP_VERSION "0.2.2-prealpha"

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
    int skin;               // Tenue : modele du pantin vu par les autres (piétons 1-299 ; 106 = fam2 par defaut)
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
void StartWatchdog();
void WatchdogFrame();
void OnFrame();             // une fois par image, sur le fil du jeu (window.cpp, juste avant Present)
void CoopFrame(bool inGameLoop);   // chaque tour de la boucle du jeu (game.cpp) : menu (false) ou partie (true)
