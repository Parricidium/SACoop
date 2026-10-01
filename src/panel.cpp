// Interface en jeu, comme VCCoop (dessin moderne : ui.cpp ; vignettes 3D : thumbs.cpp).
//  - F10 : menu a la souris. Onglets JOUEURS (aller vers un joueur, tir ami), VEHICULES (voitures, deux-roues,
//    bateaux, aeriens : vignettes 3D, un clic le fait apparaitre devant soi), OUTILS (sante et gilet, armes, argent,
//    reparer, etoiles a zero, jetpack), TENUE (portraits), MONDE et HOTE (l'hote : heure, meteo, police partagee).
//  - F1 : aide (touches, bon a savoir). Bienvenue a l'arrivee en jeu : " Appuyez sur F1 ".
//  - Souris : lue a la source (DirectInput, dllmain.cpp) tant qu'un panneau est ouvert, et cachee au jeu (camera et
//    tirs immobiles) ; le clavier aussi (le jeu ne voit plus les touches) ; les messages de la fenetre sont avales.
//  - Les actions (faire apparaitre, armes...) sont mises en file pendant le dessin et faites dans la boucle du jeu.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "chat.h"
#include "hud.h"
#include "mirror.h"
#include "panel.h"
#include "combat.h"
#include "ui.h"
#include "thumbs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using namespace game;

static const bool g_fr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
static const char *L(const char *fr, const char *en) { return g_fr ? fr : en; }
static int ScreenW() { return *(int *)0xC17044; }
static int ScreenH() { return *(int *)0xC17048; }
static bool InGameNow() { return GameState() == 9 && FindPlayerPed() && !*(uint8_t *)(0xBA6748 + 0x5C); }

// ======================================================================= Etat
static bool g_open, g_help;
static int g_tab, g_vehTab;
static float g_mx = -1, g_my = -1;
static bool g_lmb, g_click;
static int g_wheel;
static float g_vehScroll[4];
static uint32_t g_welcomeUntil;
enum { T_PLAYERS, T_VEHICLES, T_TOOLS, T_OUTFIT, T_DISPLAY, T_WORLD, T_HOST, T_COUNT };

bool PanelOpen() { return g_open || g_help; }
bool PanelCapturesKeys() { return g_open || g_help || ChatTyping(); }

void PanelMouseInput(int dx, int dy, int dz, bool lmb)
{
    if (g_mx < 0) { g_mx = ScreenW() * 0.5f; g_my = ScreenH() * 0.5f; }
    float k = ScreenH() / 760.0f;
    g_mx += dx * 1.1f * k;
    g_my += dy * 1.1f * k;
    if (g_mx < 0) g_mx = 0;
    if (g_my < 0) g_my = 0;
    if (g_mx > ScreenW() - 1) g_mx = (float)ScreenW() - 1;
    if (g_my > ScreenH() - 1) g_my = (float)ScreenH() - 1;
    if (lmb && !g_lmb) g_click = true;
    g_lmb = lmb;
    if (dz > 0) g_wheel--;
    else if (dz < 0) g_wheel++;
}

// ======================================================================= Actions (boucle du jeu)
enum { A_SPAWN, A_HEAL, A_WEAPONS, A_MONEY, A_REPAIR, A_NOWANTED, A_JETPACK, A_SKIN, A_HOUR, A_WEATHER, A_GOTO, A_FRIENDLY, A_SHAREWANTED, A_HOSTPOLICE, A_DRAWDIST, A_ZONE, A_DENSITY, A_ANISO };
struct Action { int kind, arg; };
static std::vector<Action> g_actions;
static void Queue(int kind, int arg = 0) { g_actions.push_back({ kind, arg }); }

static void SaveIni(const char *key, int v)
{
    char b[16];
    wsprintfA(b, "%d", v);
    WritePrivateProfileStringA("SACoop", key, b, IniPath());
}

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

// Vehicules du jeu (data\vehicles.ide, section cars) : modele, nom, categorie.
struct VehEntry { int id; char model[24], name[24]; int cat; };   // cat : 0 voitures, 1 deux-roues, 2 bateaux, 3 aeriens
static std::vector<VehEntry> g_vehList;
static void LoadVehicleList()
{
    static bool done;
    if (done) return;
    done = true;
    char path[MAX_PATH];
    wsprintfA(path, "%sdata\\vehicles.ide", GameDir());
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    bool in = false;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!in) { if (!_strnicmp(p, "cars", 4)) in = true; continue; }
        if (!_strnicmp(p, "end", 3)) break;
        if (*p == '#' || *p < '0' || *p > '9') continue;
        char fields[8][32] = {};
        int n = 0;
        for (char *tok = strtok(p, ",\r\n"); tok && n < 8; tok = strtok(NULL, ",\r\n"), n++) {
            while (*tok == ' ' || *tok == '\t') tok++;
            lstrcpynA(fields[n], tok, 32);
            for (char *e = fields[n] + lstrlenA(fields[n]) - 1; e >= fields[n] && (*e == ' ' || *e == '\t'); e--) *e = 0;
        }
        if (n < 4) continue;
        const char *type = fields[3];
        int cat = !_stricmp(type, "car") || !_stricmp(type, "mtruck") || !_stricmp(type, "quad") ? 0
                : !_stricmp(type, "bike") || !_stricmp(type, "bmx") ? 1 : !_stricmp(type, "boat") ? 2
                : !_stricmp(type, "heli") || !_stricmp(type, "plane") ? 3 : -1;
        if (cat < 0 || !_strnicmp(fields[1], "rc", 2)) continue;   // (trains, remorques, modeles reduits)
        VehEntry e = { atoi(fields[0]), {}, {}, cat };
        lstrcpynA(e.model, fields[1], 24);
        lstrcpynA(e.name, fields[1], 24);
        e.name[0] = (char)toupper((unsigned char)e.name[0]);
        g_vehList.push_back(e);
    }
    fclose(f);
    Log("menu jeu : %d vehicules", (int)g_vehList.size());
}

