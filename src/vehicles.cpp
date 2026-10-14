// Vehicules partages. Chaque vehicule occupe par un joueur recoit un identifiant reseau (joueur << 24 | compteur).
// Son proprietaire (celui qui conduit, ou le dernier a l'avoir conduit) envoie son etat (MSG_VEHICLE) 30 fois par
// seconde en roulant, 2 fois par seconde dans les 10 s qui suivent. Chez les autres, une copie le suit : vehicule de
// mission cree comme par un script (CCarCtrl::CreateCarForScript 0x431F80), en etat "abandonne" (aucune IA ne le
// conduit), recale vers la position recue a chaque image. Monter au volant d'une copie en prend la propriete : le
// proprietaire precedent voit arriver des messages d'un autre proprietaire pour le meme identifiant et transforme
// son vehicule en copie.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "vehicles.h"
#include "hud.h"
#include "population.h"
#include "peds.h"
#include "mirror.h"
#include "interp.h"
#include <math.h>
#include <string.h>

using namespace game;

enum { MAX_NETVEH = 64 };
struct NetVeh {
    uint32_t id;         // 0 : case libre
    void *veh;
    int ref;             // reference de pool (le vehicule existe-t-il encore ?)
    uint8_t owner;       // joueur proprietaire
    uint32_t lastRecv;   // GetTickCount de la derniere reception (copies)
    uint32_t lastSend;   // GetTickCount du dernier envoi (vehicules a nous)
    uint32_t lastDriven; // GetTickCount de la derniere image ou un joueur local l'occupait
    MsgVehicle last;     // dernier etat recu (copies)
    bool damaged;        // le dernier etat recu portait des degats (une remise a neuf = reparation)
    bool mission;        // vehicule de mission de l'hote : envoye tant qu'il existe
    bool ambient;        // vehicule ordinaire de l'hote pres d'un invite partage (population.cpp) : envoye tant qu'il y est
    uint32_t blownAt;    // derniere demande d'explosion de la copie
    float sentPos[3];    // (vehicules a nous) position au dernier envoi, et quand : vitesse reelle
    uint32_t sentAt;
    bool cutHidden;      // (copies) cachee par nous pendant une cinematique (CutsceneHide)
    uint32_t blockedSince; // (copies) depuis quand un vehicule de chez nous occupe l'emplacement de la copie
    uint32_t nearSince;    // (copies) depuis quand le joueur local est a moins de 4 m de l'emplacement de la copie
    uint8_t waitLogged;    // (copies) raisons d'attente deja journalisees (1 : joueur trop pres, 2 : place occupee)
    Track track;         // (copies) etats recus horodates : interpolation apres la physique (interp.cpp)
};
static NetVeh g_veh[MAX_NETVEH];
static uint32_t g_vehCounter;

// --- Pool des vehicules (0xB74494, cases de 0xA18) ---
static int VehRef(void *v)
{
    Pool *p = *(Pool **)0xB74494;
    int i = (int)(((uint8_t *)v - p->objects) / 0xA18);
    return (i << 8) | p->flags[i];
}
static void *VehFromRef(int ref)
{
    Pool *p = *(Pool **)0xB74494;
    int i = ref >> 8;
    if (i < 0 || i >= p->size || (p->flags[i] & 0x80) || p->flags[i] != (ref & 0xFF)) return nullptr;
    return p->objects + i * 0xA18;
}
static bool Alive(NetVeh &n) { return n.id && n.veh && VehFromRef(n.ref) == n.veh; }

static NetVeh *FindById(uint32_t id)
{
    for (auto &n : g_veh) if (n.id == id) return &n;
    return nullptr;
}
static NetVeh *FindByVeh(void *v)
{
    for (auto &n : g_veh) if (n.id && n.veh == v && Alive(n)) return &n;
    return nullptr;
}
static NetVeh *NewSlot()
{
    for (auto &n : g_veh) if (!n.id || !Alive(n)) { memset(&n, 0, sizeof(n)); return &n; }
    return nullptr;
}

// Copie gelee : CPhysical::bDontApplySpeed (+0x40, bit 13). La physique du jeu ne deplace plus la copie (elle remettait
// sa vitesse a zero et la laissait derive entre deux messages, a-coups et etincelles pour un passager) ; sa pose vient
// du proprietaire (VehiclesAfterProcess). Retire quand le joueur local en prend le volant.
static void FreezeCopy(void *v, bool on)
{
    uint32_t &f = *(uint32_t *)((uint8_t *)v + 0x40);
    if (on) f |= 0x2000; else f &= ~0x2000u;
}

// Vehicule (copie) dont le handle de script chez le joueur owner vaut ref (miroir des missions).
void *NetVehicleByOwnerRef(int owner, int ref)
{
    for (auto &n : g_veh)
        if (n.id && n.owner == owner && n.last.ownerRef == ref && Alive(n)) return n.veh;
    return nullptr;
}
int VehicleRef(void *veh) { return VehRef(veh); }

// Copie d'un vehicule de mission de l'hote (autotests).
void *AnyMissionVehicleCopy()
{
    for (auto &n : g_veh)
        if (n.id && n.owner != g_localId && (n.last.flags & VF_MISSION) && Alive(n)) return n.veh;
    return nullptr;
}

void *NetVehicleById(uint32_t id)
{
    NetVeh *n = FindById(id);
    return n && Alive(*n) ? n->veh : nullptr;
}

// Identifiant reseau du vehicule occupe par le joueur local (enregistre au besoin : il en devient proprietaire).
uint32_t LocalVehicleId(void *veh, bool driver)
{
    if (IsTrain(veh)) return 0;   // trains: trains.cpp
    NetVeh *n = FindByVeh(veh);
    if (!n) {
        n = NewSlot();
        if (!n) return 0;
        n->id = ((uint32_t)(g_localId < 0 ? 0 : g_localId) << 24) | (++g_vehCounter & 0xFFFFFF);
        n->veh = veh;
        n->ref = VehRef(veh);
        n->owner = (uint8_t)g_localId;
        Log("vehicule %08X (modele %d, %p, cree par %d) enregistre", n->id, *(int16_t *)((uint8_t *)veh + 0x22), veh, ((uint8_t *)veh)[0x4A4]);
    } else if (driver && n->owner != g_localId) {
        n->owner = (uint8_t)g_localId;   // au volant d'une copie : elle devient la notre
        n->track.Clear();
        FreezeCopy(veh, false);
        Log("vehicule %08X : on en prend la propriete", n->id);
    }
    n->lastDriven = GetTickCount();
    return n->id;
}

// Hote : vehicule de mission (CreatedBy +0x4A4 == 2) ou occupe par un personnage de mission. Il devient un vehicule
// reseau de l'hote, envoye tant qu'il existe (chez les invites, sa copie disparait 3 s apres le dernier message).
bool IsNetVehicle(void *veh) { return FindByVeh(veh) != nullptr; }
bool IsRemoteVehicle(void *veh) { NetVeh *n = FindByVeh(veh); return n && n->owner != g_localId; }   // copie du vehicule d'un autre joueur

