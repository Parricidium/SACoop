// Interface en jeu : menu F10, aide F1, bienvenue (panel.cpp).
#pragma once
#include <windows.h>

bool PanelOpen();                                 // menu ou aide ouverts
bool PanelCapturesKeys();                         // dllmain.cpp : le clavier DirectInput est cache au jeu
void PanelMouseInput(int dx, int dy, int dz, bool lmb);   // dllmain.cpp : souris DirectInput (panneau ouvert)
bool PanelWindowMessage(UINT msg, WPARAM wp);     // window.cpp : vrai = touche pour l'interface
void PanelFrame();                                // coop.cpp : actions en file (boucle du jeu)
void PanelRender();                               // render.cpp : dessin, apres l'interface du jeu
void PanelTest();                                 // autotest TestMenuJeu
void PanelDraw();                                 // (ancien panneau, vide)
bool GoToPlayer(int id);                          // pres du joueur id (ou passager de sa voiture)
