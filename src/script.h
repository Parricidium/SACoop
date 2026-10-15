// Scripts SCM (script.cpp).
#pragma once
bool GuestOwnCall();   // guest: his own script is making a phone call (released when the host took his) (script.cpp)

typedef char(__thiscall *ScriptHandler_t)(void *script, int op);
void InstallScripts();
ScriptHandler_t ScriptOriginalHandler(int index);   // gestionnaire d'origine d'une centaine de commandes
bool ScriptIsMission(void *script);
const char *ScriptName(void *script);
bool IsStoryMission(int mission);
const char *RunningMissionScript();   // nom du script de mission en cours, ou nullptr   // main.scm 1.0 : 2 INTRO, 11 INTRO1 ... 112 FINALEC
extern unsigned char g_driveByScriptValue;   // the player's drive-by permission as a script last set it (script.cpp)
extern unsigned int g_driveByScriptSeq;      // ... and how many times a script changed it
