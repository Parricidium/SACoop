// Population partagee (passants, circulation, police). Un invite proche de l'hote (moins de 90 m, meme interieur ;
// il en sort au-dela de 140 m) passe en population partagee :
//  - chez lui : plus de population locale (CPopulation::PedDensityMultiplier 0x8D2530 et CCarCtrl::CarDensityMultiplier
//    0x8A5B20 a 0, generateurs de voitures garees sautes : appel de CTheCarGenerators::Process en 0x53C06A), et les
//    passants / vehicules locaux deja la sont retires des qu'ils ne sont plus a l'ecran (ou a plus de 60 m), sauf sa
//    police quand celle de l'hote ne s'occupe pas de lui (PoliceHote=0 : un invite recherche garde la sienne) ;
//  - chez l'hote : ses passants et vehicules ordinaires a moins de 150 m d'un invite partage lui sont envoyes
//    (entities.cpp : MSG_PED marque PF_AMBIENT ; vehicles.cpp : vehicules reseau de l'hote), et ses copies les suivent.
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

static float Dist2(const float *a, const float *b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

// --- Etat partage, vu des deux cotes ---
static bool g_shared[MAX_PLAYERS];   // hote : invite i partage ; invite : g_shared[g_localId] = moi

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
        float d2 = Dist2(gp, hp);
        if (garea != harea) g_shared[i] = false;
        else if (g_shared[i]) g_shared[i] = d2 < 140.0f * 140.0f;
        else g_shared[i] = d2 < 90.0f * 90.0f;
    }
}

bool PopulationShared() { return !g_cfg.host && g_localId > 0 && g_shared[g_localId]; }

bool NearSharedGuest(const float *pos, float radius)
{
    if (!g_cfg.host) return false;
    for (int i = 1; i < MAX_PLAYERS; i++)
        if (g_shared[i] && Dist2(pos, g_players[i].state.pos) < radius * radius) return true;
    return false;
}

// --- Invite : population locale coupee ---
static float g_savedPed = 1.0f, g_savedCar = 1.0f;
static bool g_wasShared;

static void __cdecl h_CarGenerators()
{
    if (PopulationShared()) return;   // (voitures garees : jamais, meme recherche)
    ((void(__cdecl *)())0x6F3F40)();
}

static bool OnScreen(void *e) { return ((bool(__thiscall *)(void *))0x534540)(e); }

static void RemoveLocalPopulation()
{
    void *me = FindPlayerPed();
    const float *mp = EntityPos(me);
    Pool *peds = *(Pool **)0xB74490;
    for (int i = 0; i < peds->size; i++) {
        if (peds->flags[i] & 0x80) continue;
        void *ped = peds->objects + i * 0x7C4;
        if (ped == me || Field<uint8_t>(ped, 0x484) != 1 || PedVehicle(ped)) continue;   // passant ordinaire a pied
        if (Field<int>(ped, 0x598) == 6 && !HostPoliceOnGuests()) continue;   // policier (PEDTYPE_COP) : la police de l'invite recherche reste la sienne
        if (OnScreen(ped) && Dist2(EntityPos(ped), mp) < 60.0f * 60.0f) continue;
        ((void(__cdecl *)(void *))0x610F20)(ped);   // CPopulation::RemovePed
    }
    Pool *veh = *(Pool **)0xB74494;
    void *mine = PedVehicle(me);
    for (int i = 0; i < veh->size; i++) {
        if (veh->flags[i] & 0x80) continue;
        uint8_t *v = veh->objects + i * 0xA18;
        if (v == mine || (v[0x4A4] != 1 && v[0x4A4] != 3)) continue;   // aleatoire ou garee
        if ((v[0x428] & 1) && !HostPoliceOnGuests()) continue;   // vehicule des forces de l'ordre (bIsLawEnforcer) : idem
        if (OnScreen(v) && Dist2(EntityPos(v), mp) < 60.0f * 60.0f) continue;
        bool playerInside = false;
        void *drv = Field<void *>(v, VEH_DRIVER);
        if (drv && (drv == me || PuppetIndex(drv) >= 0 || IsMissionCopy(drv) || Field<uint8_t>(drv, 0x484) != 1)) playerInside = true;
        for (int k = 0; k < 8 && !playerInside; k++) {
            void *p = Field<void *>(v, VEH_PASSENGERS + k * 4);
            if (p && (p == me || PuppetIndex(p) >= 0 || IsMissionCopy(p) || Field<uint8_t>(p, 0x484) != 1)) playerInside = true;
        }
        if (playerInside || IsNetVehicle(v)) continue;
        WorldRemove(v);
        RemoveReferencesToDeletedObject(v);
        DeleteEntity(v);
    }
}

static void GuestFrame()
{
    float &ped = *(float *)0x8D2530, &car = *(float *)0x8A5B20;
    // Recherche : avec PoliceHote, la police de l'hote vient aussi pour l'invite (police.cpp) : rien de local. Sans,
    // l'invite recherche garde la generation de son jeu (elle amene sa police), le reste de sa population locale est
    // retire comme d'habitude.
    bool shared = PopulationShared(), quiet = shared && (WantedLevel() == 0 || HostPoliceOnGuests());
    if (quiet != g_wasShared) {
        g_wasShared = quiet;
        if (quiet) { g_savedPed = ped; g_savedCar = car; }
        else { ped = g_savedPed; car = g_savedCar; }
        Log("population %s", quiet ? "partagee (celle de l'hote)" : shared ? "partagee, generation locale (recherche)" : "locale");
    }
    if (!shared) return;
    if (quiet) {
        if (ped != 0.0f) { g_savedPed = ped; ped = 0.0f; }   // un script a pu la changer : retenue pour la sortie
        if (car != 0.0f) { g_savedCar = car; car = 0.0f; }
    }
    static uint32_t last, lastReport;
    if (GetTickCount() - last < 250) return;
    last = GetTickCount();
    RemoveLocalPopulation();
    if (GetTickCount() - lastReport >= 10000) {   // releve : copies de l'hote et population locale restante
        lastReport = GetTickCount();
        int copies = 0, local = 0, cars = 0, localCars = 0;
        Pool *peds = *(Pool **)0xB74490;
        for (int i = 0; i < peds->size; i++) {
            if (peds->flags[i] & 0x80) continue;
            void *ped = peds->objects + i * 0x7C4;
            if (IsMissionCopy(ped)) copies++; else if (Field<uint8_t>(ped, 0x484) == 1) local++;
        }
        Pool *veh = *(Pool **)0xB74494;
        for (int i = 0; i < veh->size; i++) {
            if (veh->flags[i] & 0x80) continue;
            uint8_t *v = veh->objects + i * 0xA18;
            if (IsNetVehicle(v)) cars++; else if (v[0x4A4] == 1 || v[0x4A4] == 3) localCars++;
        }
        Log("population partagee : %d personnages et %d vehicules de l'hote, %d passants et %d vehicules locaux, recherche %d", copies, cars, local, localCars, WantedLevel());
    }
}

void PopulationFrame()
{
    if (!NetRunning() || GameState() != 9 || !FindPlayerPed()) return;
    UpdateShared();
    if (g_cfg.host) HostRegisterAmbientVehicles();
    else GuestFrame();
}

void InstallPopulation()
{
    PatchCall(0x53C06A, (void *)h_CarGenerators);
}
