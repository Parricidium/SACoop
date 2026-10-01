// Grand ecran. SA 1.0 calcule la 3D en 4:3 (ou 16:9 avec l'option "Ecran large") et l'etire a la taille de l'ecran.
//  - CDraw::CalculateAspectRatio (0x6FF420) : remplace, CDraw::ms_fAspectRatio (0xC3EFA4) = largeur / hauteur reelles
//    de l'image (RsGlobal 0xC17044 / 0xC17048).
//  - CDraw::SetFOV (0x6FF410) : remplace, champ de vision horizontal elargi au-dela de 4:3 (Hor+ : la meme hauteur de
//    vue qu'en 4:3, plus de cote). La valeur va la ou le jeu la range lui-meme : adresse masquee
//    *(0x15605DC) ^ *(0x419F03) (CDraw::ms_fFOV), comme le fait son propre code (0x401025).
// Option GrandEcran=0 : comportement d'origine.
#include "util.h"
#include "sacoop.h"
#include "widescreen.h"
#include <math.h>

static float ScreenAspect()
{
    int w = *(int *)0xC17044, h = *(int *)0xC17048;
    return w > 0 && h > 0 ? (float)w / (float)h : 4.0f / 3.0f;
}

static void __cdecl h_CalculateAspectRatio()
{
    *(float *)0xC3EFA4 = ScreenAspect();
}

static void __cdecl h_SetFOV(float fov)
{
    float aspect = ScreenAspect();
    if (aspect > 4.0f / 3.0f + 0.01f && fov > 1.0f && fov < 170.0f) {
        const float d2r = 3.14159265f / 180.0f;
        fov = 2.0f * atanf(tanf(fov * 0.5f * d2r) * aspect / (4.0f / 3.0f)) / d2r;
    }
    float *dst = (float *)(*(uintptr_t *)0x15605DC ^ *(uintptr_t *)0x419F03);
    *dst = fov;
}

void InstallWidescreen()
{
    if (!g_cfg.widescreen) return;
    static const uint8_t ar[] = { 0xA0, 0x93, 0x67, 0xBA, 0x00 };   // mov al, [0xBA6793]
    static const uint8_t fv[] = { 0x8B, 0x44, 0x24, 0x04 };          // mov eax, [esp+4]
    if (memcmp((void *)0x6FF420, ar, sizeof(ar)) || memcmp((void *)0x6FF410, fv, sizeof(fv))) { Log("grand ecran : code inattendu, pas installe"); return; }
    PatchJump(0x6FF420, (void *)h_CalculateAspectRatio);
    PatchJump(0x6FF410, (void *)h_SetFOV);
    Log("grand ecran : 3D au format de l'ecran");
}
