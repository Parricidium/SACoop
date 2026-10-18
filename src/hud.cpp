// Affichage coop : pseudo au-dessus de chaque autre joueur, et point radar de sa couleur.
// Les pseudos sont dessines apres le HUD du jeu : appel de CHud::Draw (0x58FAE0) dans Render2dStuff (en 0x53E4FF)
// remplace par le notre, qui appelle l'original puis ecrit les pseudos (CFont, puis CFont::DrawFonts).
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "hud.h"
#include "widescreen.h"
#include "chat.h"
#include "font.h"
#include "panel.h"
#include <math.h>
#include <string.h>

using namespace game;

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
    if (!g_puppetOf || GameState() != 9 || *(uint8_t *)0xB5F851) return;   // (pas pendant les cinematiques)
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
        font::SetScale(0.42f * k * sw / 640.0f * HudAspectFactor(), 0.95f * k * sh / 448.0f);
        font::SetEdge(1);
        font::SetDropColor(RGBA(0, 0, 0, 255));
        font::SetColor(kNameColor[i]);
        {   // menu pause ouvert chez ce joueur (gear 0x20) : "(paused)" a cote de son nom
            static const bool fr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
            char label[64];
            lstrcpynA(label, g_players[i].state.name[0] ? g_players[i].state.name : "?", 40);
            if (g_players[i].state.gear & 0x20) lstrcatA(label, fr ? " (en pause)" : " (paused)");
            font::Print(scr[0], scr[1], label);
        }
        any = true;
    }
    if (any) font::DrawFonts();
}

// Messages coop en bas de l'ecran (arrivee d'un joueur, sauvegarde recue...) : texte ASCII, quelques secondes.
static char g_toast[3][96];
static uint32_t g_toastUntil[3];

void HudToast(const char *text, unsigned ms)
{
    int slot = 0;
    for (int i = 0; i < 3; i++) if (g_toastUntil[i] < g_toastUntil[slot]) slot = i;
    lstrcpynA(g_toast[slot], text, sizeof(g_toast[slot]));
    g_toastUntil[slot] = GetTickCount() + ms;
    Log("message : %s", text);
}

// A question to the player (the mission vehicle offer): at the top of the screen, out of the way of the mission texts at
// the bottom (request of 15/10).
static char g_prompt[128];
static uint32_t g_promptUntil;
void HudPrompt(const char *text, unsigned ms)
{
    bool fresh = (int)(g_promptUntil - GetTickCount()) <= 0 || lstrcmpA(g_prompt, text) != 0;
    lstrcpynA(g_prompt, text, sizeof(g_prompt));
    g_promptUntil = GetTickCount() + ms;
    if (fresh) Log("question : %s", text);
}
void HudPromptClear() { g_promptUntil = 0; }

static void DrawToasts()
{
    if (GameState() != 9) return;
    int sw = *(int *)0xC17044, sh = *(int *)0xC17048;
    uint32_t now = GetTickCount();
    float y = sh * 0.78f;
    bool any = false;
    if ((int)(g_promptUntil - now) > 0) {
        font::SetFontStyle(1);
        font::SetProportional(true);
        font::SetBackground(false, false);
        font::SetOrientation(0);
        font::SetCentreSize((float)sw);
        font::SetScale(0.5f * sw / 640.0f * HudAspectFactor(), 1.1f * sh / 448.0f);
        font::SetEdge(1);
        font::SetDropColor(RGBA(0, 0, 0, 255));
        font::SetColor(RGBA(255, 230, 120, 255));
        font::Print(sw * 0.5f, sh * 0.06f, g_prompt);
        any = true;
    }
    for (int i = 0; i < 3; i++) {
        if ((int)(g_toastUntil[i] - now) <= 0) continue;
        font::SetFontStyle(1);
        font::SetProportional(true);
        font::SetBackground(false, false);
        font::SetOrientation(0);
        font::SetCentreSize((float)sw);
        font::SetScale(0.5f * sw / 640.0f * HudAspectFactor(), 1.1f * sh / 448.0f);
        font::SetEdge(1);
        font::SetDropColor(RGBA(0, 0, 0, 255));
        font::SetColor(RGBA(255, 255, 255, 255));
        font::Print(sw * 0.5f, y, g_toast[i]);
        y -= sh * 0.05f;
        any = true;
    }
    if (any) font::DrawFonts();
}

