// Menu COOP dans le menu du jeu. Table des ecrans aScreens (0x8CE008) : 42 ecrans de 0xE2 octets {titre[8], precedent,
// premier element, 12 elements de 18 octets {action, cle GXT[8], type, cible, -, x, y, alignement, -}} ; menu
// principal 34, pause 41. FrontEndMenuManager (0xBA6748) : +0x15D ecran courant, +0x54 element choisi ;
// SwitchToNewScreen 0x573680 (thiscall, ecran ; -2 = retour) ; ProcessMenuOptions 0x576FE0 (action de l'element).
//  - Un element COOP est insere dans les menus principal et pause (action 80).
//  - La page COOP reutilise l'ecran 28 (Langue, dont le contenu est remis quand on le rouvre depuis les options) :
//    Heberger / Quitter la session, Rejoindre (adresse saisie), Pseudo (saisi), etat de la session, Retour.
//  - Textes : detour de CText::Get (0x6A0050) pour les cles "SC_*" (majuscules ASCII).
//  - Pendant une saisie, CMenuManager::UserInput (0x57FD70, il lit le clavier par DirectInput) est saute : les
//    touches ne vont qu'a la saisie (WM_CHAR, procedure de fenetre).
//  - TestMenuCoop=1 (instances de test) : la page COOP s'ouvre seule puis "Heberger" est choisi (captures).
//  - Rejoindre : l'invite se connecte puis suit tout seul la partie de l'hote (RL_SESSION, mirror.cpp).
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "hud.h"
#include "menu.h"
#include <string.h>
#include <stdio.h>

enum { SCREENS = 0x8CE008, SCREEN_SIZE = 0xE2, ITEM_SIZE = 18, MENU = 0xBA6748, PAGE_COOP = 28 };
enum : uint8_t { ACT_COOP = 80, ACT_HOST = 81, ACT_JOIN = 82, ACT_NAME = 83, ACT_STATUS = 84 };

static uint8_t *Screen(int s) { return (uint8_t *)SCREENS + s * SCREEN_SIZE; }
static uint8_t *Item(int s, int i) { return Screen(s) + 10 + i * ITEM_SIZE; }
static const bool g_fr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;

static void RestoreLanguagePageLater();
static uint8_t g_langBackup[SCREEN_SIZE];
static bool g_coopShown;

// --- Saisie (adresse, pseudo) ---
static int g_input;          // 0, ACT_JOIN ou ACT_NAME
static char g_buf[64];
static int g_len;

static void StartNet(bool host)
{
    if (NetRunning()) NetStop();
    g_cfg.host = host;
    if (NetStart()) Log("menu coop : %s", host ? "session hebergee" : "connexion a l'hote");
    else HudToast(g_fr ? "Reseau impossible (port deja pris ?)" : "Network failed (port in use?)", 4000);
}

static void SavePlayer()
{
    WritePrivateProfileStringA("SACoop", "Pseudo", g_cfg.playerName, PlayerIniPath());
    WritePrivateProfileStringA("SACoop", "Adresse", g_cfg.address, PlayerIniPath());
}

bool MenuWindowMessage(UINT msg, WPARAM wp)
{
    if (!g_input) return false;
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            if (g_input == ACT_NAME && g_len) lstrcpynA(g_cfg.playerName, g_buf, sizeof(g_cfg.playerName));
            if (g_input == ACT_JOIN && g_len) { lstrcpynA(g_cfg.address, g_buf, sizeof(g_cfg.address)); SavePlayer(); StartNet(false); }
            if (g_input == ACT_NAME) SavePlayer();
            g_input = 0;
        } else if (wp == VK_ESCAPE) {
            g_input = 0;
        } else if (wp == VK_BACK && g_len) {
            g_buf[--g_len] = 0;
        }
        return true;
    }
    if (msg == WM_CHAR) {
        bool ok = g_input == ACT_JOIN ? ((wp >= '0' && wp <= '9') || wp == '.' || (wp >= 'a' && wp <= 'z') || (wp >= 'A' && wp <= 'Z') || wp == '-')
                                      : (wp >= 32 && wp < 127);
        int max = g_input == ACT_NAME ? 20 : 60;
        if (ok && g_len < max) { g_buf[g_len++] = (char)wp; g_buf[g_len] = 0; }
        return true;
    }
    return msg >= WM_KEYFIRST && msg <= WM_KEYLAST;
}