static void SpawnVehicle(int model)
{
    void *me = FindPlayerPed();
    if (!me || PedVehicle(me)) { HudToast(L("Descendez d'abord du vehicule", "Get out of the vehicle first"), 3000); return; }
    if (!ModelLoaded(model)) { RequestModel(model, 2); LoadAllRequestedModels(false); }
    if (!ModelLoaded(model)) return;
    const float *p = EntityPos(me);
    float h = Field<float>(me, PED_ROTATION), d = 5.0f;
    float pos[3] = { p[0] - sinf(h) * d, p[1] + cosf(h) * d, p[2] + 0.5f };
    void *v = ((void *(__cdecl *)(int, float, float, float, bool))0x431F80)(model, pos[0], pos[1], pos[2], false);
    if (!v) return;
    if (uint8_t *mat = *(uint8_t **)((uint8_t *)v + 0x14)) {   // de travers devant soi
        float a = h + 1.5708f, *r = (float *)mat, *f = (float *)(mat + 0x10);
        f[0] = -sinf(a); f[1] = cosf(a); f[2] = 0;
        r[0] = cosf(a); r[1] = sinf(a); r[2] = 0;
    }
    EntityArea(v) = EntityArea(me);
    Log("menu jeu : vehicule %d pose devant le joueur", model);
}

void PanelFrame()
{
    std::vector<Action> todo;
    todo.swap(g_actions);
    void *me = FindPlayerPed();
    if (!me || GameState() != 9) return;
    for (const Action &a : todo) {
        switch (a.kind) {
        case A_SPAWN: SpawnVehicle(a.arg); break;
        case A_HEAL: Field<float>(me, PED_HEALTH) = 100.0f; Field<float>(me, PED_ARMOUR) = 100.0f; HudToast(L("Sante et gilet au maximum", "Health and armour full"), 2500); break;
        case A_WEAPONS: {
            static const int kit[] = { 16, 34, 24, 25, 29, 31 };   // grenades, sniper, Desert Eagle, fusil a pompe, MP5, M4 (en main)
            for (int w : kit) { int cache = -1; EnsurePedWeapon(me, w, cache); }
            HudToast(L("Armes donnees", "Weapons given"), 2500);
            break;
        }
        case A_MONEY: { int args[2] = { 0, 10000 }; RunScriptCommand(0x0109, 2, args); break; }   // ADD_SCORE
        case A_REPAIR:
            if (void *v = PedVehicle(me)) { ((void(__thiscall *)(void *))(*(void ***)v)[50])(v); *(float *)((uint8_t *)v + 0x4C0) = 1000.0f; HudToast(L("Vehicule repare", "Vehicle repaired"), 2500); }
            else HudToast(L("Montez dans un vehicule", "Get in a vehicle"), 2500);
            break;
        case A_NOWANTED: { int args[2] = { 0, 0 }; RunScriptCommand(0x010D, 2, args); break; }   // SET_PLAYER_WANTED_LEVEL 0
        case A_JETPACK: { int ref = PedRef(me); RunScriptCommand(0x07A7, 1, &ref); break; }        // TASK_JETPACK
        case A_SKIN: g_cfg.skin = a.arg; SaveIni("Tenue", g_cfg.skin); break;
        case A_HOUR: *(uint8_t *)0xB70153 = (uint8_t)a.arg; *(uint8_t *)0xB70152 = 0; *(uint32_t *)0xB70158 = *(uint32_t *)0xB7CB84; break;
        case A_WEATHER: if (a.arg < 0) RunScriptCommand(0x01B7, 0, nullptr); else { int w = a.arg; RunScriptCommand(0x01B6, 1, &w); } break;
        case A_GOTO: if (GoToPlayer(a.arg)) g_open = false; break;
        case A_FRIENDLY: g_cfg.friendlyFire = !g_cfg.friendlyFire; SaveIni("TirAmi", g_cfg.friendlyFire); break;
        case A_SHAREWANTED: g_cfg.shareWanted = !g_cfg.shareWanted; SaveIni("RecherchePartagee", g_cfg.shareWanted); break;
        case A_HOSTPOLICE: g_cfg.hostPolice = !g_cfg.hostPolice; SaveIni("PoliceHote", g_cfg.hostPolice); break;
        case A_DRAWDIST: g_cfg.drawDistance = a.arg; SaveIni("DistanceAffichage", a.arg); break;   // (gfx.cpp : lu a chaque image)
        case A_ZONE: g_cfg.zonePop = a.arg; SaveIni("ZonePopulation", a.arg); break;
        case A_DENSITY: g_cfg.popDensity = a.arg; SaveIni("DensitePopulation", a.arg); break;
        case A_ANISO: g_cfg.aniso = !g_cfg.aniso; SaveIni("FiltrageAnisotrope", g_cfg.aniso); break;
        }
    }
    // Bienvenue : la premiere fois en jeu
    static bool welcomed;
    if (!welcomed && InGameNow() && !*(uint8_t *)0xB5F851) { welcomed = true; g_welcomeUntil = GetTickCount() + 10000; }
}

