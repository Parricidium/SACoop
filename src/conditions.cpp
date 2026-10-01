// Conditions de mission elargies aux invites (hote). Les scripts de mission testent le joueur de l'hote : "est-il dans
// la zone du marqueur", "est-il dans la voiture"... Si le test est faux pour lui mais vrai pour le pantin d'un invite,
// il devient vrai : n'importe quel joueur peut faire avancer la mission.
//  - Detour de CRunningScript::UpdateCompareFlag (0x4859D0, thiscall(script, bool)) : le resultat brut, avant
//    l'inversion "NOT" (+0xD2) et la logique ET/OU (+0xD0), est corrige pendant les commandes de la liste.
//  - Marqueurs au sol : quand une commande LOCATE / IS_CHAR_IN_AREA de l'hote a son drapeau "sphere", elle est envoyee
//    aux invites (MSG_MARKER, 5 fois par seconde au plus) ; chacun la rejoue a chaque image avec son propre joueur
//    (le jeu dessine le marqueur), jusqu'a 600 ms apres le dernier message.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "script.h"
#include "mirror.h"
#include "conditions.h"
#include "vehicles.h"
#include <string.h>
#include <math.h>

using namespace game;

// mode : 0 tous moyens, 1 a pied, 2 en voiture ; stopped : a l'arret ; shape : 0 rayon 2D, 1 rayon 3D, 2 boite 2D,
// 3 boite 3D, 4 dans telle voiture, 5 dans une voiture, 6 dans tel modele.
struct CondOp { uint16_t op; uint8_t shape, mode, stopped, n; };
static const CondOp kConds[] = {
    { 0x00EC, 0, 0, 0, 6 }, { 0x00ED, 0, 1, 0, 6 }, { 0x00EE, 0, 2, 0, 6 },
    { 0x00EF, 0, 0, 1, 6 }, { 0x00F0, 0, 1, 1, 6 }, { 0x00F1, 0, 2, 1, 6 },
    { 0x00FE, 1, 0, 0, 8 }, { 0x00FF, 1, 1, 0, 8 }, { 0x0100, 1, 2, 0, 8 },
    { 0x0101, 1, 0, 1, 8 }, { 0x0102, 1, 1, 1, 8 }, { 0x0103, 1, 2, 1, 8 },
    { 0x00A3, 2, 0, 0, 6 }, { 0x01A1, 2, 1, 0, 6 }, { 0x01A2, 2, 2, 0, 6 },
    { 0x01A3, 2, 0, 1, 6 }, { 0x01A4, 2, 1, 1, 6 }, { 0x01A5, 2, 2, 1, 6 },
    { 0x00A4, 3, 0, 0, 8 }, { 0x01A6, 3, 1, 0, 8 }, { 0x01A7, 3, 2, 0, 8 },
    { 0x01A8, 3, 0, 1, 8 }, { 0x01A9, 3, 1, 1, 8 }, { 0x01AA, 3, 2, 1, 8 },
    { 0x00DB, 4, 0, 0, 2 }, { 0x00DF, 5, 0, 0, 1 }, { 0x00DD, 6, 0, 0, 2 },
};
static const CondOp *FindCond(int op)
{
    for (auto &c : kConds) if (c.op == op) return &c;
    return nullptr;
}

static void *g_curScript;
static const CondOp *g_curCond;
static int g_vals[8];
static bool g_valsOk;

static float F(int v) { float f; memcpy(&f, &v, 4); return f; }

static bool Stopped(void *ped)
{
    void *veh = PedVehicle(ped);
    const float *s = veh ? (const float *)((uint8_t *)veh + 0x44) : MoveSpeed(ped);
    return s[0] * s[0] + s[1] * s[1] + s[2] * s[2] < 0.0004f;
}

static bool Satisfies(void *ped, const CondOp &c, const int *v)
{
    void *veh = PedVehicle(ped);
    if (c.mode == 1 && veh) return false;
    if (c.mode == 2 && !veh) return false;
    if (c.stopped && !Stopped(ped)) return false;
    const float *p = EntityPos(ped);
    switch (c.shape) {
    case 0: return fabsf(p[0] - F(v[1])) < F(v[3]) && fabsf(p[1] - F(v[2])) < F(v[4]);
    case 1: return fabsf(p[0] - F(v[1])) < F(v[4]) && fabsf(p[1] - F(v[2])) < F(v[5]) && fabsf(p[2] - F(v[3])) < F(v[6]);
    case 2: {
        float x1 = fminf(F(v[1]), F(v[3])), x2 = fmaxf(F(v[1]), F(v[3])), y1 = fminf(F(v[2]), F(v[4])), y2 = fmaxf(F(v[2]), F(v[4]));
        return p[0] >= x1 && p[0] <= x2 && p[1] >= y1 && p[1] <= y2;
    }
    case 3: {
        float x1 = fminf(F(v[1]), F(v[4])), x2 = fmaxf(F(v[1]), F(v[4])), y1 = fminf(F(v[2]), F(v[5])), y2 = fmaxf(F(v[2]), F(v[5]));
        float z1 = fminf(F(v[3]), F(v[6])), z2 = fmaxf(F(v[3]), F(v[6]));
        return p[0] >= x1 && p[0] <= x2 && p[1] >= y1 && p[1] <= y2 && p[2] >= z1 && p[2] <= z2;
    }
    case 4: return veh && VehicleRef(veh) == v[1];
    case 5: return veh != nullptr;
    case 6: return veh && *(int16_t *)((uint8_t *)veh + 0x22) == v[1];
    }
    return false;
}

