// PNJ de l'hote face aux invites (npc.cpp).
#pragma once

void NpcFrame();                              // chaque tour de la boucle en partie (coop.cpp)
void NpcScriptCommand(void *script, int op);  // hote : chaque commande de script, avant son execution
void NpcHunterNoted(int pedRef);              // un PNJ vient d'etre lance sur l'hote