// Tableau des joueurs, touche F5 maintenue (fenetre du jeu active) : pseudo, hote, vie, distance. Tableau force a
// l'ecran par l'autotest TestTableau=1.
static void DrawPlayers()
{
    bool show = (GameHasFocus() && (GetAsyncKeyState(VK_F5) & 0x8000)) || g_cfg.testBoard;
    if (!show || GameState() != 9 || !NetRunning()) return;
    void *me = FindPlayerPed();
    if (!me) return;
    static const bool fr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
    int sw = *(int *)0xC17044, sh = *(int *)0xC17048;
    float x = sw * 0.18f, y = sh * 0.22f, line = sh * 0.055f;
    auto text = [&](float px, float py, uint32_t color, const char *s, int align) {
        font::SetFontStyle(1);
        font::SetProportional(true);
        font::SetBackground(false, false);
        font::SetOrientation(align);
        font::SetCentreSize((float)sw);
        font::SetScale(0.42f * sw / 640.0f * HudAspectFactor(), 0.95f * sh / 448.0f);
        font::SetEdge(1);
        font::SetDropColor(RGBA(0, 0, 0, 255));
        font::SetColor(color);
        font::Print(px, py, s);
    };
    text(sw * 0.5f, y, RGBA(255, 255, 255, 255), fr ? "JOUEURS" : "PLAYERS", 0);
    y += line * 1.3f;
    const float *mp = EntityPos(me);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        bool local = i == g_localId;
        if (!local && !g_players[i].connected) continue;
        const MsgState &s = g_players[i].state;
        char left[64], right[96];
        wsprintfA(left, "%s%s", local ? g_cfg.playerName : (s.name[0] ? s.name : "?"), i == 0 ? (fr ? " (hote)" : " (host)") : "");
        if (local) wsprintfA(right, fr ? "vie %d  -  vous" : "health %d  -  you", (int)Field<float>(me, PED_HEALTH));
        else {
            float dx = s.pos[0] - mp[0], dy = s.pos[1] - mp[1], dz = s.pos[2] - mp[2];
            int dist = (int)sqrtf(dx * dx + dy * dy + dz * dz);
            if (s.inGame) wsprintfA(right, fr ? "vie %d  -  %d m" : "health %d  -  %d m", (int)s.health, dist);
            else lstrcpyA(right, fr ? "au menu" : "in menu");
        }
        // Ping: the guests' to the host (the host measures it and shares it; a guest sees his own and the other guests' to the host).
        float pg = NetPing(i);
        if (pg >= 0.0f) {
            int ms = (int)(pg + 0.5f);
            wsprintfA(right + lstrlenA(right), "  -  %d ms", ms < 1 ? 1 : ms);
        }
        text(x, y, kNameColor[i], left, 1);
        text(sw - x, y, RGBA(230, 230, 230, 255), right, 2);
        y += line;
    }
    font::DrawFonts();
}

// Tchat : les derniers messages a gauche (couleur du joueur), la ligne de saisie dessous.
static float g_chatY;
static void DrawChatLine(int player, const char *name, const char *text, float alpha)
{
    int sw = *(int *)0xC17044, sh = *(int *)0xC17048;
    char line[128];
    wsprintfA(line, "%s: %s", name, text);
    uint32_t c = kNameColor[player >= 0 && player < MAX_PLAYERS ? player : 0];
    c = (c & 0x00FFFFFF) | ((uint32_t)(255 * alpha) << 24);
    font::SetFontStyle(1);
    font::SetProportional(true);
    font::SetBackground(false, false);
    font::SetOrientation(1);
    font::SetCentreSize((float)sw);
    font::SetScale(0.36f * sw / 640.0f * HudAspectFactor(), 0.82f * sh / 448.0f);
    font::SetEdge(1);
    font::SetDropColor(RGBA(0, 0, 0, (int)(255 * alpha)));
    font::SetColor(c);
    font::Print(sw * 0.03f, g_chatY, line);
    g_chatY += sh * 0.04f;
}