// ======================================================================= Touches (fenetre)
bool PanelWindowMessage(UINT msg, WPARAM wp)
{
    bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN, keyMsg = msg >= WM_KEYFIRST && msg <= WM_KEYLAST;
    if (ChatTyping()) return false;
    if (!InGameNow()) { g_open = g_help = false; return keyMsg && wp == VK_F10; }   // F10 : jamais la barre de menus de Windows
    if (keyMsg && wp == VK_F10) {
        if (down) { g_open = !g_open; g_help = false; if (g_open && g_mx < 0) { g_mx = ScreenW() * 0.5f; g_my = ScreenH() * 0.5f; } Log("menu jeu : %s", g_open ? "ouvert" : "ferme"); }
        return true;
    }
    if (keyMsg && wp == VK_F1) {
        if (down) { g_help = !g_help; g_open = false; Log("aide : %s", g_help ? "ouverte" : "fermee"); }
        return true;
    }
    if (g_open || g_help) {
        if (down && wp == VK_ESCAPE) g_open = g_help = false;
        if (down && wp == 'T') { g_open = g_help = false; return false; }   // T : le tchat (chat.cpp) prend la main
        return keyMsg || msg == WM_CHAR;
    }
    return false;
}

// ======================================================================= Dessin
static const uint32_t C_INK = 0xF2F2F2FF, C_GREY = 0xA8A8A8FF, C_DIM = 0x707070FF, C_DARK = 0x111111FF;
static float K() { return ScreenH() / 760.0f; }
static bool Hover(float x0, float y0, float x1, float y1) { return g_mx >= x0 && g_mx < x1 && g_my >= y0 && g_my < y1; }

static bool Button(float x0, float y0, float x1, float y1, const char *label, bool active = false, bool enabled = true, float px = 0)
{
    float k = K();
    bool hot = enabled && Hover(x0, y0, x1, y1);
    if (active) UiRect(x0, y0, x1, y1, 9 * k, 0xFFFFFFFF, 0xDDDDDDFF);
    else UiRect(x0, y0, x1, y1, 9 * k, hot ? 0xFFFFFF30 : 0xFFFFFF14, hot ? 0xFFFFFF26 : 0xFFFFFF0C, hot ? 0xFFFFFF90 : 0xFFFFFF22, 1.2f * k);
    UiText((x0 + x1) * 0.5f, (y0 + y1) * 0.5f - (px ? px : 14 * k) * 0.62f, px ? px : 14 * k, active ? C_DARK : enabled ? C_INK : C_DIM, UI_CENTER, label, true);
    bool clicked = hot && g_click;
    if (clicked) g_click = false;
    return clicked;
}

static void Toggle(float x0, float y, float x1, const char *label, bool on, int action)
{
    float k = K();
    UiText(x0, y, 14 * k, C_INK, UI_LEFT, label);
    float tx1 = x1, tx0 = tx1 - 46 * k, ty0 = y - 1 * k, ty1 = y + 21 * k;
    if (Button(tx0 - 70 * k, ty0, tx1, ty1, on ? L("OUI", "ON") : L("NON", "OFF"), on)) Queue(action);
}

