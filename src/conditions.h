// Conditions de mission elargies aux invites, marqueurs au sol (conditions.cpp).
#pragma once

void InstallConditions();
void ConditionBefore(void *script, int op);   // hote : avant chaque commande (script.cpp)
void ConditionAfter();
void ConditionsFrame();                       // invite : marqueurs, chaque tour de la boucle en partie
