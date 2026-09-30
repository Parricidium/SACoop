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

void InstallPuppetRender()
{
    o_CivRender = (PedRender_t)PatchPointer((void **)(0x86C0A8 + 18 * 4), (void *)h_CivRender);
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
    }
    NetSendState(s);
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

static bool IsPuppet(void *ped)
{
    for (auto &p : g_puppets) if (p.ped == ped) return true;
    return false;
}

static void DestroyPuppet(int id)
{
    Puppet &p = g_puppets[id];
    if (!p.ped) return;
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

    // Position visee : un peu en avant selon la vitesse (le joueur a continue d'avancer depuis l'envoi).
    float lead = (GetTickCount() - s.time) / 1000.0f;
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
            float pos[3] = { 2232.0f, -1262.3f, 23.9f };   // ruelle de Ganton, 10 m derriere le depart de l'hote
            PlacePuppet(ped, pos, -1.5708f);
            Log("autotest : place derriere l'hote en %.1f %.1f %.1f", pos[0], pos[1], pos[2]);
        }
        ((void(__thiscall *)(void *))0x50BD40)((void *)0xB6F028);   // CCamera::SetCameraDirectlyBehindForFollowPed_CamOnAString
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
    NetPoll();
    SendLocalState();
    SendLocalClothes();
    if (!inGameLoop) return;
    if (!InGame() || *(uint8_t *)0xB5F851) g_calmSince = GetTickCount();
    // Menu Pause ouvert par un joueur : tant qu'un autre joueur est connecte, le monde continue (CTimer::m_UserPause).
    bool others = false;
    for (int i = 0; i < MAX_PLAYERS; i++) others |= i != g_localId && g_players[i].connected;
    if (others) *(uint8_t *)0xB7CB49 = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (i != g_localId) UpdatePuppet(i);
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