static void PlayersTab(float x0, float y0, float x1, float y1)
{
    float k = K(), y = y0;
    UiText(x0, y, 12.5f * k, C_GREY, UI_LEFT, L("Cliquez sur \xAB" " Aller vers \xBB" " pour vous retrouver \xE0" " c\xF4" "t\xE9" " d'un joueur (ou dans sa voiture).",
                                                 "Click \"Go to\" to appear next to a player (or in their car)."));
    y += 26 * k;
    void *me = FindPlayerPed();
    for (int i = 0; i < MAX_PLAYERS; i++) {
        bool self = i == g_localId || (g_localId < 0 && i == 0);
        const NetPlayer &p = g_players[i];
        if (!self && !p.connected) continue;
        UiRect(x0, y, x1, y + 48 * k, 10 * k, 0xFFFFFF10, 0xFFFFFF08, 0xFFFFFF20, 1 * k);
        char name[64];
        sprintf(name, "%s%s", self ? g_cfg.playerName : (p.state.name[0] ? p.state.name : "?"), i == 0 ? L("  (h\xF4" "te)", "  (host)") : "");
        UiText(x0 + 16 * k, y + 6 * k, 16 * k, C_INK, UI_LEFT, name, true);
        char info[96];
        if (self) sprintf(info, "%s", L("vous", "you"));
        else if (!p.state.inGame) sprintf(info, "%s", L("au menu", "in the menu"));
        else {
            const float *a = me ? EntityPos(me) : p.state.pos, *b = p.state.pos;
            float d = sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
            sprintf(info, L("%.0f m  -  vie %.0f", "%.0f m  -  health %.0f"), d, p.state.health);
        }
        UiText(x0 + 16 * k, y + 27 * k, 12.5f * k, C_GREY, UI_LEFT, info);
        if (!self && p.state.inGame && Button(x1 - 150 * k, y + 9 * k, x1 - 12 * k, y + 39 * k, L("ALLER VERS", "GO TO"))) Queue(A_GOTO, i);
        y += 56 * k;
    }
    y += 10 * k;
    Toggle(x0 + 4 * k, y, x1, L("Tir ami (les autres joueurs peuvent vous blesser)", "Friendly fire (other players can hurt you)"), g_cfg.friendlyFire, A_FRIENDLY);
}

static void VehiclesTab(float x0, float y0, float x1, float y1)
{
    LoadVehicleList();
    float k = K();
    static const char *catFr[] = { "VOITURES", "DEUX-ROUES", "BATEAUX", "A\xC9" "RIENS" }, *catEn[] = { "CARS", "BIKES", "BOATS", "AIR" };
    float tw = (x1 - x0 - 3 * 8 * k) / 4;
    for (int c = 0; c < 4; c++)
        if (Button(x0 + c * (tw + 8 * k), y0, x0 + c * (tw + 8 * k) + tw, y0 + 30 * k, (g_fr ? catFr : catEn)[c], g_vehTab == c)) g_vehTab = c;
    float y = y0 + 40 * k;
    UiText(x0, y, 12.5f * k, C_GREY, UI_LEFT, L("Cliquez : le v\xE9" "hicule appara\xEE" "t devant vous, tout le monde le voit. Molette : d\xE9" "filer.",
                                                 "Click: the vehicle appears in front of you, everybody sees it. Wheel: scroll."));
    y += 22 * k;
    std::vector<const VehEntry *> list;
    for (auto &e : g_vehList) if (e.cat == g_vehTab) list.push_back(&e);
    const int cols = 4, rows = 2;
    int pages = ((int)list.size() + cols * rows - 1) / (cols * rows);
    float &scroll = g_vehScroll[g_vehTab];
    if (Hover(x0, y, x1, y1) && g_wheel) { scroll += g_wheel; g_wheel = 0; }
    if (scroll < 0) scroll = 0;
    if (pages > 0 && scroll > pages - 1) scroll = (float)(pages - 1);
    int first = (int)scroll * cols * rows;
    float cw = (x1 - x0 - (cols - 1) * 8 * k) / cols, ch = (y1 - y - 22 * k - (rows - 1) * 8 * k) / rows;
    for (int i = 0; i < cols * rows && first + i < (int)list.size(); i++) {
        const VehEntry *e = list[first + i];
        float cx0 = x0 + (i % cols) * (cw + 8 * k), cy0 = y + (i / cols) * (ch + 8 * k), cx1 = cx0 + cw, cy1 = cy0 + ch;
        bool hot = Hover(cx0, cy0, cx1, cy1);
        UiRect(cx0, cy0, cx1, cy1, 10 * k, hot ? 0xFFFFFF22 : 0xFFFFFF10, 0xFFFFFF08, hot ? 0xFFFFFFC0 : 0xFFFFFF20, hot ? 1.8f * k : 1 * k);
        float uv[4], aspect = 1.6f;
        if (ThumbGet(THUMB_VEHICLE, e->model, uv, &aspect)) {
            float ih = ch - 30 * k, iw = ih * aspect;
            if (iw > cw - 12 * k) { iw = cw - 12 * k; ih = iw / aspect; }
            float ix = (cx0 + cx1) * 0.5f - iw * 0.5f, iy = cy0 + 6 * k + (ch - 30 * k - ih) * 0.5f;
            UiImage(ix, iy, ix + iw, iy + ih, uv);
        }
        UiText((cx0 + cx1) * 0.5f, cy1 - 22 * k, 13 * k, C_INK, UI_CENTER, e->name, true);
        if (hot && g_click) { g_click = false; Queue(A_SPAWN, e->id); g_open = false; }
    }
    char pg[32];
    sprintf(pg, "%d / %d", pages ? (int)scroll + 1 : 0, pages);
    UiText(x1, y1 - 16 * k, 12 * k, C_GREY, UI_RIGHT, pg);
}

