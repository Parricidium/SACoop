// Reglages graphiques du jeu d'origine, au-dela de ce que son menu permet (comme VCCoop) :
//  - DistanceAffichage (100-250 % ; 300 % retire, plantages chez JD le 01/10) : distance des modeles detailles (CRenderer::ms_lodDistScale 0x8CD800, que le menu
//    du jeu met a son reglage 0,925-1,8 : FrontEndMenuManager 0xBA6748 +0x40) ; plan lointain et brouillard du cycle
//    du temps (CTimeCycle::m_CurrentColours : m_fFarClip 0xB7C4F0, m_fFogStart 0xB7C4F4, recalcules a chaque image puis
//    passes a RwCameraSetFarClipPlane dans Idle, 0x53EA93 et 0x53DCB6) ; memoire de chargement (CStreaming::
//    ms_memoryAvailable 0x8A5A80) agrandie pour suivre.
//  - ZonePopulation (100-200 %) : distance d'apparition des pietons et des voitures (CCamera +0xF4,
//    m_fGenerationDistMultiplier, recopie de +0xF0 dans CCamera::Process en 0x52C9EB) et plafonds de population.
//  - DensitePopulation (50-300 %) : plafonds (CPopulation::MaxNumberOfPedsInUse 0x8D2538 = 25, CCarCtrl::
//    MaxNumberOfCarsInUse 0x8A5B24 = 12) et densites (0x8D2530, 0x8A5B20) quand le script les laisse a 1 ;
//    plafonnes a 90 personnages et 45 voitures.
//  - FiltrageAnisotrope : render.cpp.
#include "util.h"
#include "sacoop.h"
#include "game.h"
#include "gfx.h"
#include <math.h>

static float g_genMul = 1.0f;   // ZonePopulation / 100

// Remplace "mov eax, [esi+0F0h] ; mov [esi+0F4h], eax" (12 octets en 0x52C9EB, CCamera::Process) : la distance
// d'apparition suit la distance des modeles du champ de vision (avant le reglage du menu), multipliee par la zone.
static void __declspec(naked) GenDistStub()
{
    __asm {
        fld dword ptr [esi + 0F0h]
        fmul dword ptr [g_genMul]
        fstp dword ptr [esi + 0F4h]
        ret
    }
}

// Plan lointain : multiplie une fois par calcul du cycle du temps (le jeu en pause ne le recalcule pas : on reconnait
// alors la valeur deja ecrite).
static float g_farWritten = -1, g_fogWritten = -1;
static void ApplyFarClip()
{
    float &farClip = *(float *)0xB7C4F0, &fogStart = *(float *)0xB7C4F4;
    float f = g_cfg.drawDistance / 100.0f;
    if (farClip != g_farWritten) { farClip *= f; if (farClip > 3600.0f) farClip = 3600.0f; g_farWritten = farClip; }
    if (fogStart != g_fogWritten) { fogStart *= f; if (fogStart > farClip * 0.9f) fogStart = farClip * 0.9f; g_fogWritten = fogStart; }
}
static void __cdecl h_BeforeFarClip()
{
    ((void(__cdecl *)())0x734650)();
    ApplyFarClip();
}

// Listes des objets a dessiner (CRenderer::ms_aVisibleEntityPtrs 0xB75898 et ms_aVisibleLodPtrs 0xB748F8, 1000 cases
// chacune, remplies SANS verifier la place : 0x553529 / 0x5534F2) : avec un plan lointain repousse, elles debordent l'une
// sur l'autre puis sur leurs compteurs, et des pans entiers du decor disparaissent. Deplacees dans des listes de 16000
// (toutes les references du code : 4 et 3).
static void *g_visEntities[16000], *g_visLods[16000];
static void RelocateVisibleLists()
{
    static const uintptr_t ent[] = { 0x553529, 0x553944, 0x553A53, 0x553B03 }, lod[] = { 0x5534F5, 0x553923, 0x553CB3 };
    for (uintptr_t a : ent) if (*(uint32_t *)a != 0xB75898) { Log("graphismes : listes visibles inattendues, laissees"); return; }
    for (uintptr_t a : lod) if (*(uint32_t *)a != 0xB748F8) { Log("graphismes : listes visibles inattendues, laissees"); return; }
    uint32_t e = (uint32_t)(uintptr_t)g_visEntities, l = (uint32_t)(uintptr_t)g_visLods;
    for (uintptr_t a : ent) Patch(a, &e, 4);
    for (uintptr_t a : lod) Patch(a, &l, 4);
}

