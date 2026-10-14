// Evenements du monde partages : pickups, tags (events.cpp).
#pragma once
#include <stdint.h>

void InstallEvents();
void EventsFrame();                                         // en partie, chaque image
bool EventsReliable(int from, const uint8_t *d, int len);  // message du flux fiable traite ici ?

// Armes perdues a la mort (events.cpp) : posees autour du corps, visibles du seul joueur mort (jamais envoyees aux autres).
void DeathDropFrame();                                      // chaque image (en solo aussi) : retient les armes, detecte la mort
bool DeathSpot(float pos[3], int *area);                       // last death: position and interior
bool GoToDeathSpot();                                       // menu F10 "Ou je suis mort" : pose le joueur sur place
