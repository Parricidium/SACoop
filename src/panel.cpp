// Panneau coop (F10). Au clavier : haut / bas pour choisir, gauche / droite ou Entree pour changer, Echap ou F10 pour
// fermer. Les touches sont avalees tant qu'il est ouvert (procedure de fenetre, window.cpp).
//  - Aller vers un joueur : pose a cote de lui, ou passager de sa voiture s'il conduit avec une place libre.
//  - Tir ami, Tenue (CJ ou gangs, change le pantin chez les autres en direct) : retenus dans sacoop.ini.
//  - Hote : heure (+1 h) et meteo (auto, soleil, nuages, pluie, brouillard : FORCE_WEATHER_NOW 01B6 /
//    RELEASE_WEATHER 01B7) ; les invites les recoivent par MSG_WORLD.
// Dessin : CSprite2d::DrawRect (0x727B60, rect {x1, y1, x2, y2}, CRGBA*) pour le fond, CFont pour le texte (hud.cpp).
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "chat.h"
#include "hud.h"
#include "mirror.h"
#include "widescreen.h"
#include "panel.h"
#include "font.h"
#include <math.h>
#include <stdio.h>

using namespace game;

static bool g_open;
static int g_sel;
static const bool g_fr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;

static const int kSkins[] = { 0, 105, 106, 107, 102, 103, 104, 108, 109, 110, 114, 115, 116 };
static const char *kSkinFr[] = { "CJ (vos vetements)", "Grove Street 1", "Grove Street 2", "Grove Street 3", "Ballas 1", "Ballas 2", "Ballas 3", "Vagos 1", "Vagos 2", "Vagos 3", "Aztecas 1", "Aztecas 2", "Aztecas 3" };
static const char *kSkinEn[] = { "CJ (your clothes)", "Grove Street 1", "Grove Street 2", "Grove Street 3", "Ballas 1", "Ballas 2", "Ballas 3", "Vagos 1", "Vagos 2", "Vagos 3", "Aztecas 1", "Aztecas 2", "Aztecas 3" };
static const int kWeather[] = { -1, 1, 4, 8, 9 };
static const char *kWeatherFr[] = { "Auto", "Soleil", "Nuages", "Pluie", "Brouillard" };
static const char *kWeatherEn[] = { "Auto", "Sun", "Clouds", "Rain", "Fog" };
static int g_weather;

enum Kind { K_GOTO, K_FRIENDLY, K_SKIN, K_HOUR, K_WEATHER, K_CLOSE };
struct Item { Kind kind; int arg; char text[96]; };

bool PanelOpen() { return g_open; }

// Pres du joueur id (2 m derriere lui), ou passager de sa voiture s'il y a une place.
bool GoToPlayer(int id)
{
    void *me = FindPlayerPed(), *other = PuppetOf(id);
    if (!me || !other) { HudToast(g_fr ? "Joueur absent" : "Player not here", 3000); return false; }
    if (PedVehicle(me)) { HudToast(g_fr ? "Descendez d'abord du vehicule" : "Get out of the vehicle first", 3000); return false; }
    if (void *veh = PedVehicle(other)) {
        int maxPass = Field<uint8_t>(veh, VEH_MAXPASS);
        for (int i = 0; i < maxPass && i < 8; i++)
            if (!Field<void *>(veh, VEH_PASSENGERS + i * 4)) { WarpPuppetIn(me, veh, i + 1); return true; }
    }
    const float *p = EntityPos(other);
    float h = Field<float>(other, PED_ROTATION);
    float pos[3] = { p[0] + sinf(h) * 2.0f, p[1] - cosf(h) * 2.0f, p[2] };
    EntityArea(me) = EntityArea(other);
    PlacePuppet(me, pos, h);
    return true;
}

static int SkinIndex()
{
    for (int i = 0; i < (int)(sizeof(kSkins) / sizeof(kSkins[0])); i++) if (kSkins[i] == g_cfg.skin) return i;
    return 0;
}

static int BuildItems(Item *items)
{
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i == g_localId || !g_players[i].connected) continue;
        Item &it = items[n++];
        it.kind = K_GOTO; it.arg = i;
        const char *name = g_players[i].state.name[0] ? g_players[i].state.name : "?";
        sprintf(it.text, g_fr ? "Aller vers %s%s" : "Go to %s%s", name, i == 0 ? (g_fr ? " (hote)" : " (host)") : "");
    }
    items[n].kind = K_FRIENDLY;
    sprintf(items[n++].text, g_fr ? "Tir ami : %s" : "Friendly fire: %s", g_cfg.friendlyFire ? (g_fr ? "oui" : "on") : (g_fr ? "non" : "off"));
    items[n].kind = K_SKIN;
    sprintf(items[n++].text, g_fr ? "Tenue : %s" : "Outfit: %s", (g_fr ? kSkinFr : kSkinEn)[SkinIndex()]);
    if (g_cfg.host) {
        items[n].kind = K_HOUR;
        sprintf(items[n++].text, g_fr ? "Heure : %02d:%02d (+1 h)" : "Time: %02d:%02d (+1 h)", *(uint8_t *)0xB70153, *(uint8_t *)0xB70152);
        items[n].kind = K_WEATHER;
        sprintf(items[n++].text, g_fr ? "Meteo : %s" : "Weather: %s", (g_fr ? kWeatherFr : kWeatherEn)[g_weather]);
    }
    items[n].kind = K_CLOSE;
    sprintf(items[n++].text, "%s", g_fr ? "Fermer" : "Close");
    return n;
}