static void ToolsTab(float x0, float y0, float x1, float y1)
{
    float k = K();
    struct { const char *fr, *en; int action; } tools[] = {
        { "SANT\xC9" " + GILET", "HEALTH + ARMOUR", A_HEAL }, { "ARMES", "WEAPONS", A_WEAPONS }, { "+10 000 $", "+10,000 $", A_MONEY },
        { "R\xC9" "PARER LE V\xC9" "HICULE", "REPAIR VEHICLE", A_REPAIR }, { "\xC9" "TOILES \xC0" " Z\xC9" "RO", "CLEAR WANTED LEVEL", A_NOWANTED }, { "JETPACK", "JETPACK", A_JETPACK } };
    float bw = (x1 - x0 - 2 * 10 * k) / 3, bh = 42 * k;
    for (int i = 0; i < 6; i++) {
        float bx = x0 + (i % 3) * (bw + 10 * k), by = y0 + (i / 3) * (bh + 10 * k);
        if (Button(bx, by, bx + bw, by + bh, g_fr ? tools[i].fr : tools[i].en)) { Queue(tools[i].action); if (tools[i].action == A_JETPACK) g_open = false; }
    }
    UiText(x0, y0 + 2 * (bh + 10 * k) + 10 * k, 12.5f * k, C_GREY, UI_LEFT,
           L("Armes : Desert Eagle, fusil \xE0" " pompe, MP5, M4, fusil de pr\xE9" "cision, grenades.", "Weapons: Desert Eagle, shotgun, MP5, M4, sniper rifle, grenades."));
}

static const int kSkinIds[] = { 0, 105, 106, 107, 102, 103, 104, 108, 109, 110, 114, 115, 116 };
static const char *const kSkinModels[] = { "player", "fam1", "fam2", "fam3", "ballas1", "ballas2", "ballas3", "lsv1", "lsv2", "lsv3", "vla1", "vla2", "vla3" };
static const char *const kSkinNames[] = { "CJ", "Grove 1", "Grove 2", "Grove 3", "Ballas 1", "Ballas 2", "Ballas 3", "Vagos 1", "Vagos 2", "Vagos 3", "Aztecas 1", "Aztecas 2", "Aztecas 3" };

static void OutfitTab(float x0, float y0, float x1, float y1)
{
    float k = K();
    UiText(x0, y0, 12.5f * k, C_GREY, UI_LEFT, L("Ce que les autres joueurs voient (CJ : avec vos v\xEA" "tements, magasins compris).",
                                                  "What the other players see (CJ: with your clothes, shops included)."));
    const int cols = 7;
    float cw = (x1 - x0 - (cols - 1) * 8 * k) / cols, ch = cw * 1.25f, y = y0 + 24 * k;
    for (int i = 0; i < 13; i++) {
        float cx0 = x0 + (i % cols) * (cw + 8 * k), cy0 = y + (i / cols) * (ch + 8 * k), cx1 = cx0 + cw, cy1 = cy0 + ch;
        bool sel = g_cfg.skin == kSkinIds[i], hot = Hover(cx0, cy0, cx1, cy1);
        UiRect(cx0, cy0, cx1, cy1, 10 * k, sel ? 0xFFFFFF30 : 0xFFFFFF10, 0xFFFFFF08, sel ? 0xFFFFFFFF : hot ? 0xFFFFFF90 : 0xFFFFFF20, sel ? 2.2f * k : 1 * k);
        float uv[4], aspect = 0.66f;
        if (ThumbGet(THUMB_PED, kSkinModels[i], uv, &aspect)) {
            float ih = ch - 24 * k, iw = ih * aspect;
            if (iw > cw - 6 * k) { iw = cw - 6 * k; ih = iw / aspect; }
            UiImage((cx0 + cx1) * 0.5f - iw * 0.5f, cy0 + 4 * k, (cx0 + cx1) * 0.5f + iw * 0.5f, cy0 + 4 * k + ih, uv);
        }
        UiText((cx0 + cx1) * 0.5f, cy1 - 19 * k, 12 * k, C_INK, UI_CENTER, kSkinNames[i], true);
        if (hot && g_click) { g_click = false; Queue(A_SKIN, kSkinIds[i]); }
    }
}

