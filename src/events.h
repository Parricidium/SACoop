// Evenements du monde partages : pickups, tags (events.cpp).
#pragma once
#include <stdint.h>

void InstallEvents();
void EventsFrame();                                         // en partie, chaque image
bool EventsReliable(int from, const uint8_t *d, int len);  // message du flux fiable traite ici ?
