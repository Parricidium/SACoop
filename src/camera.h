// Vue a la premiere personne (camera.cpp).
#pragma once
#include <windows.h>

void InstallCamera();
void CameraFrame();                              // chaque tour de la boucle (coop.cpp)
bool CameraWindowMessage(UINT msg, WPARAM wp);   // touche de la vue (F6)
bool FirstPersonActive();
