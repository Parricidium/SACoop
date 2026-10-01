// Armes, tirs et degats entre joueurs (combat.cpp).
#pragma once
#include "net.h"

void InstallCombat();
void CombatFillState(MsgState &s);                                    // tirs et point vise du joueur local
void CombatPuppetCreated(int id);                                     // pantin (re)cree : son arme sera redonnee
void CombatUpdatePuppet(int id, void *ped, const MsgState &s);        // arme en main et tirs rejoues
void CombatKillPuppet(void *ped, int weapon);                          // le joueur du pantin est mort chez lui
void CombatTestShot(void *ped, const float *aim);                     // autotest : tir de l'arme en main vers un point
int PuppetIndex(void *ped);                                           // coop.cpp : joueur dont c'est le pantin, sinon -1
