// Conditions de mission elargies aux invites, marqueurs au sol (conditions.cpp).
#pragma once

void InstallConditions();
void ConditionBefore(void *script, int op);   // hote : avant chaque commande (script.cpp)
void ConditionAfter();
void ConditionsFrame();                       // invite : marqueurs, chaque tour de la boucle en partie
void TeleportPed(void *ped, const float *pos, float heading, int area, const char *why);   // pose le joueur local en entier (interieur, decor, vehicule) ; voir conditions.cpp
