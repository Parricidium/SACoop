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
        Log("vehicule %08X (modele %d) enregistre", n->id, *(int16_t *)((uint8_t *)veh + 0x22));
    } else if (driver && n->owner != g_localId) {
        n->owner = (uint8_t)g_localId;   // au volant d'une copie : elle devient la notre
        Log("vehicule %08X : on en prend la propriete", n->id);
    }
    n->lastDriven = GetTickCount();
    return n->id;
}

// Hote : vehicule de mission (CreatedBy +0x4A4 == 2) ou occupe par un personnage de mission. Il devient un vehicule
// reseau de l'hote, envoye tant qu'il existe (chez les invites, sa copie disparait 3 s apres le dernier message).
uint32_t HostVehicleId(void *veh, bool occupied)
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
        Log("vehicule de mission %08X (modele %d) enregistre", n->id, *(int16_t *)((uint8_t *)veh + 0x22));
    }
    n->mission = true;
    if (occupied) n->lastDriven = GetTickCount();
    return n->id;
}

// Hote : tous les vehicules de mission du pool (0xB74494) deviennent des vehicules reseau.
void HostRegisterMissionVehicles()
{
    Pool *p = *(Pool **)0xB74494;
    for (int i = 0; i < p->size; i++) {
        if (p->flags[i] & 0x80) continue;
        uint8_t *v = p->objects + i * 0xA18;
        if (v[0x4A4] == 2 && !FindByVeh(v)) HostVehicleId(v, false);
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
        const float *spd = (const float *)((uint8_t *)n.veh + 0x44);
        bool moving = spd[0] * spd[0] + spd[1] * spd[1] + spd[2] * spd[2] > 0.0001f;
        uint32_t every = driving ? 33 : n.mission && moving ? 66 : 500;
        if (!driving && !n.mission && now - n.lastDriven > 10000) continue;   // gare depuis 10 s : plus rien a envoyer
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
        if (n.mission) m.flags |= VF_MISSION;
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
    memcpy(right, m.right, 12);
    memcpy(fwd, m.fwd, 12);
    up[0] = right[1] * fwd[2] - right[2] * fwd[1];
    up[1] = right[2] * fwd[0] - right[0] * fwd[2];
    up[2] = right[0] * fwd[1] - right[1] * fwd[0];
    memcpy(v + 0x44, m.speed, 12);
    memcpy(v + 0x50, m.turn, 12);
    if (err > 6.0f) WorldAdd(v);
}

void VehiclesFrame()
{
    g_onVehicle = OnVehicle;
    if (GameState() != 9 || !FindPlayerPed()) return;
    uint32_t now = GetTickCount();
    for (auto &n : g_veh) {
        if (!n.id || n.owner == g_localId) continue;
        if (n.veh && !Alive(n)) { Log("copie du vehicule %08X detruite par le jeu", n.id); n.veh = nullptr; }
        uint32_t timeout = (n.last.flags & VF_MISSION) ? 3000 : 30000;   // mission : l'hote l'envoie tant qu'il existe
        if (now - n.lastRecv > timeout && !PuppetInVehicle(n.veh)) { DestroyCopy(n, true); continue; }
        if (!n.veh) CreateCopy(n);
        if (n.veh && now - n.lastRecv < 1500) { UpdateCopy(n); ApplyBody(n); }
    }
    SendOwned();
}
