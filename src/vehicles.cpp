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
        if ((v[0x4A4] == 1 || v[0x4A4] == 3) && !FindByVeh(v) && NearSharedGuest(EntityPos(v), 150.0f)) HostVehicleId(v, false, true);
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
        if (v[0x4A4] == 2 && !FindByVeh(v)) HostVehicleId(v, false, false);
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
        if (!driving && !hosted && now - n.lastDriven > 10000) continue;   // gare depuis 10 s : plus rien a envoyer
        if (now - n.lastSend < every) continue;
        n.lastSend = now;
        uint8_t *v = (uint8_t *)n.veh;
        MsgVehicle m = {};
        m.type = MSG_VEHICLE;
        m.owner = (uint8_t)g_localId;
        m.id = n.id;
        m.model = (uint16_t)*(int16_t *)(v + 0x22);
        m.color1 = v[0x434];
        m.color2 = v[0x435];
        memcpy(m.pos, EntityPos(v), 12);
        MatrixOf(v, m.right, m.fwd);
        memcpy(m.speed, v + 0x44, 12);
        memcpy(m.turn, v + 0x50, 12);
        m.driven = driving;
        m.ownerRef = n.ref;
        m.steer = *(float *)(v + 0x494);
        m.gas = *(float *)(v + 0x49C);
        m.brake = *(float *)(v + 0x4A0);
        m.handbrake = (v[0x428] & 0x20) ? 1 : 0;
        if (n.mission || n.ambient) m.flags |= VF_MISSION;   // envoye tant qu'il existe (copie retiree 3 s apres)
        if (v[0x4A4] == 2) m.flags |= VF_SCRIPT;
        m.health = *(float *)(v + 0x4C0);
        if ((v[0x36] >> 3) == 5) m.flags |= VF_WRECKED;
        if (v[0x42D] & 0x80) m.flags |= VF_SIREN;
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
    if (n && n->owner == g_localId && Alive(*n)) {
        // Un autre joueur a pris le volant de notre vehicule : il devient une copie chez nous.
        Log("vehicule %08X : le joueur %d en prend la propriete", m.id, m.owner);
    }
    if (!n) {
        n = NewSlot();
        if (!n) return;
        n->id = m.id;
    }
    n->owner = m.owner;
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

static void CreateCopy(NetVeh &n)
{
    int model = n.last.model;
    if (model < 400 || model > 611) return;
    // Pas sur le joueur : une copie qui apparaitrait a moins de 4 m de lui (ou de sa voiture) attend que la place soit
    // libre (voitures qui surgissaient sur l'invite qui suivait l'hote, 01/10).
    if (void *me = FindPlayerPed()) {
        void *mine = PedVehicle(me);
        const float *a = EntityPos(mine ? mine : me), *b = n.last.pos;
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        if (dx * dx + dy * dy + dz * dz < 16.0f && !(n.last.flags & VF_MISSION && n.last.driven)) return;
    }
    if (!ModelLoaded(model)) {
        RequestModel(model, 2);
        LoadAllRequestedModels(false);
        if (!ModelLoaded(model)) return;
    }
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
    else n.veh = nullptr;
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
    float k = err > 6.0f ? 1.0f : 0.25f;
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
    float blend = err > 6.0f ? 1.0f : 0.5f;
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
    NetVeh *n = FindByVeh(veh);
    if (!n || n->owner == g_localId || !n->last.driven || GetTickCount() - n->lastRecv > 1500) return false;
    uint8_t *v = (uint8_t *)veh;
    *(float *)(v + 0x494) = n->last.steer;
    *(float *)(v + 0x49C) = n->last.gas;
    *(float *)(v + 0x4A0) = n->last.brake;
    v[0x428] = n->last.handbrake ? (v[0x428] | 0x20) : (v[0x428] & ~0x20);
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
    if (!n || !Alive(*n) || !(n->last.flags & VF_SCRIPT) || PedVehicle(me)) return;
    const float *hp = h.state.pos, *mp = EntityPos(me);
    float dx = hp[0] - mp[0], dy = hp[1] - mp[1];
    if (dx * dx + dy * dy > 60.0f * 60.0f) return;
    handled = h.state.vehicleId;
    uint8_t *hv = (uint8_t *)n->veh;
    int seat = g_localId > 0 ? g_localId : 1;   // place passager de ce joueur
    bool freeSeat = seat <= hv[VEH_MAXPASS] && !Field<void *>(hv, VEH_PASSENGERS + (seat - 1) * 4);
    if (freeSeat) { HudToast(g_fr ? "G : monter avec l'hote" : "G: ride with the host", 5000); Log("vehicule de mission de l'hote : place passager libre (G)"); return; }
    int model = n->last.model;
    if (!ModelLoaded(model)) { RequestModel(model, 2); LoadAllRequestedModels(false); }
    if (!ModelLoaded(model)) return;
    float h0 = Field<float>(me, PED_ROTATION);
    float pos[3] = { mp[0] + cosf(h0) * 3.0f, mp[1] + sinf(h0) * 3.0f, mp[2] + 0.5f };   // 3 m sur sa droite
    void *v = ((void *(__cdecl *)(int, float, float, float, bool))0x431F80)(model, pos[0], pos[1], pos[2], false);
    if (!v) return;
    if (uint8_t *mat = *(uint8_t **)((uint8_t *)v + 0x14)) {   // tourne comme l'invite
        float *r = (float *)mat, *f = (float *)(mat + 0x10);
        f[0] = -sinf(h0); f[1] = cosf(h0); f[2] = 0;
        r[0] = cosf(h0); r[1] = sinf(h0); r[2] = 0;
    }
    ((uint8_t *)v)[0x4A4] = 1;   // vehicule ordinaire : le jeu pourra le retirer plus tard
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
        if (n.veh && now - n.lastRecv < 1500) { UpdateCopy(n); ApplyBody(n); }
    }
    GuestMissionVehicle();
    SendOwned();
}
