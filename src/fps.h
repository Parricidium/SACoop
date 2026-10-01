// Jeu a plus de 30 images/s : corrections de cadence (portees de Framerate Vigilante) et plafonds (fps.cpp).
#pragma once

void InstallFps();
void FpsFrame();        // a chaque image (boucle du jeu)
int FpsCurrentCap();    // images/s a tenir maintenant (window.cpp, limiteur)
