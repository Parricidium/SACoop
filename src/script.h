// Scripts SCM (script.cpp).
#pragma once

typedef char(__thiscall *ScriptHandler_t)(void *script, int op);
void InstallScripts();
ScriptHandler_t ScriptOriginalHandler(int index);   // gestionnaire d'origine d'une centaine de commandes
bool ScriptIsMission(void *script);
const char *ScriptName(void *script);
bool IsStoryMission(int mission);
const char *RunningMissionScript();   // nom du script de mission en cours, ou nullptr   // main.scm 1.0 : 2 INTRO, 11 INTRO1 ... 112 FINALEC
