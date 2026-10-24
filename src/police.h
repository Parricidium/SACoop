// Police : recherche partagee et police de l'hote sur les invites (police.cpp).
#pragma once

void PoliceClearAll();      // menu: clear the wanted level of everybody
bool PoliceBribe(int from, const short *pos8);   // events.cpp: a police bribe taken by player from (pos in 1/8 m); host takes a star off (0.54.96)
int WantedLevel();         // niveau de recherche du joueur local (0-6)
void PoliceFrame();        // chaque tour de la boucle en partie (coop.cpp)
bool HostPoliceOnGuests(); // invite : la police de l'hote s'occupe de nous (pas de police locale en population partagee)
bool PoliceReliable(const unsigned char *data, int len);   // mirror.cpp : message fiable de la police ?
