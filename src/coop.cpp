// Boucle coop : reseau, etat du joueur local, et doubles des autres joueurs (pantins).
// Pantin : un CCivilianPed marque "personnage de mission" pour que la population ne le retire pas. Modele : CJ habille
// comme le joueur qu'il represente (Tenue=0, par defaut), ou un pieton du jeu (Tenue=1..299). Il se deplace par une
// tache CTaskSimpleGoToPoint vers la position recue (marche / course / sprint selon le joueur) : les animations sont
// celles du jeu. S'il s'ecarte trop, il est replace d'un coup.
//
// CJ habille : le modele 0 n'a pas de maillage fixe, CClothes::ConstructPedModel (0x5A81E0) le construit dans les infos
// du modele 0 a partir d'un jeu de vetements (CPedClothesDesc), et chaque personnage qui prend le modele 0 en fait une
// copie. Pour un pantin : modele 0 construit avec les vetements du joueur distant (recus par MSG_CLOTHES), modele
// attribue au pantin, puis modele 0 reconstruit avec les vetements du joueur local (sinon son prochain changement de
// modele prendrait ceux de l'autre). Sans reconstruction juste avant, la copie etait invisible.
#include "util.h"
#include "panel.h"
#include "gfx.h"
#include "prefs.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "vehicles.h"
#include "hud.h"
#include "combat.h"
#include "peds.h"
#include "entities.h"
#include "mirror.h"
#include "conditions.h"
#include "savesync.h"
#include "population.h"
#include "passenger.h"
#include "script.h"
#include "police.h"
#include "camera.h"
#include "mods.h"
#include "npc.h"
#include <math.h>
#include <string.h>

using namespace game;

struct Puppet {
    void *ped;
    int ref;             // reference de pool : le pantin est-il toujours la ?
    uint32_t lastTask;   // GetTickCount de la derniere tache donnee
    int moveState;
    int model;           // 0 = CJ habille
    uint32_t clothes;    // empreinte des vetements avec lesquels il a ete construit
    uint32_t builtAt;    // GetTickCount de la derniere construction (pas plus d'une toutes les 2 s)
    int carTask;         // montee (1 volant, 2 passager) / descente (3) en cours, jouee par la tache du jeu
    void *carVeh;
    uint32_t carTaskAt;
};
static Puppet g_puppets[MAX_PLAYERS];

// Vetements des autres joueurs (MSG_CLOTHES) : CPedClothesDesc, 30 mots.
static uint32_t g_clothes[MAX_PLAYERS][30];
static uint32_t g_clothesHash[MAX_PLAYERS];   // 0 = pas encore recus

static uint32_t *LocalClothes() { return *(uint32_t **)(0xB7CD9C + 4); }   // CWorld::Players[0].m_PlayerData.m_pPedClothesDesc
static uint32_t ClothesHash(const uint32_t *d)
{
    bool any = false;
    for (int k = 0; k < 28; k++) any |= d[k] != 0;
    if (!any) return 0;   // jeu vide : le script ne l'a pas encore rempli (debut de partie)
    uint32_t h = 2166136261u;
    for (int k = 0; k < 30; k++) { h ^= d[k]; h *= 16777619u; }
    return h ? h : 1;
}
static void ConstructPedModel0(const uint32_t *desc)
{
    ((void(__cdecl *)(int, const void *, const void *, bool))0x5A81E0)(0, desc, nullptr, false);
}
static uint32_t g_stateSeq;

// Rendu des pantins : CPed::Render (vtable[18] des CCivilianPed, 0x86C0A8) ne coupe l'elimination des faces arriere
// (rwRENDERSTATECULLMODE 0x1E = 1, aucune) que pour les joueurs (types 0 et 1). Le maillage de CJ construit a partir de
// ses vetements est rendu comme celui du joueur.
typedef void(__thiscall *PedRender_t)(void *);
static PedRender_t o_CivRender;
static bool IsPuppet(void *ped);
static void __fastcall h_CivRender(void *ped, void *)
{
    if (!IsPuppet(ped)) { o_CivRender(ped); return; }
    uint8_t *rw = *(uint8_t **)0xC97B24;   // RwEngineInstance : +0x20 RenderStateSet, +0x24 RenderStateGet
    int old = 1;
    ((int(__cdecl *)(int, int *))*(void **)(rw + 0x24))(0x1E, &old);
    ((int(__cdecl *)(int, int))*(void **)(rw + 0x20))(0x1E, 1);
    o_CivRender(ped);
    ((int(__cdecl *)(int, int))*(void **)(rw + 0x20))(0x1E, old);
}

void *PuppetOf(int id);

void InstallPuppetRender()
{
    o_CivRender = (PedRender_t)PatchPointer((void **)(0x86C0A8 + 18 * 4), (void *)h_CivRender);
    InstallHud(PuppetOf);
    InstallCombat();
    InstallEntities();
    InstallConditions();
    InstallPopulation();
}
static uint32_t g_calmSince;   // depuis quand on est en partie sans cinematique (creation des pantins)

bool WorldCalm() { return GetTickCount() - g_calmSince >= 5000; }

static bool InGame()
{
    return GameState() == 9 && FindPlayerPed() != nullptr;
}

