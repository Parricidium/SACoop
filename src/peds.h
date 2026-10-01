// Personnages pilotes par le reseau (coop.cpp) : pantins des joueurs, copies des personnages de mission.
#pragma once
#include <stdint.h>

void SetHeading(void *ped, float h);
void PlacePuppet(void *ped, const float *pos, float heading);   // pose d'un coup (retire puis remet dans le monde)
void WarpPuppetIn(void *ped, void *veh, int seat);              // seat 0 : volant, 1..8 : passager
void WarpPuppetOut(void *ped, const float *pos);
bool FollowOnFoot(void *ped, const float *pos, const float *speed, float heading, int remoteMove, uint32_t ageMs, int &moveState);
void *PuppetOf(int id);                                         // pantin du joueur id (nullptr : aucun / soi)
int PuppetIndex(void *ped);                                     // joueur dont c'est le pantin, sinon -1
