// Grand ecran. SA 1.0 calcule la 3D en 4:3 (ou 16:9 avec l'option "Ecran large") et l'etire a la taille de l'ecran.
//  - CDraw::CalculateAspectRatio (0x6FF420) : remplace, CDraw::ms_fAspectRatio (0xC3EFA4) = largeur / hauteur reelles
//    de l'image (RsGlobal 0xC17044 / 0xC17048).
//  - CDraw::SetFOV (0x6FF410) : remplace, champ de vision horizontal elargi au-dela de 4:3 (Hor+ : la meme hauteur de
//    vue qu'en 4:3, plus de cote). La valeur va la ou le jeu la range lui-meme : adresse masquee
//    *(0x15605DC) ^ *(0x419F03) (CDraw::ms_fFOV), comme le fait son propre code (0x401025).
//  - HUD et radar : leurs fonctions (CRadar 0x582000-0x587FFF, CHud 0x588000-0x590FFF) mettent a l'echelle en X par
//    la constante 1/640 (0x859520), etiree sur un ecran large. Leurs references a cette constante sont redirigees
//    vers g_hudScaleX = 1/640 x (4/3) / format : proportions d'origine, elements ancres a droite toujours a droite.
// Option GrandEcran=0 : comportement d'origine.
//  - Resolution : au premier RwEngineSetVideoMode (0x7F2D50, appele par psSelectDevice), le mode de la taille voulue
//    est choisi s'il existe (RwEngineGetNumVideoModes 0x7F2CC0, RwEngineGetVideoModeInfo 0x7F2CF0, RwVideoMode de 24 octets {l, h, prof,
//    drapeaux, frequence, format}) : celle du bureau en plein ecran sans bordure, TailleFenetre en fenetre. Le mode retenu est aussi
//    range comme choix du jeu (GcurSelVM 0x8D6220, FrontEndMenuManager.m_nDisplayVideoMode 0xBA6820).
#include "util.h"
#include "sacoop.h"
#include "widescreen.h"
#include <math.h>

static float ScreenAspect()
{
    int w = *(int *)0xC17044, h = *(int *)0xC17048;
    return w > 0 && h > 0 ? (float)w / (float)h : 4.0f / 3.0f;
}

static float g_hudScaleX = 1.0f / 640.0f;

static void __cdecl h_CalculateAspectRatio()
{
    float aspect = ScreenAspect();
    *(float *)0xC3EFA4 = aspect;
    g_hudScaleX = aspect > 4.0f / 3.0f ? (1.0f / 640.0f) * (4.0f / 3.0f) / aspect : 1.0f / 640.0f;
}

// Facteur horizontal du HUD (1 en 4:3, 0.75 en 16:9) : pour nos propres textes (hud.cpp).
float HudAspectFactor() { return g_hudScaleX * 640.0f; }

// Redirige les references (operandes d'instructions) a la constante 1/640 dans [from, to).
static int RedirectScaleX(uintptr_t from, uintptr_t to)
{
    int n = 0;
    for (uintptr_t a = from; a + 4 <= to; a++) {
        if (*(uint32_t *)a != 0x859520) continue;
        uint8_t prev = *(uint8_t *)(a - 1);
        // modrm "disp32 seul" (05/0D/15/1D/25/2D/35/3D) apres D8/D9/DC (fmul, fld...) : seulement des operandes memoire
        if ((prev & 0xC7) != 0x05) continue;
        uint8_t op = *(uint8_t *)(a - 2);
        if (op != 0xD8 && op != 0xD9 && op != 0xDC && op != 0xDD) continue;
        uint32_t v = (uint32_t)(uintptr_t)&g_hudScaleX;
        Patch(a, &v, 4);
        n++;
    }
    return n;
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

struct VideoModeInfo { int width, height, depth, flags, refRate, format, pad[2]; };   // RwVideoMode (24 octets)
typedef int(__cdecl *SetVideoMode_t)(int);
static SetVideoMode_t o_SetVideoMode;

static int __cdecl h_SetVideoMode(int idx)
{
    static bool done;
    if (!done && g_cfg.windowed) {
        done = true;
        int w = g_cfg.borderless ? GetSystemMetrics(SM_CXSCREEN) : g_cfg.winW;
        int h = g_cfg.borderless ? GetSystemMetrics(SM_CYSCREEN) : g_cfg.winH;
        int n = ((int(__cdecl *)())0x7F2CC0)();
        for (int i = 0; i < n; i++) {
            VideoModeInfo vm = {};
            ((void(__cdecl *)(VideoModeInfo *, int))0x7F2CF0)(&vm, i);
            if (vm.width == w && vm.height == h && vm.depth == 32) {
                idx = i;
                *(int *)0x8D6220 = i;
                *(int *)0xBA6820 = i;
                Log("resolution : mode %d (%dx%d)", i, w, h);
                break;
            }
        }
    }
    return o_SetVideoMode(idx);
}

void InstallResolution()
{
    static const uint8_t pro[] = { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x0D, 0x24, 0x7B, 0xC9, 0x00 };
    o_SetVideoMode = (SetVideoMode_t)MakeDetour(0x7F2D50, pro, sizeof(pro), (void *)h_SetVideoMode);
}

void InstallWidescreen()
{
    if (!g_cfg.widescreen) return;
    static const uint8_t ar[] = { 0xA0, 0x93, 0x67, 0xBA, 0x00 };   // mov al, [0xBA6793]
    static const uint8_t fv[] = { 0x8B, 0x44, 0x24, 0x04 };          // mov eax, [esp+4]
    if (memcmp((void *)0x6FF420, ar, sizeof(ar)) || memcmp((void *)0x6FF410, fv, sizeof(fv))) { Log("grand ecran : code inattendu, pas installe"); return; }
    PatchJump(0x6FF420, (void *)h_CalculateAspectRatio);
    PatchJump(0x6FF410, (void *)h_SetFOV);
    int n = RedirectScaleX(0x582000, 0x591000);
    Log("grand ecran : 3D au format de l'ecran, %d echelles du HUD et du radar", n);
}