// Reglages graphiques du jeu d'origine (gfx.cpp), pour chaque joueur, appliques tout de suite.
static void DisplayTab(float x0, float y0, float x1, float y1)
{
    float k = K(), y = y0;
    struct Row { const char *fr, *en; int action; int vals[5]; int cur; } rows[] = {
        { "DISTANCE D'AFFICHAGE", "DRAW DISTANCE", A_DRAWDIST, { 100, 125, 150, 200, 250 }, g_cfg.drawDistance },
        { "ZONE DE POPULATION", "POPULATION AREA", A_ZONE, { 100, 125, 150, 175, 200 }, g_cfg.zonePop },
        { "DENSIT\xC9" " DE POPULATION", "POPULATION DENSITY", A_DENSITY, { 50, 100, 150, 200, 300 }, g_cfg.popDensity } };
    float bw = (x1 - x0 - 4 * 8 * k) / 5;
    for (auto &r : rows) {
        UiText(x0, y, 13 * k, C_GREY, UI_LEFT, g_fr ? r.fr : r.en, true);
        y += 22 * k;
        for (int i = 0; i < 5; i++) {
            char t[16];
            sprintf(t, "%d %%", r.vals[i]);
            float bx = x0 + i * (bw + 8 * k);
            if (Button(bx, y, bx + bw, y + 32 * k, t, r.cur == r.vals[i])) Queue(r.action, r.vals[i]);
        }
        y += 48 * k;
    }
    Toggle(x0, y + 4 * k, x1, L("Filtrage anisotrope (textures nettes de loin et de biais)", "Anisotropic filtering (sharp textures far away and at an angle)"), g_cfg.aniso, A_ANISO);
    UiText(x0, y + 46 * k, 12.5f * k, C_GREY, UI_LEFT, L("100 % = jeu d'origine. Appliqu\xE9" " tout de suite, retenu pour les prochaines parties.",
                                                          "100% = original game. Applied right away, kept for the next sessions."));
}

static void WorldTab(float x0, float y0, float x1, float y1)
{
    float k = K(), y = y0;
    char t[48];
    sprintf(t, L("HEURE : %02d:%02d", "TIME: %02d:%02d"), *(uint8_t *)0xB70153, *(uint8_t *)0xB70152);
    UiText(x0, y, 13 * k, C_GREY, UI_LEFT, t, true);
    y += 22 * k;
    static const int hours[] = { 6, 9, 12, 16, 20, 0 };
    float bw = (x1 - x0 - 5 * 8 * k) / 6;
    for (int i = 0; i < 6; i++) {
        char h[8];
        sprintf(h, "%dH", hours[i]);
        if (Button(x0 + i * (bw + 8 * k), y, x0 + i * (bw + 8 * k) + bw, y + 32 * k, h)) Queue(A_HOUR, hours[i]);
    }
    y += 50 * k;
    UiText(x0, y, 13 * k, C_GREY, UI_LEFT, L("M\xC9" "T\xC9" "O", "WEATHER"), true);
    y += 22 * k;
    static const int weather[] = { -1, 1, 4, 8, 9, 19 };
    static const char *wFr[] = { "AUTO", "SOLEIL", "NUAGES", "PLUIE", "BROUILLARD", "TEMP\xCA" "TE DE SABLE" }, *wEn[] = { "AUTO", "SUN", "CLOUDS", "RAIN", "FOG", "SANDSTORM" };
    float ww = (x1 - x0 - 2 * 8 * k) / 3;
    for (int i = 0; i < 6; i++) {
        float bx = x0 + (i % 3) * (ww + 8 * k), by = y + (i / 3) * 40 * k;
        if (Button(bx, by, bx + ww, by + 32 * k, (g_fr ? wFr : wEn)[i])) Queue(A_WEATHER, weather[i]);
    }
    UiText(x0, y + 90 * k, 12.5f * k, C_GREY, UI_LEFT, L("L'heure et la m\xE9" "t\xE9" "o de l'h\xF4" "te valent pour tout le monde.", "The host's time and weather apply to everybody."));
}

static void HostTab(float x0, float y0, float x1, float y1)
{
    float k = K(), y = y0;
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) n += i == 0 || g_players[i].connected;
    char s[64];
    sprintf(s, L("Partie : %d / 4 joueurs  -  port %d", "Session: %d / 4 players  -  port %d"), n, g_cfg.port);
    UiText(x0, y, 14 * k, C_INK, UI_LEFT, s, true);
    y += 34 * k;
    Toggle(x0, y, x1, L("Recherche partag\xE9" "e (un seul niveau de recherche pour tous)", "Shared wanted level (one level for everybody)"), g_cfg.shareWanted, A_SHAREWANTED);
    y += 40 * k;
    Toggle(x0, y, x1, L("Police de l'h\xF4" "te (elle poursuit aussi les invit\xE9" "s)", "Host's police (also chases the guests)"), g_cfg.hostPolice, A_HOSTPOLICE);
}