uint32_t HostVehicleId(void *veh, bool occupied, bool ambient)
{
    if (IsTrain(veh)) return 0;   // trains: trains.cpp
    NetVeh *n = FindByVeh(veh);
    if (n && n->owner != g_localId) return n->id;   // copie du vehicule d'un invite
    if (!n) {
        n = NewSlot();
        if (!n) return 0;
        n->id = ((uint32_t)(g_localId < 0 ? 0 : g_localId) << 24) | (++g_vehCounter & 0xFFFFFF);
        n->veh = veh;
        n->ref = VehRef(veh);
        n->owner = (uint8_t)g_localId;
        Log("vehicule de mission %08X (modele %d, %p, cree par %d) enregistre", n->id, *(int16_t *)((uint8_t *)veh + 0x22), veh, ((uint8_t *)veh)[0x4A4]);
    }
    if (ambient && !n->mission) n->ambient = true;
    else { n->mission = true; n->ambient = false; }
    if (occupied) n->lastDriven = GetTickCount();
    return n->id;
}

// Hote : vehicules ordinaires (CreatedBy 1 aleatoire, 3 gare) a moins de 150 m d'un invite partage -> vehicules reseau ;
// oublies au-dela de 200 m (leur copie disparait chez l'invite 3 s plus tard).
void HostRegisterAmbientVehicles()
{
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (now - last < 500) return;
    last = now;
    Pool *p = *(Pool **)0xB74494;
    for (int i = 0; i < p->size; i++) {
        if (p->flags[i] & 0x80) continue;
        uint8_t *v = p->objects + i * 0xA18;
        if ((v[0x4A4] == 1 || v[0x4A4] == 3) && !IsTrain(v) && !FindByVeh(v) && NearSharedGuest(EntityPos(v), 150.0f)) HostVehicleId(v, false, true);
    }
    for (auto &n : g_veh) {
        if (!n.id || !n.ambient || n.owner != g_localId) continue;
        if (!Alive(n)) { memset(&n, 0, sizeof(n)); continue; }
        if (now - n.lastDriven > 2000 && !NearSharedGuest(EntityPos(n.veh), 200.0f)) memset(&n, 0, sizeof(n));
    }
}

// Hote : tous les vehicules de mission du pool (0xB74494) deviennent des vehicules reseau.
void HostRegisterMissionVehicles()
{
    Pool *p = *(Pool **)0xB74494;
    for (int i = 0; i < p->size; i++) {
        if (p->flags[i] & 0x80) continue;
        uint8_t *v = p->objects + i * 0xA18;
        if (v[0x4A4] == 2 && !IsTrain(v) && !FindByVeh(v)) HostVehicleId(v, false, false);
    }
}

static void MatrixOf(void *e, float *right, float *fwd)
{
    uint8_t *m = *(uint8_t **)((uint8_t *)e + 0x14);
    if (!m) return;
    memcpy(right, m, 12);
    memcpy(fwd, m + 0x10, 12);
}

// CAutomobile et derives (types 0-4 : voiture, monster truck, quad, helico, avion ; 11 : remorque) : CDamageManager
// en +0x5A0 (+5 roues, +9 portieres, +0x10 phares, +0x14 panneaux, 4 bits chacun).
static bool HasDamageManager(const uint8_t *v)
{
    int type = *(const int *)(v + 0x590);
    return type <= 4 || type == 11;
}
static bool AnyDamage(const MsgVehicle &m)
{
    if (!(m.flags & VF_DAMAGE)) return false;
    for (int k = 0; k < 4; k++) if (m.wheels[k]) return true;
    for (int k = 0; k < 6; k++) if (m.doors[k]) return true;
    return m.lights || m.panels;
}

// Un autre joueur en partie a moins de radius (m) de ce point (meme interieur, ou presque) ?
static bool OtherPlayerNear(const float *pos, float radius)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i == g_localId || !g_players[i].connected || !g_players[i].state.inGame) continue;
        const float *q = g_players[i].state.pos;
        float dx = pos[0] - q[0], dy = pos[1] - q[1];
        if (dx * dx + dy * dy < radius * radius) return true;
    }
    return false;
}

// Vehicule pose par le joueur local (menu du jeu, code de triche) : vehicule reseau envoye tant qu'il existe, comme un
// vehicule de mission de l'hote. Avant, seul le vehicule occupe etait partage : une voiture posee n'etait vue de
// l'autre joueur que lorsque son proprietaire y montait, et disparaissait quand il en descendait.
// The vehicles this player spawned (F10 menu, cheat code), oldest first: the F10 buttons "remove my last vehicle" and
// "remove all my vehicles" (VehiclesRemoveSpawned).
struct Spawned { void *veh; int ref; uint32_t id; };
static Spawned g_spawned[24];
static int g_spawnedN;
static void NoteSpawned(void *veh, uint32_t id)
{
    for (int i = 0; i < g_spawnedN; i++) if (g_spawned[i].veh == veh && g_spawned[i].ref == VehRef(veh)) return;
    if (g_spawnedN == 24) { memmove(g_spawned, g_spawned + 1, sizeof(Spawned) * 23); g_spawnedN = 23; }
    g_spawned[g_spawnedN++] = { veh, VehRef(veh), id };
}

void VehicleSpawnedHere(void *veh)
{
    if (!veh || IsTrain(veh)) return;
    if (FindByVeh(veh)) { NoteSpawned(veh, FindByVeh(veh)->id); return; }
    uint32_t id = HostVehicleId(veh, false, false);
    NoteSpawned(veh, id);
    if (id) Log("vehicule pose par le joueur %08X (modele %d) : partage", id, *(int16_t *)((uint8_t *)veh + 0x22));
}

// The local player out of this vehicle, standing next to it (WARP_CHAR_FROM_CAR_TO_COORD 0362).
static void EjectLocalPlayer(void *veh)
{
    void *me = FindPlayerPed();
    if (!me || PedVehicle(me) != veh) return;
    const float *p = EntityPos(veh);
    const uint8_t *mat = *(uint8_t **)((uint8_t *)veh + 0x14);
    float side[3] = { mat ? ((const float *)mat)[0] : 1.0f, mat ? ((const float *)mat)[1] : 0.0f, 0.0f };
    float to[3] = { p[0] + side[0] * 3.0f, p[1] + side[1] * 3.0f, p[2] + 0.8f };
    int warp[4] = { PedRef(me) };
    memcpy(warp + 1, to, 12);
    RunScriptCommandTyped(0x0362, 4, "ifff", warp);
}

