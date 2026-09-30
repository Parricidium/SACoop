// Affichage coop : pseudo au-dessus de chaque autre joueur, et point radar de sa couleur.
// Les pseudos sont dessines apres le HUD du jeu : appel de CHud::Draw (0x58FAE0) dans Render2dStuff (en 0x53E4FF)
// remplace par le notre, qui appelle l'original puis ecrit les pseudos (CFont, puis CFont::DrawFonts).
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "hud.h"
#include <math.h>
#include <string.h>

using namespace game;

namespace font {
    inline void SetScale(float x, float y) { ((void(__cdecl *)(float, float))0x719380)(x, y); }
    inline void SetColor(uint32_t rgba) { ((void(__cdecl *)(uint32_t))0x719430)(rgba); }   // CRGBA par valeur : r en octet bas
    inline void SetFontStyle(int s) { ((void(__cdecl *)(char))0x719490)((char)s); }       // 0 gothique, 1 sous-titres, 2 menu, 3 pricedown
    inline void SetProportional(bool b) { ((void(__cdecl *)(bool))0x7195B0)(b); }
    inline void SetBackground(bool b, bool box) { ((void(__cdecl *)(bool, bool))0x7195C0)(b, box); }
    inline void SetDropShadow(int n) { ((void(__cdecl *)(char))0x719570)((char)n); }
    inline void SetEdge(int n) { ((void(__cdecl *)(char))0x719590)((char)n); }
    inline void SetDropColor(uint32_t rgba) { ((void(__cdecl *)(uint32_t))0x719510)(rgba); }
    inline void SetOrientation(int o) { ((void(__cdecl *)(char))0x719610)((char)o); }      // 0 centre, 1 gauche, 2 droite
    inline void SetCentreSize(float w) { ((void(__cdecl *)(float))0x7194E0)(w); }
    inline void Print(float x, float y, const char *s) { ((void(__cdecl *)(float, float, const char *))0x71A700)(x, y, s); }
    inline void DrawFonts() { ((void(__cdecl *)())0x71A210)(); }
}

static inline uint32_t RGBA(int r, int g, int b, int a) { return (uint32_t)r | (g << 8) | (b << 16) | ((uint32_t)a << 24); }

// Couleur de chaque joueur (pseudo et point radar) : blanc, jaune, bleu clair, violet.
static const uint32_t kNameColor[MAX_PLAYERS] = { RGBA(255, 255, 255, 255), RGBA(255, 220, 60, 255), RGBA(110, 190, 255, 255), RGBA(200, 130, 255, 255) };
static const int kBlipColor[MAX_PLAYERS] = { 3, 4, 6, 5 };   // CRadar : 3 blanc, 4 jaune, 6 cyan, 5 violet

// --- Points radar ---
struct Blip { int ped; int id; };   // reference du pantin au moment de la pose, identifiant du point
static Blip g_blips[MAX_PLAYERS];

void HudUpdateBlip(int player, void *puppet)
{
    Blip &b = g_blips[player];
    int ref = puppet ? PedRef(puppet) : 0;
    if (b.id && b.ped == ref) return;
    if (b.id) { ((void(__cdecl *)(int))0x587CE0)(b.id); b.id = 0; }   // CRadar::ClearBlip
    if (!puppet) return;
    // CRadar::SetEntityBlip(BLIP_CHAR, reference, 0, BLIP_DISPLAY_BLIPONLY) puis ChangeBlipColour.
    b.id = ((int(__cdecl *)(int, int, int, int))0x5839A0)(2, ref, 0, 2);
    b.ped = ref;
    if (b.id) ((void(__cdecl *)(int, int))0x583AB0)(b.id, kBlipColor[player]);
    Log("point radar du joueur %d : %d", player, b.id);
}

// --- Pseudos ---
static void *(*g_puppetOf)(int);

static void DrawNames()
{
    if (!g_puppetOf || GameState() != 9) return;
    void *me = FindPlayerPed();
    if (!me) return;
    int sw = *(int *)0xC17044, sh = *(int *)0xC17048;   // RsGlobal.maximumWidth / maximumHeight
    const float *mine = EntityPos(me);
    bool any = false;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        void *ped = g_puppetOf(i);
        if (!ped) continue;
        const float *p = EntityPos(ped);
        float dx = p[0] - mine[0], dy = p[1] - mine[1], dz = p[2] - mine[2];
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist > 80.0f) continue;
        float world[3] = { p[0], p[1], p[2] + 1.15f }, scr[3], w, h;
        if (!((bool(__cdecl *)(const float *, float *, float *, float *, bool, bool))0x70CE30)(world, scr, &w, &h, true, true)) continue;
        if (scr[2] <= 1.0f) continue;
        float k = dist < 10.0f ? 1.0f : 10.0f / dist;
        if (k < 0.55f) k = 0.55f;
        font::SetFontStyle(1);
        font::SetProportional(true);
        font::SetBackground(false, false);
        font::SetOrientation(0);
        font::SetCentreSize((float)sw);
        font::SetScale(0.42f * k * sw / 640.0f, 0.95f * k * sh / 448.0f);
        font::SetEdge(1);
        font::SetDropColor(RGBA(0, 0, 0, 255));
        font::SetColor(kNameColor[i]);
        font::Print(scr[0], scr[1], g_players[i].state.name[0] ? g_players[i].state.name : "?");
        any = true;
    }
    if (any) font::DrawFonts();
}

static void __cdecl h_HudDraw()
{
    ((void(__cdecl *)())0x58FAE0)();   // CHud::Draw
    DrawNames();
}

void InstallHud(void *(*puppetOf)(int))
{
    g_puppetOf = puppetOf;
    static const uint8_t call[] = { 0xE8 };
    if (memcmp((void *)0x53E4FF, call, 1) == 0) PatchCall(0x53E4FF, (void *)h_HudDraw);
    else Log("hud : appel de CHud::Draw introuvable");
}