static void DrawMenu()
{
    float k = K(), sw = (float)ScreenW(), sh = (float)ScreenH();
    float w = 800 * k, h = 500 * k, x0 = sw * 0.5f - w * 0.5f, y0 = sh * 0.5f - h * 0.5f, x1 = x0 + w, y1 = y0 + h;
    UiShadow(x0, y0, x1, y1, 18 * k, 28 * k, 0x00000070);
    UiGlass(x0, y0, x1, y1, 18 * k, 0x0C0C0EC8, 0xFFFFFF38, 1.4f * k);
    UiText(x0 + 22 * k, y0 + 14 * k, 18 * k, C_INK, UI_LEFT, "SACOOP", true);
    char who[64];
    sprintf(who, "%s  -  %s", g_cfg.playerName, g_cfg.host ? L("h\xF4" "te", "host") : L("invit\xE9" "", "guest"));
    UiText(x0 + 140 * k, y0 + 18 * k, 13 * k, C_GREY, UI_LEFT, who);
    if (Button(x1 - 44 * k, y0 + 12 * k, x1 - 14 * k, y0 + 40 * k, "X")) g_open = false;
    UiRect(x0 + 16 * k, y0 + 48 * k, x1 - 16 * k, y0 + 50 * k, 1 * k, 0xFFFFFF70, 0xFFFFFF70);
    static const char *tabFr[] = { "JOUEURS", "V\xC9" "HICULES", "OUTILS", "TENUE", "AFFICHAGE", "MONDE", "H\xD4" "TE" }, *tabEn[] = { "PLAYERS", "VEHICLES", "TOOLS", "OUTFIT", "DISPLAY", "WORLD", "HOST" };
    int tabs = g_cfg.host ? T_COUNT : T_WORLD;   // (MONDE et HOTE : l'hote)
    if (g_tab >= tabs) g_tab = 0;
    float tw = (w - 32 * k - (tabs - 1) * 8 * k) / tabs;
    for (int t = 0; t < tabs; t++) {
        float bx = x0 + 16 * k + t * (tw + 8 * k);
        if (Button(bx, y0 + 60 * k, bx + tw, y0 + 94 * k, (g_fr ? tabFr : tabEn)[t], g_tab == t)) g_tab = t;
    }
    float cx0 = x0 + 16 * k, cy0 = y0 + 108 * k, cx1 = x1 - 16 * k, cy1 = y1 - 34 * k;
    switch (g_tab) {
    case T_PLAYERS: PlayersTab(cx0, cy0, cx1, cy1); break;
    case T_VEHICLES: VehiclesTab(cx0, cy0, cx1, cy1); break;
    case T_TOOLS: ToolsTab(cx0, cy0, cx1, cy1); break;
    case T_OUTFIT: OutfitTab(cx0, cy0, cx1, cy1); break;
    case T_DISPLAY: DisplayTab(cx0, cy0, cx1, cy1); break;
    case T_WORLD: WorldTab(cx0, cy0, cx1, cy1); break;
    case T_HOST: HostTab(cx0, cy0, cx1, cy1); break;
    }
    UiText((x0 + x1) * 0.5f, y1 - 24 * k, 12 * k, C_GREY, UI_CENTER, L("\xC9" "chap ou F10 : fermer      F1 : aide      T : tchat", "Esc or F10: close      F1: help      T: chat"));
}

static void DrawHelp()
{
    float k = K(), sw = (float)ScreenW(), sh = (float)ScreenH();
    float w = 640 * k, h = 470 * k, x0 = sw * 0.5f - w * 0.5f, y0 = sh * 0.5f - h * 0.5f, x1 = x0 + w, y1 = y0 + h;
    UiShadow(x0, y0, x1, y1, 18 * k, 28 * k, 0x00000070);
    UiGlass(x0, y0, x1, y1, 18 * k, 0x0C0C0EC8, 0xFFFFFF38, 1.4f * k);
    UiText(x0 + 22 * k, y0 + 14 * k, 18 * k, C_INK, UI_LEFT, L("AIDE SACOOP", "SACOOP HELP"), true);
    if (Button(x1 - 44 * k, y0 + 12 * k, x1 - 14 * k, y0 + 40 * k, "X")) g_help = false;
    UiRect(x0 + 16 * k, y0 + 48 * k, x1 - 16 * k, y0 + 50 * k, 1 * k, 0xFFFFFF70, 0xFFFFFF70);
    UiText(x0 + 22 * k, y0 + 60 * k, 12.5f * k, C_GREY, UI_LEFT, L("TOUCHES", "KEYS"), true);
    struct { const char *key, *fr, *en; } keys[] = {
        { "F10", "Menu : joueurs, v\xE9" "hicules, outils, tenue, affichage, monde", "Menu: players, vehicles, tools, outfit, display, world" },
        { "T", "Tchat (Entr\xE9" "e pour envoyer, \xC9" "chap pour annuler)", "Chat (Enter to send, Esc to cancel)" },
        { "F5", "Tableau des joueurs (touche maintenue)", "Players board (held)" },
        { "F6", "Vue \xE0" " la premi\xE8" "re personne", "First-person view" },
        { "G / F", "Monter en passager dans la voiture d'un joueur", "Ride as a passenger in a player's car" },
        { "F1", "Cette aide", "This help" } };
    float y = y0 + 84 * k;
    for (auto &r : keys) {
        UiRect(x0 + 22 * k, y, x0 + 92 * k, y + 24 * k, 6 * k, 0xFFFFFF22, 0xFFFFFF14);
        UiText(x0 + 57 * k, y + 4 * k, 12.5f * k, C_INK, UI_CENTER, r.key, true);
        UiText(x0 + 104 * k, y + 4 * k, 13 * k, C_INK, UI_LEFT, g_fr ? r.fr : r.en);
        y += 30 * k;
    }
    y += 10 * k;
    UiText(x0 + 22 * k, y, 12.5f * k, C_GREY, UI_LEFT, L("BON \xC0" " SAVOIR", "GOOD TO KNOW"), true);
    y += 22 * k;
    static const char *tipsFr[] = { "Les missions de l'histoire tournent chez l'h\xF4" "te : tout le monde les joue avec lui.",
                                    "Dans le tchat, /rejoindre vous ram\xE8" "ne pr\xE8" "s de l'h\xF4" "te (ou dans sa voiture).",
                                    "Quand l'h\xF4" "te prend un v\xE9" "hicule de mission, les invit\xE9" "s proches en re\xE7" "oivent un.",
                                    "La sauvegarde de l'h\xF4" "te est envoy\xE9" "e \xE0" " tous les joueurs." };
    static const char *tipsEn[] = { "Story missions run on the host: everybody plays them with him.",
                                    "In the chat, /join brings you back next to the host (or in his car).",
                                    "When the host takes a mission vehicle, nearby guests get one too.",
                                    "The host's save is sent to every player." };
    for (int i = 0; i < 4; i++) { UiText(x0 + 22 * k, y, 13 * k, C_INK, UI_LEFT, (g_fr ? tipsFr : tipsEn)[i]); y += 22 * k; }
    UiText((x0 + x1) * 0.5f, y1 - 24 * k, 12 * k, C_GREY, UI_CENTER, L("F1 ou \xC9" "chap : fermer", "F1 or Esc: close"));
}

