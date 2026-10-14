// Conditions de mission elargies aux invites (hote). Les scripts de mission testent le joueur de l'hote : "est-il dans
// la zone du marqueur", "est-il dans la voiture"... Si le test est faux pour lui mais vrai pour le pantin d'un invite,
// il devient vrai : n'importe quel joueur peut faire avancer la mission.
//  - Detour de CRunningScript::UpdateCompareFlag (0x4859D0, thiscall(script, bool)) : le resultat brut, avant
//    l'inversion "NOT" (+0xD2) et la logique ET/OU (+0xD0), est corrige pendant les commandes de la liste.
//  - Marqueurs au sol : quand une commande LOCATE / IS_CHAR_IN_AREA de l'hote a son drapeau "sphere", elle est envoyee
//    aux invites (MSG_MARKER, 5 fois par seconde au plus) ; chacun la rejoue a chaque image avec son propre joueur
//    (le jeu dessine le marqueur), jusqu'a 600 ms apres le dernier message.
//  - Un invite fait avancer la mission loin de l'hote (zone ou marqueur, a plus de 40 m ou dans un autre interieur) :
//    l'hote est pose a cote de lui a l'image suivante, interieur compris. Sinon la suite (cinematique, interieur) se
//    jouait chez l'hote la ou il etait, sans le decor autour (le jeu ne charge le monde qu'autour de son joueur) :
//    essai de JD et GG du 01/10, porte du coiffeur passee par l'invite, cinematique avec seulement les personnages.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "script.h"
#include "mirror.h"
#include "conditions.h"
#include "vehicles.h"
#include "hud.h"
#include <string.h>
#include <math.h>

using namespace game;

// Guest: a story mission cannot be started from a guest's game. Its main script still draws the start marker (the host
// can use it to find the place), but the "is the player standing in it" test of a mission-start marker is made false.
// A mission-start marker is a LOCATE test of the main script (not a mission script) around a contact-point blip
// (radar blip type 5), so the places of those blips are remembered (g_spots) and the test is refused there.
static float g_spots[96][2];
static int g_spotN;
static void NoteContactSpots()
{
    for (int i = 0; i < 175; i++) {
        const uint8_t *t = (const uint8_t *)0xBA86F0 + i * 0x28;
        if (((t[0x26] >> 2) & 15) != 5 || *(const uint32_t *)(t + 0x20) || (int8_t)t[0x24] == 41) continue;
        float x = *(const float *)(t + 0x08), y = *(const float *)(t + 0x0C);
        bool known = false;
        for (int k = 0; k < g_spotN && !known; k++) known = fabsf(g_spots[k][0] - x) < 2.0f && fabsf(g_spots[k][1] - y) < 2.0f;
        if (!known && g_spotN < 96) { g_spots[g_spotN][0] = x; g_spots[g_spotN][1] = y; g_spotN++; }
    }
}

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
static void *g_guardScript;
static const CondOp *g_guardCond;
static int g_guardVals[8];