static void DrawChat()
{
    if (GameState() != 9 || !NetRunning()) return;
    int sw = *(int *)0xC17044, sh = *(int *)0xC17048;
    g_chatY = sh * 0.30f;
    const char *input = nullptr;
    ChatForEachLine(DrawChatLine, &input);
    if (input) {
        static const bool fr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
        char line[128];
        wsprintfA(line, "%s %s_", fr ? "Dire :" : "Say:", input);
        font::SetFontStyle(1);
        font::SetProportional(true);
        font::SetBackground(false, false);
        font::SetOrientation(1);
        font::SetCentreSize((float)sw);
        font::SetScale(0.36f * sw / 640.0f * HudAspectFactor(), 0.82f * sh / 448.0f);
        font::SetEdge(1);
        font::SetDropColor(RGBA(0, 0, 0, 255));
        font::SetColor(RGBA(255, 255, 255, 255));
        font::Print(sw * 0.03f, g_chatY + sh * 0.01f, line);
    }
    font::DrawFonts();
}

// Viseur du passager (0.54.62) : le viseur du jeu reste sur les 4 vues fixes de la camera de visee en voiture ; ce point
// montre ou vont les balles (rouge : cible verrouillee).
bool PassengerAimPoint(float *out, bool *locked);   // coop.cpp
bool PassengerAimCentered();                          // coop.cpp : visee libre, viseur au centre de l'ecran
// 0.54.68 : un seul viseur, le notre, dessine au pixel pres a l'endroit ou vont les balles (deux petits rectangles, sans
// decalage de police). Celui du jeu (CHud::DrawCrossHairs, a ecran * 0xB6EC14 / 0xB6EC10) est mis hors de l'ecran le temps
// de CHud::Draw puis remis : la camera de visee lit ces valeurs.
static float g_chairSaveX, g_chairSaveY;
static bool g_chairHidden;
static bool g_passAimDraw;      // point vise valide cette image
static float g_passAimScr[2];   // ... et sa position a l'ecran (pixels)
static bool g_passAimLocked;
static void PassengerCrosshairBegin()
{
    g_passAimDraw = false;
    float world[3];
    bool locked = false;
    if (!PassengerAimPoint(world, &locked)) return;
    float scr[3], w, h;
    int sw = *(int *)0xC17044, sh = *(int *)0xC17048;
    if (sw <= 0 || sh <= 0) return;
    if (PassengerAimCentered()) { scr[0] = sw * 0.5f; scr[1] = sh * 0.5f; scr[2] = 2.0f; }
    else if (!((bool(__cdecl *)(const float *, float *, float *, float *, bool, bool))0x70CE30)(world, scr, &w, &h, true, true) || scr[2] <= 1.0f) return;
    g_passAimScr[0] = scr[0]; g_passAimScr[1] = scr[1]; g_passAimLocked = locked; g_passAimDraw = true;
    g_chairSaveX = *(float *)0xB6EC14; g_chairSaveY = *(float *)0xB6EC10;
    *(float *)0xB6EC14 = 5.0f; *(float *)0xB6EC10 = 5.0f;
    g_chairHidden = true;
}
static void PassengerCrosshairEnd()
{
    if (g_chairHidden) { *(float *)0xB6EC14 = g_chairSaveX; *(float *)0xB6EC10 = g_chairSaveY; g_chairHidden = false; }
}

