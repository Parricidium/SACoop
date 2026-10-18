// Population fusionnee (passants, circulation, police). Chaque point du monde est peuple par un seul joueur :
//  - l'hote, a moins de HOST_ZONE (110 m) de lui ;
//  - au-dela, le joueur le plus proche (meme interieur).
// Un invite a moins de HOST_ZONE + 160 m de l'hote (il en sort a + 190 m) est "partage" : l'hote lui envoie ses
// passants et vehicules ordinaires de sa zone a moins de 150 m de lui (entities.cpp : MSG_PED marque PF_AMBIENT ;
// vehicles.cpp : vehicules reseau de l'hote), et ses copies les suivent. Chacun retire ce que son jeu fait naitre
// dans la zone d'un autre : tout de suite s'il vient de naitre (hors de l'ecran : invisible), sinon des qu'il n'est
// plus a l'ecran et a plus de 40 m. Rien ne disparait sous les yeux, et rien n'est en double.
//  - Invite colle a l'hote (moins de 40 m) : densites a 0 (CPopulation::PedDensityMultiplier 0x8D2530,
//    CCarCtrl::CarDensityMultiplier 0x8A5B20) et generateurs de voitures garees sautes (appel de
//    CTheCarGenerators::Process en 0x53C06A) : tout ce qu'il ferait naitre serait dans la zone de l'hote.
//  - Police : un invite recherche garde la sienne quand celle de l'hote ne s'occupe pas de lui (PoliceHote=0) ; l'hote
//    garde toujours la sienne (elle poursuit aussi les invites, police.cpp).
// Loin de l'hote, chacun garde sa propre population.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "entities.h"
#include "vehicles.h"
#include "population.h"
#include "police.h"
#include <math.h>
#include <string.h>

using namespace game;

static const float HOST_ZONE = 110.0f, CLOSE = 40.0f;

static float Dist2(const float *a, const float *b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}
static float Dist2D(const float *a, const float *b) { float dx = a[0] - b[0], dy = a[1] - b[1]; return dx * dx + dy * dy; }

// --- Etat partage, vu des deux cotes ---
static bool g_shared[MAX_PLAYERS];   // hote : invite i partage ; invite : g_shared[g_localId] = moi
static bool g_hostClose;             // invite : a moins de CLOSE de l'hote

static void UpdateShared()
{
    void *me = FindPlayerPed();
    if (!me) return;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i == 0) continue;
        bool guestHere = g_cfg.host ? g_players[i].connected && g_players[i].state.inGame : i == g_localId;
        const NetPlayer &host = g_players[0];
        if (!guestHere || (!g_cfg.host && !(host.connected && host.state.inGame))) { g_shared[i] = false; continue; }
        const float *gp = g_cfg.host ? g_players[i].state.pos : EntityPos(me);
        const float *hp = g_cfg.host ? EntityPos(me) : host.state.pos;
        int garea = g_cfg.host ? g_players[i].state.area : EntityArea(me), harea = g_cfg.host ? EntityArea(me) : host.state.area;
        float d2 = Dist2(gp, hp), in = HOST_ZONE + 160.0f, out = HOST_ZONE + 190.0f;
        if (garea != harea) g_shared[i] = false;
        else if (g_shared[i]) g_shared[i] = d2 < out * out;
        else g_shared[i] = d2 < in * in;
        if (!g_cfg.host) g_hostClose = g_shared[i] && d2 < CLOSE * CLOSE;
    }
}

bool PopulationShared() { return !g_cfg.host && g_localId > 0 && g_shared[g_localId]; }

// Hote : un point de sa zone (marge de 20 m pour ce qui en sort) pres d'un invite partage ?
bool NearSharedGuest(const float *pos, float radius)
{
    if (!g_cfg.host) return false;
    void *me = FindPlayerPed();
    if (!me || Dist2D(pos, EntityPos(me)) > (HOST_ZONE + 20.0f) * (HOST_ZONE + 20.0f)) return false;
    for (int i = 1; i < MAX_PLAYERS; i++)
        if (g_shared[i] && Dist2(pos, g_players[i].state.pos) < radius * radius) return true;
    return false;
}

// Hote : un invite partage est-il a moins de radius (sans condition de zone) ?
bool NearSharedGuestAnywhere(const float *pos, float radius)
{
    if (!g_cfg.host) return false;
    for (int i = 1; i < MAX_PLAYERS; i++)
        if (g_shared[i] && Dist2(pos, g_players[i].state.pos) < radius * radius) return true;
    return false;
}

