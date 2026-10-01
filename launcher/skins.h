// Tenues proposees aux joueurs (onglet TENUE du lanceur, onglet TENUE du menu F10 du jeu) : CJ (avec ses vetements)
// puis tous les pietons de data\peds.ide (numeros 1 a 288), dans cet ordre : gangs, forces de l'ordre et secours, puis
// les autres. Pas les personnages de l'histoire (Sweet, Big Smoke, Ryder...) : San Andreas n'a pas de numero a eux,
// il les charge par leur nom dans les dix cases speciales 290-299, que les missions utilisent aussi.
// Nom affiche : celui de la table ci-dessous, sinon le nom du modele (BFORI, WMYCR...). Le nom sert aussi a designer la
// tenue dans le salon du lanceur : il ne depend pas de la langue.
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <ctype.h>
#include <stdio.h>

struct SkinNamed { int id; const char *name; };
static const SkinNamed kSkinNamed[] = {
    { 0, "CJ" },
    { 105, "Grove 1" }, { 106, "Grove 2" }, { 107, "Grove 3" },
    { 102, "Ballas 1" }, { 103, "Ballas 2" }, { 104, "Ballas 3" },
    { 108, "Vagos 1" }, { 109, "Vagos 2" }, { 110, "Vagos 3" },
    { 114, "Aztecas 1" }, { 115, "Aztecas 2" }, { 116, "Aztecas 3" },
    { 173, "Rifa 1" }, { 174, "Rifa 2" }, { 175, "Rifa 3" },
    { 117, "Triads 1" }, { 118, "Triads 2" }, { 120, "Triads boss" },
    { 121, "Da Nang 1" }, { 122, "Da Nang 2" }, { 123, "Da Nang 3" },
    { 124, "Mafia 1" }, { 125, "Mafia 2" }, { 126, "Mafia 3" }, { 127, "Mafia 4" },
    { 111, "Russian mafia 1" }, { 112, "Russian mafia 2" }, { 113, "Russian mafia boss" },
    { 247, "Biker 1" }, { 248, "Biker 2" },
    { 280, "Police LS" }, { 281, "Police SF" }, { 282, "Police LV" }, { 284, "Police bike" }, { 283, "Sheriff" },
    { 288, "Desert sheriff" }, { 285, "SWAT" }, { 286, "FBI" }, { 287, "Army" },
    { 274, "Medic LS" }, { 275, "Medic LV" }, { 276, "Medic SF" },
    { 277, "Firefighter LS" }, { 278, "Firefighter LV" }, { 279, "Firefighter SF" },
};

// Rang dans la liste (table d'abord, dans son ordre ; puis les autres par numero).
inline int SkinRank(int id)
{
    for (int i = 0; i < (int)(sizeof(kSkinNamed) / sizeof(kSkinNamed[0])); i++) if (kSkinNamed[i].id == id) return i;
    return 1000 + id;
}

// Nom affiche ; model = nom du modele (peds.ide), pour les pietons hors table.
inline std::string SkinLabel(int id, const std::string &model)
{
    for (auto &s : kSkinNamed) if (s.id == id) return s.name;
    std::string up = model;
    for (char &c : up) c = (char)toupper((unsigned char)c);
    return up.empty() ? std::to_string(id) : up;
}

// Tri d'une liste de numeros (CJ = 0 en tete).
inline void SkinSort(std::vector<int> &ids)
{
    std::sort(ids.begin(), ids.end(), [](int a, int b) { return SkinRank(a) < SkinRank(b); });
}
