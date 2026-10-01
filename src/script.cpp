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
#include <string.h>

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
