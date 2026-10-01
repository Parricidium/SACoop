// Scripts SCM (script.cpp).
#pragma once

void InstallScripts();
bool ScriptIsMission(void *script);
const char *ScriptName(void *script);
bool IsStoryMission(int mission);   // main.scm 1.0 : 11 INTRO1 ... 112 FINALEC
