// Population partagee (population.cpp).
#pragma once

void InstallPopulation();
void PopulationFrame();                               // chaque tour de la boucle en partie
bool PopulationShared();                              // invite : en population partagee (celle de l'hote) ?
bool NearSharedGuest(const float *pos, float radius); // hote : un invite partage est-il a moins de radius ?
