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

static void MergePopulation()
{
    void *me = FindPlayerPed();
    const float *mp = EntityPos(me);
    uint32_t now = GetTickCount();
    Pool *peds = *(Pool **)0xB74490;
    for (int i = 0; i < peds->size && i < 512; i++) {
        if (peds->flags[i] & 0x80) { g_pedSeen[i] = 0; continue; }
        void *ped = peds->objects + i * 0x7C4;
        bool born = Newborn(g_pedSeen, g_pedBirth, i, PedRef(ped), now);
        if (ped == me || Field<uint8_t>(ped, 0x484) != 1 || PuppetIndex(ped) >= 0 || IsMissionCopy(ped) || PedVehicle(ped)) continue;   // passant ordinaire a pied
        if (Field<int>(ped, 0x598) == 6 && OwnPolice()) continue;   // policier (PEDTYPE_COP)
        if (!OtherPopulates(EntityPos(ped))) continue;
        if (!born && (OnScreen(ped) || Dist2(EntityPos(ped), mp) < CLOSE * CLOSE)) continue;
        ((void(__cdecl *)(void *))0x610F20)(ped);   // CPopulation::RemovePed
        g_removedPeds++;
    }
    Pool *veh = *(Pool **)0xB74494;
    void *mine = PedVehicle(me);
    for (int i = 0; i < veh->size && i < 256; i++) {
        if (veh->flags[i] & 0x80) { g_vehSeen[i] = 0; continue; }
        uint8_t *v = veh->objects + i * 0xA18;
        bool born = Newborn(g_vehSeen, g_vehBirth, i, VehicleRef(v), now);
        if (v == mine || (v[0x4A4] != 1 && v[0x4A4] != 3) || IsNetVehicle(v)) continue;   // aleatoire ou garee, pas du reseau
        if ((v[0x428] & 1) && OwnPolice()) continue;   // vehicule des forces de l'ordre (bIsLawEnforcer)
        bool playerInside = PlayerOrNet(Field<void *>(v, VEH_DRIVER), me);
        for (int k = 0; k < 8 && !playerInside; k++) playerInside = PlayerOrNet(Field<void *>(v, VEH_PASSENGERS + k * 4), me);
        if (playerInside || !OtherPopulates(EntityPos(v))) continue;
        if (!born && (OnScreen(v) || Dist2(EntityPos(v), mp) < CLOSE * CLOSE)) continue;
        void *occ[9] = { Field<void *>(v, VEH_DRIVER) };
        for (int k = 0; k < 8; k++) occ[k + 1] = Field<void *>(v, VEH_PASSENGERS + k * 4);
        WorldRemove(v);
        RemoveReferencesToDeletedObject(v);
        DeleteEntity(v);
        for (void *p : occ) if (p) ((void(__cdecl *)(void *))0x610F20)(p);   // passagers ordinaires : retires avec elle
        g_removedCars++;
    }
}

// --- Invite colle a l'hote : densites a 0 ---
static float g_savedPed = 1.0f, g_savedCar = 1.0f;
static bool g_wasQuiet;

static void GuestDensity()
{
    float &ped = *(float *)0x8D2530, &car = *(float *)0x8A5B20;
    bool quiet = PopulationShared() && g_hostClose && (WantedLevel() == 0 || HostPoliceOnGuests());
    if (quiet != g_wasQuiet) {
        g_wasQuiet = quiet;
        if (quiet) { g_savedPed = ped; g_savedCar = car; }
        else { ped = g_savedPed; car = g_savedCar; }
        Log("population : %s", quiet ? "colle a l'hote, rien de local" : PopulationShared() ? "partagee, generation locale hors de la zone de l'hote" : "locale");
    }
    if (quiet) {
        if (ped != 0.0f) { g_savedPed = ped; ped = 0.0f; }   // un script a pu la changer : retenue pour la sortie
        if (car != 0.0f) { g_savedCar = car; car = 0.0f; }
    }
}

static void Report()
{
    static uint32_t lastReport;
    if (GetTickCount() - lastReport < 10000) return;
    lastReport = GetTickCount();
    bool any = PopulationShared();
    for (int i = 1; i < MAX_PLAYERS; i++) any |= g_cfg.host && g_shared[i];
    if (!any) return;
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
        "%d passants et %d vehicules retires, hote a %.0f m, recherche %d", copies, cars, local, inOther, localCars, g_removedPeds, g_removedCars, dh, WantedLevel());
    g_removedPeds = g_removedCars = 0;
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
}