// A vehicle out of the world at once, its other occupants (passengers, driver) deleted first (DELETE_CHAR 009B, DELETE_CAR 00A6).
static void RemoveVehicleNow(void *veh)
{
    for (int k = 0; k < 8; k++)
        if (void *ps = Field<void *>(veh, VEH_PASSENGERS + k * 4)) if (ps != FindPlayerPed() && PuppetIndex(ps) < 0) { int r = PedRef(ps); RunScriptCommand(0x009B, 1, &r); }
    void *drv = Field<void *>(veh, VEH_DRIVER);
    if (drv && drv != FindPlayerPed() && PuppetIndex(drv) < 0) { int r = PedRef(drv); RunScriptCommand(0x009B, 1, &r); }
    int ref = VehRef(veh);
    RunScriptCommand(0x00A6, 1, &ref);
}

// Gone notices go out four times (the network is not reliable): 0 = free.
struct GoneNote { uint32_t id; uint8_t left; uint32_t next; };
static GoneNote g_gone[8];
static void SendGoneNotices()
{
    uint32_t now = GetTickCount();
    for (auto &g : g_gone) {
        if (!g.id || (int)(now - g.next) < 0) continue;
        MsgVehicle m = {};
        m.type = MSG_VEHICLE;
        m.owner = (uint8_t)g_localId;
        m.id = g.id;
        m.flags = VF_GONE;
        m.time = now;
        NetSendToAll(&m, sizeof(m));
        g.next = now + 120;
        if (--g.left == 0) g.id = 0;
    }
}

// The F10 buttons: removes the vehicle this player spawned last (all = false) or every one of them. The player is put
// out of it first; a vehicle shared with the others disappears on their side at once. Returns how many were removed.
int VehiclesRemoveSpawned(bool all)
{
    int removed = 0;
    for (int i = g_spawnedN - 1; i >= 0; i--) {
        Spawned s = g_spawned[i];
        void *v = VehFromRef(s.ref);
        if (v != s.veh) {   // gone already (destroyed, deleted by the game)
            if (!all) { g_spawnedN = i; if (removed) break; continue; }
            continue;
        }
        EjectLocalPlayer(v);
        NetVeh *n = FindByVeh(v);
        uint32_t id = n ? n->id : s.id;
        if (id)
            for (auto &g : g_gone) if (!g.id) { g.id = id; g.left = 4; g.next = 0; break; }
        RemoveVehicleNow(v);
        if (n) memset(n, 0, sizeof(*n));
        removed++;
        Log("vehicle %08X removed by its owner (F10 menu)", id);
        if (!all) { g_spawnedN = i; break; }
    }
    if (all) g_spawnedN = 0;
    return removed;
}

// Vehicule qui vient d'apparaitre tout pres du joueur local, a pied, sans conducteur : posee par un code de triche
// (CCheat::VehicleCheat : a 2 m devant le joueur, CreatedBy 1 comme la circulation) -> partage. La circulation et les
// voitures garees naissent a distance, hors de vue : jamais a moins de 12 m d'un joueur a pied.
static void ShareSpawnedVehicles()
{
    static uint32_t last;
    static int seen[256];
    static bool primed;
    uint32_t now = GetTickCount();
    if (now - last < 200) return;
    last = now;
    Pool *p = *(Pool **)0xB74494;
    void *me = FindPlayerPed();
    if (!p || !me || p->size > 256) return;
    bool onFoot = !PedVehicle(me);
    const float *mp = EntityPos(me);
    for (int i = 0; i < p->size; i++) {
        if (p->flags[i] & 0x80) { seen[i] = 0; continue; }
        uint8_t *v = p->objects + i * 0xA18;
        int ref = VehRef(v);
        if (seen[i] == ref) continue;
        seen[i] = ref;
        if (!primed || !onFoot || v[0x4A4] != 1 || IsTrain(v) || FindByVeh(v) || Field<void *>(v, VEH_DRIVER)) continue;
        const float *vp = EntityPos(v);
        float dx = vp[0] - mp[0], dy = vp[1] - mp[1], dz = vp[2] - mp[2];
        if (dx * dx + dy * dy + dz * dz > 12.0f * 12.0f) continue;
        VehicleSpawnedHere(v);
    }
    primed = true;
}

// --- Envoi : vehicules dont on est proprietaire ---
static void SendOwned()
{
    uint32_t now = GetTickCount();
    for (auto &n : g_veh) {
        if (!n.id || n.owner != g_localId || !Alive(n)) continue;
        bool driving = now - n.lastDriven < 200;
        uint8_t *v0 = (uint8_t *)n.veh;
        const float *spd = (const float *)(v0 + 0x44);
        bool moving = spd[0] * spd[0] + spd[1] * spd[1] + spd[2] * spd[2] > 0.0001f;
        bool hosted = n.mission || n.ambient;
        uint32_t every = driving ? 33 : hosted && moving ? 66 : 500;
        // circulation de l'hote loin de tout invite (80 m) : 5 fois par seconde suffisent
        if (n.ambient && !driving && g_cfg.host && every < 200 && !NearSharedGuestAnywhere(EntityPos(n.veh), 80.0f)) every = 200;
        // Gare depuis plus de 10 s : l'etat est quand meme renvoye (une fois par seconde) tant qu'un autre joueur est a
        // moins de 350 m. Avant, la copie disparaissait chez l'autre joueur des qu'on descendait (voiture posee par le
        // menu ou par un code de triche, voiture volee et laissee la). Au-dela, plus rien : de toute facon hors de vue.
        if (!driving && !hosted && now - n.lastDriven > 10000) {
            if (!OtherPlayerNear(EntityPos(n.veh), 350.0f)) continue;
            if (every < 1000) every = 1000;
        }
        if (now - n.lastSend < every) continue;
        n.lastSend = now;
        uint8_t *v = (uint8_t *)n.veh;
        MsgVehicle m = {};
        m.type = MSG_VEHICLE;
        m.owner = (uint8_t)g_localId;
        m.id = n.id;
        m.time = now;
        m.model = (uint16_t)*(int16_t *)(v + 0x22);
        m.color1 = v[0x434];
        m.color2 = v[0x435];
        memcpy(m.pos, EntityPos(v), 12);
        MatrixOf(v, m.right, m.fwd);
        memcpy(m.speed, v + 0x44, 12);
        // Vitesse reelle d'apres le deplacement depuis le dernier envoi (en m par 1/50 s, comme le jeu) quand celle du
        // jeu est bien plus faible : un vehicule sur un trajet enregistre par le script (la Voodoo de la mission des
        // velos) avance sans vitesse physique ; sa copie restait sur place et sautait a chaque message (02/10).
        float sdt = (now - n.sentAt) / 1000.0f;
        if (n.sentAt && sdt > 0.02f && sdt < 0.6f) {
            float cs[3], c2 = 0, s2 = 0;
            for (int k = 0; k < 3; k++) { cs[k] = (m.pos[k] - n.sentPos[k]) / sdt / 50.0f; c2 += cs[k] * cs[k]; s2 += m.speed[k] * m.speed[k]; }
            if (c2 > 0.0004f && s2 < c2 * 0.25f && c2 < 4.0f) memcpy(m.speed, cs, 12);
        }
        memcpy(n.sentPos, m.pos, 12);
        n.sentAt = now;
        memcpy(m.turn, v + 0x50, 12);
        m.driven = driving;
        m.ownerRef = n.ref;
        m.steer = *(float *)(v + 0x494);
        m.gas = *(float *)(v + 0x49C);
        m.brake = *(float *)(v + 0x4A0);
        m.handbrake = (v[0x428] & 0x20) ? 1 : 0;
        {   // helicopter rotor / plane propeller speed: the copy shows the blades turning
            int vt = *(const int *)(v + VEH_TYPE);
            float r = vt == 3 ? *(const float *)(v + 0x84C) : vt == 4 ? *(const float *)(v + 0x9C4) : 0.0f;
            m.rotor = fabsf(r) < 10.0f ? r : 0.0f;
        }
        if (n.mission || n.ambient) m.flags |= VF_MISSION;   // envoye tant qu'il existe (copie retiree 3 s apres)
        if (v[0x4A4] == 2) m.flags |= VF_SCRIPT;
        m.health = *(float *)(v + 0x4C0);
        if ((v[0x36] >> 3) == 5) m.flags |= VF_WRECKED;
        if (v[0x42D] & 0x80) m.flags |= VF_SIREN;
        if (*(uint32_t *)(v + 0x514)) m.flags |= VF_HORN;
        for (int k = 0; k < 16; k++) if (((void **)0x97D840)[k] == v) m.flags |= VF_PLAYBACK;   // compteur du klaxon (1 tant que le joueur klaxonne, 45 pour l'IA)
        if (HasDamageManager(v)) {
            const uint8_t *dm = v + 0x5A0;
            m.flags |= VF_DAMAGE;
            memcpy(m.wheels, dm + 5, 4);
            memcpy(m.doors, dm + 9, 6);
            m.lights = *(const uint32_t *)(dm + 0x10);
            m.panels = *(const uint32_t *)(dm + 0x14);
        }
        NetSendToAll(&m, sizeof(m));
    }
}