// Vrai si un AUTRE joueur peuple ce point : invite -> dans la zone de l'hote (partage) ; hote -> hors de sa zone et
// un invite partage en est plus proche.
static bool OtherPopulates(const float *pos)
{
    void *me = FindPlayerPed();
    if (!me) return false;
    if (!g_cfg.host) return PopulationShared() && Dist2D(pos, g_players[0].state.pos) < HOST_ZONE * HOST_ZONE;
    float dh = Dist2D(pos, EntityPos(me));
    if (dh < HOST_ZONE * HOST_ZONE) return false;
    for (int i = 1; i < MAX_PLAYERS; i++)   // (un invite colle a l'hote ne fait rien naitre : il ne peuple rien)
        if (g_shared[i] && Dist2D(pos, g_players[i].state.pos) < dh && Dist2D(g_players[i].state.pos, EntityPos(me)) >= CLOSE * CLOSE) return true;
    return false;
}

static void __cdecl h_CarGenerators()
{
    if (PopulationShared() && g_hostClose) return;   // (voitures garees : jamais, meme recherche)
    ((void(__cdecl *)())0x6F3F40)();
}

static bool OnScreen(void *e) { return ((bool(__thiscall *)(void *))0x534540)(e); }

// Un invite recherche a sa propre police (PoliceHote=0 cote invite) ; l'hote garde toujours la sienne.
static bool OwnPolice() { return g_cfg.host || (WantedLevel() > 0 && !HostPoliceOnGuests()); }

// "Vient de naitre" : reference vue dans cette case depuis moins de 1,5 s.
static uint16_t g_pedSeen[512], g_vehSeen[256];
static uint32_t g_pedBirth[512], g_vehBirth[256];
static bool Newborn(uint16_t *seen, uint32_t *birth, int slot, int ref, uint32_t now)
{
    uint16_t h = (uint16_t)(ref & 0xFFFF);
    if (seen[slot] != h) { seen[slot] = h; birth[slot] = now; }
    return now - birth[slot] < 1500;
}

static bool PlayerOrNet(void *p, void *me) { return p && (p == me || PuppetIndex(p) >= 0 || IsMissionCopy(p) || Field<uint8_t>(p, 0x484) != 1); }

static int g_removedPeds, g_removedCars;
static int g_keptPeds, g_keptCars, g_guardedPeds;   // (host: guests' view, below)

// Resync (0.54.39, reports of 15/10: "when the client or the host teleports to the other, massive desync, it doesn't
// re-sync"; "out of the host's sight, both see different cars"). What our game made in another player's zone used to be
// removed only once off screen or 40 m away: a guest looking at his own cars and people kept them for good, next to the
// host's copies. Now:
//  - right after a jump of either side (ours, or the host's / a guest's position moving more than 40 m at once: teleport,
//    go to player, a script move), for 3 s everything of ours in another's zone goes, on screen or not;
//  - otherwise what has stayed in another's zone more than 6 s goes, on screen or not, unless within 25 m of us.
// (Never a vehicle with a player or a copy inside, nor our police while it chases us. Guest side only for now: see StaleInOther.)
static uint32_t g_purgeUntil;
static uint32_t g_pedOtherSince[512], g_vehOtherSince[256];
static void WatchJumps(uint32_t now)
{
    static float last[MAX_PLAYERS][3];
    static bool have[MAX_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const float *p = nullptr;
        if (i == g_localId) { if (void *me = FindPlayerPed()) p = EntityPos(me); }
        else if (g_players[i].connected && g_players[i].state.inGame) p = g_players[i].state.pos;
        if (!p) { have[i] = false; continue; }
        if (have[i] && Dist2(p, last[i]) > 40.0f * 40.0f) {
            g_purgeUntil = now + 3000;
            static int said;
            if (said < 20) { said++; Log("population : %s a saute de %.0f m, resynchronisation (3 s)", i == g_localId ? "nous" : g_players[i].state.name, sqrtf(Dist2(p, last[i]))); }
        }
        memcpy(last[i], p, 12);
        have[i] = true;
    }
}

