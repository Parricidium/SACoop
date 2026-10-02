// Personnages de mission partages. Les missions ne tournent que chez l'hote (script.cpp) : ses personnages de mission
// (CPed +0x484 CharCreatedBy == 2, hors joueurs et pantins) sont envoyes aux invites ~15 fois par seconde (MSG_PED),
// leurs vehicules deviennent des vehicules reseau de l'hote (vehicles.cpp). Chez l'invite, chaque personnage a une
// copie (CCivilianPed de mission) qui suit sa position, son arme, sa place en vehicule, et meurt avec lui.
// Les coups ne se decident que chez l'hote : un coup de l'invite sur une copie lui est envoye (MSG_PEDHIT), et un coup
// d'un personnage de mission sur le pantin d'un invite est envoye a ce joueur (combat.cpp, MsgDamage.pedId).
// Modeles speciaux (290-299 : Sweet, Big Smoke...) : charges par leur nom (CStreaming::RequestSpecialModel 0x409D10),
// retenu chez chacun par un detour de cette fonction.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "combat.h"
#include "vehicles.h"
#include "entities.h"
#include "population.h"
#include "mirror.h"
#include "anims.h"
#include <string.h>

using namespace game;

// --- Noms des modeles speciaux ---
static char g_special[10][8];
typedef void(__cdecl *RequestSpecial_t)(int model, const char *name, int flags);
static RequestSpecial_t o_RequestSpecial;
static void __cdecl h_RequestSpecial(int model, const char *name, int flags)
{
    if (model >= 290 && model <= 299 && name) strncpy(g_special[model - 290], name, 7);
    o_RequestSpecial(model, name, flags);
}

static bool IsDead(void *ped)
{
    int st = Field<int>(ped, PED_STATE);
    return st == 54 || st == 55 || Field<float>(ped, PED_HEALTH) <= 0.0f;
}

// --- Hote ---
// Personnage partage : de mission (CharCreatedBy 2), ou ordinaire (1) a moins de 160 m d'un invite partage
// (population.cpp). Hors joueurs et pantins.
uint32_t MissionPedId(void *ped)
{
    if (!ped || !g_cfg.host || (*((uint8_t *)ped + 0x36) & 7) != 3) return 0;
    if (ped == FindPlayerPed() || PuppetIndex(ped) >= 0) return 0;
    uint8_t by = Field<uint8_t>(ped, 0x484);
    if (by == 2) return (uint32_t)PedRef(ped);
    if (by == 1 && NearSharedGuest(EntityPos(ped), 160.0f)) return (uint32_t)PedRef(ped);
    // policiers : partout pres d'un invite partage (ceux qui le poursuivent hors de la zone de l'hote n'etaient pas vus)
    if (by == 1 && Field<int>(ped, 0x598) == 6 && NearSharedGuestAnywhere(EntityPos(ped), 160.0f)) return (uint32_t)PedRef(ped);
    return 0;
}