static void SaveIni(const char *key, int v)
{
    char b[16];
    wsprintfA(b, "%d", v);
    WritePrivateProfileStringA("SACoop", key, b, IniPath());
}

static void Activate(const Item &it, int dir)
{
    switch (it.kind) {
    case K_GOTO: if (GoToPlayer(it.arg)) g_open = false; break;
    case K_FRIENDLY: g_cfg.friendlyFire = !g_cfg.friendlyFire; SaveIni("TirAmi", g_cfg.friendlyFire); break;
    case K_SKIN: {
        int n = (int)(sizeof(kSkins) / sizeof(kSkins[0]));
        int i = (SkinIndex() + (dir < 0 ? n - 1 : 1)) % n;
        g_cfg.skin = kSkins[i];
        SaveIni("Tenue", g_cfg.skin);
        break;
    }
    case K_HOUR: {
        uint8_t &h = *(uint8_t *)0xB70153;
        h = (uint8_t)((h + (dir < 0 ? 23 : 1)) % 24);
        *(uint32_t *)0xB70158 = *(uint32_t *)0xB7CB84;
        break;
    }
    case K_WEATHER: {
        int n = (int)(sizeof(kWeather) / sizeof(kWeather[0]));
        g_weather = (g_weather + (dir < 0 ? n - 1 : 1)) % n;
        if (kWeather[g_weather] < 0) RunScriptCommand(0x01B7, 0, nullptr);
        else { int w = kWeather[g_weather]; RunScriptCommand(0x01B6, 1, &w); }
        break;
    }
    case K_CLOSE: g_open = false; break;
    }
}

bool PanelWindowMessage(UINT msg, WPARAM wp)
{
    if (!NetRunning() || ChatTyping()) return false;
    if (!g_open) {
        bool inGame = GameState() == 9 && !*(uint8_t *)(0xBA6748 + 0x5C);
        if (msg == WM_KEYDOWN && wp == VK_F10 && inGame) { g_open = true; g_sel = 0; return true; }
        return false;
    }
    if (msg == WM_KEYDOWN) {
        Item items[16];
        int n = BuildItems(items);
        if (g_sel >= n) g_sel = n - 1;
        switch (wp) {
        case VK_UP: g_sel = (g_sel + n - 1) % n; break;
        case VK_DOWN: g_sel = (g_sel + 1) % n; break;
        case VK_LEFT: Activate(items[g_sel], -1); break;
        case VK_RIGHT: case VK_RETURN: Activate(items[g_sel], 1); break;
        case VK_ESCAPE: case VK_F10: g_open = false; break;
        }
    }
    // Pendant le panneau, aucune touche ne va au jeu (pas de F10 / Echap repris par le menu Pause).
    return msg >= WM_KEYFIRST && msg <= WM_KEYLAST;
}

void PanelDraw()
{
    if (!g_open) return;
    if (GameState() != 9) { g_open = false; return; }
    Item items[16];
    int n = BuildItems(items);
    if (g_sel >= n) g_sel = n - 1;
    int sw = *(int *)0xC17044, sh = *(int *)0xC17048;
    float k = HudAspectFactor();
    float line = sh * 0.05f, w = sw * 0.40f * k, h = line * (n + 2.2f);
    float x1 = sw * 0.5f - w * 0.5f, y1 = sh * 0.5f - h * 0.5f;
    struct { float x1, y1, x2, y2; } rect = { x1, y1, x1 + w, y1 + h };
    uint8_t color[4] = { 0, 0, 0, 190 };
    uint8_t *rw = *(uint8_t **)0xC97B24;
    ((int(__cdecl *)(int, int))*(void **)(rw + 0x20))(1, 0);   // pas de texture
    ((void(__cdecl *)(const void *, const void *))0x727B60)(&rect, color);
    auto text = [&](float px, float py, uint32_t c, const char *s, int align, float scale) {
        font::SetFontStyle(1);
        font::SetProportional(true);
        font::SetBackground(false, false);
        font::SetOrientation(align);
        font::SetCentreSize((float)sw);
        font::SetScale(scale * sw / 640.0f * k, scale * 2.2f * sh / 448.0f);
        font::SetEdge(1);
        font::SetDropColor(0xFF000000);
        font::SetColor(c);
        font::Print(px, py, s);
    };
    char title[64];
    wsprintfA(title, "SACOOP %s", SACOOP_VERSION);
    text(sw * 0.5f, y1 + line * 0.3f, 0xFFFFFFFF, title, 0, 0.5f);
    for (int i = 0; i < n; i++) {
        bool sel = i == g_sel;
        char s[110];
        wsprintfA(s, sel ? "> %s <" : "%s", items[i].text);
        text(sw * 0.5f, y1 + line * (1.6f + i), sel ? 0xFFFFFFFF : 0xFFA0A0A0, s, 0, 0.42f);
    }
    font::DrawFonts();
}