static void FillRect(float x1, float y1, float x2, float y2, uint8_t r, uint8_t g, uint8_t b)
{
    float rect[4] = { x1, y2, x2, y1 };   // CRect : gauche, bas, droite, haut
    uint8_t col[4] = { r, g, b, 255 };
    ((void(__cdecl *)(const float *, const uint8_t *))0x727B60)(rect, col);   // CSprite2d::DrawRect
}

static void DrawPassengerReticle()
{
    if (!g_passAimDraw) return;
    int sh = *(int *)0xC17048;
    float k = sh / 720.0f;
    float x = floorf(g_passAimScr[0]) + 0.5f, y = floorf(g_passAimScr[1]) + 0.5f;
    float arm = 11.0f * k, gap = 3.0f * k, t = 1.0f * k < 1.0f ? 1.0f : 1.0f * k, o = 1.0f;
    uint8_t r = g_passAimLocked ? 255 : 255, g = g_passAimLocked ? 40 : 255, b = g_passAimLocked ? 40 : 255;
    // contour noir puis croix (4 branches + point central)
    float parts[5][4] = { { x - arm, y - t, x - gap, y + t }, { x + gap, y - t, x + arm, y + t },
                          { x - t, y - arm, x + t, y - gap }, { x - t, y + gap, x + t, y + arm },
                          { x - t, y - t, x + t, y + t } };
    for (auto &q : parts) FillRect(q[0] - o, q[1] - o, q[2] + o, q[3] + o, 0, 0, 0);
    for (auto &q : parts) FillRect(q[0], q[1], q[2], q[3], r, g, b);
}

// Reperes des cibles du passager : "v" au-dessus de la tete ; rouge = verrouille, jaune = le plus proche du viseur, gris = autre.
int PassengerMarkers(float (*out)[4], int *kind, int max);   // coop.cpp
static void DrawPassengerMarkers()
{
    float pts[24][4]; int kind[24];
    int n = PassengerMarkers(pts, kind, 24);
    if (n <= 0) return;
    int sw = *(int *)0xC17044, sh = *(int *)0xC17048;
    for (int i = 0; i < n; i++) {
        float scr[3], w, h;
        if (!((bool(__cdecl *)(const float *, float *, float *, float *, bool, bool))0x70CE30)(pts[i], scr, &w, &h, true, true)) continue;
        if (scr[2] <= 1.0f || scr[0] < 0 || scr[0] > sw || scr[1] < 0 || scr[1] > sh) continue;
        float k = kind[i] == 2 ? 1.0f : kind[i] == 1 ? 0.8f : 0.5f;
        font::SetFontStyle(1);
        font::SetProportional(true);
        font::SetBackground(false, false);
        font::SetOrientation(0);
        font::SetCentreSize((float)sw);
        font::SetScale(0.7f * k * sw / 640.0f * HudAspectFactor(), 1.5f * k * sh / 448.0f);
        font::SetEdge(1);
        font::SetDropColor(RGBA(0, 0, 0, 255));
        font::SetColor(kind[i] == 2 ? RGBA(255, 40, 40, 255) : kind[i] == 1 ? RGBA(255, 220, 40, 255) : RGBA(190, 190, 190, 200));
        font::Print(scr[0], scr[1] - 18.0f * k * sh / 448.0f, "v");
        font::DrawFonts();
    }
}

static void __cdecl h_HudDraw()
{
    PassengerCrosshairBegin();
    ((void(__cdecl *)())0x58FAE0)();   // CHud::Draw
    PassengerCrosshairEnd();
    DrawNames();
    DrawPassengerReticle();
    DrawPassengerMarkers();
    DrawToasts();
    DrawChat();
    DrawPlayers();
    PanelDraw();
}

void InstallHud(void *(*puppetOf)(int))
{
    g_puppetOf = puppetOf;
    static const uint8_t call[] = { 0xE8 };
    if (memcmp((void *)0x53E4FF, call, 1) == 0) PatchCall(0x53E4FF, (void *)h_HudDraw);
    else Log("hud : appel de CHud::Draw introuvable");
}