static void HostSend()
{
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (now - last < 66) return;
    last = now;
    HostRegisterMissionVehicles();
    static int cycle;
    cycle++;
    Pool *p = *(Pool **)0xB74490;
    for (int i = 0; i < p->size; i++) {
        if (p->flags[i] & 0x80) continue;
        void *ped = p->objects + i * 0x7C4;
        uint32_t id = MissionPedId(ped);
        if (!id) continue;
        bool ambient = Field<uint8_t>(ped, 0x484) != 2;
        // passants : 7-8 fois par seconde pres d'un invite (60 m), 3-4 fois plus loin (ils ne font que passer au loin)
        if (ambient && (cycle & 1)) continue;
        if (ambient && (cycle & 2) && !NearSharedGuestAnywhere(EntityPos(ped), 60.0f)) continue;
        MsgPed m = {};
        m.type = MSG_PED;
        m.id = id;
        m.model = (uint16_t)*(int16_t *)((uint8_t *)ped + 0x22);
        memcpy(m.pos, EntityPos(ped), 12);
        memcpy(m.speed, MoveSpeed(ped), 12);
        m.heading = Field<float>(ped, PED_ROTATION);
        m.health = Field<float>(ped, PED_HEALTH);
        m.moveState = (uint8_t)Field<int>(ped, PED_MOVESTATE);
        m.weapon = (uint8_t)Field<int>(ped, PED_WEAPONS + Field<uint8_t>(ped, PED_WEAPONSLOT) * 0x1C);
        m.area = EntityArea(ped);
        if (IsDead(ped)) m.flags |= PF_DEAD;
        if (ambient) m.flags |= PF_AMBIENT;
        if (PedVehicle(ped) && HasTaskType(ped, 1022)) m.flags |= PF_DRIVEBY;   // CTaskSimpleGangDriveBy (tir par la fenetre)
        m.animCount = (uint8_t)AnimsCollect(ped, m.anims, 2);
        if (!PedVehicle(ped)) {   // hauteur suivie par la copie (escalade, saut, chute)
            if (AnimsClimbing(ped)) m.flags |= PF_CLIMB;
            else if (!(Field<uint8_t>(ped, 0x46C) & 1)) m.flags |= PF_AIR;
        }
        if (void *veh = PedVehicle(ped)) {
            m.vehicleId = HostVehicleId(veh, true, ambient);
            if (Field<void *>(veh, VEH_DRIVER) != ped)
                for (int k = 0; k < 8; k++) if (Field<void *>(veh, VEH_PASSENGERS + k * 4) == ped) m.seat = (uint8_t)(k + 1);
        }
        if (m.model >= 290 && m.model <= 299) memcpy(m.special, g_special[m.model - 290], 8);
        CombatNpcShots(ped, m.shots, m.aim);
        float aimNow[3];
        if (!PedVehicle(ped) && AimOf(ped, aimNow)) { m.flags |= PF_AIMING; memcpy(m.aim, aimNow, 12); }   // vise : arme levee chez l'invite
        NetSendToGuests(&m, sizeof(m));
    }
}

static void OnPedHit(const MsgPedHit &h)
{
    void *ped = PedFromRef((int)h.id);
    if (!ped || MissionPedId(ped) != h.id) return;
    int damage = (int)(h.damage + 0.5f);
    if (damage < 1 || damage > 1000) return;
    ApplyPedHit(ped, PuppetOf(h.from), h.weapon, damage, h.bodyPart);
    Log("personnage de mission %08X touche par le joueur %d : %d (arme %d) -> vie %.0f", h.id, h.from, damage, h.weapon, Field<float>(ped, PED_HEALTH));
}

// --- Invite : copies ---
enum { MAX_COPIES = 96 };
struct Copy {
    uint32_t id;         // 0 : case libre
    void *ped;
    int ref;
    MsgPed last;
    uint32_t lastRecv;
    int moveState, weapon;
    bool killed;
    uint8_t lastShots;
    bool shotsKnown;
    bool driveby;        // tir par la fenetre en cours (TASK_DRIVE_BY donnee a la copie)
    AnimMirror anims;    // animations d'action donnees a la copie (anims.cpp)
    bool air;            // suit la hauteur de l'original (FollowAir)
};
static Copy g_copies[MAX_COPIES];

static Copy *FindCopy(uint32_t id)
{
    for (auto &c : g_copies) if (c.id == id) return &c;
    return nullptr;
}
static bool CopyAlive(const Copy &c) { return c.ped && PedFromRef(c.ref) == c.ped; }

bool IsMissionCopy(void *ped)
{
    if (!ped || g_cfg.host) return false;
    for (auto &c : g_copies) if (c.id && c.ped == ped && CopyAlive(c)) return true;
    return false;
}
// Copie vivante la plus proche du joueur local, a moins de 60 m (autotests).
void *AnyMissionCopy()
{
    void *me = FindPlayerPed(), *best = nullptr;
    if (!me) return nullptr;
    const float *mp = EntityPos(me);
    float bestD = 60.0f * 60.0f;
    for (auto &c : g_copies) {
        if (!c.id || !CopyAlive(c) || IsDead(c.ped) || PedVehicle(c.ped)) continue;
        const float *p = EntityPos(c.ped);
        float dx = p[0] - mp[0], dy = p[1] - mp[1], dz = p[2] - mp[2], d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) { bestD = d; best = c.ped; }
    }
    return best;
}
void *MissionCopyById(uint32_t id)
{
    Copy *c = FindCopy(id);
    return c && CopyAlive(*c) ? c->ped : nullptr;
}
void SendMissionPedHit(void *copy, int weapon, int bodyPart, float damage)
{
    for (auto &c : g_copies)
        if (c.id && c.ped == copy) {
            MsgPedHit h = { MSG_PEDHIT, (uint8_t)g_localId, (uint8_t)weapon, (uint8_t)bodyPart, c.id, damage };
            NetSendToAll(&h, sizeof(h));
            return;
        }
}