// --- Textes ---
static char g_text[8][96];
static const char *CoopText(const char *key)
{
    static int slot;
    char *t = g_text[slot = (slot + 1) % 8];
    auto up = [](char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; };
    if (!strcmp(key, "SC_COOP") || !strcmp(key, "SC_TIT")) strcpy(t, "COOP");
    else if (!strcmp(key, "SC_HST")) strcpy(t, NetRunning() ? (g_fr ? "QUITTER LA SESSION" : "LEAVE THE SESSION") : (g_fr ? "HEBERGER" : "HOST"));
    else if (!strcmp(key, "SC_JOI")) {
        if (g_input == ACT_JOIN) sprintf(t, g_fr ? "ADRESSE : %s <" : "ADDRESS: %s <", g_buf);   // (pas de "_" dans la police des menus)
        else sprintf(t, g_fr ? "REJOINDRE %s" : "JOIN %s", g_cfg.address[0] ? g_cfg.address : "...");
    } else if (!strcmp(key, "SC_NAM")) {
        if (g_input == ACT_NAME) sprintf(t, g_fr ? "PSEUDO : %s <" : "NAME: %s <", g_buf);
        else sprintf(t, g_fr ? "PSEUDO : %s" : "NAME: %s", g_cfg.playerName);
    } else if (!strcmp(key, "SC_STA")) {
        int n = 0;
        for (int i = 0; i < MAX_PLAYERS; i++) if (g_players[i].connected || i == g_localId) n++;
        if (!NetRunning()) strcpy(t, g_fr ? "HORS LIGNE" : "OFFLINE");
        else if (g_cfg.host) sprintf(t, g_fr ? "HOTE - %d JOUEUR(S)" : "HOST - %d PLAYER(S)", n);
        else if (g_localId > 0) sprintf(t, g_fr ? "CONNECTE - %d JOUEUR(S)" : "CONNECTED - %d PLAYER(S)", n);
        else strcpy(t, g_fr ? "CONNEXION..." : "CONNECTING...");
    } else return nullptr;
    up(t);
    return t;
}

typedef const char *(__thiscall *TextGet_t)(void *, const char *);
static TextGet_t o_TextGet;
static const char *__fastcall h_TextGet(void *text, void *, const char *key)
{
    if (key && key[0] == 'S' && key[1] == 'C' && key[2] == '_')
        if (const char *t = CoopText(key)) return t;
    return o_TextGet(text, key);
}

// --- Page COOP (ecran 28) ---
static void SetItem(uint8_t *it, uint8_t action, const char *key, uint8_t target, const uint8_t *posFrom)
{
    memset(it, 0, ITEM_SIZE);
    it[0] = action;
    strncpy((char *)it + 1, key, 8);
    it[9] = 11;   // type d'element des listes du jeu
    it[10] = target;
    if (posFrom) memcpy(it + 11, posFrom + 11, ITEM_SIZE - 11);   // position / alignement d'un element d'origine
}

static void ShowCoopPage()
{
    uint8_t *s = Screen(PAGE_COOP);
    if (!g_coopShown) memcpy(g_langBackup, s, SCREEN_SIZE);
    uint8_t first[ITEM_SIZE], back[ITEM_SIZE];
    memcpy(first, g_langBackup + 10, ITEM_SIZE);
    memcpy(back, g_langBackup + 10 + 2 * ITEM_SIZE, ITEM_SIZE);
    memset(s + 10, 0, 12 * ITEM_SIZE);
    strncpy((char *)s, "SC_TIT", 8);
    s[8] = *(uint8_t *)(MENU + 0x15D);   // retour : l'ecran d'ou l'on vient (34 ou 41)
    s[9] = 0;
    SetItem(Item(PAGE_COOP, 0), ACT_HOST, "SC_HST", PAGE_COOP, first);
    SetItem(Item(PAGE_COOP, 1), ACT_JOIN, "SC_JOI", PAGE_COOP, nullptr);
    SetItem(Item(PAGE_COOP, 2), ACT_NAME, "SC_NAM", PAGE_COOP, nullptr);
    SetItem(Item(PAGE_COOP, 3), ACT_STATUS, "SC_STA", PAGE_COOP, nullptr);
    SetItem(Item(PAGE_COOP, 4), 2, "FEDS_TB", s[8], back);
    g_coopShown = true;
    ((void(__thiscall *)(void *, char))0x573680)((void *)MENU, PAGE_COOP);
}

static void RestoreLanguagePage()
{
    if (!g_coopShown) return;
    memcpy(Screen(PAGE_COOP), g_langBackup, SCREEN_SIZE);
    g_coopShown = false;
}

