// Sauvegarde partagee (savesync.cpp).
#pragma once
#include <stdint.h>

void SaveSyncFrame();                                   // chaque image (hote : surveillance des emplacements)
bool SaveSyncReliable(const uint8_t *data, int len);    // message fiable de sauvegarde ? (traite s'il l'est)
