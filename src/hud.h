// Affichage coop (hud.cpp) : pseudos au-dessus des autres joueurs, points radar.
#pragma once

void InstallHud(void *(*puppetOf)(int));   // puppetOf(joueur) : son pantin s'il existe, sinon nullptr
void HudUpdateBlip(int player, void *puppet);
void HudPrompt(const char *text, unsigned ms);   // question at the top of the screen (vehicle offer)
void HudPromptClear();
void HudToast(const char *text, unsigned ms);   // message en bas de l'ecran (texte ASCII)   // a chaque image : pose, deplace ou retire le point radar du joueur
