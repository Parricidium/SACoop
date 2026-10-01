// Police.
//  - Recherche partagee (RecherchePartagee=1, defaut) : l'hote prend le plus haut niveau des joueurs (le crime d'un
//    invite attire aussi sa police) ; les invites prennent celui de l'hote chaque fois qu'il change (hausse, ou police
//    semee : tout le monde retombe a zero). Niveau pose par SET_PLAYER_WANTED_LEVEL (010D), lu dans CWanted +0x2C.
//  - Police de l'hote sur les invites (PoliceHote=1, defaut) : la police du jeu ne connait qu'un joueur
//    (FindPlayerPed). Chez l'hote, un policier a pied plus pres du pantin d'un invite recherche que de l'hote (et a
//    moins de 40 m) recoit TASK_KILL_CHAR_ON_FOOT (05E2) sur ce pantin : il le traque et lui tire dessus ; ces coups
//    partent chez l'invite (combat.cpp, personnage de l'hote pres d'un invite partage). CLEAR_CHAR_TASKS (0687) le rend
//    a lui-meme quand l'invite n'est plus recherche, est mort ou loin. Chez l'invite en population partagee, plus de
//    police locale : celle de l'hote (ses copies) suffit (population.cpp).
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "entities.h"
#include "mirror.h"
#include "population.h"
#include "police.h"
#include <math.h>
#include <string.h>

using namespace game;

// CWorld::Players[0].m_PlayerData.m_pWanted (0xB7CD9C), CWanted +0x2C : niveau
int WantedLevel()
{
    uint8_t *w = *(uint8_t **)0xB7CD9C;
    int lvl = w ? *(int *)(w + 0x2C) : 0;
    return lvl >= 0 && lvl <= 6 ? lvl : 0;
}

static void SetWantedLevel(int level)
{
    int a[2] = { 0, level };
    RunScriptCommand(0x010D, 2, a);   // SET_PLAYER_WANTED_LEVEL (joueur 0)
}

bool HostPoliceOnGuests()
{
    return g_cfg.hostPolice && !g_cfg.host && g_players[0].connected && g_players[0].state.inGame;
}

static bool PlayerUp(int i)
{
    const NetPlayer &p = g_players[i];
    return p.connected && p.state.inGame && p.state.health > 0.0f;
}

static void ShareWanted()
{
    static int lastHost = -1;
    if (!g_cfg.shareWanted) { lastHost = -1; return; }
    if (g_cfg.host) {
        // Quand la recherche de l'hote baisse (police semee), les invites l'apprennent un peu apres : pendant 2 s on
        // ignore leurs anciens niveaux, sinon on remontait aussitot.
        static int lastMine;
        static uint32_t quietUntil;
        int mine = WantedLevel(), best = mine;
        if (mine < lastMine) quietUntil = GetTickCount() + 2000;
        lastMine = mine;
        if (GetTickCount() < quietUntil) return;
        for (int i = 1; i < MAX_PLAYERS; i++)
            if (PlayerUp(i) && g_players[i].state.wanted > best) best = g_players[i].state.wanted;
        if (best > mine && best <= 6) { SetWantedLevel(best); lastMine = best; Log("police : recherche %d (crime d'un invite)", best); }
        return;
    }
    const NetPlayer &h = g_players[0];
    if (g_localId <= 0 || !h.connected || !h.state.inGame) { lastHost = -1; return; }
    if (h.state.wanted != lastHost) {
        lastHost = h.state.wanted;
        if (WantedLevel() != lastHost) { SetWantedLevel(lastHost); Log("police : recherche %d (comme l'hote)", lastHost); }
    }
}

// --- Hote : policiers lances sur les invites recherches ---
struct Chase { int cop, player; };
static Chase g_chases[32];
static int g_numChases;

static float Dist2D(const float *a, const float *b) { float dx = a[0] - b[0], dy = a[1] - b[1]; return dx * dx + dy * dy; }

static bool Chased(int ref) { for (int i = 0; i < g_numChases; i++) if (g_chases[i].cop == ref) return true; return false; }

static void HostPoliceChasesGuests()
{
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (!g_cfg.hostPolice || now - last < 500) return;
    last = now;
    void *me = FindPlayerPed();
    // Poursuites en cours : rendues au jeu quand l'invite n'est plus recherche, est a terre ou loin.
    for (int i = 0; i < g_numChases;) {
        Chase &c = g_chases[i];
        void *cop = PedFromRef(c.cop), *pup = PuppetOf(c.player);
        bool keep = cop && pup && PlayerUp(c.player) && g_players[c.player].state.wanted && Field<float>(cop, PED_HEALTH) > 0.0f &&
                    Dist2D(EntityPos(cop), EntityPos(pup)) < 60.0f * 60.0f;
        if (!keep) {
            if (cop && Field<float>(cop, PED_HEALTH) > 0.0f) RunScriptCommand(0x0687, 1, &c.cop);   // CLEAR_CHAR_TASKS
            if (g_cfg.logScripts) Log("police : le policier %08X laisse le joueur %d", c.cop, c.player);
            c = g_chases[--g_numChases];
            continue;
        }
        i++;
    }
    bool anyWanted = false;
    for (int p = 1; p < MAX_PLAYERS; p++) anyWanted |= PlayerUp(p) && g_players[p].state.wanted && PuppetOf(p);
    if (!anyWanted) return;
    int perPlayer[MAX_PLAYERS] = {};
    for (int i = 0; i < g_numChases; i++) perPlayer[g_chases[i].player]++;
    Pool *pool = *(Pool **)0xB74490;
    for (int i = 0; i < pool->size && g_numChases < 32; i++) {
        if (pool->flags[i] & 0x80) continue;
        void *cop = pool->objects + i * 0x7C4;
        if (cop == me || Field<int>(cop, 0x598) != 6 || Field<uint8_t>(cop, 0x484) != 1 || PuppetIndex(cop) >= 0 || PedVehicle(cop)) continue;
        if (Field<float>(cop, PED_HEALTH) <= 0.0f) continue;
        int ref = PedRef(cop);
        if (Chased(ref)) continue;
        float dh = me && WantedLevel() ? Dist2D(EntityPos(cop), EntityPos(me)) : 1e12f;   // l'hote recherche garde les siens
        int best = -1;
        float bestD = 40.0f * 40.0f;
        for (int p = 1; p < MAX_PLAYERS; p++) {
            void *pup = PuppetOf(p);
            if (!pup || !PlayerUp(p) || !g_players[p].state.wanted || perPlayer[p] >= 2 + g_players[p].state.wanted) continue;
            float d = Dist2D(EntityPos(cop), EntityPos(pup));
            if (d < bestD && d < dh) { bestD = d; best = p; }
        }
        if (best < 0) continue;
        int a[2] = { ref, PedRef(PuppetOf(best)) };
        RunScriptCommand(0x05E2, 2, a);   // TASK_KILL_CHAR_ON_FOOT
        g_chases[g_numChases++] = { ref, best };
        perPlayer[best]++;
        Log("police : le policier %08X poursuit le joueur %d (recherche %d)", ref, best, g_players[best].state.wanted);
    }
}

void PoliceFrame()
{
    if (!NetRunning() || GameState() != 9 || !FindPlayerPed()) return;
    ShareWanted();
    if (g_cfg.host) HostPoliceChasesGuests();
}
