// Grand ecran (widescreen.cpp).
#pragma once

void InstallWidescreen();
void InstallResolution();
void WidescreenFrame();     // chaque image (window.cpp) : menus resserres   // mode video a la taille de l'ecran / de la fenetre
float HudAspectFactor();    // 1 en 4:3, plus petit sur ecran large (largeur des textes du HUD)
float MenuBarWidth();       // largeur des bandes noires laterales pendant un menu resserre (0 sinon)