// --- Reception ---
static void OnVehicle(const MsgVehicle &m)
{
    if (m.owner == g_localId) return;
    NetVeh *n = FindById(m.id);
    if (m.flags & VF_GONE) {   // the owner removed it (F10 menu): the copy goes at once, whoever sits in it gets out first
        if (n && n->owner != g_localId) {
            if (Alive(*n)) {
                void *me = FindPlayerPed();
                if (me && PedVehicle(me) == n->veh) EjectLocalPlayer(n->veh);
                RemoveVehicleNow(n->veh);
            }
            Log("vehicle %08X removed by its owner", m.id);
            memset(n, 0, sizeof(*n));
        }
        return;
    }
    if (n && n->owner == g_localId && Alive(*n)) {
        // Un autre joueur a pris le volant de notre vehicule : il devient une copie chez nous.
        Log("vehicule %08X : le joueur %d en prend la propriete", m.id, m.owner);
    }
    if (!n) {
        n = NewSlot();
        if (!n) return;
        n->id = m.id;
    }
    if (n->owner != m.owner) n->track.Clear();   // autre proprietaire = autre horloge
    n->owner = m.owner;
    ClockSample(m.owner, m.time);
    {
        Snap sn = {};
        sn.t = m.time;
        for (int k = 0; k < 3; k++) { sn.pos[k] = m.pos[k]; sn.vel[k] = m.speed[k]; sn.right[k] = m.right[k]; sn.fwd[k] = m.fwd[k]; }
        n->track.Push(sn);
    }
    // Remis a neuf chez son proprietaire (Pay'n'Spray, garage) : la copie aussi (CVehicle::Fix, vtable[50]).
    bool damaged = AnyDamage(m);
    if (n->damaged && !damaged && !(m.flags & VF_WRECKED) && Alive(*n) && (((uint8_t *)n->veh)[0x36] >> 3) != 5) {
        ((void(__thiscall *)(void *))((*(void ***)n->veh)[50]))(n->veh);
        Log("vehicule %08X repare", m.id);
    }
    n->damaged = damaged;
    n->last = m;
    n->lastRecv = GetTickCount();
}

// Un vehicule de chez nous a l'emplacement ou la copie va naitre ? Voiture ordinaire de notre jeu (circulation, voiture
// garee) : elle est retiree avec ses occupants (la zone est celle d'un autre joueur, il y fait foi ; sans cela les deux
// voitures se rentraient dedans a la naissance de la copie, test du 14/10 : "la circulation de l'invite apparait dans
// celle de l'hote"). Autre chose (vehicule partage, du joueur, de mission) : la copie attend, au plus 4 s.
static bool ClearSpotForCopy(NetVeh &n)
{
    Pool *p = *(Pool **)0xB74494;
    if (!p) return true;
    uint32_t now = GetTickCount();
    bool blocked = false, hard = false;
    uint8_t *blocker = nullptr;
    for (int i = 0; i < p->size; i++) {
        if (p->flags[i] & 0x80) continue;
        uint8_t *v = p->objects + i * 0xA18;
        if (v == n.veh) continue;
        const float *vp = EntityPos(v);
        float dx = vp[0] - n.last.pos[0], dy = vp[1] - n.last.pos[1], dz = vp[2] - n.last.pos[2];
        if (dx * dx + dy * dy > 6.0f * 6.0f || fabsf(dz) > 3.0f) continue;
        void *drv = Field<void *>(v, VEH_DRIVER);
        bool ordinary = (v[0x4A4] == 1 || v[0x4A4] == 3) && !IsTrain(v) && !FindByVeh(v) && (!drv || (drv != FindPlayerPed() && PuppetIndex(drv) < 0));
        if (ordinary) {
            for (int k = 0; k < 8; k++) if (void *ps = Field<void *>(v, VEH_PASSENGERS + k * 4)) { int r = PedRef(ps); RunScriptCommand(0x009B, 1, &r); }
            if (drv) { int r = PedRef(drv); RunScriptCommand(0x009B, 1, &r); }
            int ref = VehRef(v);
            RunScriptCommand(0x00A6, 1, &ref);   // DELETE_CAR
            static int said;
            if (said < 20) { said++; Log("circulation : voiture locale retiree, la copie %08X va naitre a sa place", n.id); }
        } else {
            blocked = true;
            // Occupe (joueur, pantin) ou en mouvement : la copie attend longtemps (4 s) qu'il s'ecarte. Vehicule partage a
            // l'arret et vide (celui que l'autre joueur vient de poser a cote, essai du 14/10 : la voiture de l'invite
            // mettait 4 s a apparaitre chez l'hote parce que la Sentinel de l'hote etait a 5 m) : 0,6 s suffisent.
            const float *sp = (const float *)(v + 0x44);
            if (drv || sp[0] * sp[0] + sp[1] * sp[1] + sp[2] * sp[2] > 0.0004f) hard = true;
            if (!blocker) blocker = v;
            for (int k = 0; k < 8; k++) if (Field<void *>(v, VEH_PASSENGERS + k * 4)) hard = true;
        }
    }
    if (!blocked) { n.blockedSince = 0; return true; }
    if (!n.blockedSince) n.blockedSince = now;
    if (!(n.waitLogged & 2)) {
        n.waitLogged |= 2;
        Log("copie du vehicule %08X en attente : place occupee par un vehicule (modele %d, %s)", n.id, blocker ? (int)*(int16_t *)(blocker + 0x22) : 0, hard ? "occupe ou en mouvement, 4 s" : "a l'arret et vide, 0,6 s");
    }
    return now - n.blockedSince > (hard ? 4000u : 600u);
}

