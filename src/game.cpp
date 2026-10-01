// Correctifs du jeu et boucle du mod. La boucle principale de WinMain (0x748710) avance selon gGameState
// (0xC8D4C0) : 0-4 logos et videos, 5-6 initialisation, 7 menu d'ouverture, 8 chargement, 9 partie.
// Chaque tour de boucle appelle RsEventHandler (0x619B60) : rsFRONTENDIDLE (0x1B) au menu, rsIDLE (0x1A) en
// partie. Ces deux appels passent par nous : le mod tourne sur le fil du jeu, hors du rendu.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"

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
// Demarrage depuis le menu : nouvelle partie (slot -1) ou chargement de l'emplacement slot (0-7). Chargement :
// chemin du fichier dans 0xC15FC8 (base "<dossier>\GTASAsf" en 0xC16F18 + numero + ".b", comme 0x5D0D20) et
// m_bLoadingData (FrontEndMenuManager +0x60), lu par CGame::InitialiseWhenRestarting (0x53C680).
static int g_startSlot = -2;   // -2 : rien de demande
static int g_startFrames;

bool RequestGameStart(int slot)
{
    if (g_startSlot != -2 || game::GameState() != 7 || !*(uint8_t *)MenuActive) return false;
    g_startSlot = slot;
    g_startFrames = 0;
    return true;
}

static void ProcessGameStart()
{
    if (g_startSlot == -2) return;
    if (g_startFrames++ == 0) {
        if (g_startSlot >= 0) {
            wsprintfA((char *)0xC15FC8, "%s%i.b", (const char *)0xC16F18, g_startSlot + 1);
            *(uint8_t *)(FrontEndMenuManager + 0x60) = 1;
            Log("demarrage : chargement de %s", (const char *)0xC15FC8);
        } else {
            Log("demarrage : nouvelle partie");
        }
        *(uint8_t *)bMissionPackGame = 0;
        ((void(__thiscall *)(void *))0x573330)((void *)FrontEndMenuManager);
        *(uint8_t *)(FrontEndMenuManager + 0x32) = 1;
        return;
    }
    if (!*(uint8_t *)MenuActive || game::GameState() != 7) { g_startSlot = -2; return; }
    if (g_startFrames == 30) {   // le menu ne s'est pas ferme de lui-meme
        Log("demarrage : menu ferme a la main");
        *(uint8_t *)MenuActive = 0;
        g_startSlot = -2;
    }
}

// Partie choisie par le joueur local au menu (hote : annoncee aux invites) : emplacement charge, sinon -1.
int MenuLoadingSlot()
{
    if (!*(uint8_t *)(FrontEndMenuManager + 0x60)) return -1;
    const char *p = (const char *)0xC15FC8;
    int n = lstrlenA(p);
    if (n < 3 || p[n - 2] != '.') return -1;
    int i = n - 3, slot = 0, mul = 1;
    while (i >= 0 && p[i] >= '0' && p[i] <= '9') { slot += (p[i] - '0') * mul; mul *= 10; i--; }
    return slot >= 1 && slot <= 8 ? slot - 1 : -1;
}

static void AutoStart()
{
    static int frames;
    static bool done;
    if (!g_cfg.autoStart || done) return;
    if (++frames < 15) return;   // quelques images de menu d'abord (musique, textures chargees)
    done = true;
    if (g_startSlot == -2) RequestGameStart(g_cfg.testLoadSlot - 1);   // ChargerEmplacement (tests), sinon nouvelle partie
}

static int __cdecl h_FrontendIdle(int ev, void *arg)
{
    int r = RsEventHandler(ev, arg);
    AutoStart();
    ProcessGameStart();
    CoopFrame(false);
    return r;
}

// Temps passe par image dans le jeu (Idle) et dans SACoop (CoopFrame), au journal toutes les 10 s.
static int __cdecl h_GameIdle(int ev, void *arg)
{
    static LARGE_INTEGER freq, since;
    static double gameMs, coopMs;
    static int n;
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&since); }
    LARGE_INTEGER a, b, c;
    QueryPerformanceCounter(&a);
    int r = RsEventHandler(ev, arg);
    QueryPerformanceCounter(&b);
    CoopFrame(true);
    QueryPerformanceCounter(&c);
    gameMs += (b.QuadPart - a.QuadPart) * 1000.0 / freq.QuadPart;
    coopMs += (c.QuadPart - b.QuadPart) * 1000.0 / freq.QuadPart;
    n++;
    if ((c.QuadPart - since.QuadPart) > freq.QuadPart * 10) {
        Log("temps par image : jeu %.1f ms, coop %.1f ms (%d images)", gameMs / n, coopMs / n, n);
        gameMs = coopMs = 0;
        n = 0;
        since = c;
    }
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
