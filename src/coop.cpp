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
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "vehicles.h"
#include "hud.h"
#include "combat.h"
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
}
static uint32_t g_calmSince;   // depuis quand on est en partie sans cinematique (creation des pantins)

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
        if (void *veh = PedVehicle(ped)) {
            bool driver = Field<void *>(veh, VEH_DRIVER) == ped;
            s.seat = 0;
            if (!driver)
                for (int i = 0; i < 8; i++) if (Field<void *>(veh, VEH_PASSENGERS + i * 4) == ped) s.seat = (uint8_t)(i + 1);
            if (driver || s.seat) s.vehicleId = LocalVehicleId(veh, driver);
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
static void SetHeading(void *ped, float h)
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

static void PlacePuppet(void *ped, const float *pos, float heading)
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
static void WarpPuppetIn(void *ped, void *veh, int seat)
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
static void WarpPuppetOut(void *ped, const float *pos)
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
    void *ped = NewCivilianPed(4, model);   // PEDTYPE_CIVMALE
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

    // En vehicule : le pantin est mis a sa place dans la copie ; il en sort quand le joueur est a pied.
    void *inVeh = PedVehicle(ped);
    if (s.vehicleId) {
        void *veh = NetVehicleById(s.vehicleId);
        if (!veh) return;   // copie pas encore creee (modele en chargement)
        bool placed = inVeh == veh && (s.seat == 0 ? Field<void *>(veh, VEH_DRIVER) == ped : Field<void *>(veh, VEH_PASSENGERS + (s.seat - 1) * 4) == ped);
        if (!placed) {
            if (inVeh) WarpPuppetOut(ped, s.pos);
            WarpPuppetIn(ped, veh, s.seat);
            p.moveState = 0;
            Log("pantin du joueur %d mis dans le vehicule %08X (place %d)", id, s.vehicleId, s.seat);
        }
        return;
    }
    if (inVeh) {
        WarpPuppetOut(ped, s.pos);
        p.moveState = 0;
        Log("pantin du joueur %d sorti du vehicule", id);
        return;
    }

    // Position visee : un peu en avant selon la vitesse (le joueur a continue d'avancer depuis l'envoi).
    float lead = (GetTickCount() - np.lastStateAt) / 1000.0f;   // (horloge locale : celle de l'autre PC n'est pas la meme)
    if (lead < 0) lead = 0;
    if (lead > 0.3f) lead = 0.3f;
    float target[3];
    for (int k = 0; k < 3; k++) target[k] = s.pos[k] + s.speed[k] * 50.0f * lead;   // vitesse du jeu : par 1/50 s
    float *pos = EntityPos(ped);
    float dx = target[0] - pos[0], dy = target[1] - pos[1], dz = target[2] - pos[2];
    float d2 = dx * dx + dy * dy, dist = sqrtf(d2);
    if (dist > 4.0f || fabsf(dz) > 3.0f) {   // trop loin : replace d'un coup
        PlacePuppet(ped, s.pos, s.heading);
        SetPrimaryTask(ped, nullptr, 3);
        p.moveState = 0;
        return;
    }
    int move = PuppetMove(s.moveState);
    if (move == MOVE_STILL && dist < 0.6f) {
        if (p.moveState != MOVE_STILL) { SetPrimaryTask(ped, nullptr, 3); p.moveState = MOVE_STILL; }
        SetHeading(ped, s.heading);
        return;
    }
    if (move == MOVE_STILL) move = MOVE_WALK;   // petit rattrapage a pied
    // Cible : devant le joueur, pour que le pantin ne s'arrete pas entre deux messages.
    CVector goal = { target[0] + dx * 0.0f, target[1], target[2] };
    if (dist > 0.01f) { goal.x = target[0] + dx / dist * 1.0f; goal.y = target[1] + dy / dist * 1.0f; }
    void **tasks = PrimaryTasks(ped);
    void *cur = tasks[3];
    if (cur && *(uintptr_t *)cur == VT_TaskSimpleGoToPoint && p.moveState == move) {
        CVector *t = (CVector *)((uint8_t *)cur + 0xC);
        *t = goal;
    } else if (void *task = NewGoToPoint(move, goal, 0.5f)) {
        SetPrimaryTask(ped, task, 3);
        p.moveState = move;
        p.lastTask = GetTickCount();
    }
}

// --- Autotest (instances de test) : le joueur local court tout droit par intervalles ---
// Pas de tache sur le joueur (un CTaskSimpleGoToPoint donne au CPlayerPed faisait planter le jeu) : on pousse le
// manche gauche de la manette 0 comme le ferait une vraie manette. CPad[0] en 0xB73458 ; PCTempJoyState (+0xA8) est
// fusionne dans NewState a chaque CPad::Update ; LeftStickY (+2) = -128 : en avant.
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
            if (void *veh = NetVehicleById(hostCar)) {
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
    // "voiture" (hote) : un Greenwood (492) pose a 5 m, l'hote mis au volant, puis avance 2 s / s'arrete 2 s / recule 2 s
    // (Croix : accelerer, Carre : freiner et reculer, sur la manette 0).
    if (_stricmp(g_cfg.autotest, "voiture") == 0) {
        static void *car;
        static bool done;
        void *ped = FindPlayerPed();
        if (!done) {
            done = true;
            float pos[3] = { 2242.0f, -1262.3f, 23.9f };
            PlacePuppet(ped, pos, -1.5708f);
            if (!ModelLoaded(492)) { RequestModel(492, 2); LoadAllRequestedModels(false); }
            car = ((void *(__cdecl *)(int, float, float, float, bool))0x431F80)(492, 2247.0f, -1262.3f, 24.2f, false);
            if (car) {
                if (uint8_t *m = *(uint8_t **)((uint8_t *)car + 0x14)) {   // tournee vers +x
                    float *r = (float *)m, *f = (float *)(m + 0x10);
                    r[0] = 0; r[1] = -1; r[2] = 0; f[0] = 1; f[1] = 0; f[2] = 0;
                }
                WarpPuppetIn(ped, car, 0);
                Log("autotest : au volant d'un Greenwood");
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
        int phase = c < 1000 ? 0 : c < 2400 ? 1 : 2;
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
}

void CoopFrame(bool inGameLoop)
{
    static bool netTried;
    if (g_cfg.netAuto && !netTried) { netTried = true; NetStart(); }
    if (!NetRunning()) return;
    g_onClothes = OnClothes;
    g_onWorld = OnWorld;
    NetPoll();
    SendLocalState();
    SendLocalClothes();
    if (!inGameLoop) return;
    if (!InGame() || *(uint8_t *)0xB5F851) g_calmSince = GetTickCount();
    // Menu Pause ouvert par un joueur : tant qu'un autre joueur est connecte, le monde continue (CTimer::m_UserPause).
    bool others = false;
    for (int i = 0; i < MAX_PLAYERS; i++) others |= i != g_localId && g_players[i].connected;
    if (others) *(uint8_t *)0xB7CB49 = 0;
    VehiclesFrame();
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