static bool StaleInOther(uint32_t *since, int slot, bool inOther, bool born, bool onScreen, float d2, uint32_t now)
{
    if (!inOther) { since[slot] = 0; return false; }
    if (!since[slot]) since[slot] = now;
    // The host keeps the old rule (what he sees stays): the guests do not send him their own population yet, so taking his
    // away where a guest populates would leave him looking at empty streets.
    if (g_cfg.host) return born || (!onScreen && d2 >= CLOSE * CLOSE);
    if (born || (int)(g_purgeUntil - now) > 0) return true;
    if (!onScreen && d2 >= CLOSE * CLOSE) return true;
    return now - since[slot] > 6000 && d2 > 25.0f * 25.0f;
}

static void MergePopulation()
{
    void *me = FindPlayerPed();
    const float *mp = EntityPos(me);
    uint32_t now = GetTickCount();
    WatchJumps(now);
    Pool *peds = *(Pool **)0xB74490;
    for (int i = 0; i < peds->size && i < 512; i++) {
        if (peds->flags[i] & 0x80) { g_pedSeen[i] = 0; g_pedOtherSince[i] = 0; continue; }
        void *ped = peds->objects + i * 0x7C4;
        bool born = Newborn(g_pedSeen, g_pedBirth, i, PedRef(ped), now);
        if (ped == me || Field<uint8_t>(ped, 0x484) != 1 || PuppetIndex(ped) >= 0 || IsMissionCopy(ped) || PedVehicle(ped)) continue;   // passant ordinaire a pied
        if (Field<int>(ped, 0x598) == 6 && OwnPolice()) continue;   // policier (PEDTYPE_COP)
        if (!StaleInOther(g_pedOtherSince, i, OtherPopulates(EntityPos(ped)), born, OnScreen(ped), Dist2(EntityPos(ped), mp), now)) continue;
        ((void(__cdecl *)(void *))0x610F20)(ped);   // CPopulation::RemovePed
        g_removedPeds++;
    }
    Pool *veh = *(Pool **)0xB74494;
    void *mine = PedVehicle(me);
    for (int i = 0; i < veh->size && i < 256; i++) {
        if (veh->flags[i] & 0x80) { g_vehSeen[i] = 0; g_vehOtherSince[i] = 0; continue; }
        uint8_t *v = veh->objects + i * 0xA18;
        bool born = Newborn(g_vehSeen, g_vehBirth, i, VehicleRef(v), now);
        if (v == mine || (v[0x4A4] != 1 && v[0x4A4] != 3) || IsTrain(v) || IsNetVehicle(v)) continue;   // aleatoire ou garee, pas du reseau (jamais un wagon de train : trains.cpp)
        if ((v[0x428] & 1) && OwnPolice()) continue;   // vehicule des forces de l'ordre (bIsLawEnforcer)
        bool playerInside = PlayerOrNet(Field<void *>(v, VEH_DRIVER), me);
        for (int k = 0; k < 8 && !playerInside; k++) playerInside = PlayerOrNet(Field<void *>(v, VEH_PASSENGERS + k * 4), me);
        if (playerInside) { g_vehOtherSince[i] = 0; continue; }
        if (!StaleInOther(g_vehOtherSince, i, OtherPopulates(EntityPos(v)), born, OnScreen(v), Dist2(EntityPos(v), mp), now)) continue;
        void *occ[9] = { Field<void *>(v, VEH_DRIVER) };
        for (int k = 0; k < 8; k++) occ[k + 1] = Field<void *>(v, VEH_PASSENGERS + k * 4);
        WorldRemove(v);
        RemoveReferencesToDeletedObject(v);
        DeleteEntity(v);
        for (void *p : occ) if (p) ((void(__cdecl *)(void *))0x610F20)(p);   // passagers ordinaires : retires avec elle
        g_removedCars++;
    }
}