// --- Hote : autour de chaque commande (script.cpp) ---
void ConditionBefore(void *script, int op)
{
    g_curCond = nullptr;
    if (!g_cfg.host || !NetRunning() || !ScriptIsMission(script)) return;
    const CondOp *c = FindCond(op);
    if (!c) return;
    void *me = FindPlayerPed();
    uint8_t *ip = *(uint8_t **)((uint8_t *)script + 0x14);
    g_valsOk = me && ScriptReadValues(script, ip, c->n, g_vals) && g_vals[0] == PedRef(me);
    if (!g_valsOk) return;
    g_curScript = script;
    g_curCond = c;
    // Marqueur au sol (drapeau sphere = dernier parametre des zones) : envoye aux invites.
    if (c->shape <= 3 && g_vals[c->n - 1]) {
        static uint32_t last;
        static int lastKey;
        int key = op ^ g_vals[1] ^ (g_vals[2] << 1);
        uint32_t now = GetTickCount();
        if (key != lastKey || now - last >= 200) {
            last = now; lastKey = key;
            MsgMarker m = { MSG_MARKER, (uint8_t)c->n, (uint16_t)op, {} };
            memcpy(m.vals, g_vals, sizeof(m.vals));
            NetSendToGuests(&m, sizeof(m));
        }
    }
}
void ConditionAfter() { g_curCond = nullptr; }

typedef void(__thiscall *UpdateCompareFlag_t)(void *script, bool result);
static UpdateCompareFlag_t o_UpdateCompareFlag;
static void __fastcall h_UpdateCompareFlag(void *script, void *, bool result)
{
    if (!result && g_curCond && script == g_curScript && g_valsOk) {
        for (int i = 1; i < MAX_PLAYERS; i++)
            if (void *ped = PuppetOf(i))
                if (Satisfies(ped, *g_curCond, g_vals)) { result = true; break; }
    }
    o_UpdateCompareFlag(script, result);
}

// --- Invite : marqueurs ---
struct Marker { uint16_t op; uint8_t n; int vals[8]; uint32_t until; };
static Marker g_markers[8];

static void OnMarker(const MsgMarker &m)
{
    if (m.n < 1 || m.n > 8) return;
    Marker *slot = nullptr;
    for (auto &k : g_markers)
        if (k.until && k.op == m.op && k.vals[1] == m.vals[1] && k.vals[2] == m.vals[2]) { slot = &k; break; }
    if (!slot) {
        slot = &g_markers[0];
        for (auto &k : g_markers) if (k.until < slot->until) slot = &k;
    }
    slot->op = m.op;
    slot->n = m.n;
    memcpy(slot->vals, m.vals, sizeof(slot->vals));
    slot->until = GetTickCount() + 600;
}

void ConditionsFrame()
{
    g_onMarker = OnMarker;
    if (g_cfg.host || GameState() != 9 || !FindPlayerPed()) return;
    uint32_t now = GetTickCount();
    for (auto &k : g_markers) {
        if (!k.until || (int)(k.until - now) < 0) { k.until = 0; continue; }
        int vals[8];
        memcpy(vals, k.vals, sizeof(vals));
        vals[0] = PedRef(FindPlayerPed());
        char types[9] = "iffffffi";
        types[k.n - 1] = 'i';
        RunScriptCommandTyped(k.op, k.n, types, vals);
        static int said;
        if (said < 5) { said++; Log("marqueur de l'hote : commande %04X en %.1f %.1f", k.op, F(k.vals[1]), F(k.vals[2])); }
    }
}

void InstallConditions()
{
    static const uint8_t pro[] = { 0x8A, 0x81, 0xD2, 0x00, 0x00, 0x00 };
    o_UpdateCompareFlag = (UpdateCompareFlag_t)MakeDetour(0x4859D0, pro, sizeof(pro), (void *)h_UpdateCompareFlag);
}
