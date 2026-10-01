// Tchat. T ouvre la saisie (en partie, hors menu), Entree envoie, Echap annule. Pendant la saisie, les touches vont au
// tchat (WM_KEYDOWN / WM_KEYUP / WM_CHAR avales dans la procedure de fenetre, window.cpp) : CJ ne bouge pas.
// Les messages passent par le flux fiable (RL_CHAT) : un invite envoie a l'hote, qui l'affiche et le relaie aux
// autres invites. Affichage : les 8 derniers messages a gauche de l'ecran, 15 s chacun ; la ligne de saisie dessous.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "chat.h"
#include <string.h>

enum : uint8_t { RL_CHAT = 20 };
enum { MAX_TEXT = 90, LINES = 8 };

struct Line { int player; char name[24]; char text[MAX_TEXT + 1]; uint32_t at; };
static Line g_lines[LINES];
static int g_next;
static bool g_typing, g_skipChar;
static char g_input[MAX_TEXT + 1];
static int g_len;

bool ChatTyping() { return g_typing; }

static void AddLine(int player, const char *name, const char *text)
{
    Line &l = g_lines[g_next];
    g_next = (g_next + 1) % LINES;
    l.player = player;
    lstrcpynA(l.name, name, sizeof(l.name));
    lstrcpynA(l.text, text, sizeof(l.text));
    l.at = GetTickCount();
    Log("tchat : %s : %s", name, text);
}

static const char *PlayerName(int id)
{
    if (id == g_localId) return g_cfg.playerName;
    return id >= 0 && id < MAX_PLAYERS && g_players[id].state.name[0] ? g_players[id].state.name : "?";
}

static void Send(const char *text)
{
    uint8_t buf[2 + MAX_TEXT + 1];
    buf[0] = RL_CHAT;
    buf[1] = (uint8_t)(g_localId < 0 ? 0 : g_localId);
    int n = lstrlenA(text);
    memcpy(buf + 2, text, n + 1);
    NetSendReliable(buf, 2 + n + 1);   // hote : a tous les invites ; invite : a l'hote, qui relaie
    AddLine(g_localId, g_cfg.playerName, text);
}

bool ChatReliable(int from, const uint8_t *d, int len)
{
    if (len < 3 || d[0] != RL_CHAT) return false;
    char text[MAX_TEXT + 1];
    int n = len - 2 < MAX_TEXT ? len - 2 : MAX_TEXT;
    memcpy(text, d + 2, n);
    text[n] = 0;
    for (int i = 0; text[i]; i++) if ((uint8_t)text[i] < 32 || (uint8_t)text[i] > 126) text[i] = '?';
    int sender = g_cfg.host ? from : d[1];   // l'hote sait qui parle ; un invite croit l'hote
    if (sender < 0 || sender >= MAX_PLAYERS) return true;
    AddLine(sender, PlayerName(sender), text);
    if (g_cfg.host) {   // relais aux autres invites
        uint8_t buf[2 + MAX_TEXT + 1];
        buf[0] = RL_CHAT; buf[1] = (uint8_t)sender;
        memcpy(buf + 2, text, strlen(text) + 1);
        for (int i = 1; i < MAX_PLAYERS; i++)
            if (i != sender && g_players[i].connected) NetSendReliableTo(i, buf, 2 + (int)strlen(text) + 1);
    }
    return true;
}

// Procedure de fenetre : vrai si le message est pour le tchat (et ne doit pas aller au jeu).
bool ChatWindowMessage(UINT msg, WPARAM wp)
{
    if (!NetRunning()) return false;
    if (!g_typing) {
        bool inGame = game::GameState() == 9 && !*(uint8_t *)(0xBA6748 + 0x5C);   // en partie, menu ferme
        if (msg == WM_KEYDOWN && wp == 'T' && inGame) {
            g_typing = true; g_skipChar = true; g_len = 0; g_input[0] = 0;
            return true;
        }
        return false;
    }
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            if (g_len) Send(g_input);
            g_typing = false;
        } else if (wp == VK_ESCAPE) {
            g_typing = false;
        } else if (wp == VK_BACK && g_len) {
            g_input[--g_len] = 0;
        }
        return true;
    case WM_CHAR:
        if (g_skipChar) { g_skipChar = false; if (wp == 't' || wp == 'T') return true; }
        if (wp >= 32 && wp < 127 && g_len < MAX_TEXT) { g_input[g_len++] = (char)wp; g_input[g_len] = 0; }
        return true;
    case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSCHAR:
        return true;
    }
    return false;
}

void ChatForEachLine(void (*draw)(int player, const char *name, const char *text, float alpha), const char **input)
{
    uint32_t now = GetTickCount();
    for (int k = 0; k < LINES; k++) {
        const Line &l = g_lines[(g_next + k) % LINES];
        if (!l.at) continue;
        uint32_t age = now - l.at;
        if (age > 15000 && !g_typing) continue;
        float a = age > 13000 && !g_typing ? (15000 - age) / 2000.0f : 1.0f;
        draw(l.player, l.name, l.text, a);
    }
    *input = g_typing ? g_input : nullptr;
}

// Autotest "tchat" : message envoye a 30 s (via la meme saisie).
void ChatTest(const char *text)
{
    lstrcpynA(g_input, text, sizeof(g_input));
    g_len = lstrlenA(g_input);
    Send(g_input);
}