// Guest: the host's script cleared an area (CLEAR_AREA 0395, CLEAR_AREA_OF_CARS 03BA, CLEAR_AREA_OF_CHARS 042B), typically
// before a cutscene or at the start of a scene. The guest has his own passers-by and parked cars there (the area is the host's
// to populate): they go too, on screen or not, or the guest watches people next to the van that the host does not have (Home
// Invasion, 14/10). Only ordinary local ones: never the player, a puppet, a mission character's copy, a networked vehicle, the
// vehicle the guest is in, and the guest's own police while he is wanted. centre/half: box (round: sphere of radius half[0]).
int PopulationClearArea(const float *centre, const float *half, bool round, bool peds, bool cars)
{
    void *me = FindPlayerPed();
    if (!me) return 0;
    auto inside = [&](const float *p) {
        float dx = p[0] - centre[0], dy = p[1] - centre[1], dz = p[2] - centre[2];
        if (round) return dx * dx + dy * dy + dz * dz <= half[0] * half[0];
        return fabsf(dx) <= half[0] && fabsf(dy) <= half[1] && fabsf(dz) <= half[2];
    };
    int n = 0;
    if (peds) {
        Pool *pool = *(Pool **)0xB74490;
        for (int i = 0; i < pool->size && i < 512; i++) {
            if (pool->flags[i] & 0x80) continue;
            void *ped = pool->objects + i * 0x7C4;
            if (ped == me || Field<uint8_t>(ped, 0x484) != 1 || PuppetIndex(ped) >= 0 || IsMissionCopy(ped) || PedVehicle(ped)) continue;
            if (Field<int>(ped, 0x598) == 6 && OwnPolice()) continue;
            if (!inside(EntityPos(ped))) continue;
            ((void(__cdecl *)(void *))0x610F20)(ped);   // CPopulation::RemovePed
            g_removedPeds++;
            n++;
        }
    }
    if (cars) {
        Pool *veh = *(Pool **)0xB74494;
        void *mine = PedVehicle(me);
        for (int i = 0; i < veh->size && i < 256; i++) {
            if (veh->flags[i] & 0x80) continue;
            uint8_t *v = veh->objects + i * 0xA18;
            if (v == mine || (v[0x4A4] != 1 && v[0x4A4] != 3) || IsTrain(v) || IsNetVehicle(v)) continue;
            if ((v[0x428] & 1) && OwnPolice()) continue;
            bool playerInside = PlayerOrNet(Field<void *>(v, VEH_DRIVER), me);
            for (int k = 0; k < 8 && !playerInside; k++) playerInside = PlayerOrNet(Field<void *>(v, VEH_PASSENGERS + k * 4), me);
            if (playerInside || !inside(EntityPos(v))) continue;
            void *occ[9] = { Field<void *>(v, VEH_DRIVER) };
            for (int k = 0; k < 8; k++) occ[k + 1] = Field<void *>(v, VEH_PASSENGERS + k * 4);
            WorldRemove(v);
            RemoveReferencesToDeletedObject(v);
            DeleteEntity(v);
            for (void *p : occ) if (p) ((void(__cdecl *)(void *))0x610F20)(p);
            g_removedCars++;
            n++;
        }
    }
    return n;
}

// --- Densites de l'invite ---
// Colle a l'hote : 0 (tout ce qu'il ferait naitre serait dans la zone de l'hote). Dans la zone de l'hote : celles de
// la mission de l'hote (03DE / 01EB, recues par le miroir). Ailleurs : 1, sa propre population. Avant, celles de la
// mission s'appliquaient partout (souvent 0) : loin de l'hote, ni passants ni circulation (test du 04/10). Etat
// change avec une marge (35 / 45 m) : il basculait plusieurs fois par seconde autour de 40 m.
static float g_hostPed = 1.0f, g_hostCar = 1.0f;
static int g_densityMode = -1, g_densityModeApplied = -1;

void PopulationHostDensity(bool car, float value)
{
    if (value < 0.0f || value > 10.0f) return;
    (car ? g_hostCar : g_hostPed) = value;
    Log("population : densite %s de la mission de l'hote %.2f", car ? "des vehicules" : "des passants", value);
}

static void GuestDensity()
{
    float &ped = *(float *)0x8D2530, &car = *(float *)0x8A5B20;
    const NetPlayer &h = g_players[0];
    void *me = FindPlayerPed();
    float d2 = me && h.connected ? Dist2(EntityPos(me), h.state.pos) : 1e12f;
    static bool close;
    close = PopulationShared() && d2 < (close ? 45.0f * 45.0f : 35.0f * 35.0f);
    bool quiet = close && (WantedLevel() == 0 || HostPoliceOnGuests());
    bool hostZone = PopulationShared() && d2 < HOST_ZONE * HOST_ZONE;
    int mode = quiet ? 0 : hostZone ? 1 : 2;
    if (mode != g_densityMode) {
        g_densityMode = mode;
        Log("population : %s", mode == 0 ? "colle a l'hote, rien de local" : mode == 1 ? "dans la zone de l'hote (densites de sa mission)" : PopulationShared() ? "partagee, generation locale hors de la zone de l'hote" : "locale");
    }
    // (colle : tenu a chaque image ; zone de l'hote : a chaque changement de la mission ; ailleurs : seulement en y
    // entrant, une mission annexe de l'invite garde ses propres reglages)
    static float lastHostPed = -1, lastHostCar = -1;
    bool changed = mode != g_densityModeApplied || (mode == 1 && (lastHostPed != g_hostPed || lastHostCar != g_hostCar));
    g_densityModeApplied = mode;
    lastHostPed = g_hostPed; lastHostCar = g_hostCar;
    if (mode == 0 || changed) {
        ped = mode == 0 ? 0.0f : mode == 1 ? g_hostPed : 1.0f;
        car = mode == 0 ? 0.0f : mode == 1 ? g_hostCar : 1.0f;
    }
}

