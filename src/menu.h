// Menu COOP dans le menu du jeu (menu.cpp).
#pragma once
#include <windows.h>

void InstallMenu();
void MenuFrame();                              // chaque image (window.cpp)
bool MenuWindowMessage(UINT msg, WPARAM wp);   // window.cpp : saisie de l'adresse / du pseudo
const char *GxtLookup(const char *key);              // the text the game has for a GXT key (16-bit characters), nullptr before the first lookup
