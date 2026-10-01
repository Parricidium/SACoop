// PNJ de l'hote face aux invites.
//  - Les pantins des invites sont crees avec le type de personnage PLAYER_NETWORK (2) : le jeu ne le traite pas comme
//    un joueur (CPed::IsPlayer 0x5DF8F0 : types 0 et 1 seulement), mais les relations entre types s'y appliquent.
//  - Relations (chez l'hote, toutes les 500 ms) : tout ce que les PNJ pensent du joueur 1 (type 0), ils le pensent
//    aussi du type 2. Table des types : *(0xC0BBE8), 32 types x 5 masques (respect, aime, ignore, n'aime pas,
//    deteste ; bit = 1 << type) ; chaque PNJ en garde sa copie (CPed +0x4E0, recopiee du type a sa creation ; les
//    scripts la changent aussi). Gangs hostiles, ennemis de mission qui "detestent le joueur" : ils s'en prennent
//    aussi aux invites, et leurs coups partent chez eux (combat.cpp).
//  - Chasseurs : un script qui lance un PNJ sur l'hote (TASK_KILL_CHAR_ON_FOOT 05E2 sur $PLAYER_ACTOR) le note ; tant
//    que ce PNJ est dans cette tache (CTaskComplexKillPedOnFoot, type 1000), il est relance (05E2) sur le joueur
//    nettement le plus proche (invite a moins de 40 m et a moins de 70 % de la distance de sa cible actuelle), puis
//    revient a l'hote de meme.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "mirror.h"
#include "npc.h"
#include <math.h>
#include <string.h>

using namespace game;

static void MirrorMasks(uint32_t *m)
{
    for (int k = 0; k < 5; k++) m[k] = (m[k] & ~4u) | ((m[k] & 1u) << 2);
}

static void MirrorRelations()
{
    uint32_t *types = *(uint32_t **)0xC0BBE8;
    if (types) for (int t = 0; t < 32; t++) MirrorMasks(types + t * 5);
    Pool *pool = PedPool();
    void *me = FindPlayerPed();
    for (int i = 0; i < pool->size; i++) {
        if (pool->flags[i] & 0x80) continue;
        void *ped = pool->objects + i * 0x7C4;
        if (ped == me || PuppetIndex(ped) >= 0) continue;
        MirrorMasks((uint32_t *)((uint8_t *)ped + 0x4E0));
    }
    static bool logged;
    if (!logged && types) {   // une fois : qui deteste le joueur au depart
        logged = true;
        char line[160] = "";
        for (int t = 0; t < 32; t++) if (types[t * 5 + 4] & 1) { char b[8]; wsprintfA(b, " %d", t); lstrcatA(line, b); }
        Log("pnj : types qui detestent le joueur (recopies sur les invites) :%s", line[0] ? line : " aucun");
    }
}

// --- chasseurs ---
struct Hunter { int ref; int target; };   // target : 0 = hote, sinon joueur
static Hunter g_hunters[48];
static int g_numHunters;

void NpcHunterNoted(int pedRef)
{
    for (int i = 0; i < g_numHunters; i++) if (g_hunters[i].ref == pedRef) { g_hunters[i].target = 0; return; }
    if (g_numHunters < 48) g_hunters[g_numHunters++] = { pedRef, 0 };
}

void NpcScriptCommand(void *script, int op)
{
    if (op != 0x05E2 || !g_cfg.host || !NetRunning()) return;
    int v[2];
    uint8_t *ip = *(uint8_t **)((uint8_t *)script + 0x14);
    void *me = FindPlayerPed();
    if (!me || !ScriptReadValues(script, ip, 2, v) || v[1] != PedRef(me)) return;
    NpcHunterNoted(v[0]);
    if (g_cfg.logScripts) Log("pnj : %08X lance sur l'hote par %.8s", v[0], (const char *)script + 8);
}