static void Report()
{
    static uint32_t lastReport;
    if (GetTickCount() - lastReport < 10000) return;
    lastReport = GetTickCount();
    bool any = PopulationShared();
    for (int i = 1; i < MAX_PLAYERS; i++) any |= g_cfg.host && g_shared[i];
    static int quiet;
    if (!any && (g_cfg.host || ++quiet % 3)) return;   // (a guest away from the host: every 30 s, to see his own population too)
    int copies = 0, local = 0, inOther = 0, cars = 0, localCars = 0;
    Pool *peds = *(Pool **)0xB74490;
    for (int i = 0; i < peds->size; i++) {
        if (peds->flags[i] & 0x80) continue;
        void *ped = peds->objects + i * 0x7C4;
        if (IsMissionCopy(ped)) copies++;
        else if (Field<uint8_t>(ped, 0x484) == 1 && PuppetIndex(ped) < 0) { local++; if (OtherPopulates(EntityPos(ped))) inOther++; }
    }
    Pool *veh = *(Pool **)0xB74494;
    for (int i = 0; i < veh->size; i++) {
        if (veh->flags[i] & 0x80) continue;
        uint8_t *v = veh->objects + i * 0xA18;
        if (IsNetVehicle(v)) cars++; else if (v[0x4A4] == 1 || v[0x4A4] == 3) localCars++;
    }
    float dh = g_cfg.host ? 0.0f : sqrtf(Dist2D(EntityPos(FindPlayerPed()), g_players[0].state.pos));
    Log("population fusionnee : %d personnages et %d vehicules du reseau, %d passants (%d dans la zone d'un autre) et %d vehicules locaux, "
        "%d passants et %d vehicules retires, hote a %.0f m, recherche %d, densites passants %.2f vehicules %.2f (mode %d, mission de l'hote %.2f / %.2f)",
        copies, cars, local, inOther, localCars, g_removedPeds, g_removedCars, dh, WantedLevel(), *(float *)0x8D2530, *(float *)0x8A5B20, g_densityMode, g_hostPed, g_hostCar);
    if (g_cfg.host && (g_keptPeds || g_keptCars || g_guardedPeds))
        Log("population : vus par un invite, %d retraits de passants et %d de voitures evites, %d passants nes sous ses yeux retires", g_keptPeds, g_keptCars, g_guardedPeds);
    g_removedPeds = g_removedCars = 0;
    g_keptPeds = g_keptCars = g_guardedPeds = 0;
}

// --- The guests' view on the host's population (0.54.39) ---
// The host's game makes and removes people and traffic out of ITS camera's view only. A guest near him looking another way
// (behind, when they travel together) saw them appear and vanish (reports of 15/10). Each guest sends where his camera looks
// (MsgState.camFwd). On the host:
//  - what a guest sees (within nearR of him, or within farR in a 130-degree cone around his camera) is not removed by the
//    game: CPopulation::ManagePopulation's removal (call of CPopulation::RemovePed at 0x612227) and CCarCtrl::RemoveDistantCars'
//    call of PossiblyRemoveVehicle (0x42CD55) are skipped for it (checked in gta_sa.exe 1.0 US); it goes once out of sight;
//  - a person of ours born in a guest's view (within 70 m, or 25 m around him) is removed before it is ever sent
//    (PopulationSpawnGuard), as the cars are (vehicles.cpp HostTrafficGuard, now with the guest's camera too).
bool GuestSees(const float *pos, float nearR, float farR)
{
    if (!g_cfg.host) return false;
    for (int g = 1; g < MAX_PLAYERS; g++) {
        const NetPlayer &np = g_players[g];
        if (!np.connected || !np.state.inGame) continue;
        float dx = pos[0] - np.state.pos[0], dy = pos[1] - np.state.pos[1], dz = pos[2] - np.state.pos[2];
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < nearR * nearR) return true;
        if (d2 > farR * farR) continue;
        float fx = np.state.camFwd[0], fy = np.state.camFwd[1];
        if (fx == 0 && fy == 0) { fx = -sinf(np.state.heading); fy = cosf(np.state.heading); }
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 0.1f || (dx * fx + dy * fy) / d > 0.42f) return true;   // (cos 65 degrees)
    }
    return false;
}