void ConditionBefore(void *script, int op)
{
    g_curCond = nullptr;
    g_guardCond = nullptr;
    if (!g_cfg.host && g_cfg.lockGuestMissions && NetRunning() && !ScriptIsMission(script) && g_spotN) {
        const CondOp *c = FindCond(op);
        void *me = FindPlayerPed();
        if (c && c->shape <= 3 && me) {
            uint8_t *ip = *(uint8_t **)((uint8_t *)script + 0x14);
            if (ScriptReadValues(script, ip, c->n, g_guardVals) && g_guardVals[0] == PedRef(me)) { g_guardScript = script; g_guardCond = c; }
        }
        return;
    }
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
void ConditionAfter() { g_curCond = nullptr; g_guardCond = nullptr; }

static int g_follow = -1;   // invite a rejoindre (hote)
static uint32_t g_lastFollow;

typedef void(__thiscall *UpdateCompareFlag_t)(void *script, bool result);
static UpdateCompareFlag_t o_UpdateCompareFlag;
// Centre (x, y) of the area of a LOCATE test, from its parameters.
static void AreaCentre(const CondOp &c, const int *v, float &x, float &y)
{
    if (c.shape >= 2) { int j = c.shape == 2 ? 3 : 4; x = (F(v[1]) + F(v[j])) * 0.5f; y = (F(v[2]) + F(v[j + 1])) * 0.5f; }
    else { x = F(v[1]); y = F(v[2]); }
}
static void __fastcall h_UpdateCompareFlag(void *script, void *, bool result)
{
    if (result && g_guardCond && script == g_guardScript && !*((uint8_t *)script + 0xD2)) {
        float x, y;
        AreaCentre(*g_guardCond, g_guardVals, x, y);
        for (int k = 0; k < g_spotN; k++)
            if (fabsf(g_spots[k][0] - x) < 3.5f && fabsf(g_spots[k][1] - y) < 3.5f) {
                result = false;
                static uint32_t said;
                if (GetTickCount() - said > 10000) { said = GetTickCount(); Log("conditions : marqueur de depart de mission en %.1f %.1f : l'invite ne peut pas lancer de mission (marqueur visible)", x, y); }
                break;
            }
    }
    if (!result && g_curCond && script == g_curScript && g_valsOk) {
        for (int i = 1; i < MAX_PLAYERS; i++)
            if (void *ped = PuppetOf(i))
                if (Satisfies(ped, *g_curCond, g_vals)) {
                    result = true;
                    void *me = FindPlayerPed();
                    if (me && g_curCond->shape <= 3 && GetTickCount() - g_lastFollow > 15000) {
                        const float *a = EntityPos(me), *b = EntityPos(ped);
                        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
                        if (dx * dx + dy * dy + dz * dz > 40.0f * 40.0f || EntityArea(me) != EntityArea(ped)) g_follow = i;
                    }
                    break;
                }
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

// Pose le joueur local en (pos, heading) dans l'interieur "area" (0 : dehors), comme une commande de script le ferait :
// interieur choisi et lie au joueur (SELECT_INTERIOR 04BB, LINK_CHAR_TO_INTERIOR 0860), decor charge tout de suite autour
// du point (LOAD_SCENE 03CB : sans lui, les collisions d'un endroit lointain ou d'un interieur ne sont pas la et le
// joueur tombe sans fin : essai du 14/10, l'hote "aller vers" l'invite dans sa maison, tombe de 1014 a 936, mort), sorti
// du vehicule s'il y est (WARP_CHAR_FROM_CAR_TO_COORD 0362), vitesse remise a zero. Journalise.
void TeleportPed(void *ped, const float *pos, float heading, int area, const char *why)
{
    if (!ped) return;
    int ref = PedRef(ped);
    int cur = *(int *)0xB72914;   // CGame::currArea
    if (ped == FindPlayerPed() && area != cur) {
        RunScriptCommand(0x04BB, 1, &area);
        int link[2] = { ref, area };
        RunScriptCommand(0x0860, 2, link);
    }
    int scene[3];
    memcpy(scene, pos, 12);
    RunScriptCommandTyped(0x03CB, 3, "fff", scene);
    if (PedVehicle(ped)) {
        int warp[4] = { ref };
        memcpy(warp + 1, pos, 12);
        RunScriptCommandTyped(0x0362, 4, "ifff", warp);
    }
    EntityArea(ped) = (uint8_t)area;
    PlacePuppet(ped, pos, heading);
    memset(MoveSpeed(ped), 0, 12);
    if (ped == FindPlayerPed()) NoteLocalRelocated();
    Log("teleportation (%s) : %.1f %.1f %.1f, interieur %d (etait %d)", why ? why : "?", pos[0], pos[1], pos[2], area, cur);
}

// Hote : rejoindre l'invite qui vient de faire avancer la mission (voir en tete).
static void FollowGuest()
{
    int i = g_follow;
    g_follow = -1;
    void *me = FindPlayerPed(), *ped = PuppetOf(i);
    if (!me || !ped) return;
    g_lastFollow = GetTickCount();
    const float *b = EntityPos(ped);
    float h = Field<float>(ped, PED_ROTATION);
    float pos[3] = { b[0] + sinf(h) * 1.5f, b[1] - cosf(h) * 1.5f, b[2] };
    int area = EntityArea(ped);
    TeleportPed(me, pos, h, area, "suivre l'invite");
    const char *name = g_players[i].state.name[0] ? g_players[i].state.name : "?";
    char msg[96];
    wsprintfA(msg, PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH ? "Mission : tu rejoins %s" : "Mission: joining %s", name);
    HudToast(msg, 4000);
    Log("conditions : %s a fait avancer la mission loin de l'hote, hote pose a cote (interieur %d)", name, area);
}

void ConditionsFrame()
{
    g_onMarker = OnMarker;
    if (!g_cfg.host && g_cfg.lockGuestMissions && GameState() == 9 && FindPlayerPed()) NoteContactSpots();
    if (g_cfg.host && g_follow >= 0 && GameState() == 9) FollowGuest();
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