typedef void(__thiscall *ProcessMenuOptions_t)(void *, int8_t, bool *, bool);
static ProcessMenuOptions_t o_ProcessMenuOptions;
static void __fastcall h_ProcessMenuOptions(void *menu, void *, int8_t dir, bool *goBack, bool enter)
{
    int page = *(int8_t *)((uint8_t *)menu + 0x15D), sel = *(int *)((uint8_t *)menu + 0x54);
    if (page >= 0 && page < 42 && sel >= 0 && sel < 12) {
        uint8_t *it = Item(page, sel);
        uint8_t act = it[0];
        if (g_input) return;   // (Entree de la saisie : pas une action du menu)
        if (act == 5 && it[10] == PAGE_COOP && page != PAGE_COOP) RestoreLanguagePage();   // Langue depuis les options
        if (act >= ACT_COOP && act <= ACT_STATUS) {
            if (dir != 0 && act != ACT_COOP) return;   // fleches gauche / droite : rien
            switch (act) {
            case ACT_COOP: ShowCoopPage(); break;
            case ACT_HOST: if (NetRunning()) { NetStop(); HudToast(g_fr ? "Session quittee" : "Session left", 3000); } else StartNet(true); break;
            case ACT_JOIN: if (!NetRunning()) { g_input = ACT_JOIN; lstrcpynA(g_buf, g_cfg.address, sizeof(g_buf)); g_len = lstrlenA(g_buf); } break;
            case ACT_NAME: g_input = ACT_NAME; lstrcpynA(g_buf, g_cfg.playerName, sizeof(g_buf)); g_len = lstrlenA(g_buf); break;
            }
            return;
        }
        if (page == PAGE_COOP && act == 2) RestoreLanguagePageLater();
    }
    o_ProcessMenuOptions(menu, dir, goBack, enter);
}

// Le retour de la page COOP passe par le jeu (action 2) ; le contenu de l'ecran 28 est remis a l'image suivante.
static bool g_restorePending;
static void RestoreLanguagePageLater() { g_restorePending = true; }

typedef void(__thiscall *UserInput_t)(void *);
static UserInput_t o_UserInput;
static void __fastcall h_UserInput(void *menu, void *)
{
    if (g_input) return;   // saisie en cours : le menu n'entend rien
    o_UserInput(menu);
}

void MenuFrame()
{
    if (g_cfg.testMenuCoop && *(uint8_t *)(MENU + 0x5C) && game::GameState() == 7) {   // autotest
        static int frames;
        ++frames;
        if (frames == 300) { Log("test menu : page COOP"); ShowCoopPage(); }
        if (frames == 450) { Log("test menu : %s", g_cfg.testMenuCoop == 2 ? "Rejoindre" : "Heberger"); StartNet(g_cfg.testMenuCoop != 2); }
        if (frames == 600 && g_cfg.testMenuCoop == 1) { Log("test menu : saisie du pseudo"); g_input = ACT_NAME; lstrcpynA(g_buf, "Joueur1-CJ", sizeof(g_buf)); g_len = lstrlenA(g_buf); }
    }
    if (g_restorePending && *(int8_t *)(MENU + 0x15D) != PAGE_COOP) { g_restorePending = false; RestoreLanguagePage(); }
    if (g_input && !*(uint8_t *)(MENU + 0x5C)) g_input = 0;   // menu ferme pendant une saisie
}

// Insere un element COOP dans l'ecran s a la place index (les suivants descendent d'une place).
static void InsertCoop(int s, int index)
{
    for (int i = 11; i > index; i--) memcpy(Item(s, i), Item(s, i - 1), ITEM_SIZE);
    SetItem(Item(s, index), ACT_COOP, "SC_COOP", 0, nullptr);
    if (index == 0) { uint8_t *next = Item(s, 1); memcpy(Item(s, 0) + 11, next + 11, ITEM_SIZE - 11); memset(next + 11, 0, ITEM_SIZE - 11); }
}

void InstallMenu()
{
    static const uint8_t pmo[] = { 0x8B, 0x44, 0x24, 0x0C, 0x53 };
    static const uint8_t txt[] = { 0x83, 0xEC, 0x20, 0x56, 0x57 };
    o_ProcessMenuOptions = (ProcessMenuOptions_t)MakeDetour(0x576FE0, pmo, sizeof(pmo), (void *)h_ProcessMenuOptions);
    o_TextGet = (TextGet_t)MakeDetour(0x6A0050, txt, sizeof(txt), (void *)h_TextGet);
    static const uint8_t ui[] = { 0x83, 0xEC, 0x18, 0x56, 0x8B, 0xF1 };
    o_UserInput = (UserInput_t)MakeDetour(0x57FD70, ui, sizeof(ui), (void *)h_UserInput);
    if (!o_ProcessMenuOptions || !o_TextGet) { Log("menu coop : crochets impossibles"); return; }
    DWORD old;
    VirtualProtect((void *)SCREENS, 42 * SCREEN_SIZE, PAGE_READWRITE, &old);
    InsertCoop(34, 1);   // menu principal : Commencer, COOP, Options, Quitter
    InsertCoop(41, 5);   // pause : ... Breves, COOP, Options, Quitter
    Log("menu coop : installe (menus principal et pause)");
}