static void __cdecl h_ManageRemovePed(void *ped)
{
    if (NetRunning() && ped && GuestSees(EntityPos(ped), 45.0f, 150.0f)) { g_keptPeds++; return; }
    ((void(__cdecl *)(void *))0x610F20)(ped);   // CPopulation::RemovePed
}
static void __cdecl h_PossiblyRemoveVehicle(void *veh)
{
    if (NetRunning() && veh && GuestSees(EntityPos(veh), 50.0f, 200.0f)) { g_keptCars++; return; }
    ((void(__cdecl *)(void *))0x424F80)(veh);
}

void PopulationSpawnGuard()
{
    if (!g_cfg.host || !NetRunning() || GameState() != 9 || !FindPlayerPed() || !g_cfg.trafficGuardRange) return;
    static uint16_t seen[512];
    static bool primed;
    Pool *peds = *(Pool **)0xB74490;
    void *me = FindPlayerPed();
    bool inside = EntityArea(me) != 0;   // (indoors: the bars, the casino keep their crowd, made under everybody's eyes)
    float range = g_cfg.trafficGuardRange < 70 ? (float)g_cfg.trafficGuardRange : 70.0f;
    for (int i = 0; i < peds->size && i < 512; i++) {
        if (peds->flags[i] & 0x80) { seen[i] = 0; continue; }
        void *ped = peds->objects + i * 0x7C4;
        uint16_t h = (uint16_t)(PedRef(ped) & 0xFFFF);
        if (seen[i] == h) continue;
        seen[i] = h;
        if (!primed || inside || ped == me || Field<uint8_t>(ped, 0x484) != 1 || PuppetIndex(ped) >= 0 || IsMissionCopy(ped) || PedVehicle(ped)) continue;
        if (Field<int>(ped, 0x598) == 6) continue;   // (police: the chase must go on)
        float pr = range;   // (0.54.79 : a guest riding with the host looks where the host's game spawns people: only the very close ones go)
        if (const float *hp = EntityPos(me)) {
            for (int g = 1; g < MAX_PLAYERS; g++) {
                if (!g_players[g].connected || !g_players[g].state.inGame) continue;
                float hx = hp[0] - g_players[g].state.pos[0], hy = hp[1] - g_players[g].state.pos[1];
                if (hx * hx + hy * hy < 25.0f * 25.0f) { pr = pr < 28.0f ? pr : 28.0f; break; }
            }
        }
        if (!GuestSees(EntityPos(ped), 25.0f, pr)) continue;
        ((void(__cdecl *)(void *))0x610F20)(ped);
        g_guardedPeds++;
    }
    primed = true;
}

void PopulationFrame()
{
    if (!NetRunning() || GameState() != 9 || !FindPlayerPed()) return;
    UpdateShared();
    if (g_cfg.host) HostRegisterAmbientVehicles();
    else GuestDensity();
    static uint32_t last;
    if (GetTickCount() - last < 250) return;
    last = GetTickCount();
    MergePopulation();
    Report();
}

void InstallPopulation()
{
    PatchCall(0x53C06A, (void *)h_CarGenerators);
    // (each call checked first: another exe or another mod there, nothing is patched)
    auto callsTo = [](uintptr_t at, uintptr_t to) { return *(const uint8_t *)at == 0xE8 && at + 5 + *(const int32_t *)(at + 1) == to; };
    if (callsTo(0x612227, 0x610F20)) PatchCall(0x612227, (void *)h_ManageRemovePed); else Log("population : appel 0x612227 inattendu, gens vus par un invite non gardes");
    if (callsTo(0x42CD55, 0x424F80)) PatchCall(0x42CD55, (void *)h_PossiblyRemoveVehicle); else Log("population : appel 0x42CD55 inattendu, voitures vues par un invite non gardees");
}
