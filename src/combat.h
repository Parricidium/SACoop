// Armes, tirs et degats entre joueurs (combat.cpp).
#pragma once
#include "net.h"

void InstallCombat();
void CombatFillState(MsgState &s);                                    // tirs et point vise du joueur local
void CombatPuppetCreated(int id);                                     // pantin (re)cree : son arme sera redonnee
void CombatUpdatePuppet(int id, void *ped, const MsgState &s);        // arme en main et tirs rejoues
void CombatKillPuppet(void *ped, int weapon);
void CombatNpcShots(void *ped, uint8_t &shots, float *aim);   // hote : tirs d'un PNJ (envoyes avec lui)
void CombatReplayCopyShots(void *ped, int weaponId, uint8_t shots, const float *aim, uint8_t &last, bool &known);   // invite                          // le joueur du pantin est mort chez lui
void ApplyPedHit(void *victim, void *damager, int weapon, int damage, int bodyPart);   // coup comme une balle
void EnsurePedWeapon(void *ped, int type, int &current);
void CombatTestShot(void *ped, const float *aim);                     // autotest : tir de l'arme en main vers un point
int PuppetIndex(void *ped);                                           // coop.cpp : joueur dont c'est le pantin, sinon -1
