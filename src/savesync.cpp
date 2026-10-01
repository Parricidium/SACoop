// Sauvegarde partagee. Quand l'hote sauvegarde (planque), le fichier GTASAsfN.b est envoye aux invites sur le flux
// fiable et ecrit au meme emplacement chez eux : chacun peut ensuite recharger la meme partie (menu Charger).
//  - Hote : les 8 emplacements sont surveilles toutes les 2 s ; une sauvegarde plus recente que le lancement du jeu et
//    stable (taille et date inchangees depuis 2 s) est envoyee en morceaux (RL_SAVE_BEGIN / DATA / END + empreinte).
//  - Invite : les morceaux sont assembles, verifies, ecrits dans un fichier temporaire puis mis a la place.
//  - Dossier : celui du jeu (0xC16F18) : "<jeu>\GTA San Andreas User Files" (SauvegardesLocales=1) ou Mes documents.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "savesync.h"
#include "hud.h"
#include <stdio.h>
#include <string.h>
#include <vector>

enum : uint8_t { RL_SAVE_BEGIN = 10, RL_SAVE_DATA = 11, RL_SAVE_END = 12 };
enum { CHUNK = 1100, MAX_SAVE = 2 * 1024 * 1024 };

static bool g_frLang = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;

// Base des sauvegardes du jeu lui-meme ("<dossier>\\GTASAsf" en 0xC16F18, cf. files.cpp) : dossier et fichiers.
static void SaveDir(char *out)
{
    lstrcpynA(out, (const char *)0xC16F18, MAX_PATH);
    char *slash = strrchr(out, '\\');
    if (slash) *slash = 0;
}
static void SlotPath(int slot, char *out)
{
    wsprintfA(out, "%s%d.b", (const char *)0xC16F18, slot + 1);
}

static uint32_t Fnv(const uint8_t *d, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    return h;
}

// --- Hote ---
static FILETIME g_start;
static struct { FILETIME time; DWORD size; uint32_t seenAt; bool sent; } g_slots[8];

static void SendSave(int slot)
{
    char path[MAX_PATH];
    SlotPath(slot, path);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && data.size() < MAX_SAVE) data.insert(data.end(), buf, buf + n);
    fclose(f);
    if (data.empty() || data.size() >= MAX_SAVE) return;
    uint8_t msg[1 + 8 + CHUNK];
    msg[0] = RL_SAVE_BEGIN; msg[1] = (uint8_t)slot;
    uint32_t size = (uint32_t)data.size();
    memcpy(msg + 2, &size, 4);
    NetSendReliable(msg, 6);
    for (uint32_t off = 0; off < size; off += CHUNK) {
        uint32_t len = size - off < CHUNK ? size - off : CHUNK;
        msg[0] = RL_SAVE_DATA;
        memcpy(msg + 1, &off, 4);
        memcpy(msg + 5, &data[off], len);
        NetSendReliable(msg, 5 + len);
    }
    uint32_t hash = Fnv(data.data(), data.size());
    msg[0] = RL_SAVE_END; msg[1] = (uint8_t)slot;
    memcpy(msg + 2, &hash, 4);
    NetSendReliable(msg, 6);
    Log("sauvegarde %d envoyee aux invites (%u octets)", slot + 1, size);
    char note[96];
    wsprintfA(note, g_frLang ? "Sauvegarde %d envoyee aux autres joueurs" : "Save %d sent to the other players", slot + 1);
    HudToast(note, 5000);
}

static void HostFrame()
{
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (now - last < 2000) return;
    last = now;
    if (!g_start.dwLowDateTime && !g_start.dwHighDateTime) {
        SYSTEMTIME st; GetSystemTime(&st); SystemTimeToFileTime(&st, &g_start);
    }
    for (int s = 0; s < 8; s++) {
        char path[MAX_PATH];
        SlotPath(s, path);
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) continue;
        if (CompareFileTime(&a.ftLastWriteTime, &g_start) <= 0) continue;   // anterieure au lancement
        auto &sl = g_slots[s];
        if (CompareFileTime(&a.ftLastWriteTime, &sl.time) != 0 || a.nFileSizeLow != sl.size) {
            sl.time = a.ftLastWriteTime; sl.size = a.nFileSizeLow; sl.seenAt = now; sl.sent = false;
            continue;
        }
        if (!sl.sent && now - sl.seenAt >= 2000) { sl.sent = true; SendSave(s); }
    }
}

// --- Invite ---
static std::vector<uint8_t> g_rx;
static int g_rxSlot = -1;

bool SaveSyncReliable(const uint8_t *d, int len)
{
    if (len < 1 || d[0] < RL_SAVE_BEGIN || d[0] > RL_SAVE_END) return false;
    if (g_cfg.host) return true;
    if (d[0] == RL_SAVE_BEGIN && len >= 6) {
        uint32_t size;
        memcpy(&size, d + 2, 4);
        g_rxSlot = size < MAX_SAVE && d[1] < 8 ? d[1] : -1;
        g_rx.assign(g_rxSlot >= 0 ? size : 0, 0);
    } else if (d[0] == RL_SAVE_DATA && len > 5 && g_rxSlot >= 0) {
        uint32_t off;
        memcpy(&off, d + 1, 4);
        if (off + (len - 5) <= g_rx.size()) memcpy(&g_rx[off], d + 5, len - 5);
    } else if (d[0] == RL_SAVE_END && len >= 6 && g_rxSlot == d[1]) {
        uint32_t hash;
        memcpy(&hash, d + 2, 4);
        if (Fnv(g_rx.data(), g_rx.size()) != hash) { Log("sauvegarde de l'hote : empreinte fausse, ignoree"); g_rxSlot = -1; return true; }
        char dir[MAX_PATH], path[MAX_PATH], tmp[MAX_PATH];
        SaveDir(dir);
        CreateDirectoryA(dir, NULL);
        SlotPath(g_rxSlot, path);
        wsprintfA(tmp, "%s.sacoop", path);
        if (FILE *f = fopen(tmp, "wb")) {
            fwrite(g_rx.data(), 1, g_rx.size(), f);
            fclose(f);
            MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
            Log("sauvegarde de l'hote recue : emplacement %d (%u octets)", g_rxSlot + 1, (unsigned)g_rx.size());
            char msg[96];
            wsprintfA(msg, g_frLang ? "Sauvegarde de l'hote recue (emplacement %d)" : "Host's save received (slot %d)", g_rxSlot + 1);
            HudToast(msg, 6000);
        }
        g_rxSlot = -1;
        g_rx.clear();
    }
    return true;
}

void SaveSyncFrame()
{
    if (NetRunning() && g_cfg.host) HostFrame();
}
