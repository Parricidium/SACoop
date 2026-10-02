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
#include "vehicles.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

extern void *g_testCopCar;
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
    // Voitures de police (bIsLawEnforcer +0x428 bit 0) plus pres d'un invite recherche que de l'hote, a moins de
    // 150 m : vers lui (CAR_GOTO_COORDINATES 00A7, redonne toutes les 2 s : il bouge) ; a moins de 20 m d'un invite a
    // pied, les policiers descendent (TASK_LEAVE_ANY_CAR 0633) et la poursuite a pied ci-dessus prend le relais.
    static uint32_t lastDrive;
    bool drive = now - lastDrive >= 2000;
    if (drive) lastDrive = now;
    Pool *vp = *(Pool **)0xB74494;
    if (g_cfg.logScripts) {   // releve : voitures des forces de l'ordre (createur, distance a l'hote)
        static uint32_t lastReport;
        if (now - lastReport > 10000) {
            lastReport = now;
            char line[256] = "";
            int n = 0;
            for (int i = 0; i < vp->size && n < 8; i++) {
                if (vp->flags[i] & 0x80) continue;
                uint8_t *v = vp->objects + i * 0xA18;
                if (!(v[0x428] & 1)) continue;
                char b[48];
                sprintf(b, " [cree par %d, %.0f m%s]", v[0x4A4], me ? sqrtf(Dist2D(EntityPos(v), EntityPos(me))) : 0.0f, IsNetVehicle(v) ? ", reseau" : "");
                strcat(line, b);
                n++;
            }
            Log("police : %d voitures des forces de l'ordre%s", n, line);
        }
    }
    for (int i = 0; i < vp->size; i++) {
        if (vp->flags[i] & 0x80) continue;
        uint8_t *v = vp->objects + i * 0xA18;
        if ((!(v[0x428] & 1) || v[0x4A4] != 1) && v != g_testCopCar) continue;
        if (IsRemoteVehicle(v) || (v[0x36] >> 3) == 5) continue;   // aleatoire, a nous, pas une epave
        void *drv = Field<void *>(v, VEH_DRIVER);
        if (!drv || Field<int>(drv, 0x598) != 6 || PuppetIndex(drv) >= 0 || Field<float>(drv, PED_HEALTH) <= 0.0f) continue;
        float dh = me && WantedLevel() ? Dist2D(EntityPos(v), EntityPos(me)) : 1e12f;
        int who = -1;
        float bestD = 150.0f * 150.0f;
        for (int p = 1; p < MAX_PLAYERS; p++) {
            void *pup = PuppetOf(p);
            if (!pup || !PlayerUp(p) || !g_players[p].state.wanted) continue;
            float d = Dist2D(EntityPos(v), EntityPos(pup));
            if (d < bestD && d < dh) { bestD = d; who = p; }
        }
        if (who < 0) continue;
        const MsgState &st = g_players[who].state;
        if (!st.vehicleId && bestD < 20.0f * 20.0f) {
            void *occ[9] = { drv };
            for (int k = 0; k < 8; k++) occ[k + 1] = Field<void *>(v, VEH_PASSENGERS + k * 4);
            for (void *c : occ) {
                if (!c || Field<int>(c, 0x598) != 6 || PuppetIndex(c) >= 0) continue;
                int ref = PedRef(c);
                RunScriptCommand(0x0633, 1, &ref);   // TASK_LEAVE_ANY_CAR
            }
            if (g_cfg.logScripts) Log("police : voiture %08X arrivee pres du joueur %d, les policiers descendent", VehicleRef(v), who);
            continue;
        }
        // Invite en voiture a moins de 80 m : la voiture de police l'eperonne, comme le joueur de l'hote (pilote
        // automatique +0x390 : mission +0x3BA RAMCAR_FARAWAY 15 / RAMCAR_CLOSE 16, voiture cible +0x41C enregistree comme
        // le fait CAR_FOLLOW_CAR 0x41C960 : CEntity::CleanUpOldReference 0x571A00, RegisterReference 0x571B70 ;
        // vitesse de croisiere +0x3D0). A chaque image, la mission passe de loin a pres selon la distance.
        if (st.vehicleId && bestD < 80.0f * 80.0f) {
            if (void *target = NetVehicleById(st.vehicleId)) {
                void **slot = (void **)(v + 0x41C);
                if (*slot != target || (v[0x3BA] != 15 && v[0x3BA] != 16)) {
                    if (*slot && *slot != target) ((void(__thiscall *)(void *, void **))0x571A00)(*slot, slot);
                    *slot = nullptr;
                    ((void(__cdecl *)(void *, void *))0x41C8A0)(v, target);   // CCarCtrl : eperonner cette voiture (mission 15)
                    Log("police : voiture %08X eperonne le joueur %d (%.0f m)", VehicleRef(v), who, sqrtf(bestD));
                }
                static uint32_t lastRam;
                if (g_cfg.logScripts && now - lastRam > 2000) { lastRam = now; const float *sp = (const float *)(v + 0x44); Log("police : eperonnage, %.1f m, mission %d, %.0f km/h, statut %d, +428 %02X, croisiere %d, style %d", sqrtf(bestD), v[0x3BA], sqrtf(sp[0] * sp[0] + sp[1] * sp[1]) * 180.0f, v[0x36] >> 3, v[0x428], v[0x3D0], v[0x3B9]); }
                if (v[0x3D0] < 40) v[0x3D0] = 40;
                continue;
            }
        }
        if (v[0x3BA] == 15 || v[0x3BA] == 16) v[0x3BA] = 1;   // l'invite est descendu ou loin : plus d'eperonnage
        if (!drive) continue;
        int a[4] = { VehicleRef(v) };
        memcpy(&a[1], st.pos, 12);
        RunScriptCommandTyped(0x00A7, 4, "ifff", a);   // CAR_GOTO_COORDINATES
        Log("police : voiture %08X envoyee vers le joueur %d (%.0f m)", a[0], who, sqrtf(bestD));
    }
}