static void CreateCopy(NetVeh &n)
{
    int model = n.last.model;
    if (model < 400 || model > 611) return;
    if (model == 449 || model == 537 || model == 538 || model == 569 || model == 570 || model == 590) return;   // trains and trams: trains.cpp
    // Pas sur le joueur : une copie qui apparaitrait a moins de 4 m de lui (ou de sa voiture) attend que la place soit
    // libre (voitures qui surgissaient sur l'invite qui suivait l'hote, 01/10).
    if (void *me = FindPlayerPed()) {
        void *mine = PedVehicle(me);
        const float *a = EntityPos(mine ? mine : me), *b = n.last.pos;
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        if (dx * dx + dy * dy + dz * dz < 16.0f && !(n.last.flags & VF_MISSION && n.last.driven)) {
            // (un vehicule pose a l'arret pres de nous attend 1,5 s au plus : sans cela, l'autre joueur le posant devant
            // un joueur qui se tient a cote de lui ne le voyait jamais apparaitre)
            const float *sp = n.last.speed;
            bool still = sp[0] * sp[0] + sp[1] * sp[1] + sp[2] * sp[2] < 0.0004f && !n.last.driven;
            uint32_t now = GetTickCount();
            if (!n.nearSince) n.nearSince = now;
            if (!(n.waitLogged & 1)) { n.waitLogged |= 1; Log("copie du vehicule %08X en attente : joueur local a moins de 4 m (%s)", n.id, still ? "a l'arret, 1,5 s" : "en mouvement"); }
            if (!still || now - n.nearSince < 1500) return;
        } else n.nearSince = 0;
    }
    if (!ModelLoaded(model)) {
        RequestModel(model, 2);
        LoadAllRequestedModels(false);
        if (!ModelLoaded(model)) return;
    }
    if (!ClearSpotForCopy(n)) return;
    void *v = ((void *(__cdecl *)(int, float, float, float, bool))0x431F80)(model, n.last.pos[0], n.last.pos[1], n.last.pos[2], false);
    if (!v) return;
    n.veh = v;
    n.ref = VehRef(v);
    uint8_t *p = (uint8_t *)v;
    // Orientation recue tout de suite (sinon cap 0 puis rotation sur plusieurs images : voitures en travers)
    if (uint8_t *mat = *(uint8_t **)(p + 0x14)) {
        float *r = (float *)mat, *f = (float *)(mat + 0x10), *u = (float *)(mat + 0x20);
        memcpy(r, n.last.right, 12);
        memcpy(f, n.last.fwd, 12);
        u[0] = r[1] * f[2] - r[2] * f[1]; u[1] = r[2] * f[0] - r[0] * f[2]; u[2] = r[0] * f[1] - r[1] * f[0];
    }
    memcpy(p + 0x44, n.last.speed, 12);
    p[0x434] = n.last.color1;
    p[0x435] = n.last.color2;
    Log("copie du vehicule %08X (modele %d, joueur %d) creee", n.id, model, n.owner);
}

static void DestroyCopy(NetVeh &n, bool forget = true)
{
    if (Alive(n)) {
        WorldRemove(n.veh);
        RemoveReferencesToDeletedObject(n.veh);
        DeleteEntity(n.veh);
        Log("copie du vehicule %08X retiree", n.id);
    }
    if (forget) memset(&n, 0, sizeof(n));
    else { n.veh = nullptr; n.cutHidden = false; }
}

// Etat de la carrosserie recu : sante, sirene, explosion, degats (seulement ceux en plus : une copie abimee par sa
// propre physique ne se repare pas toute seule). Visuels : CAutomobile::SetDoorDamage 0x6B1600 / SetPanelDamage
// 0x6B1480 / SetBumperDamage 0x6B1350 (porte ou panneau, sans effet), qui lisent l'etat du CDamageManager.
static void ApplyBody(NetVeh &n)
{
    uint8_t *v = (uint8_t *)n.veh;
    const MsgVehicle &m = n.last;
    if ((v[0x36] >> 3) == 5) {   // deja une epave : rendue brulee (drapeau pose par BlowUpCar, +0x40 bit 29)
        *(uint32_t *)(v + 0x40) |= 0x20000000;
        return;
    }
    if (m.flags & VF_WRECKED) {   // CVehicle::BlowUpCar(auteur, bool) : vtable[41] (0x6B3780 pour les voitures)
        // (une fois toutes les 2 s : une copie a l'epreuve des explosions (vehicule de mission protege) la refusait,
        // et l'explosion etait redemandee a chaque image, test reel du 02/10)
        if (GetTickCount() - n.blownAt < 2000) return;
        n.blownAt = GetTickCount();
        ((void(__thiscall *)(void *, void *, bool))((*(void ***)v)[41]))(v, nullptr, false);
        Log("copie du vehicule %08X : explosion", n.id);
        return;
    }
    *(float *)(v + 0x4C0) = m.health;
    if (m.flags & VF_SIREN) v[0x42D] |= 0x80; else v[0x42D] &= 0x7F;
    if (!(m.flags & VF_DAMAGE) || !HasDamageManager(v)) return;
    uint8_t *dm = v + 0x5A0;
    for (int k = 0; k < 4; k++) if (m.wheels[k] > dm[5 + k]) dm[5 + k] = m.wheels[k];
    for (int k = 0; k < 6; k++)
        if (m.doors[k] > dm[9 + k]) {
            dm[9 + k] = m.doors[k];
            ((void(__thiscall *)(void *, int, bool))0x6B1600)(v, k, false);
        }
    *(uint32_t *)(dm + 0x10) |= m.lights;
    uint32_t &panels = *(uint32_t *)(dm + 0x14);
    for (int k = 0; k < 7; k++) {
        uint32_t want = (m.panels >> (k * 4)) & 0xF, have = (panels >> (k * 4)) & 0xF;
        if (want <= have) continue;
        panels = (panels & ~(0xFu << (k * 4))) | (want << (k * 4));
        ((void(__thiscall *)(void *, int, bool))(k >= 5 ? 0x6B1350 : 0x6B1480))(v, k, false);
    }
}

