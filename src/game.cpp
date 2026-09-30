// Correctifs du jeu et boucle du mod. La boucle principale de WinMain (0x748710) avance selon gGameState
// (0xC8D4C0) : 0-4 logos et videos, 5-6 initialisation, 7 menu d'ouverture, 8 chargement, 9 partie.
// Chaque tour de boucle appelle RsEventHandler (0x619B60) : rsFRONTENDIDLE (0x1B) au menu, rsIDLE (0x1A) en
// partie. Ces deux appels passent par nous : le mod tourne sur le fil du jeu, hors du rendu.
#include "util.h"
#include "sacoop.h"
#include "net.h"

typedef int(__cdecl *RsEventHandler_t)(int ev, void *arg);
static const RsEventHandler_t RsEventHandler = (RsEventHandler_t)0x619B60;

enum : uintptr_t {
    ForegroundApp = 0x8D621C,          // 0 : fenetre au second plan (le jeu dort par tranches de 100 ms)
    FrontEndMenuManager = 0xBA6748,    // CMenuManager
    MenuActive = FrontEndMenuManager + 0x5C,
    bMissionPackGame = 0xB72910,
};

// --- Nouvelle partie lancee toute seule (AutoDemarrer, instances de test) ---
// Comme l'action "Nouvelle partie" du menu (0x57D731 et ProcessMenuOptions) : CGame::bMissionPackGame = 0, puis
// CMenuManager::DoSettingsBeforeStartingAGame (0x573330, thiscall), puis m_bDontDrawFrontEnd (+0x32) = 1. Le menu
// se ferme, WinMain passe a l'etat 8 (chargement de DATA\GTA.DAT) puis 9.
static void AutoStart()
{
    static int frames;
    static bool done;
    if (!g_cfg.autoStart || done) return;
    if (++frames < 15) return;   // quelques images de menu d'abord (musique, textures chargees)
    if (frames == 15) {
        Log("demarrage auto : nouvelle partie");
        *(uint8_t *)bMissionPackGame = 0;
        ((void(__thiscall *)(void *))0x573330)((void *)FrontEndMenuManager);
        *(uint8_t *)(FrontEndMenuManager + 0x32) = 1;
        return;
    }
    if (!*(uint8_t *)MenuActive) { done = true; return; }
    if (frames == 45) {   // le menu ne s'est pas ferme de lui-meme
        Log("demarrage auto : menu ferme a la main");
        *(uint8_t *)MenuActive = 0;
        done = true;
    }
}

static int __cdecl h_FrontendIdle(int ev, void *arg)
{
    int r = RsEventHandler(ev, arg);
    AutoStart();
    CoopFrame(false);
    return r;
}

static int __cdecl h_GameIdle(int ev, void *arg)
{
    int r = RsEventHandler(ev, arg);
    CoopFrame(true);
    return r;
}

void InstallGamePatches()
{
    // Au second plan, WinMain ne fait plus que Sleep(100) (cmp ForegroundApp, 0 / jz en 0x748A8D) : une partie coop
    // ne doit pas s'arreter quand on change de fenetre, et les instances de test n'ont jamais le premier plan.
    static const uint8_t jzSleep[] = { 0x0F, 0x84, 0x20, 0x03, 0x00, 0x00 };
    if (memcmp((void *)0x748A8D, jzSleep, sizeof(jzSleep)) == 0) PatchNop(0x748A8D, 6);
    else Log("correctif : attente au second plan introuvable");

    // Sans intro : l'etat 1 saute la video du logo (jnz 0x748AF8 toujours pris) et passe directement a l'etat 5
    // (au lieu de 2) : ni logo, ni video du titre.
    if (g_cfg.skipIntro) {
        static const uint8_t jnz[] = { 0x75, 0x0E }, mov2[] = { 0xC7, 0x05, 0xC0, 0xD4, 0xC8, 0x00, 0x02 };
        if (memcmp((void *)0x748AF8, jnz, 2) == 0 && memcmp((void *)0x748B08, mov2, sizeof(mov2)) == 0) {
            uint8_t jmp = 0xEB, five = 5;
            Patch(0x748AF8, &jmp, 1);
            Patch(0x748B08 + 6, &five, 1);
        } else Log("correctif : videos d'intro introuvables");
        // Etat 0 : logos EA et NVIDIA en fondu (0x748AA8-0x748AE4, ~500 images) : on saute directement a la fin de
        // l'etat (mov gGameState, 1 en 0x748AE7). Etat 6 : fondu de sortie de l'ecran du titre (CLoadingScreen, 0x590990,
        // 250 images) : retire. Le fondu d'entree de l'etat 5 (0x590860) reste : sans lui l'ecran de chargement de la
        // partie plantait (0x72837D).
        static const uint8_t st0[] = { 0x53, 0x57, 0xE8 }, fadeOut[] = { 0xE8, 0xF1, 0x7C, 0xE4, 0xFF };
        if (memcmp((void *)0x748AA8, st0, 3) == 0) PatchJump(0x748AA8, (void *)0x748AE7);
        else Log("correctif : logos introuvables");
        if (memcmp((void *)0x748C9A, fadeOut, 5) == 0) PatchNop(0x748C9A, 5);
        else Log("correctif : fondu de sortie du titre introuvable");
    }

    // Boucle du mod : les appels de RsEventHandler du menu (0x748CC2) et de la partie (0x748D9B).
    static const uint8_t callFe[] = { 0xE8, 0x99, 0x0E, 0xED, 0xFF }, callGame[] = { 0xE8, 0xC0, 0x0D, 0xED, 0xFF };
    if (memcmp((void *)0x748CC2, callFe, 5) == 0) PatchCall(0x748CC2, (void *)h_FrontendIdle);
    else Log("correctif : appel du menu introuvable");
    if (memcmp((void *)0x748D9B, callGame, 5) == 0) PatchCall(0x748D9B, (void *)h_GameIdle);
    else Log("correctif : appel de la partie introuvable");
}
