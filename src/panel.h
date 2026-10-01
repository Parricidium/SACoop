// Panneau coop F10 (panel.cpp).
#pragma once
#include <windows.h>

bool PanelOpen();
bool PanelWindowMessage(UINT msg, WPARAM wp);   // window.cpp : vrai = touche pour le panneau
void PanelDraw();                              // hud.cpp, a chaque image
bool GoToPlayer(int id);                       // pres du joueur id (ou passager de sa voiture)
