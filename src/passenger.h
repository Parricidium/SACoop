// Passager (passenger.cpp).
#pragma once
#include <windows.h>

bool PassengerWindowMessage(UINT msg, WPARAM wp);   // window.cpp : touche G
void PassengerFrame();                              // chaque tour de la boucle en partie
void PassengerRequest();                            // autotest : comme un appui sur G
