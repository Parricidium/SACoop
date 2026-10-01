// Animations d'action et visee recopiees sur les pantins et les copies de PNJ (anims.cpp).
#pragma once
#include "net.h"

struct AnimMirror { int16_t ids[ANIMS_MAX]; void *assoc[ANIMS_MAX]; int count; };   // animations creees par nous sur cette copie

int AnimsCollect(void *ped, NetAnim *out, int max);         // animations d'action du personnage (les plus fortes)
void AnimsApply(void *ped, const NetAnim *in, int n, AnimMirror &m);
void AimMirror(void *ped, bool want, const float *aim);     // visee rejouee (tache secondaire, mise a jour a chaque image)
bool AimOf(void *ped, float *aim);                          // le personnage vise-t-il ? (point vise)