// Un policier de l'hote a sorti le pantin d'un invite de son vehicule (il y est encore d'apres l'invite, plus chez
// l'hote, policier a moins de 4 m) : l'invite descend aussi chez lui (RL_EJECT ; TASK_LEAVE_ANY_CAR sur lui-meme).
void *g_testCopCar;   // autotest "flic" : voiture de police de script traitee comme une aleatoire
enum : uint8_t { RL_EJECT = 21 };
static void HostEjections()
{
    static uint32_t lastSent[MAX_PLAYERS], inSince[MAX_PLAYERS], lastIn[MAX_PLAYERS];
    uint32_t now = GetTickCount();
    for (int p = 1; p < MAX_PLAYERS; p++) {
        void *pup = PuppetOf(p);
        if (!pup) continue;
        // Le pantin doit avoir ete assis (au moins 1 s) puis en etre sorti il y a moins de 1,5 s : sinon un joueur qui
        // montait dans une voiture pres d'un policier etait "ejecte" pendant que son pantin ouvrait encore la portiere
        // (le joueur 2 ne pouvait plus monter dans aucune voiture pendant une poursuite, 01/10).
        if (PedVehicle(pup)) { if (!inSince[p]) inSince[p] = now; lastIn[p] = now; continue; }
        bool wasSeated = inSince[p] && lastIn[p] - inSince[p] > 1000 && now - lastIn[p] < 1500;
        if (now - lastIn[p] > 1500) inSince[p] = 0;
        if (!wasSeated || !PlayerUp(p) || !g_players[p].state.vehicleId || now - lastSent[p] < 3000) continue;
        Pool *pool = *(Pool **)0xB74490;
        bool cop = false;
        for (int i = 0; i < pool->size && !cop; i++) {
            if (pool->flags[i] & 0x80) continue;
            void *ped = pool->objects + i * 0x7C4;
            cop = Field<int>(ped, 0x598) == 6 && PuppetIndex(ped) < 0 && Dist2D(EntityPos(ped), EntityPos(pup)) < 4.0f * 4.0f;
        }
        if (!cop) continue;
        lastSent[p] = now;
        uint8_t msg[1] = { RL_EJECT };
        NetSendReliableTo(p, msg, 1);
        Log("police : le joueur %d sorti de son vehicule par un policier", p);
    }
}

bool PoliceReliable(const uint8_t *d, int len)
{
    if (len < 1 || d[0] != RL_EJECT) return false;
    void *me = FindPlayerPed();
    void *veh = me ? PedVehicle(me) : nullptr;
    const float *spd = veh ? (const float *)((uint8_t *)veh + 0x44) : nullptr;
    if (veh && spd[0] * spd[0] + spd[1] * spd[1] > 0.08f * 0.08f) { Log("police : ejection ignoree (vehicule en mouvement)"); return true; }
    if (me && veh) {
        int ref = PedRef(me);
        RunScriptCommand(0x0633, 1, &ref);   // TASK_LEAVE_ANY_CAR
        Log("police : sorti du vehicule par un policier de l'hote");
    }
    return true;
}

void PoliceFrame()
{
    if (!NetRunning() || GameState() != 9 || !FindPlayerPed()) return;
    ShareWanted();
    if (g_cfg.host) { HostPoliceChasesGuests(); HostEjections(); }
}
