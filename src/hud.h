// Affichage coop (hud.cpp) : pseudos au-dessus des autres joueurs, points radar.
#pragma once

void InstallHud(void *(*puppetOf)(int));   // puppetOf(joueur) : son pantin s'il existe, sinon nullptr
void HudUpdateBlip(int player, void *puppet);   // a chaque image : pose, deplace ou retire le point radar du joueur
