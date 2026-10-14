// Population partagee (population.cpp).
#pragma once

void InstallPopulation();
void PopulationFrame();                               // chaque tour de la boucle en partie
void PopulationHostDensity(bool car, float value);   // invite : densite reglee par la mission de l'hote (mirror.cpp)
bool PopulationShared();                              // invite : en population partagee (celle de l'hote) ?
bool NearSharedGuest(const float *pos, float radius); // hote : un invite partage est-il a moins de radius ?
bool NearSharedGuestAnywhere(const float *pos, float radius);   // idem, meme hors de la zone de l'hote
int PopulationClearArea(const float *centre, const float *half, bool round, bool peds, bool cars);   // invite : la zone nettoyee par l'hote (mirror.cpp) ; nombre retire