// Recale une copie vers l'etat recu : position anticipee selon la vitesse, orientation, vitesses (la physique du jeu
// continue entre deux messages). Grand ecart : placee d'un coup.
static void UpdateCopy(NetVeh &n)
{
    uint8_t *v = (uint8_t *)n.veh;
    const MsgVehicle &m = n.last;
    float lead = (GetTickCount() - n.lastRecv) / 1000.0f;
    if (lead > 0.25f) lead = 0.25f;
    float target[3];
    for (int k = 0; k < 3; k++) target[k] = m.pos[k] + m.speed[k] * 50.0f * lead;
    float *pos = EntityPos(v);
    float dx = target[0] - pos[0], dy = target[1] - pos[1], dz = target[2] - pos[2];
    float err = sqrtf(dx * dx + dy * dy + dz * dz);
    uint8_t *mat = *(uint8_t **)(v + 0x14);
    if (!mat) return;
    bool playback = (m.flags & VF_PLAYBACK) != 0;   // trajet enregistre chez son proprietaire : suivi exact, sans glisse
    float k = err > 6.0f || playback ? 1.0f : 0.25f;
    if (err > 6.0f) WorldRemove(v);
    pos[0] += dx * k; pos[1] += dy * k; pos[2] += dz * k;
    float *right = (float *)mat, *fwd = (float *)(mat + 0x10), *up = (float *)(mat + 0x20);
    // Orientation recue anticipee selon la vitesse de rotation (comme la position), puis rejointe en douceur ; avant,
    // elle etait recopiee telle quelle, en retard sur la position anticipee : la caisse semblait trainer.
    float t = 50.0f * lead, wr[3], wf[3];
    const float *w = m.turn;
    wr[0] = m.right[0] + (w[1] * m.right[2] - w[2] * m.right[1]) * t;
    wr[1] = m.right[1] + (w[2] * m.right[0] - w[0] * m.right[2]) * t;
    wr[2] = m.right[2] + (w[0] * m.right[1] - w[1] * m.right[0]) * t;
    wf[0] = m.fwd[0] + (w[1] * m.fwd[2] - w[2] * m.fwd[1]) * t;
    wf[1] = m.fwd[1] + (w[2] * m.fwd[0] - w[0] * m.fwd[2]) * t;
    wf[2] = m.fwd[2] + (w[0] * m.fwd[1] - w[1] * m.fwd[0]) * t;
    float blend = err > 6.0f || playback ? 1.0f : 0.5f;
    for (int k = 0; k < 3; k++) { right[k] += (wr[k] - right[k]) * blend; fwd[k] += (wf[k] - fwd[k]) * blend; }
    float lf = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (lf > 1e-4f) for (int k = 0; k < 3; k++) fwd[k] /= lf;
    float d = right[0] * fwd[0] + right[1] * fwd[1] + right[2] * fwd[2];   // droite perpendiculaire a l'avant
    for (int k = 0; k < 3; k++) right[k] -= fwd[k] * d;
    float lr = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (lr > 1e-4f) for (int k = 0; k < 3; k++) right[k] /= lr;
    up[0] = right[1] * fwd[2] - right[2] * fwd[1];
    up[1] = right[2] * fwd[0] - right[0] * fwd[2];
    up[2] = right[0] * fwd[1] - right[1] * fwd[0];
    memcpy(v + 0x44, m.speed, 12);
    memcpy(v + 0x50, m.turn, 12);
    if (err > 6.0f) WorldAdd(v);
}

// IA des copies : le pantin au volant est un conducteur PNJ sans mission, et CAutomobile::ProcessAI (vtable[66])
// freinait a fond a chaque image (frein 1,0 : roues bloquees, copie qui "freine tout le temps", 1er test reel). Pour
// une copie conduite par un autre joueur, les commandes de son conducteur (volant, gaz, frein, frein a main) sont
// posees a la place, et la physique du jeu fait le reste (roues, suspension, inclinaison des deux-roues).
// Vtables : CAutomobile 0x871120 et ses derives 0x871680 / 0x8717D8 / 0x871948 (0x6B4800), CBike 0x871360
// (0x6BC930), CBmx 0x871528 (0x6C1470). ProcessAI renvoie vrai pour sauter la physique : on renvoie faux.
typedef bool(__thiscall *ProcessAI_t)(void *veh, unsigned int &flags);
struct AIHook { uintptr_t vt; ProcessAI_t orig; };
static AIHook g_aiHooks[6] = { { 0x871120 }, { 0x871680 }, { 0x8717D8 }, { 0x871948 }, { 0x871360 }, { 0x871528 } };

static bool RemoteControls(void *veh)
{
    // (Aussi les copies conduites par une copie de PNJ : depuis la 0.43 la copie du conducteur est assise au volant et
    // l'IA du jeu reprenait la voiture, frein a fond, feux stop allumes chez l'invite, test du 01/10.)
    NetVeh *n = FindByVeh(veh);
    if (!n || n->owner == g_localId || GetTickCount() - n->lastRecv > 1500) return false;
    if (!n->last.driven && !Field<void *>(veh, VEH_DRIVER)) return false;
    uint8_t *v = (uint8_t *)veh;
    *(float *)(v + 0x494) = n->last.steer;
    *(float *)(v + 0x49C) = n->last.gas;
    *(float *)(v + 0x4A0) = n->last.brake;
    v[0x428] = n->last.handbrake ? (v[0x428] | 0x20) : (v[0x428] & ~0x20);
    if (n->last.flags & VF_HORN) *(uint32_t *)(v + 0x514) = 2;   // le jeu le decompte : klaxon tant qu'il est recu
    if (g_cfg.logScripts) {   // releve : commandes rejouees par la copie
        static uint32_t lastLog;
        if (GetTickCount() - lastLog > 2000) { lastLog = GetTickCount(); const float *sp = (const float *)(v + 0x44); Log("vehicules : copie %08X volant %.2f gaz %.2f frein %.2f vitesse %.1f km/h", n->id, n->last.steer, n->last.gas, n->last.brake, sqrtf(sp[0] * sp[0] + sp[1] * sp[1] + sp[2] * sp[2]) * 180.0f); }
    }
    return true;
}
template <int I> static bool __fastcall h_ProcessAI(void *veh, void *, unsigned int &flags)
{
    if (RemoteControls(veh)) return false;
    return g_aiHooks[I].orig(veh, flags);
}
template <int I> static void HookAI()
{
    void **slot = (void **)(g_aiHooks[I].vt + 66 * 4);
    if (*slot != (void *)&h_ProcessAI<I>) g_aiHooks[I].orig = (ProcessAI_t)PatchPointer(slot, (void *)&h_ProcessAI<I>);
}

