// Police : recherche partagee et police de l'hote sur les invites (police.cpp).
#pragma once

int WantedLevel();         // niveau de recherche du joueur local (0-6)
void PoliceFrame();        // chaque tour de la boucle en partie (coop.cpp)
bool HostPoliceOnGuests(); // invite : la police de l'hote s'occupe de nous (pas de police locale en population partagee)
bool PoliceReliable(const unsigned char *data, int len);   // mirror.cpp : message fiable de la police ?
