// Miroir des missions (mirror.cpp).
#pragma once

void MirrorBefore(void *script, int op);   // hote : avant chaque commande (capture des parametres)
void MirrorAfter(void *script, int op);    // hote : apres (sorties, envoi)
void MirrorFrame();
void MirrorMissionStart();                 // hote : une mission de l'histoire demarre (regroupement)
bool ScriptReadValues(void *script, unsigned char *ip, int n, int *vals);
void RunScriptCommandTyped(int op, int nargs, const char *types, const int *args);
void RunScriptCommand(int op, int nargs, const int *args);   // commande a parametres entiers (script fantome)                        // invite : rejeu de la file, chaque tour de la boucle en partie