static void OnPed(const MsgPed &m)
{
    Copy *c = FindCopy(m.id);
    if (!c) {
        for (auto &e : g_copies) if (!e.id) { c = &e; break; }
        if (!c) return;
        memset(c, 0, sizeof(*c));
        c->id = m.id;
        c->weapon = -1;
    }
    c->last = m;
    c->lastRecv = GetTickCount();
}

static void DestroyCopy(Copy &c)
{
    if (CopyAlive(c)) {
        if (PedVehicle(c.ped)) WarpPuppetOut(c.ped, EntityPos(c.ped));
        WorldRemove(c.ped);
        RemoveReferencesToDeletedObject(c.ped);
        DeleteEntity(c.ped);
    }
    memset(&c, 0, sizeof(c));
}

static bool LoadModel(int model, const char *special, bool async)
{
    if (async && model < 290) {   // passant : chargement en fond, la copie attend (pas d'a-coup)
        if (!ModelLoaded(model)) RequestModel(model, 0);
        return ModelLoaded(model);
    }
    if (model >= 290 && model <= 299) {
        if (!special[0]) return false;
        char name[9] = {};
        memcpy(name, special, 8);
        o_RequestSpecial(model, name, 2);
    } else if (!ModelLoaded(model)) {
        RequestModel(model, 2);
    }
    if (!ModelLoaded(model)) LoadAllRequestedModels(false);
    return ModelLoaded(model);
}

static void CreateCopy(Copy &c)
{
    const MsgPed &m = c.last;
    if (m.model >= 300 || !LoadModel(m.model, m.special, (m.flags & PF_AMBIENT) != 0)) return;
    void *ped = NewCivilianPed(4, m.model);
    if (!ped) return;
    SetCharCreatedBy(ped, 2);
    EntityArea(ped) = m.area;
    memcpy(EntityPos(ped), m.pos, 12);
    SetHeading(ped, m.heading);
    WorldAdd(ped);
    c.ped = ped;
    c.ref = PedRef(ped);
    c.moveState = 0;
    c.weapon = -1;
    c.killed = false;
    Log("copie du personnage de mission %08X (modele %d%s%.8s) creee", c.id, m.model, m.special[0] ? " " : "", m.special);
}

