// Boucle coop : reseau, etat du joueur local, et doubles des autres joueurs (pantins).
// Pantin : un CCivilianPed marque "personnage de mission" pour que la population ne le retire pas. Modele : un pieton
// choisi par le joueur (Tenue, fam2 par defaut). Le modele 0 de CJ n'a pas de maillage a lui (celui du joueur est
// construit a partir de ses vetements, sur son personnage) : un pantin avec le modele 0 etait invisible. Il se deplace par une tache
// CTaskSimpleGoToPoint vers la position recue (marche / course / sprint selon le joueur) : les animations sont
// celles du jeu. S'il s'ecarte trop, il est replace d'un coup.
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
};
static Puppet g_puppets[MAX_PLAYERS];
static uint32_t g_stateSeq;

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

static void CreatePuppet(int id, const MsgState &s)
{
    int model = s.skin >= 1 && s.skin <= 299 ? s.skin : 106;
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
    g_puppets[id] = { ped, PedRef(ped), 0, 0 };
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
    // Pas pendant une cinematique : le jeu nettoie la zone a la fin et supprimerait le pantin.
    if (!p.ped && *(uint8_t *)0xB5F851) return;
    if (!p.ped) { CreatePuppet(id, s); if (!p.ped) return; }
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
    if (_stricmp(g_cfg.autotest, "marche") == 0) {
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
    NetPoll();
    SendLocalState();
    if (!inGameLoop) return;
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