static bool Hunting(void *ped)
{
    void *task = ActiveTask(ped);
    if (!task) return false;
    int type = ((int(__thiscall *)(void *))(*(void ***)task)[4])(task);   // CTask::GetTaskType
    return type == 1000;   // TASK_COMPLEX_KILL_PED_ON_FOOT
}

static float Dist2D(const float *a, const float *b) { float dx = a[0] - b[0], dy = a[1] - b[1]; return dx * dx + dy * dy; }

static void UpdateHunters()
{
    void *me = FindPlayerPed();
    for (int i = 0; i < g_numHunters;) {
        Hunter &h = g_hunters[i];
        void *ped = PedFromRef(h.ref);
        if (!ped || Field<float>(ped, PED_HEALTH) <= 0.0f || !Hunting(ped)) { g_hunters[i] = g_hunters[--g_numHunters]; continue; }
        i++;
        const float *pp = EntityPos(ped);
        void *cur = h.target ? PuppetOf(h.target) : me;
        float dc = cur ? Dist2D(pp, EntityPos(cur)) : 1e12f;
        int best = -1;
        float bestD = 40.0f * 40.0f;
        for (int p = 1; p < MAX_PLAYERS; p++) {
            void *pup = PuppetOf(p);
            const NetPlayer &np = g_players[p];
            if (!pup || !np.connected || !np.state.inGame || np.state.health <= 0.0f || np.state.area != EntityArea(me)) continue;
            float d = Dist2D(pp, EntityPos(pup));
            if (d < bestD) { bestD = d; best = p; }
        }
        int want = h.target;
        if (best > 0 && best != h.target && bestD < dc * 0.49f) want = best;          // (0,7 au carre)
        else if (h.target && me && Dist2D(pp, EntityPos(me)) < dc * 0.49f) want = 0;   // l'hote redevient le plus proche
        if (h.target && !cur) want = 0;                                                 // invite parti
        if (want == h.target) continue;
        void *target = want ? PuppetOf(want) : me;
        if (!target) continue;
        int a[2] = { h.ref, PedRef(target) };
        RunScriptCommand(0x05E2, 2, a);   // TASK_KILL_CHAR_ON_FOOT
        Log("pnj : %08X passe de %s a %s", h.ref, h.target ? "un invite" : "l'hote", want ? "un invite" : "l'hote");
        h.target = want;
    }
}

void NpcFrame()
{
    if (!g_cfg.host || !NetRunning() || GameState() != 9 || !FindPlayerPed()) return;
    static uint32_t last;
    if (GetTickCount() - last < 500) return;
    last = GetTickCount();
    MirrorRelations();
    UpdateHunters();
    if (g_cfg.logScripts) {   // releve : tache des PNJ de gang / de mission proches de l'hote
        static uint32_t lastReport;
        if (GetTickCount() - lastReport < 3000) return;
        lastReport = GetTickCount();
        void *me = FindPlayerPed();
        Pool *pool = PedPool();
        char line[400] = "";
        int n = 0;
        for (int i = 0; i < pool->size && n < 8; i++) {
            if (pool->flags[i] & 0x80) continue;
            void *ped = pool->objects + i * 0x7C4;
            int type = Field<int>(ped, PED_TYPE);
            if (ped == me || (type < 7 || type > 16) && type < 24) continue;
            if (Dist2D(EntityPos(ped), EntityPos(me)) > 60.0f * 60.0f) continue;
            void *task = ActiveTask(ped);
            int tt = task ? ((int(__thiscall *)(void *))(*(void ***)task)[4])(task) : -1;
            char b[64];
            wsprintfA(b, " [type %d, tache %d, vie %d, haine %08X]", type, tt, (int)Field<float>(ped, PED_HEALTH), Field<uint32_t>(ped, 0x4E0 + 16));
            lstrcatA(line, b);
            n++;
        }
        if (n) Log("pnj :%s", line);
    }
}