static void UpdateCopy(Copy &c, uint32_t now)
{
    const MsgPed &m = c.last;
    void *ped = c.ped;
    if (EntityArea(ped) != m.area) EntityArea(ped) = m.area;
    if (c.air && (m.flags & PF_DEAD)) FollowAirEnd(ped, c.air);
    if (m.flags & PF_DEAD) {
        if (!c.killed && !IsDead(ped)) CombatKillPuppet(ped, m.weapon);
        c.killed = true;
        return;
    }
    Field<float>(ped, PED_HEALTH) = m.health > 1.0f ? m.health : 1.0f;
    EnsurePedWeapon(ped, m.weapon, c.weapon);
    CombatReplayCopyShots(ped, m.weapon, m.shots, m.aim, c.lastShots, c.shotsKnown);
    ClearEventResponses(ped);   // (pas d'IA locale : la copie suit le personnage de l'hote, voir coop.cpp)
    void *inVeh = PedVehicle(ped);
    if (c.air && (inVeh || m.vehicleId)) FollowAirEnd(ped, c.air);
    if (m.vehicleId) {
        void *veh = NetVehicleById(m.vehicleId);
        if (!veh) {
            static uint32_t said;
            if (now - said > 5000) { said = now; Log("copie %08X : vehicule %08X pas (encore) la", c.id, m.vehicleId); }
            return;
        }
        bool placed = inVeh == veh && (m.seat == 0 ? Field<void *>(veh, VEH_DRIVER) == ped : Field<void *>(veh, VEH_PASSENGERS + (m.seat - 1) * 4) == ped);
        // Tir par la fenetre (personnage de l'hote en CTaskSimpleGangDriveBy) : la copie se penche et vise le point de
        // ses derniers tirs (TASK_DRIVE_BY 0713, cadence 0 : les balles sont celles rejouees plus haut).
        bool wantDb = placed && (m.flags & PF_DRIVEBY);
        if (wantDb && !c.driveby) {
            float tgt[3] = { m.aim[0], m.aim[1], m.aim[2] };
            if (tgt[0] == 0 && tgt[1] == 0) { const float *vp = EntityPos(veh); tgt[0] = vp[0] + 10.0f; tgt[1] = vp[1]; tgt[2] = vp[2]; }
            float rad = 60.0f;
            int args[10] = { PedRef(ped), -1, -1, 0, 0, 0, 0, 0, 0, 0 };
            memcpy(&args[3], tgt, 12);
            memcpy(&args[6], &rad, 4);
            RunScriptCommandTyped(0x0713, 10, "iiiffffiii", args);
            c.driveby = true;
            static int said;
            if (said < 10) { said++; Log("copie %08X : tir par la fenetre", c.id); }
        } else if (!wantDb && c.driveby) {
            SetPrimaryTask(ped, nullptr, 3);
            c.driveby = false;
        }
        if (!placed) {
            if (inVeh) WarpPuppetOut(ped, m.pos);
            WarpPuppetIn(ped, veh, m.seat);
            c.moveState = 0;
            static int said;
            if (said < 30) { said++; Log("copie %08X mise dans le vehicule %08X (place %d)", c.id, m.vehicleId, m.seat); }
        }
        return;
    }
    if (inVeh) { WarpPuppetOut(ped, m.pos); c.moveState = 0; return; }
    // Visee (policiers, gangs : arme levee vers leur cible) et animations d'action, comme les pantins des joueurs.
    AimMirror(ped, (m.flags & PF_AIMING) && m.weapon >= 22 && m.weapon <= 38, m.aim);
    AnimsApply(ped, m.anims, m.animCount, c.anims);
    int air = (m.flags & PF_CLIMB) ? 2 : (m.flags & PF_AIR) ? 1 : 0;
    if (air) { FollowAir(ped, m.pos, m.speed, m.heading, air, now - c.lastRecv, c.air); c.moveState = 0; return; }
    if (c.air) FollowAirEnd(ped, c.air);
    FollowOnFoot(ped, m.pos, m.speed, m.heading, m.moveState, now - c.lastRecv, c.moveState);
}

static void GuestUpdate()
{
    uint32_t now = GetTickCount();
    for (auto &c : g_copies) {
        if (!c.id) continue;
        if (now - c.lastRecv > 2000) {
            if (c.ped) Log("copie du personnage de mission %08X retiree", c.id);
            DestroyCopy(c);
            continue;
        }
        if (c.ped && !CopyAlive(c)) { Log("copie du personnage de mission %08X supprimee par le jeu", c.id); c.ped = nullptr; }
        if (!c.ped) {
            if ((c.last.flags & PF_DEAD) || !WorldCalm()) continue;
            CreateCopy(c);
            if (!c.ped) continue;
        }
        UpdateCopy(c, now);
    }
}

void EntitiesFrame()
{
    g_onPed = OnPed;
    g_onPedHit = OnPedHit;
    if (!NetRunning() || GameState() != 9 || !FindPlayerPed()) return;
    if (g_cfg.host) HostSend();
    else GuestUpdate();
}

void EntitiesReset()
{
    for (auto &c : g_copies) if (c.id) DestroyCopy(c);
}

void InstallEntities()
{
    static const uint8_t pro[] = { 0x83, 0xEC, 0x08, 0x53, 0x8B, 0x5C, 0x24, 0x10 };
    o_RequestSpecial = (RequestSpecial_t)MakeDetour(0x409D10, pro, sizeof(pro), (void *)h_RequestSpecial);
}