// --- Etat du joueur local, envoye 30 fois par seconde ---
static void SendLocalState()
{
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (now - last < 33) return;
    last = now;
    MsgState s = {};
    s.type = MSG_STATE;
    s.id = (uint8_t)(g_localId < 0 ? 0 : g_localId);
    s.seq = ++g_stateSeq;
    s.time = now;
    lstrcpynA(s.name, g_cfg.playerName, sizeof(s.name));
    s.skin = (uint16_t)g_cfg.skin;
    void *ped = InGame() ? FindPlayerPed() : nullptr;
    if (ped) {
        s.inGame = 1;
        s.area = EntityArea(ped);
        memcpy(s.pos, EntityPos(ped), sizeof(s.pos));
        memcpy(s.speed, MoveSpeed(ped), sizeof(s.speed));
        s.heading = Field<float>(ped, PED_ROTATION);
        s.health = Field<float>(ped, PED_HEALTH);
        s.armour = Field<float>(ped, PED_ARMOUR);
        s.moveState = (uint8_t)Field<int>(ped, PED_MOVESTATE);
        int slot = Field<uint8_t>(ped, PED_WEAPONSLOT);
        s.weapon = (uint8_t)Field<int>(ped, PED_WEAPONS + slot * 0x1C);
        CombatFillState(s);
        float alpha = *(float *)(0xB6F028 + 0xBFC);
        s.fade = (uint8_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
        s.wanted = (uint8_t)WantedLevel();
        if (void *veh = PedVehicle(ped)) {
            bool driver = Field<void *>(veh, VEH_DRIVER) == ped;
            s.seat = 0;
            if (!driver)
                for (int i = 0; i < 8; i++) if (Field<void *>(veh, VEH_PASSENGERS + i * 4) == ped) s.seat = (uint8_t)(i + 1);
            if (driver || s.seat) s.vehicleId = LocalVehicleId(veh, driver);
        }
        // Montee / descente en cours : rejouee par le pantin (animation de portiere comprise).
        void *task = ActiveTask(ped);
        uintptr_t vt = TaskVtable(task);
        if (vt == VT_TaskEnterCarAsDriver || vt == VT_TaskEnterCarAsPassenger || vt == VT_TaskLeaveCar) {
            void *veh = Field<void *>(task, 0xC);
            if (veh) {
                s.carTask = vt == VT_TaskEnterCarAsDriver ? 1 : vt == VT_TaskEnterCarAsPassenger ? 2 : 3;
                s.carDoor = (uint8_t)Field<int>(task, 0x1C);
                s.carTaskVeh = LocalVehicleId(veh, false);
                if (!s.carTaskVeh) s.carTask = 0;
            }
        }
    }
    NetSendState(s);
}

// --- Heure et meteo : celles de l'hote pour tout le monde (MSG_WORLD, 1 fois par seconde) ---
// CClock : heures 0xB70153, minutes 0xB70152, derniere minute 0xB70158 (CTimer::m_snTimeInMilliseconds 0xB7CB84).
// CWeather : ancienne 0xC81320, nouvelle 0xC8131C, forcee 0xC81318, transition 0xC8130C.
static void OnWorld(const MsgWorld &w)
{
    if (GameState() != 9) return;
    uint8_t &h = *(uint8_t *)0xB70153, &m = *(uint8_t *)0xB70152;
    int mine = h * 60 + m, host = w.hours * 60 + w.minutes, diff = host - mine;
    if (diff > 720) diff -= 1440;
    if (diff < -720) diff += 1440;
    if (diff < -1 || diff > 1) {   // a plus d'une minute : on se cale (sinon l'horloge locale suit d'elle-meme)
        h = w.hours; m = w.minutes;
        *(uint32_t *)0xB70158 = *(uint32_t *)0xB7CB84;
    }
    *(short *)0xC81320 = w.oldWeather;
    *(short *)0xC8131C = w.newWeather;
    *(short *)0xC81318 = w.forcedWeather;
    *(float *)0xC8130C = w.weatherBlend;
}

static void SyncWorld()
{
    if (!g_cfg.host || GameState() != 9) return;
    static uint32_t last;
    if (GetTickCount() - last < 1000) return;
    last = GetTickCount();
    MsgWorld w = { MSG_WORLD, *(uint8_t *)0xB70153, *(uint8_t *)0xB70152, *(short *)0xC81320, *(short *)0xC8131C, *(short *)0xC81318, *(float *)0xC8130C };
    NetSendToGuests(&w, sizeof(w));
}

// --- Vetements du joueur local : a chaque changement, et toutes les 2 s (UDP : un paquet perdu est rattrape) ---
static void SendLocalClothes()
{
    static uint32_t last, lastHash;
    if (!InGame()) return;
    uint32_t *d = LocalClothes();
    uint32_t h = d ? ClothesHash(d) : 0;
    if (!h || (h == lastHash && GetTickCount() - last < 2000)) return;
    last = GetTickCount();
    lastHash = h;
    MsgClothes c = { MSG_CLOTHES, (uint8_t)(g_localId < 0 ? 0 : g_localId) };
    memcpy(c.desc, d, sizeof(c.desc));
    NetSendToAll(&c, sizeof(c));
}

static void OnClothes(const MsgClothes &c)
{
    if (c.id >= MAX_PLAYERS) return;
    uint32_t h = ClothesHash(c.desc);
    if (h == g_clothesHash[c.id]) return;
    memcpy(g_clothes[c.id], c.desc, sizeof(c.desc));
    g_clothesHash[c.id] = h;
    Log("vetements du joueur %d recus (%08X)", c.id, h);
}

// --- Pantins ---
void SetHeading(void *ped, float h)
{
    Field<float>(ped, PED_ROTATION) = h;
    Field<float>(ped, PED_AIMROT) = h;
    if (uint8_t *m = *(uint8_t **)((uint8_t *)ped + 0x14)) {
        float c = cosf(h), s = sinf(h);
        float *right = (float *)m, *fwd = (float *)(m + 0x10), *up = (float *)(m + 0x20);
        right[0] = c;  right[1] = s;  right[2] = 0;
        fwd[0] = -s;   fwd[1] = c;    fwd[2] = 0;
        up[0] = 0;     up[1] = 0;     up[2] = 1;
    }
}

void PlacePuppet(void *ped, const float *pos, float heading)
{
    WorldRemove(ped);
    memcpy(EntityPos(ped), pos, 3 * sizeof(float));
    SetHeading(ped, heading);
    WorldAdd(ped);
}

bool PuppetInVehicle(void *veh)
{
    if (!veh) return false;
    for (auto &p : g_puppets) if (p.ped && PedFromRef(p.ref) == p.ped && PedVehicle(p.ped) == veh) return true;
    return false;
}

// Pantin mis dans un vehicule d'un coup, comme les commandes de script WARP_CHAR_INTO_CAR (0x36A) et
// WARP_CHAR_INTO_CAR_AS_PASSENGER (0x430) : taches videes (CPedIntelligence::FlushImmediately 0x601640), puis tache
// CTaskSimpleCarSetPedInAsDriver (0x6470E0) / AsPassenger (0x646FE0, porte : 0x64F190) construite sur la pile,
// drapeau "d'un coup" leve (+0x18 / +0x1C), executee directement (ProcessPed 0x64B950 / 0x64B5D0), detruite.
void WarpPuppetIn(void *ped, void *veh, int seat)
{
    void *intel = Field<void *>(ped, PED_INTEL);
    ((void(__thiscall *)(void *, bool))0x601640)(intel, false);
    alignas(8) uint8_t task[0x60] = {};
    if (seat == 0) {
        ((void *(__thiscall *)(void *, void *, void *))0x6470E0)(task, veh, nullptr);
        task[0x18] = 1;
        ((bool(__thiscall *)(void *, void *))0x64B950)(task, ped);
        ((void(__thiscall *)(void *))0x647170)(task);
    } else {
        int door = ((int(__cdecl *)(void *, int))0x64F190)(veh, seat - 1);
        ((void *(__thiscall *)(void *, void *, int, void *))0x646FE0)(task, veh, door, nullptr);
        task[0x1C] = 1;
        ((bool(__thiscall *)(void *, void *))0x64B5D0)(task, ped);
        ((void(__thiscall *)(void *))0x647080)(task);
    }
}

// Sortie d'un coup, comme WARP_CHAR_FROM_CAR_TO_COORD (0x362) : taches videes avec tache par defaut, puis
// CPed::Teleport (vtable[14]).
void WarpPuppetOut(void *ped, const float *pos)
{
    void *intel = Field<void *>(ped, PED_INTEL);
    ((void(__thiscall *)(void *, bool))0x601640)(intel, true);
    ((void(__thiscall *)(void *, float, float, float, bool))((*(void ***)ped)[14]))(ped, pos[0], pos[1], pos[2], false);
}

void *PuppetOf(int id)
{
    if (id < 0 || id >= MAX_PLAYERS || id == g_localId) return nullptr;
    Puppet &p = g_puppets[id];
    return p.ped && PedFromRef(p.ref) == p.ped ? p.ped : nullptr;
}

int PuppetIndex(void *ped)
{
    if (!ped) return -1;
    for (int i = 0; i < MAX_PLAYERS; i++) if (g_puppets[i].ped == ped && PedFromRef(g_puppets[i].ref) == ped) return i;
    return -1;
}

static bool IsPuppet(void *ped)
{
    for (auto &p : g_puppets) if (p.ped == ped) return true;
    return false;
}

static void DestroyPuppet(int id)
{
    Puppet &p = g_puppets[id];
    if (!p.ped) return;
    HudUpdateBlip(id, nullptr);
    if (PedFromRef(p.ref) != p.ped) { p.ped = nullptr; return; }   // deja supprime par le jeu
    WorldRemove(p.ped);
    RemoveReferencesToDeletedObject(p.ped);
    DeleteEntity(p.ped);
    p.ped = nullptr;
    Log("pantin du joueur %d retire", id);
}

// Donne au pantin CJ avec les vetements du joueur id (voir en tete). Le pantin est retire du monde et remis dedans.
static void DressPuppet(int id)
{
    Puppet &p = g_puppets[id];
    void *ped = p.ped;
    WorldRemove(ped);
    ((void(__thiscall *)(void *))((*(void ***)ped)[8]))(ped);   // DeleteRwObject (vtable[8])
    ConstructPedModel0(g_clothes[id]);
    ((void(__thiscall *)(void *))0x5E0130)(ped);                // SetModelIndex(0) + CWorld::Add, comme RebuildPlayer
    uint32_t *mine = LocalClothes();
    if (ClothesHash(mine)) ConstructPedModel0(mine);            // le modele 0 reprend les vetements du joueur local
    p.clothes = g_clothesHash[id];
    p.builtAt = GetTickCount();
    Log("pantin du joueur %d habille (vetements %08X)", id, p.clothes);
}

static void CreatePuppet(int id, const MsgState &s)
{
    int model = s.skin <= 299 ? s.skin : 0;
    if (model == 0 && !g_clothesHash[id]) return;   // CJ : on attend ses vetements
    if (!ModelLoaded(model)) {
        RequestModel(model, 2);
        LoadAllRequestedModels(false);
        if (!ModelLoaded(model)) return;   // on reessaie a l'image suivante
    }
    void *ped = NewCivilianPed(2, model);   // PEDTYPE_PLAYER_NETWORK : les relations envers le joueur s'y appliquent (npc.cpp)
    if (!ped) { Log("pantin du joueur %d : pool des personnages plein", id); return; }
    SetCharCreatedBy(ped, 2);
    EntityArea(ped) = s.area;
    memcpy(EntityPos(ped), s.pos, sizeof(s.pos));
    SetHeading(ped, s.heading);
    WorldAdd(ped);
    g_puppets[id] = { ped, PedRef(ped), 0, 0, model, 0, 0 };
    CombatPuppetCreated(id);
    if (model == 0) DressPuppet(id);
    Log("pantin du joueur %d (%s, modele %d) cree en %.1f %.1f %.1f", id, s.name, model, s.pos[0], s.pos[1], s.pos[2]);
}

static int PuppetMove(int remoteMove)
{
    if (remoteMove >= MOVE_SPRINT) return MOVE_SPRINT;
    if (remoteMove >= 5) return MOVE_RUN;
    if (remoteMove == MOVE_WALK) return MOVE_WALK;
    return MOVE_STILL;
}

static void UpdatePuppet(int id)
{
    NetPlayer &np = g_players[id];
    Puppet &p = g_puppets[id];
    void *local = FindPlayerPed();
    bool want = np.connected && np.state.inGame && local && GetTickCount() - np.lastStateAt < 3000;
    if (!want) { DestroyPuppet(id); return; }
    const MsgState &s = np.state;
    if (p.ped && PedFromRef(p.ref) != p.ped) {   // supprime par le jeu (nettoyage de fin de cinematique...)
        Log("pantin du joueur %d supprime par le jeu, recreation", id);
        p.ped = nullptr;
    }
    // Pas pendant une cinematique ni juste apres : le jeu nettoie la zone a la fin (il supprimerait le pantin) et
    // reconstruit le modele de CJ (un pantin copie a ce moment-la restait invisible).
    if (!p.ped && GetTickCount() - g_calmSince < 5000) return;
    if (p.ped && p.model != (s.skin <= 299 ? s.skin : 0)) DestroyPuppet(id);   // tenue changee : autre modele
    if (!p.ped) { CreatePuppet(id, s); if (!p.ped) return; }
    // CJ : le joueur a change de vetements (magasin, garde-robe) : on le rhabille.
    if (p.model == 0 && g_clothesHash[id] && p.clothes != g_clothesHash[id] && GetTickCount() - p.builtAt > 2000) DressPuppet(id);
    void *ped = p.ped;
    if (EntityArea(ped) != s.area) EntityArea(ped) = s.area;
    // Mort : le pantin meurt aussi (animation du jeu) ; a la reapparition du joueur (hopital), il est recree.
    int pstate = Field<int>(ped, PED_STATE);
    bool puppetDead = pstate == 54 || pstate == 55 || Field<float>(ped, PED_HEALTH) <= 0.0f;
    if (s.health <= 0.0f) {
        if (!puppetDead) { CombatKillPuppet(ped, s.weapon); Log("pantin du joueur %d : mort", id); }
        return;
    }
    if (puppetDead) { DestroyPuppet(id); return; }
    CombatUpdatePuppet(id, ped, s);

    // Montee / descente : le pantin joue la tache du jeu (marche jusqu'a la portiere, ouverture, assise ; ou sortie).
    // Pendant ce temps sa position n'est plus suivie ; s'il n'a pas fini a temps, il est place d'un coup.
    void *inVeh = PedVehicle(ped);
    uint32_t now = GetTickCount();
    uintptr_t vt = TaskVtable(ActiveTask(ped));
    bool entering = vt == VT_TaskEnterCarAsDriver || vt == VT_TaskEnterCarAsPassenger, leaving = vt == VT_TaskLeaveCar;
    if ((s.carTask == 1 || s.carTask == 2) && !inVeh && !s.vehicleId) {
        void *veh = NetVehicleById(s.carTaskVeh);
        if (veh && (p.carTask != s.carTask || p.carVeh != veh)) {
            if (void *task = NewEnterCarTask(veh, s.carTask == 1 ? -1 : s.carDoor)) {
                SetPrimaryTask(ped, task, 3);
                p.carTask = s.carTask; p.carVeh = veh; p.carTaskAt = now; p.moveState = 0;
                Log("pantin du joueur %d : monte dans le vehicule %08X (%s)", id, s.carTaskVeh, s.carTask == 1 ? "volant" : "passager");
            }
        }
        if (p.carTask == s.carTask) return;
    }
    if (s.carTask == 3 && inVeh && p.carTask != 3) {
        if (void *task = NewLeaveCarTask(inVeh)) {
            SetPrimaryTask(ped, task, 3);
            p.carTask = 3; p.carVeh = inVeh; p.carTaskAt = now;
            Log("pantin du joueur %d : descend du vehicule", id);
        }
    }
    // (descente : le pantin attend aussi que son joueur ait fini la sienne, sinon il serait remis dedans)
    bool busy = (p.carTask == 3 ? leaving || inVeh || s.carTask == 3 || s.vehicleId : entering) && now - p.carTaskAt < 5000;
    if (p.carTask && !busy) p.carTask = 0;

    // En vehicule : le pantin est mis a sa place dans la copie ; il en sort quand le joueur est a pied.
    if (s.vehicleId) {
        void *veh = NetVehicleById(s.vehicleId);
        if (!veh) return;   // copie pas encore creee (modele en chargement)
        bool placed = inVeh == veh && (s.seat == 0 ? Field<void *>(veh, VEH_DRIVER) == ped : Field<void *>(veh, VEH_PASSENGERS + (s.seat - 1) * 4) == ped);
        if (placed) { if (p.carTask != 3) p.carTask = 0; return; }
        if (p.carTask) return;   // montee (ou descente) en cours : on la laisse finir
        if (inVeh) WarpPuppetOut(ped, s.pos);
        WarpPuppetIn(ped, veh, s.seat);
        p.moveState = 0;
        Log("pantin du joueur %d mis dans le vehicule %08X (place %d)", id, s.vehicleId, s.seat);
        return;
    }
    if (p.carTask == 3) return;   // descente en cours
    if (inVeh) {
        WarpPuppetOut(ped, s.pos);
        p.moveState = 0;
        Log("pantin du joueur %d sorti du vehicule", id);
        return;
    }
    if (p.carTask) {   // montee abandonnee par le joueur : le pantin s'arrete
        p.carTask = 0;
        SetPrimaryTask(ped, nullptr, 3);
    }

    if (FollowOnFoot(ped, s.pos, s.speed, s.heading, s.moveState, GetTickCount() - np.lastStateAt, p.moveState)) p.lastTask = GetTickCount();
}

// Fait suivre a un personnage a pied la position recue (pantin d'un joueur, copie d'un personnage de mission) :
// position visee un peu en avant selon la vitesse (le joueur a continue d'avancer depuis l'envoi), marche/course par
// CTaskSimpleGoToPoint, grand ecart rattrape d'un coup. Renvoie vrai si une nouvelle tache a ete donnee.
bool FollowOnFoot(void *ped, const float *rpos, const float *rspeed, float heading, int remoteMove, uint32_t ageMs, int &moveState)
{
    float lead = ageMs / 1000.0f;   // (horloge locale : celle de l'autre PC n'est pas la meme)
    if (lead > 0.3f) lead = 0.3f;
    float target[3];
    for (int k = 0; k < 3; k++) target[k] = rpos[k] + rspeed[k] * 50.0f * lead;   // vitesse du jeu : par 1/50 s
    float *pos = EntityPos(ped);
    float dx = target[0] - pos[0], dy = target[1] - pos[1], dz = target[2] - pos[2];
    float d2 = dx * dx + dy * dy, dist = sqrtf(d2);
    if (dist > 4.0f || fabsf(dz) > 3.0f) {   // trop loin : replace d'un coup
        PlacePuppet(ped, rpos, heading);
        SetPrimaryTask(ped, nullptr, 3);
        moveState = 0;
        return false;
    }
    int move = PuppetMove(remoteMove);
    if (move == MOVE_STILL && dist < 0.6f) {
        if (moveState != MOVE_STILL) { SetPrimaryTask(ped, nullptr, 3); moveState = MOVE_STILL; }
        SetHeading(ped, heading);
        return false;
    }
    if (move == MOVE_STILL) move = MOVE_WALK;   // petit rattrapage a pied
    // Cible : devant le joueur, pour que le pantin ne s'arrete pas entre deux messages.
    CVector goal = { target[0], target[1], target[2] };
    if (dist > 0.01f) { goal.x = target[0] + dx / dist * 1.0f; goal.y = target[1] + dy / dist * 1.0f; }
    void **tasks = PrimaryTasks(ped);
    void *cur = tasks[3];
    if (cur && *(uintptr_t *)cur == VT_TaskSimpleGoToPoint && moveState == move) {
        *(CVector *)((uint8_t *)cur + 0xC) = goal;
        return false;
    }
    if (void *task = NewGoToPoint(move, goal, 0.5f)) {
        SetPrimaryTask(ped, task, 3);
        moveState = move;
        return true;
    }
    return false;
}

// --- Autotest (instances de test) : le joueur local court tout droit par intervalles ---
// Pas de tache sur le joueur (un CTaskSimpleGoToPoint donne au CPlayerPed faisait planter le jeu) : on pousse le
// manche gauche de la manette 0 comme le ferait une vraie manette. CPad[0] en 0xB73458 ; PCTempJoyState (+0xA8) est
// fusionne dans NewState a chaque CPad::Update ; LeftStickY (+2) = -128 : en avant.
static void *g_testCar;

static void Autotest()
{
    if (!g_cfg.autotest[0] || !InGame()) return;
    static uint32_t start;
    if (!start) start = GetTickCount();
    uint32_t t = GetTickCount() - start;
    int16_t *joy = (int16_t *)(0xB73458 + 0xA8);
    // D'abord la cinematique d'intro : Croix (+0x20) appuyee / relachee toutes les 0,7 s pendant 20 s.
    if (t < 20000) {
        joy[0x20 / 2] = (t / 350) % 2 ? 255 : 0;
        return;
    }
    joy[0x20 / 2] = 0;
    // "regarde" (invite) : une fois, place le joueur 8 m derriere l'hote, tourne vers +x (axe des allers-retours de
    // l'hote), camera dans le dos : les captures montrent le pantin de l'hote de face.
    if (_stricmp(g_cfg.autotest, "regarde") == 0 && g_players[0].connected && g_players[0].state.inGame) {
        static bool placed;
        void *ped = FindPlayerPed();
        if (!placed) {
            placed = true;
            // ruelle de Ganton : "voiture" (hote) -> 20 m devant son depart, tourne vers lui ; sinon 10 m derriere lui.
            bool car = g_players[0].state.vehicleId != 0;
            // (a pied : 0.5 m sur le cote, dans l'axe de la visee libre du "tireur", camera par-dessus l'epaule)
            float pos[3] = { car ? 2264.0f : 2232.0f, car ? -1262.3f : -1261.8f, 23.9f };
            PlacePuppet(ped, pos, car ? 1.5708f : -1.5708f);
            Log("autotest : place derriere l'hote en %.1f %.1f %.1f", pos[0], pos[1], pos[2]);
        }
        // Voiture de l'hote : une fois qu'il en est descendu, on prend le volant de la copie et on avance un peu.
        static uint32_t hostCar, tookAt;
        if (g_players[0].state.vehicleId) hostCar = g_players[0].state.vehicleId;
        if (hostCar && !g_players[0].state.vehicleId && t > 36000 && !tookAt) {
            void *veh = NetVehicleById(hostCar);
            if (veh && (((uint8_t *)veh)[0x36] >> 3) != 5 && *(float *)((uint8_t *)veh + 0x4C0) > 500.0f) {   // pas une epave
                WarpPuppetIn(ped, veh, 0);
                tookAt = t;
                Log("autotest : je prends le volant du vehicule %08X", hostCar);
            }
        }
        if (tookAt) {
            joy[0x20 / 2] = (t - tookAt > 1500 && t - tookAt < 2300) ? 255 : 0;
            return;
        }
        ((void(__thiscall *)(void *))0x50BD40)((void *)0xB6F028);   // CCamera::SetCameraDirectlyBehindForFollowPed_CamOnAString
        return;
    }
    // "mission" (hote) : lance la mission de l'histoire TestMission (13 SWEET1 par defaut) a 24 s, comme le script
    // principal (START_MISSION 0417) : les invites doivent en voir les textes, marqueurs, fondus et personnages.
    if (_stricmp(g_cfg.autotest, "mission") == 0) {
        static bool started;
        static uint32_t freeSince;
        const char *running = RunningMissionScript();
        static const char *lastRunning = (const char *)1;
        if (running != lastRunning) { lastRunning = running; Log("autotest : mission en cours : %.8s", running ? running : "aucune"); }
        if (running) freeSince = 0; else if (!freeSince) freeSince = t;
        if (!started && t > 24000 && freeSince && t - freeSince > 2000) {   // une seule mission a la fois (espace partage)
            started = true;
            int m = g_cfg.testMission ? g_cfg.testMission : 13;
            MirrorMissionStart();
            RunScriptCommand(0x0417, 1, &m);
            Log("autotest : mission %d lancee", m);
        }
        // TestPasser=1 : l'hote passe la cinematique d'ouverture (Croix) 8 s apres le lancement.
        static uint32_t startedAt;
        if (started && !startedAt) startedAt = t;
        if (g_cfg.testSkip && startedAt && t - startedAt > 8000 && t - startedAt < 10000) joy[0x20 / 2] = (t / 100) % 2 ? 255 : 0;
        // Echec voulu a 85 s (l'hote meurt) : la fin de mission doit retablir l'ecran des invites.
        static bool failed;
        if (started && !failed && t > 85000 && running) {
            failed = true;
            ApplyPedHit(FindPlayerPed(), nullptr, 0, 1000, 3);
            Log("autotest : l'hote meurt (echec de mission)");
        }
        return;
    }
    // "objet" (hote) : un objet de mission (baril) devant l'invite "regarde" a 26 s, deplace a 34 s, supprime a 42 s.
    if (_stricmp(g_cfg.autotest, "objet") == 0) {
        static int step;
        static const float at[3][3] = { { 2236.0f, -1262.3f, 23.4f }, { 2236.0f, -1260.0f, 23.4f }, {} };
        if (step < 3 && t > 26000u + step * 8000u) { MirrorTestObject(step, at[step]); step++; }
        return;
    }
    // "sauve" (hote) : sauvegarde dans l'emplacement 6 a 30 s (C_PcSave::SaveSlot 0x619060, 0 = reussi).
    if (_stricmp(g_cfg.autotest, "sauve") == 0) {
        static bool saved;
        if (!saved && t > 30000) {
            saved = true;
            int r = ((char(__cdecl *)(char))0x619060)(5);
            Log("autotest : sauvegarde emplacement 6 -> %d", r);
        }
        return;
    }
    // "police" (invite) : pose a 6 m de l'hote, puis recherche 2 a 30 s : la police de l'hote doit venir le chercher
    // (police.cpp) et ses coups arriver ici. Vie remise a 100 toutes les 10 s pour durer.
    // "policeloin" (invite) : pareil a 90 m a l'ouest de l'hote : les voitures de police doivent venir a lui.
    bool loin = _stricmp(g_cfg.autotest, "policeloin") == 0;
    if ((_stricmp(g_cfg.autotest, "police") == 0 || loin) && g_players[0].connected && g_players[0].state.inGame) {
        static bool placed, wanted;
        static uint32_t lastHeal;
        void *ped = FindPlayerPed();
        if (!placed && t > 26000) {
            placed = true;
            float pos[3] = { g_players[0].state.pos[0] + (loin ? -90.0f : 6.0f), g_players[0].state.pos[1] + (loin ? 8.0f : 0.0f), g_players[0].state.pos[2] + (loin ? 1.0f : 0.0f) };
            PlacePuppet(ped, pos, 1.5708f);
            Log("autotest : place a 6 m de l'hote");
        }
        if (!wanted && t > 30000) {
            wanted = true;
            int a[2] = { 0, 2 };
            RunScriptCommand(0x010D, 2, a);
            Log("autotest : recherche %d", WantedLevel());
        }
        if (wanted && t - lastHeal > 10000) {
            lastHeal = t;
            Log("autotest : vie %.0f, recherche %d", Field<float>(ped, PED_HEALTH), WantedLevel());
            if (Field<float>(ped, PED_HEALTH) > 0.0f) Field<float>(ped, PED_HEALTH) = 100.0f;
        }
        return;
    }
    // "loin" (invite) : a 26 s, se pose 130 m a l'ouest de l'hote (rue de Grove Street, hote en autotest "rue") :
    // partage mais hors de sa zone, chacun peuple son cote (population.cpp).
    if (_stricmp(g_cfg.autotest, "loin") == 0 && g_players[0].connected && g_players[0].state.inGame) {
        static bool placed;
        if (!placed && t > 26000) {
            placed = true;
            float pos[3] = { g_players[0].state.pos[0] - 130.0f, g_players[0].state.pos[1] + 8.0f, g_players[0].state.pos[2] + 1.0f };
            PlacePuppet(FindPlayerPed(), pos, 1.5708f);
            Log("autotest : pose a 130 m de l'hote (%.1f %.1f %.1f)", pos[0], pos[1], pos[2]);
        }
        return;
    }
    // "gang" (hote) : dans la ruelle ("regarde" pose l'invite 10 m a l'ouest) ; a 28 s, les Ballas (GANG1, type 7)
    // detestent le joueur et trois Ballas armes apparaissent 6 m au-dela de l'invite ; a 43 s, un ennemi de mission
    // (type 24) est lance sur l'hote (05E2) : tous doivent s'en prendre a l'invite, le plus proche (npc.cpp).
    if (_stricmp(g_cfg.autotest, "gang") == 0) {
        static int step;
        void *ped = FindPlayerPed();
        if (step == 0) {
            step = 1;
            float pos[3] = { 2242.0f, -1262.3f, 23.9f };
            PlacePuppet(ped, pos, 1.5708f);
            return;
        }
        if (step == 1 && t > 28000) {
            step = 2;
            int rel[3] = { 4, 7, 0 };
            RunScriptCommand(0x0746, 3, rel);   // SET_RELATIONSHIP hate, GANG1, PLAYER1
            if (!ModelLoaded(102)) { RequestModel(102, 2); LoadAllRequestedModels(false); }
            for (int k = 0; k < 3 && ModelLoaded(102); k++) {
                float np[3] = { 2226.0f - k * 1.2f, -1261.0f + (k % 2) * 1.5f, 23.9f };
                // CPopulation::AddPed (0x612710 : type, modele, position, flane) : un vrai passant de gang
                void *npc = ((void *(__cdecl *)(int, int, const float *, bool))0x612710)(7, 102, np, true);
                if (!npc) break;
                int w = -1;
                EnsurePedWeapon(npc, 22, w);
            }
            Log("autotest : Ballas hostiles poses");
        }
        if (step == 2 && t > 43000 && ModelLoaded(102)) {   // l'ennemi de mission, 15 s plus tard
            step = 3;
            if (void *npc = NewCivilianPed(24, 102)) {
                float np[3] = { 2222.0f, -1262.0f, 23.9f };
                memcpy(EntityPos(npc), np, 12);
                WorldAdd(npc);
                int w = -1;
                EnsurePedWeapon(npc, 22, w);
                int a[2] = { PedRef(npc), PedRef(ped) };
                RunScriptCommand(0x05E2, 2, a);
                NpcHunterNoted(PedRef(npc));
                Log("autotest : ennemi de mission lance sur l'hote");
            }
        }
        return;
    }
    // "rue" (hote) : se pose une fois dans Grove Street (impasse ouverte : les voitures de police y arrivent).
    if (_stricmp(g_cfg.autotest, "rue") == 0) {
        static bool placed;
        if (!placed) {
            placed = true;
            float pos[3] = { 2495.0f, -1668.0f, 13.34f };
            PlacePuppet(FindPlayerPed(), pos, 0.0f);
            Log("autotest : pose dans Grove Street");
        }
        return;
    }
    // "vue" : se pose une fois en haut de la plus haute tour de Los Santos, face au sud (captures de la distance
    // d'affichage).
    if (_stricmp(g_cfg.autotest, "vue") == 0) {
        static uint32_t placedAt;
        if (!placedAt) {
            placedAt = GetTickCount();
            float pos[3] = { 1544.9f, -1353.3f, 329.5f };
            PlacePuppet(FindPlayerPed(), pos, 3.14f);
            Log("autotest : pose en haut de la tour");
        } else if (placedAt != 1 && GetTickCount() - placedAt > 2000) {
            placedAt = 1;   // camera fixe au-dessus du toit, vers les collines de Vinewood et au-dela
            float cam[6] = { 1544.9f, -1353.3f, 345.0f, 0, 0, 0 }, at[3] = { 1150.0f, -850.0f, 120.0f };
            int a1[6], a2[4];
            memcpy(a1, cam, sizeof(cam));
            memcpy(a2, at, sizeof(at));
            a2[3] = 2;
            RunScriptCommandTyped(0x015F, 6, "ffffff", a1);   // SET_FIXED_CAMERA_POSITION
            RunScriptCommandTyped(0x0160, 4, "fffi", a2);     // POINT_CAMERA_AT_POINT
            Log("autotest : camera fixe sur la ville");
        }
        return;
    }
    // "pnj" (hote) : un personnage de mission (Big Smoke, modele special 290 "SMOKE") pose a 4 m, qui fait des
    // allers-retours dans la ruelle, avec un pistolet ; tue a 60 s s'il vit encore (sa copie doit tomber chez l'invite).
    if (_stricmp(g_cfg.autotest, "pnj") == 0) {
        static void *npc;
        static int npcRef;
        void *ped = FindPlayerPed();
        if (!npc) {
            float pos[3] = { 2246.0f, -1262.3f, 23.9f };
            PlacePuppet(ped, pos, 1.5708f);
            ((void(__cdecl *)(int, const char *, int))0x409D10)(290, "SMOKE", 2);
            LoadAllRequestedModels(false);
            if (!ModelLoaded(290)) return;
            npc = NewCivilianPed(4, 290);
            if (!npc) return;
            SetCharCreatedBy(npc, 2);
            float np[3] = { 2241.0f, -1260.5f, 23.9f };
            memcpy(EntityPos(npc), np, 12);
            WorldAdd(npc);
            npcRef = PedRef(npc);
            int w = -1;
            EnsurePedWeapon(npc, 22, w);
            Log("autotest : Big Smoke de mission cree");
            return;
        }
        if (PedFromRef(npcRef) != npc) return;
        static int phase = -1;
        int ph = t > 60000 ? 9 : (int)((t - 20000) / 4000) % 2;
        if (ph != phase) {
            phase = ph;
            if (ph == 9) { ApplyPedHit(npc, ped, 22, 1000, 3); Log("autotest : Big Smoke tue"); }
            else {
                CVector goal = { ph == 0 ? 2234.0f : 2244.0f, -1260.5f, 23.9f };
                if (void *task = NewGoToPoint(MOVE_WALK, goal, 0.5f)) SetPrimaryTask(npc, task, 3);
            }
        }
        return;
    }
    // "aide" (invite) : des que la mission de l'hote a un vehicule de mission (copie), on se met au volant : la
    // mission doit avancer comme si l'hote y etait monte (conditions.cpp).
    if (_stricmp(g_cfg.autotest, "aide") == 0) {
        static uint32_t seenAt;
        static bool inside;
        void *veh = AnyMissionVehicleCopy();
        if (!veh) { seenAt = 0; return; }
        if (!seenAt) seenAt = t;
        if (!inside && t - seenAt > 8000 && !Field<void *>(veh, VEH_DRIVER)) {
            inside = true;
            WarpPuppetIn(FindPlayerPed(), veh, 0);
            Log("autotest : au volant du vehicule de mission");
        }
        return;
    }
    // "passager" (invite) : quand l'hote ("taxi") est au volant, pose a 2,5 m a droite de sa voiture puis G (30 s).
    if (_stricmp(g_cfg.autotest, "passager") == 0) {
        static int step;
        void *ped = FindPlayerPed();
        void *veh = g_players[0].state.vehicleId ? NetVehicleById(g_players[0].state.vehicleId) : nullptr;
        if (step == 0 && veh && t > 28000) {
            step = 1;
            const float *vp = EntityPos(veh);
            float pos[3] = { vp[0], vp[1] - 2.5f, vp[2] };
            PlacePuppet(ped, pos, 0.0f);
            Log("autotest : a cote de la voiture de l'hote (%.1f %.1f %.1f, conducteur %p, pantin %p)", vp[0], vp[1], vp[2], Field<void *>(veh, VEH_DRIVER), PuppetOf(0));
        } else if (step == 1 && t > 30000) {
            step = 2;
            PassengerRequest();
            Log("autotest : G (passager)");
        }
        if (step < 2) ((void(__thiscall *)(void *))0x50BD40)((void *)0xB6F028);
        return;
    }
    // "taxi" (hote) : au volant d'un Greenwood pose a 5 m, immobile jusqu'a 45 s, puis avance 2 s.
    // "taximoto" (hote) : pareil sur une PCJ-600 (461) : l'invite monte derriere.
    if (_stricmp(g_cfg.autotest, "taxi") == 0 || _stricmp(g_cfg.autotest, "taximoto") == 0 || _stricmp(g_cfg.autotest, "taxivelo") == 0) {
        static bool done;
        int model = _stricmp(g_cfg.autotest, "taximoto") == 0 ? 461 : _stricmp(g_cfg.autotest, "taxivelo") == 0 ? 481 : 492;   // (481 : BMX, sans place passager)
        void *ped = FindPlayerPed();
        if (!done) {
            done = true;
            float pos[3] = { 2485.0f, -1665.0f, 13.3f };   // Grove Street, degage (portieres accessibles des deux cotes)
            PlacePuppet(ped, pos, -1.5708f);
            if (!ModelLoaded(model)) { RequestModel(model, 2); LoadAllRequestedModels(false); }
            if (void *car = ((void *(__cdecl *)(int, float, float, float, bool))0x431F80)(model, 2490.0f, -1665.0f, 13.6f, false)) {
                if (uint8_t *m = *(uint8_t **)((uint8_t *)car + 0x14)) {
                    float *r = (float *)m, *f = (float *)(m + 0x10);
                    r[0] = 0; r[1] = -1; r[2] = 0; f[0] = 1; f[1] = 0; f[2] = 0;
                }
                WarpPuppetIn(ped, car, 0);
                Log("autotest : taxi pret");
            }
            return;
        }
        joy[0x20 / 2] = (t > 45000 && t < 47000) ? 255 : 0;   // Croix : avance
        return;
    }
    // "tireinv" (invite) : place comme "regarde", un M4, puis tire sur la premiere copie de personnage de mission
    // (a partir de 32 s, un coup toutes les 500 ms) : les coups vont a l'hote.
    if (_stricmp(g_cfg.autotest, "tireinv") == 0) {
        static bool armed;
        void *ped = FindPlayerPed();
        if (!armed) {
            armed = true;
            float pos[3] = { 2232.0f, -1262.3f, 23.9f };
            PlacePuppet(ped, pos, -1.5708f);
            uint8_t *info = WeaponInfo(31);
            int m1 = *(int *)(info + 0xC);
            if (m1 > 0 && !ModelLoaded(m1)) { RequestModel(m1, 2); LoadAllRequestedModels(false); }
            SetCurrentWeapon(ped, GiveWeapon(ped, 31, 900));
            return;
        }
        if (t < 30000) ((void(__thiscall *)(void *))0x50BD40)((void *)0xB6F028);   // (pas pendant les tirs : visee faussee)
        joy[0xC / 2] = t > 30000 ? 255 : 0;   // R1 : on vise (sans visee, le tir du joueur part vers 0,0,0)
        static uint32_t lastShot;
        void *target = AnyMissionCopy();
        if (target && t > 31000) {   // face a la cible (le tir du joueur suit son cap)
            const float *a = EntityPos(ped), *b = EntityPos(target);
            float h = atan2f(-(b[0] - a[0]), b[1] - a[1]);
            Field<float>(ped, PED_ROTATION) = h;
            Field<float>(ped, PED_AIMROT) = h;
        }
        if (target && t > 32000 && t - lastShot > 500) {
            lastShot = t;
            static void *said;
            if (said != target) { said = target; const float *a = EntityPos(ped), *b = EntityPos(target); Log("autotest : cible %p a %.1f %.1f %.1f (moi %.1f %.1f)", target, b[0], b[1], b[2], a[0], a[1]); }
            const float *b = EntityPos(target);
            float aim[3] = { b[0], b[1], b[2] + 0.3f };
            CombatTestShot(ped, aim);
        }
        return;
    }
    // "monte" (hote) : un Greenwood pose a 5 m, l'hote a cote de la portiere conducteur ; Triangle a 22 s (il monte),
    // avance un peu, Triangle a 30 s (il descend).
    if (_stricmp(g_cfg.autotest, "monte") == 0) {
        static bool done;
        void *ped = FindPlayerPed();
        if (!done) {
            done = true;
            float pos[3] = { 2245.5f, -1260.4f, 23.9f };
            PlacePuppet(ped, pos, -1.5708f);
            if (!ModelLoaded(492)) { RequestModel(492, 2); LoadAllRequestedModels(false); }
            static void *car;
            car = ((void *(__cdecl *)(int, float, float, float, bool))0x431F80)(492, 2247.0f, -1262.3f, 24.2f, false);
            g_testCar = car;
            if (car)
                if (uint8_t *m = *(uint8_t **)((uint8_t *)car + 0x14)) {
                    float *r = (float *)m, *f = (float *)(m + 0x10);
                    r[0] = 0; r[1] = -1; r[2] = 0; f[0] = 1; f[1] = 0; f[2] = 0;
                }
            ((void(__thiscall *)(void *))0x50BD40)((void *)0xB6F028);
            Log("autotest : Greenwood pose, je vais monter");
            return;
        }
        // Apres la descente : portiere avant gauche arrachee, pare-chocs avant pendant (a 32 s), explosion (a 36 s).
        if (uint8_t *car = (uint8_t *)g_testCar) {
            static int step;
            if (step == 0 && t > 32000) {
                step = 1;
                car[0x5A0 + 9 + 2] = 4;   // portiere avant gauche : absente
                ((void(__thiscall *)(void *, int, bool))0x6B1600)(car, 2, false);
                uint32_t &panels = *(uint32_t *)(car + 0x5A0 + 0x14);
                panels = (panels & ~(0xFu << 20)) | (2u << 20);   // pare-chocs avant : pendant
                ((void(__thiscall *)(void *, int, bool))0x6B1350)(car, 5, false);
                *(float *)(car + 0x4C0) = 400.0f;
                Log("autotest : voiture abimee");
            } else if (step == 1 && t > 36000) {
                step = 2;
                ((void(__thiscall *)(void *, void *, bool))((*(void ***)car)[41]))(car, nullptr, false);
                Log("autotest : voiture explosee");
            }
        }
        joy[0x1E / 2] = ((t > 22000 && t < 22300) || (t > 30000 && t < 30300)) ? 255 : 0;   // Triangle
        joy[0x20 / 2] = (t > 26500 && t < 27300) ? 255 : 0;                                  // Croix : avance un peu
        return;
    }
    // "voiture" (hote) : un Greenwood (492) pose a 5 m, l'hote mis au volant, puis avance 2 s / s'arrete 2 s / recule 2 s
    // (Croix : accelerer, Carre : freiner et reculer, sur la manette 0).
    // "moto" (hote) : pareil avec une PCJ-600 (461).
    bool moto = _stricmp(g_cfg.autotest, "moto") == 0;
    if (_stricmp(g_cfg.autotest, "voiture") == 0 || moto) {
        static void *car;
        static bool done;
        void *ped = FindPlayerPed();
        if (!done) {
            done = true;
            float pos[3] = { 2242.0f, -1262.3f, 23.9f };
            PlacePuppet(ped, pos, -1.5708f);
            int model = moto ? 461 : 492;
            if (!ModelLoaded(model)) { RequestModel(model, 2); LoadAllRequestedModels(false); }
            car = ((void *(__cdecl *)(int, float, float, float, bool))0x431F80)(model, 2247.0f, -1262.3f, 24.2f, false);
            if (car) {
                if (uint8_t *m = *(uint8_t **)((uint8_t *)car + 0x14)) {   // tournee vers +x
                    float *r = (float *)m, *f = (float *)(m + 0x10);
                    r[0] = 0; r[1] = -1; r[2] = 0; f[0] = 1; f[1] = 0; f[2] = 0;
                }
                WarpPuppetIn(ped, car, 0);
                Log("autotest : au volant d'un %s", moto ? "PCJ-600" : "Greenwood");
            }
            return;
        }
        // 0,8 s en avant, 1,6 s au repos, 1,2 s en arriere (le recul est plus lent) : la voiture reste dans la ruelle.
        if (t > 32000) {   // descente (Triangle), puis a pied
            joy[0x20 / 2] = joy[0x1C / 2] = 0;
            joy[0x1E / 2] = t < 32400 ? 255 : 0;
            static bool said;
            if (!said) { said = true; Log("autotest : je descends"); }
            return;
        }
        uint32_t c = (t - 20000) % 3200;
        int phase = c < (moto ? 350u : 1000u) ? 0 : c < (moto ? 1800u : 2400u) ? 1 : 2;   // (moto : bien plus vive)
        joy[0x20 / 2] = phase == 0 ? 255 : 0;   // Croix
        joy[0x1C / 2] = phase == 2 ? 255 : 0;   // Carre
        static int last = -1;
        if (last != phase) { last = phase; Log("autotest : %s", phase == 0 ? "j'avance" : phase == 1 ? "je m'arrete" : "je recule"); }
        return;
    }
    // "tireur" (hote) : un M4, tourne vers l'invite ("regarde" en 2232), vise (R1) et tire par rafales (Rond).
    if (_stricmp(g_cfg.autotest, "tireur") == 0) {
        static bool armed;
        void *ped = FindPlayerPed();
        if (!armed) {
            armed = true;
            float pos[3] = { 2242.0f, -1262.3f, 23.9f };
            PlacePuppet(ped, pos, 1.5708f);
            uint8_t *info = WeaponInfo(31);
            int m1 = *(int *)(info + 0xC);
            if (m1 > 0 && !ModelLoaded(m1)) { RequestModel(m1, 2); LoadAllRequestedModels(false); }
            SetCurrentWeapon(ped, GiveWeapon(ped, 31, 900));
            Log("autotest : M4 en main");
            return;
        }
        if (t < 26000) ((void(__thiscall *)(void *))0x50BD40)((void *)0xB6F028);   // camera dans le dos (tourne vers l'invite)
        void *target = PuppetOf(1);
        if (target && t > 26000) {        // pile face au pantin (a 10 m, 0.1 rad d'ecart = rate)
            const float *a = EntityPos(ped), *b = EntityPos(target);
            float h = atan2f(-(b[0] - a[0]), b[1] - a[1]);
            Field<float>(ped, PED_ROTATION) = h;
            Field<float>(ped, PED_AIMROT) = h;
        }
        joy[0xC / 2] = t > 26000 ? 255 : 0;                          // R1 : on vise
        // Tirs vers la poitrine du pantin (la visee libre suit la camera, trop imprecise pour un test) : 1 coup / 400 ms.
        static uint32_t lastShot;
        const float *tp = target ? EntityPos(target) : nullptr, *mp = EntityPos(ped);
        if (tp && fabsf(tp[0] - mp[0]) + fabsf(tp[1] - mp[1]) < 30.0f && t > 27000 && t - lastShot > 400) {
            lastShot = t;
            const float *b = tp;
            float aim[3] = { b[0], b[1], b[2] + 0.3f };
            CombatTestShot(ped, aim);
        }
        return;
    }
    if (_stricmp(g_cfg.autotest, "marche") == 0) {
        static bool placed;
        if (!placed) {   // depart fixe dans la ruelle, tourne vers +x : memes images d'un test a l'autre
            placed = true;
            float pos[3] = { 2242.0f, -1262.3f, 23.9f };
            PlacePuppet(FindPlayerPed(), pos, -1.5708f);
            ((void(__thiscall *)(void *))0x50BD40)((void *)0xB6F028);
        }
        // Allers-retours autour du point de depart : 3 s en avant, 1 s arrete, 3 s en arriere (le joueur fait demi-tour).
        int phase = (int)((t - 20000) / 1000) % 8;
        int want = phase < 3 ? -128 : phase < 4 ? 0 : phase < 7 ? 127 : 0;
        static int last = 999;
        joy[1] = (int16_t)want;
        if (last != want) { last = want; Log("autotest : %s", want < 0 ? "je cours" : want > 0 ? "je reviens" : "je m'arrete"); }
    }
    // "nage" (cadence, fps.cpp) : pose en mer au large de Santa Maria, nage tout droit 10 s (distance au journal, a
    // comparer entre 30 et 60 images/s), puis pilote un Maverick 10 s (rotor).
    if (_stricmp(g_cfg.autotest, "nage") == 0) {
        static int step;
        static float from[3];
        static void *heli;
        static uint32_t frames0;
        void *ped = FindPlayerPed();
        if (step == 0) {
            step = 1;
            float pos[3] = { 250.0f, -1950.0f, 0.5f };
            PlacePuppet(ped, pos, 1.5708f);
            ((void(__thiscall *)(void *))0x50BD40)((void *)0xB6F028);
            Log("autotest : pose en mer");
        } else if (step == 1 && t > 25000) {
            step = 2;
            memcpy(from, EntityPos(ped), 12);
            frames0 = *(uint32_t *)0xB7CB4C;   // CTimer::m_FrameCounter
            Log("autotest : je nage");
        } else if (step == 2) {
            joy[1] = -128;
            if (t > 35000) {
                step = 3;
                joy[1] = 0;
                const float *p = EntityPos(ped);
                float dx = p[0] - from[0], dy = p[1] - from[1];
                Log("autotest : nage, %.1f m en 10 s a %.1f images/s (z %.1f)", sqrtf(dx * dx + dy * dy), (*(uint32_t *)0xB7CB4C - frames0) / 10.0f, p[2]);
            }
        } else if (step == 3 && t > 37000) {
            step = 4;
            if (!ModelLoaded(487)) { RequestModel(487, 2); LoadAllRequestedModels(false); }
            float pos[3] = { 170.0f, -1830.0f, 4.0f };   // plage
            PlacePuppet(ped, pos, 0.0f);
            heli = ((void *(__cdecl *)(int, float, float, float, bool))0x431F80)(487, 175.0f, -1830.0f, 5.0f, false);
            if (heli) WarpPuppetIn(ped, heli, 0);
            Log("autotest : Maverick %s", heli ? "pret" : "absent");
        } else if (step == 4 && t > 47000) {
            step = 5;
            if (heli) Log("autotest : rotor du Maverick %.3f apres 10 s", *(float *)((uint8_t *)heli + 0x84C));
        }
    }
}

static void CoopFrameInner(bool inGameLoop);
void CoopFrame(bool inGameLoop)
{
    CoopFrameInner(inGameLoop);
    NetFlush();   // (messages regroupes de l'image : net.cpp)
}

static void CoopFrameInner(bool inGameLoop)
{
    static bool netTried;
    if (g_cfg.netAuto && !netTried) { netTried = true; NetStart(); }
    CameraFrame();   // (vue F6 et mods : en solo aussi)
    ModsFrame();
    PanelFrame();    // (menu F10 : en solo aussi)
    GfxFrame();      // (distance d'affichage, population)
    PrefsFrame();    // (souris inversee ou non)
    PanelTest();
    if (!NetRunning()) return;
    SaveSyncFrame();
    {   // arrivees et departs des joueurs (message a l'ecran)
        static bool was[MAX_PLAYERS];
        static const bool fr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            bool is = g_players[i].connected && i != g_localId;
            if (is == was[i]) continue;
            was[i] = is;
            if (is && !g_players[i].state.name[0]) { was[i] = false; continue; }   // nom pas encore connu
            char msg[96];
            const char *name = g_players[i].state.name[0] ? g_players[i].state.name : "?";
            if (is) wsprintfA(msg, fr ? "%s a rejoint la partie" : "%s joined the game", name);
            else wsprintfA(msg, fr ? "%s a quitte la partie" : "%s left the game", name);
            HudToast(msg, 5000);
        }
    }
    g_onClothes = OnClothes;
    g_onWorld = OnWorld;
    NetPoll();
    SendLocalState();
    SendLocalClothes();
    MirrorMenuFrame();
    if (!inGameLoop) return;
    if (!InGame() || *(uint8_t *)0xB5F851) g_calmSince = GetTickCount();
    // Menu Pause ouvert par un joueur : tant qu'un autre joueur est connecte, le monde continue (CTimer::m_UserPause).
    bool others = false;
    for (int i = 0; i < MAX_PLAYERS; i++) others |= i != g_localId && g_players[i].connected;
    if (others) *(uint8_t *)0xB7CB49 = 0;
    VehiclesFrame();
    EntitiesFrame();
    MirrorFrame();
    ConditionsFrame();
    PopulationFrame();
    PoliceFrame();
    NpcFrame();
    PassengerFrame();
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (i != g_localId) { UpdatePuppet(i); HudUpdateBlip(i, PuppetOf(i)); }
    SyncWorld();
    Autotest();

    static uint32_t lastLog;
    if (g_cfg.logScripts && GetTickCount() - lastLog > 3000 && InGame()) {
        lastLog = GetTickCount();
        void *ped = FindPlayerPed();
        float *p = EntityPos(ped);
        Log("joueur : %.1f %.1f %.1f cap %.2f deplacement %d etat %d zone %d vehicule %p commandes %d", p[0], p[1], p[2],
            Field<float>(ped, PED_ROTATION), Field<int>(ped, PED_MOVESTATE), Field<int>(ped, PED_STATE), EntityArea(ped),
            Field<void *>(ped, 0x58C), *(int16_t *)(0xB73458 + 0x10E));
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (g_puppets[i].ped && PedFromRef(g_puppets[i].ref) == g_puppets[i].ped) {
                float *q = EntityPos(g_puppets[i].ped);
                Log("  pantin %d : %.1f %.1f %.1f deplacement %d (recu %.1f %.1f %.1f)", i, q[0], q[1], q[2],
                    Field<int>(g_puppets[i].ped, PED_MOVESTATE), g_players[i].state.pos[0], g_players[i].state.pos[1], g_players[i].state.pos[2]);
            }
    }
}

void OnFrame()
{
}
