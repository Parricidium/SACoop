// Scripts SCM. CRunningScript::Process (0x469F00) lit chaque commande et l'envoie au gestionnaire de sa centaine
// (table 0x8A6168 : 27 entrees, __thiscall(script, opcode) -> 0 = continuer, 1 = rendre la main). On remplace chaque
// entree par une enveloppe : les commandes peuvent etre observees, et certaines consommees sans effet.
//  - Invite : START_MISSION (0417) d'une mission de l'histoire est consomme sans effet : elles ne tournent que chez
//    l'hote. Les autres restent locales : 0 INITIAL et 1 INITIL2 (mise en place du monde), 3-10 jeux (billard,
//    paris, lowrider...), 113+ activites (gymnases, petits boulots, taxi, ambulance, courses, achats...).
//    Numeros de main.scm 1.0 : 2 INTRO, 11 INTRO1 ... 112 FINALEC.
//  - Autotest "mission" (hote) : l'INTRO est sautee, pour laisser la place a la mission de test.
// CRunningScript : +0x08 nom[8], +0x14 IP, +0xDC mission. CollectParameters 0x464080(this, n) -> ScriptParams 0xA43C78.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "script.h"
#include "mirror.h"
#include "conditions.h"
#include "npc.h"
#include "game.h"
#include "peds.h"
#include "vehicles.h"
#include <string.h>

using namespace game;

static ScriptHandler_t g_orig[27];

static void CollectParameters(void *script, int n) { ((void(__thiscall *)(void *, short))0x464080)(script, (short)n); }
static int *ScriptParams() { return (int *)0xA43C78; }

bool ScriptIsMission(void *script) { return *((uint8_t *)script + 0xDC) != 0; }
const char *ScriptName(void *script) { return (const char *)script + 8; }

ScriptHandler_t ScriptOriginalHandler(int index) { return g_orig[index]; }

// Script de mission en cours (liste CTheScripts::pActiveScripts 0xA8B42C, +0 suivant) : son nom, ou nullptr.
const char *RunningMissionScript()
{
    for (uint8_t *s = *(uint8_t **)0xA8B42C; s; s = *(uint8_t **)s)
        if (s[0xDC]) return (const char *)s + 8;
    return nullptr;
}

bool IsStoryMission(int mission) { return mission == 2 || (mission >= 11 && mission <= 112); }

static char Dispatch(int index, void *script, int op)
{
    if (op == 0x417) {   // START_MISSION
        uint8_t *ip = *(uint8_t **)((uint8_t *)script + 0x14);
        int mission = ip[0] == 4 ? (int8_t)ip[1] : ip[0] == 5 ? *(int16_t *)(ip + 1) : ip[0] == 1 ? *(int32_t *)(ip + 1) : -1;
        bool testSkip = g_cfg.host && mission == 2 && (_stricmp(g_cfg.autotest, "mission") == 0 || _stricmp(g_cfg.autotest, "sauve") == 0);
        if (NetRunning() && (testSkip || (!g_cfg.host && IsStoryMission(mission)))) {
            CollectParameters(script, 1);
            static int lastBlocked = -1;
            if (lastBlocked != mission) { lastBlocked = mission; Log("script %.8s : mission %d non lancee (invite : l'histoire tourne chez l'hote)", ScriptName(script), mission); }
            // L'intro remet la population (le script de depart la met a 0) : sans elle, l'invite n'avait plus ni
            // passants ni circulation de son cote (1er test reel, GG loin de l'hote).
            if (mission == 2) { *(float *)0x8D2530 = 1.0f; *(float *)0x8A5B20 = 1.0f; Log("script : population remise (intro de l'hote)"); }
            return 0;
        }
        Log("script %.8s : mission %d lancee", ScriptName(script), mission);
        if (g_cfg.host && IsStoryMission(mission)) MirrorMissionStart();
    }
    // STORE_CAR_CHAR_IS_IN (00D9 / 03C0) sur un personnage a pied : le jeu lit +0x4A4 d'un vehicule nul (plantage
    // 0x46952A, test reel du 02/10 : l'hote tombe du velo au skatepark pendant la mission des velos). La condition
    // "dans un vehicule" (00DF) est elargie aux invites (conditions.cpp) : vraie grace au velo de l'invite, et le script
    // demandait ensuite le vehicule de l'hote. Le resultat est alors le vehicule de l'invite (copie chez l'hote), sinon -1.
    if (op == 0x00D9 || op == 0x03C0) {
        int vals[1];
        void *ped = ScriptReadValues(script, *(uint8_t **)((uint8_t *)script + 0x14), 1, vals) ? PedFromRef(vals[0]) : nullptr;
        if (ped && !PedVehicle(ped)) {
            CollectParameters(script, 1);
            int handle = -1;
            for (int i = 1; i < MAX_PLAYERS && handle == -1; i++)
                if (void *pup = PuppetOf(i)) if (void *veh = PedVehicle(pup)) handle = VehicleRef(veh);
            ScriptStoreResult(script, handle);
            static int said;
            if (said < 10) { said++; Log("script %.8s : %04X sur un personnage a pied -> vehicule %08X (invite) au lieu d'un plantage", ScriptName(script), op, handle); }
            return 0;
        }
    }
    NpcScriptCommand(script, op);
    MirrorBefore(script, op);
    ConditionBefore(script, op);
    char r = g_orig[index](script, op);
    ConditionAfter();
    MirrorAfter(script, op);
    return r;
}

template <int N> static char __fastcall Handler(void *script, void *, int op) { return Dispatch(N, script, op); }
template <int N> static void InstallHandler()
{
    g_orig[N] = (ScriptHandler_t)PatchPointer((void **)(0x8A6168 + N * 4), (void *)&Handler<N>);
    InstallHandler<N + 1>();
}
template <> void InstallHandler<27>() {}

void InstallScripts()
{
    InstallHandler<0>();
    Log("scripts : gestionnaires de commandes enveloppes");
}