void InstallGfx()
{
    RelocateVisibleLists();
    const uint8_t *p = (const uint8_t *)0x52C9EB;
    static const uint8_t expect[] = { 0x8B, 0x86, 0xF0, 0, 0, 0, 0x89, 0x86, 0xF4, 0, 0, 0 };
    if (!memcmp(p, expect, sizeof(expect))) PatchCall(0x52C9EB, (void *)GenDistStub, 12);
    else Log("graphismes : CCamera::Process inattendu, zone de population d'origine");
    int ok = 0;
    static const uintptr_t calls[] = { 0x53EA8E, 0x53DCB1 };
    for (uintptr_t a : calls) {
        const uint8_t *c = (const uint8_t *)a;
        if (c[0] == 0xE8 && a + 5 + *(const int32_t *)(c + 1) == 0x734650) { PatchCall(a, (void *)h_BeforeFarClip); ok++; }
    }
    if (ok != 2) Log("graphismes : appels avant le plan lointain inattendus (%d/2)", ok);
    Log("graphismes : distance d'affichage %d %%, zone de population %d %%, densite %d %%, anisotrope %d",
        g_cfg.drawDistance, g_cfg.zonePop, g_cfg.popDensity, (int)g_cfg.aniso);
}

// A chaque image (boucle du jeu, en solo aussi).
void GfxFrame()
{
    float f = g_cfg.drawDistance / 100.0f, z = g_cfg.zonePop / 100.0f, d = g_cfg.popDensity / 100.0f;
    g_genMul = z;
    float menu = *(float *)(0xBA6748 + 0x40);   // reglage du menu du jeu (0,925 - 1,8)
    if (!(menu >= 0.5f && menu <= 2.0f)) menu = 1.2f;
    // modeles detailles : moitie de l'allongement (au-dela, les listes d'objets visibles du jeu debordent et des
    // morceaux du decor lointain disparaissent) ; le plan lointain et le brouillard suivent en entier
    float lod = menu * (1.0f + (f - 1.0f) * 0.5f);
    if (lod > 2.6f) lod = 2.6f;
    *(float *)0x8CD800 = lod;
    // memoire de chargement : 96 Mo a 100 %, + 64 Mo par tranche de 50 % (jamais en dessous de ce que le jeu a mis)
    static int logged;
    int mem = (96 + 64 * (g_cfg.drawDistance - 100) / 50) << 20;
    int &avail = *(int *)0x8A5A80;
    if (avail < mem) { avail = mem; if (logged != mem) { logged = mem; Log("graphismes : memoire de chargement %d Mo", mem >> 20); } }
    // plafonds de population, sous les reserves du jeu (140 personnages, 110 vehicules, missions comprises). La variete
    // des voitures chargees (CStreaming::desiredNumVehiclesLoaded 0x8A5A84 = 12) n'est PAS relevee : le jeu ne garde
    // que 50 modeles de vehicules a la fois (reserve des CVehicleStructure 0xB4E680) ; pleine, CVehicleModelInfo::
    // SetClump (0x4C95FF) recoit une structure nulle et plante en 0x4C8F24 (journaux de JD du 01/10, densite 300 %).
    int peds = (int)(25 * z * d + 0.5f), cars = (int)(12 * z * d + 0.5f);
    *(int *)0x8D2538 = peds > 90 ? 90 : peds;
    *(int *)0x8A5B24 = cars > 45 ? 45 : cars;
    // densites : seulement quand le script les laisse a 1 (missions : souvent 0, qu'on ne touche pas)
    static float lastPed = -1, lastCar = -1;
    float &ped = *(float *)0x8D2530, &car = *(float *)0x8A5B20;
    if (ped == 1.0f || ped == lastPed) { ped = d; lastPed = d; }
    if (car == 1.0f || car == lastCar) { car = d; lastCar = d; }
}