static void *g_guestMissionCopy;
static int g_guestMissionRef;   // invite : vehicule pose pour lui (garde jusqu'a la fin de la mission de l'hote)

// Fin de mission de l'hote (mirror.cpp) : le vehicule pose pour l'invite redevient ordinaire (le jeu pourra le retirer).
void VehiclesMissionEnded()
{
    if (g_guestMissionCopy && VehFromRef(g_guestMissionRef) == g_guestMissionCopy) ((uint8_t *)g_guestMissionCopy)[0x4A4] = 1;
    g_guestMissionCopy = nullptr;
}

// Invite : l'hote prend le volant d'un vehicule de mission (cree par son script : velo de la mission de Smoke...) ;
// invite a pied a moins de 60 m, sans place libre pour lui dedans : une copie du meme modele (a lui) apparait a
// cote de lui, une fois par vehicule de l'hote, pour faire la mission avec lui. Place libre : rappel de G (passager).
static void GuestMissionVehicle()
{
    static const bool g_fr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
    static uint32_t handled;   // dernier vehicule de l'hote traite
    const NetPlayer &h = g_players[0];
    void *me = FindPlayerPed();
    if (g_cfg.host || !me || !h.connected || !h.state.inGame || !h.state.vehicleId || h.state.seat != 0 || h.state.vehicleId == handled) return;
    NetVeh *n = FindById(h.state.vehicleId);
    if (!n || !Alive(*n) || !(n->last.flags & VF_SCRIPT)) return;
    // (en voiture aussi : un invite au volant d'une voiture volee n'avait jamais son velo ; pas s'il est deja dans un
    // vehicule de mission de l'hote, ni s'il a deja le sien ; jusqu'a 150 m)
    if (void *mine = PedVehicle(me)) { NetVeh *mn = FindByVeh(mine); if ((mn && (mn->last.flags & VF_SCRIPT)) || mine == g_guestMissionCopy) return; }
    const float *hp = h.state.pos, *mp = EntityPos(me);
    float dx = hp[0] - mp[0], dy = hp[1] - mp[1];
    if (dx * dx + dy * dy > 150.0f * 150.0f) return;
    handled = h.state.vehicleId;
    uint8_t *hv = (uint8_t *)n->veh;
    int seat = g_localId > 0 ? g_localId : 1;   // place passager de ce joueur
    bool freeSeat = seat <= hv[VEH_MAXPASS] && !Field<void *>(hv, VEH_PASSENGERS + (seat - 1) * 4);
    if (freeSeat) { HudToast(g_fr ? "G : monter avec l'hote" : "G: ride with the host", 5000); Log("vehicule de mission de l'hote : place passager libre (G)"); return; }
    int model = n->last.model;
    if (!ModelLoaded(model)) { RequestModel(model, 2); LoadAllRequestedModels(false); }
    if (!ModelLoaded(model)) return;
    float h0 = Field<float>(me, PED_ROTATION);
    float side = PedVehicle(me) ? 4.5f : 3.0f;   // a cote de lui (de sa voiture)
    float pos[3] = { mp[0] + cosf(h0) * side, mp[1] + sinf(h0) * side, mp[2] + 0.5f };   // sur sa droite
    void *v = ((void *(__cdecl *)(int, float, float, float, bool))0x431F80)(model, pos[0], pos[1], pos[2], false);
    if (!v) return;
    if (uint8_t *mat = *(uint8_t **)((uint8_t *)v + 0x14)) {   // tourne comme l'invite
        float *r = (float *)mat, *f = (float *)(mat + 0x10);
        f[0] = -sinf(h0); f[1] = cosf(h0); f[2] = 0;
        r[0] = cosf(h0); r[1] = sinf(h0); r[2] = 0;
    }
    // Vehicule de mission (CreatedBy 2) jusqu'a la fin de la mission de l'hote : ordinaire (1), la population partagee
    // de l'invite (qui retire ses vehicules locaux ordinaires) l'enlevait aussitot (velo jamais vu, test du 02/10).
    ((uint8_t *)v)[0x4A4] = 2;
    VehiclesMissionEnded();
    g_guestMissionCopy = v;
    g_guestMissionRef = VehRef(v);
    // Registered at once as the guest's own network vehicle: before, it only became one when the guest got in, so the
    // host did not see it appear (and a copy that was only seen later came late) - report of 14/10, "sometimes this
    // doesn't sync for the host when it appears". Parked, it is sent twice a second like any vehicle of ours.
    if (uint32_t nid = LocalVehicleId(v, false)) Log("host mission vehicle: our copy registered as %08X for the host to see", nid);
    HudToast(g_fr ? "Un vehicule pour toi est a cote" : "A vehicle for you is next to you", 5000);
    Log("vehicule de mission de l'hote (modele %d) : copie posee a cote de l'invite", model);
}

// Hote : circulation que le jeu vient de generer sous les yeux d'un invite. Le jeu ne cree les voitures que hors de la
// vue de SON joueur : derriere l'hote, la ou l'invite qui le suit regarde. Une voiture de passage (avec conducteur)
// apparue a moins de 90 m devant un invite partage (cone de 140 degres, cap de son vehicule ou de son pantin) est
// retiree tout de suite (DELETE_CHAR 009B pour ses occupants, DELETE_CAR 00A6), avant d'avoir ete envoyee.
static void HostTrafficGuard()
{
    static int seen[256];
    static bool primed;
    Pool *p = *(Pool **)0xB74494;
    if (!p || p->size > 256) return;
    for (int i = 0; i < p->size; i++) {
        if (p->flags[i] & 0x80) { seen[i] = 0; continue; }
        uint8_t *v = p->objects + i * 0xA18;
        int ref = VehRef(v);
        if (seen[i] == ref) continue;
        seen[i] = ref;
        if (!primed || v[0x4A4] != 1 || FindByVeh(v)) continue;   // passage seulement (pas mission, pas copie)
        void *drv = Field<void *>(v, VEH_DRIVER);
        if (!drv || drv == FindPlayerPed() || PuppetIndex(drv) >= 0) continue;
        const float *cp = EntityPos(v);
        for (int g = 1; g < MAX_PLAYERS; g++) {
            void *pup = PuppetOf(g);
            if (!pup || !g_players[g].connected) continue;
            const float *gp = EntityPos(pup);
            float dx = cp[0] - gp[0], dy = cp[1] - gp[1], d = sqrtf(dx * dx + dy * dy);
            if (d > 90.0f || d < 0.1f) continue;
            float fx, fy;
            if (void *gv = PedVehicle(pup)) { uint8_t *m = *(uint8_t **)((uint8_t *)gv + 0x14); fx = m ? ((float *)m)[4] : 0; fy = m ? ((float *)m)[5] : 1; }
            else { float h = Field<float>(pup, PED_ROTATION); fx = -sinf(h); fy = cosf(h); }
            if ((dx * fx + dy * fy) / d < 0.34f) continue;   // hors du cone de vue
            for (int k = 0; k < 8; k++) if (void *ps = Field<void *>(v, VEH_PASSENGERS + k * 4)) { int r = PedRef(ps); RunScriptCommand(0x009B, 1, &r); }
            int r = PedRef(drv);
            RunScriptCommand(0x009B, 1, &r);   // DELETE_CHAR
            RunScriptCommand(0x00A6, 1, &ref); // DELETE_CAR
            static int said;
            if (said < 20) { said++; Log("circulation : voiture apparue devant %s (%.0f m) retiree", g_players[g].state.name, d); }
            break;
        }
    }
    primed = true;
}

