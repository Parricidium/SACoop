// Personnages de mission partages (entities.cpp).
#pragma once
#include <stdint.h>

void InstallEntities();
void EntitiesFrame();                       // chaque tour de la boucle en partie (coop.cpp)
void EntitiesReset();                       // invite : retire toutes les copies
uint32_t MissionPedId(void *ped);           // hote : id reseau d'un personnage de mission (0 : ce n'en est pas un)
bool IsMissionCopy(void *ped);              // invite : copie d'un personnage de mission de l'hote ?
void *MissionCopyById(uint32_t id);
void *AnyMissionCopy();                     // autotest
void SendMissionPedHit(void *copy, int weapon, int bodyPart, float damage);
bool WorldCalm();                           // coop.cpp : en partie, sans cinematique depuis 5 s