static void DrawWelcome()
{
    if (!g_welcomeUntil || GetTickCount() > g_welcomeUntil || g_open || g_help) return;
    float k = K(), sw = (float)ScreenW();
    const char *s = L("Bienvenue dans SACOOP !   Appuyez sur F1 pour ouvrir l'aide", "Welcome to SACOOP!   Press F1 to open the help");
    float w = UiTextWidth(s, 14 * k, true) + 40 * k, x0 = sw * 0.5f - w * 0.5f, y0 = ScreenH() * 0.2f;
    UiRect(x0, y0, x0 + w, y0 + 32 * k, 16 * k, 0x101012D8, 0x101012D8, 0xFFFFFFB0, 1.5f * k);
    UiText(sw * 0.5f, y0 + 7 * k, 14 * k, C_INK, UI_CENTER, s, true);
}

static void DrawCursor()
{
    float k = K(), x = g_mx, y = g_my;
    UiTri(x - 1.5f * k, y - 2 * k, x - 1.5f * k, y + 20 * k, x + 15 * k, y + 14 * k, 0x000000C0);
    UiTri(x, y, x, y + 17 * k, x + 12 * k, y + 12 * k, 0xFFFFFFFF);
}

// Appele apres l'interface du jeu (render.cpp, apres Render2dStuff).
void PanelRender()
{
    if (!UiReady()) return;
    if ((g_open || g_help) && !InGameNow()) g_open = g_help = false;
    bool any = g_open || g_help || (g_welcomeUntil && GetTickCount() < g_welcomeUntil);
    if (!any) { g_click = false; return; }
    if (g_mx < 0) { g_mx = ScreenW() * 0.5f; g_my = ScreenH() * 0.5f; }
    ThumbFrame();
    UiBegin();
    DrawWelcome();
    if (g_open) DrawMenu();
    else if (g_help) DrawHelp();
    if (g_open || g_help) DrawCursor();
    UiEnd();
    g_click = false;
    g_wheel = 0;
}

void PanelDraw() {}   // (ancien panneau CFont : remplace par PanelRender)

// Autotest : TestMenuJeu=1 (onglets toutes les 4 s), 2 (aide), 3 (vehicules : un clic sur le premier).
void PanelTest()
{
    static int mode = -1;
    if (mode < 0) mode = GetPrivateProfileIntA("SACoop", "TestMenuJeu", 0, IniPath());
    if (mode <= 0 || !InGameNow()) return;
    static uint32_t start, step;
    uint32_t now = GetTickCount();
    if (!start) start = now;
    uint32_t t = now - start;
    if (mode == 2) { if (step == 0 && t > 25000) { step = 1; g_help = true; Log("test menu : aide"); } return; }
    if (step == 0 && t > 25000) { step = 1; g_open = true; g_tab = mode == 3 ? T_VEHICLES : T_PLAYERS; g_mx = ScreenW() * 0.47f; g_my = ScreenH() * 0.45f; Log("test menu : ouvert"); }
    else if (mode == 3 && step == 1 && t > 29000) { step = 2; g_click = true; Log("test menu : clic sur une vignette"); }
    else if (mode == 1 && step >= 1 && step < 8 && t > 25000 + step * 4000) { g_tab = (int)step % (g_cfg.host ? T_COUNT : T_WORLD); step++; Log("test menu : onglet %d", g_tab); }
    else if (mode == 1 && step == 8 && t > 25000 + 8 * 4000) { step = 9; g_open = false; Log("test menu : ferme"); }
}
