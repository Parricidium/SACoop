// Grand ecran. SA 1.0 calcule la 3D en 4:3 (ou 16:9 avec l'option "Ecran large") et l'etire a la taille de l'ecran.
//  - CDraw::CalculateAspectRatio (0x6FF420) : remplace, CDraw::ms_fAspectRatio (0xC3EFA4) = largeur / hauteur reelles
//    de l'image (RsGlobal 0xC17044 / 0xC17048).
//  - CDraw::SetFOV (0x6FF410) : remplace, champ de vision horizontal elargi au-dela de 4:3 (Hor+ : la meme hauteur de
//    vue qu'en 4:3, plus de cote). La valeur va la ou le jeu la range lui-meme : adresse masquee
//    *(0x15605DC) ^ *(0x419F03) (CDraw::ms_fFOV), comme le fait son propre code (0x401025).
//  - HUD et radar : leurs fonctions (CRadar 0x582000-0x587FFF, CHud 0x588000-0x590FFF) mettent a l'echelle en X par
//    la constante 1/640 (0x859520), etiree sur un ecran large. Leurs references a cette constante sont redirigees
//    vers g_hudScaleX = 1/640 x (4/3) / format : proportions d'origine, elements ancres a droite toujours a droite.
//  - Menus : pendant que le menu est ouvert (FrontEndMenuManager +0x5C), les sommets 2D (RwIm2DVertex de 28 octets,
//    x en tete) passes a Im2DRenderTriangle / Primitive / IndexedPrimitive (RwEngineInstance 0xC97B24, +0x2C/+0x30/
//    +0x34) sont resserres vers le centre (format 4:3) ; le curseur l'est aussi, les clics restent justes.
// Option GrandEcran=0 : comportement d'origine.
//  - Resolution : au premier RwEngineSetVideoMode (0x7F2D50, appele par psSelectDevice), le mode de la taille voulue
//    est choisi s'il existe (RwEngineGetNumVideoModes 0x7F2CC0, RwEngineGetVideoModeInfo 0x7F2CF0, RwVideoMode de 24 octets {l, h, prof,
//    drapeaux, frequence, format}) : celle du bureau en plein ecran sans bordure, TailleFenetre en fenetre. Le mode retenu est aussi
//    range comme choix du jeu (GcurSelVM 0x8D6220, FrontEndMenuManager.m_nDisplayVideoMode 0xBA6820).
//  - Fenetre du jeu « choix du peripherique » (carte graphique, resolution : DialogBoxParamA en 0x746241 dans
//    psSelectDevice, des que Direct3D voit plusieurs adaptateurs : GnumSubSystems 0xC920F0 > 1, PC a deux cartes ou ecrans) : jamais montree, SACoop choisit (entree de
//    DialogBoxParamA dans la table d'importation de gta_sa.exe : on repond OK sans l'ouvrir). Plein ecran : le mode de
//    la taille du bureau.
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
    if (!done) {
        done = true;
        int w = g_cfg.borderless || !g_cfg.windowed ? GetSystemMetrics(SM_CXSCREEN) : g_cfg.winW;
        int h = g_cfg.borderless || !g_cfg.windowed ? GetSystemMetrics(SM_CYSCREEN) : g_cfg.winH;
        // Mode 0 = le bureau (fenetre) : il laissait le jeu a sa resolution enregistree (800x600 etire, flou) ; on prend
        // le vrai mode de cette taille.
        int n = ((int(__cdecl *)())0x7F2CC0)();
        for (int i = 1; i < n; i++) {
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

// --- Menus resserres ---
typedef int(__cdecl *Im2DTri_t)(void *verts, int n, int a, int b, int c);
typedef int(__cdecl *Im2DPrim_t)(int type, void *verts, int n);
typedef int(__cdecl *Im2DIdx_t)(int type, void *verts, int n, void *idx, int ni);
static Im2DTri_t o_Tri;
static Im2DPrim_t o_Prim;
static Im2DIdx_t o_Idx;

static bool MenuSqueeze() { return *(uint8_t *)(0xBA6748 + 0x5C) && ScreenAspect() > 4.0f / 3.0f + 0.01f; }

// Copie resserree des sommets (au plus 1024 ; au-dela, dessin d'origine).
static void *Squeeze(void *verts, int n)
{
    static uint8_t buf[1024 * 28];
    if (n <= 0 || n > 1024) return verts;
    memcpy(buf, verts, n * 28);
    float w = (float)*(int *)0xC17044, cx = w * 0.5f, k = (4.0f / 3.0f) / ScreenAspect();
    for (int i = 0; i < n; i++) { float &x = *(float *)(buf + i * 28); x = cx + (x - cx) * k; }
    return buf;
}
static int __cdecl h_Tri(void *v, int n, int a, int b, int c) { return o_Tri(MenuSqueeze() ? Squeeze(v, n) : v, n, a, b, c); }
static int __cdecl h_Prim(int t, void *v, int n) { return o_Prim(t, MenuSqueeze() ? Squeeze(v, n) : v, n); }
static int __cdecl h_Idx(int t, void *v, int n, void *idx, int ni) { return o_Idx(t, MenuSqueeze() ? Squeeze(v, n) : v, n, idx, ni); }

// Largeur de chaque bande laterale pendant un menu resserre (0 sinon).
float MenuBarWidth()
{
    if (!g_cfg.widescreen || !o_Prim || !MenuSqueeze()) return 0.0f;
    float w = (float)*(int *)0xC17044;
    return w * 0.5f * (1.0f - (4.0f / 3.0f) / ScreenAspect());
}

// Les pointeurs de RwEngineInstance existent une fois le moteur ouvert : pose a la premiere image.
void WidescreenFrame()
{
    static bool done;
    if (done || !g_cfg.widescreen) return;
    uint8_t *rw = *(uint8_t **)0xC97B24;
    if (!rw) return;
    done = true;
    o_Tri = (Im2DTri_t)PatchPointer((void **)(rw + 0x2C), (void *)h_Tri);
    o_Prim = (Im2DPrim_t)PatchPointer((void **)(rw + 0x30), (void *)h_Prim);
    o_Idx = (Im2DIdx_t)PatchPointer((void **)(rw + 0x34), (void *)h_Idx);
    Log("grand ecran : menus resserres au format 4:3");
}

static INT_PTR WINAPI h_DialogBoxParamA(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM)
{
    Log("resolution : fenetre de choix du peripherique du jeu sautee (carte graphique par defaut)");
    return IDOK;
}

static void SkipDeviceDialog()
{
    void *real = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "DialogBoxParamA");
    uint8_t *mod = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(mod + ((IMAGE_DOS_HEADER *)mod)->e_lfanew);
    IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!real || !dir.VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(mod + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((const char *)(mod + d->Name), "user32.dll")) continue;
        for (void **slot = (void **)(mod + d->FirstThunk); *slot; slot++)
            if (*slot == real) { PatchPointer(slot, (void *)h_DialogBoxParamA); return; }
    }
    Log("resolution : DialogBoxParamA absent des importations du jeu");
}

void InstallResolution()
{
    SkipDeviceDialog();
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