// Apres la physique du jeu (interp.cpp : avant et apres CGame::Process) : chaque copie est placee a son etat
// interpole, dans le passe de INTERP_DELAY ms. Position, orientation, vitesse et rotation ; le volant, les roues, les
// portieres... viennent de la physique du jeu (commandes rejouees par RemoteControls) et d'ApplyBody.
void VehiclesAfterProcess()
{
    uint32_t now = GetTickCount();
    void *me = FindPlayerPed();
    for (auto &n : g_veh) {
        if (!n.id || n.owner == g_localId || !n.veh || !Alive(n) || !n.track.count) continue;
        uint8_t *v = (uint8_t *)n.veh;
        // Le joueur local est au volant : la copie va devenir la notre (LocalVehicleId), la physique est celle du jeu.
        if (me && PedVehicle(me) == n.veh && Field<void *>(v, VEH_DRIVER) == me) { FreezeCopy(v, false); continue; }
        if (now - n.lastRecv >= 1500) { FreezeCopy(v, false); continue; }   // plus de nouvelles : le jeu reprend
        Snap s;
        if (!TrackSample(n.track, n.owner, s)) continue;
        uint8_t *mat = *(uint8_t **)(v + 0x14);
        if (!mat) continue;
        float *pos = EntityPos(v);
        float jx = s.pos[0] - pos[0], jy = s.pos[1] - pos[1], jz = s.pos[2] - pos[2];
        bool jumped = jx * jx + jy * jy + jz * jz > 400.0f;   // grand ecart : retiree puis remise dans le monde (secteurs a jour)
        if (jumped) WorldRemove(v);
        pos[0] = s.pos[0]; pos[1] = s.pos[1]; pos[2] = s.pos[2];
        float *right = (float *)mat, *fwd = (float *)(mat + 0x10), *up = (float *)(mat + 0x20);
        for (int k = 0; k < 3; k++) { fwd[k] = s.fwd[k]; right[k] = s.right[k]; }
        // Matrice bien orthogonale apres le melange des deux etats : haut = droite x avant, droite = avant x haut.
        up[0] = right[1] * fwd[2] - right[2] * fwd[1];
        up[1] = right[2] * fwd[0] - right[0] * fwd[2];
        up[2] = right[0] * fwd[1] - right[1] * fwd[0];
        right[0] = fwd[1] * up[2] - fwd[2] * up[1];
        right[1] = fwd[2] * up[0] - fwd[0] * up[2];
        right[2] = fwd[0] * up[1] - fwd[1] * up[0];
        if (jumped) WorldAdd(v);
        if (g_cfg.freezeCopies) FreezeCopy(v, true); else FreezeCopy(v, false);
        {   // vitesse bornee, jamais NaN (un saut de position donnerait une vitesse enorme : hors du monde, plantage)
            float vel[3] = { s.vel[0], s.vel[1], s.vel[2] };
            float vl = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
            if (!(vl <= 4.0f)) { float k = vl > 0 && vl < 1e30f ? 4.0f / vl : 0.0f; for (float &c : vel) c *= k; }
            memcpy(v + 0x44, vel, 12);
        }
        {
            float turn[3] = { n.last.turn[0], n.last.turn[1], n.last.turn[2] };
            for (float &c : turn) if (!(fabsf(c) <= 4.0f)) c = 0.0f;
            memcpy(v + 0x50, turn, 12);
        }
        EntityRefresh(v);
    }
    // Helicopters and planes: the copy's engine is "on" and its rotor / propeller turns at the owner's speed. Written after
    // the game's own step (its engine-off copy would slow the rotor down to a stop), so that the pre-render turns the blades.
    for (auto &n : g_veh) {
        if (!n.id || n.owner == g_localId || !n.veh || !Alive(n) || now - n.lastRecv >= 1500) continue;
        uint8_t *v = (uint8_t *)n.veh;
        int vt = *(int *)(v + VEH_TYPE);
        if (vt != 3 && vt != 4) continue;
        if (me && PedVehicle(me) == n.veh && Field<void *>(v, VEH_DRIVER) == me) continue;   // we fly it: the game's own rotor
        float r = n.last.rotor;
        if (!(fabsf(r) < 10.0f)) r = 0.0f;
        if (r > 0.01f) v[0x428] |= 0x10;   // bEngineOn
        *(float *)(v + (vt == 3 ? 0x84C : 0x9C4)) = r;
    }
}

void VehiclesFrame()
{
    static bool aiHooked;
    if (!aiHooked) { aiHooked = true; HookAI<0>(); HookAI<1>(); HookAI<2>(); HookAI<3>(); HookAI<4>(); HookAI<5>(); Log("vehicules : IA des copies remplacee par les commandes de leur conducteur"); }
    g_onVehicle = OnVehicle;
    if (GameState() != 9 || !FindPlayerPed()) return;
    if (g_cfg.host) HostTrafficGuard();
    uint32_t now = GetTickCount();
    for (auto &n : g_veh) {
        if (!n.id || n.owner == g_localId) continue;
        if (n.veh && !Alive(n)) { Log("copie du vehicule %08X detruite par le jeu", n.id); n.veh = nullptr; }
        uint32_t timeout = (n.last.flags & VF_MISSION) ? 3000 : 30000;   // mission : l'hote l'envoie tant qu'il existe
        if (now - n.lastRecv > timeout && !PuppetInVehicle(n.veh)) { DestroyCopy(n, true); continue; }
        if (!n.veh) CreateCopy(n);
        if (n.veh) CutsceneHide(n.veh, g_cfg.hideCut && CutsceneNow(), n.cutHidden);
        if (n.veh && now - n.lastRecv < 1500) { if (!g_cfg.placeVeh) UpdateCopy(n); ApplyBody(n); }   // (placeVeh : la pose vient de VehiclesAfterProcess)
    }
    GuestMissionVehicle();
    ShareSpawnedVehicles();
    SendOwned();
    SendGoneNotices();
}
