// Miroir des missions (la methode de VCCoop). Les missions ne tournent que chez l'hote (script.cpp) ; ce qu'elles
// montrent a l'ecran est rejoue chez les invites : l'hote capture une liste blanche de commandes de ses scripts de
// mission, avec les parametres evalues, et les envoie sur le flux fiable (ordonne, acquitte) ; l'invite les execute
// dans un script fantome a lui (meme gestionnaire du jeu, parametres reecrits en constantes).
//  - Traduction : personnage de l'hote (sa reference de pool = id MsgPed) -> sa copie ; vehicule (reference de pool
//    chez l'hote, MsgVehicle.ownerRef) -> sa copie ; marqueurs et spheres crees -> ceux de l'invite.
//  - Une commande dont l'entite n'existe pas encore chez l'invite attend en tete de file (3 s au plus).
//  - Debut de mission (START_MISSION de l'hote) : un invite a plus de 40 m est pose derriere l'hote.
//  - Progression : les variables globales du script (ScriptSpace [8, 43808) dans main.scm 1.0) qui ont change pendant
//    une mission sont envoyees a la fin (RL_GLOBALS), et chaque invite recoit l'etat complet a son arrivee en partie.
//    Seules les valeurs -1..255 circulent (drapeaux, compteurs) : pas les references d'entites propres a chaque jeu.
//  - Arrivee en cours de mission : l'hote garde le dernier texte de mission charge et les marqueurs/spheres actifs
//    (commandes deja capturees), et les renvoie a l'invite qui arrive, apres l'etat complet.
//  - Fin de mission (00D8 dans un script de mission) : l'invite retire ses marqueurs et spheres de mission et retablit
//    l'ecran (fondu, format cinema, controles, camera, textes, sons).
// Parametres SCM : 1 int32, 2 globale (ScriptSpace 0xA49960 + decalage), 3 locale (mission : 0xA48960, sinon script
// +0x3C), 4 int8, 5 int16, 6 float, 7/8 tableaux globaux/locaux, 9 texte 8, 0xA/0xB variable texte 8 globale/locale,
// 0xE texte de longueur variable, 0xF texte 16, 0x10/0x11 variable texte 16 globale/locale.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "entities.h"
#include "vehicles.h"
#include "script.h"
#include "mirror.h"
#include "police.h"
#include "savesync.h"
#include "chat.h"
#include "events.h"
#include <string.h>
#include <math.h>

using namespace game;

enum : uint8_t { RL_MIRROR = 1, RL_MISSION_END = 2, RL_MISSION_START = 3, RL_GLOBALS = 4, RL_SESSION = 5 };

// Signature de chaque commande : i entier, f reel, s texte, P personnage, V vehicule, b/B marqueur (entree/sortie),
// q/Q sphere (entree/sortie), o/O objet (entree/sortie), k/K pickup, r/R feu de script. (A l'envoi, un P qui designe le joueur de l'hote devient H :
// son pantin chez l'invite.)
struct MirrorOp { uint16_t op; const char *sig; };
static const MirrorOp kOps[] = {
    { 0x00BA, "sii" },     // PRINT_BIG
    { 0x00BB, "sii" },     // PRINT
    { 0x00BC, "sii" },     // PRINT_NOW
    { 0x00BE, "" },        // CLEAR_PRINTS
    { 0x01E3, "siii" },    // PRINT_WITH_NUMBER_BIG
    { 0x01E4, "siii" },    // PRINT_WITH_NUMBER
    { 0x01E5, "siii" },    // PRINT_WITH_NUMBER_NOW
    { 0x03E5, "s" },       // PRINT_HELP
    { 0x03E6, "" },        // CLEAR_HELP
    { 0x054C, "s" },       // LOAD_MISSION_TEXT
    { 0x016A, "ii" },      // DO_FADE
    { 0x02A3, "i" },       // SWITCH_WIDESCREEN
    { 0x03DE, "f" },       // SET_PED_DENSITY_MULTIPLIER : la circulation suit celle des missions de l'hote
    { 0x01EB, "f" },       // SET_CAR_DENSITY_MULTIPLIER (en population partagee proche, population.cpp les retient)
    { 0x01B4, "ii" },      // SET_PLAYER_CONTROL
    { 0x0164, "b" },       // REMOVE_BLIP
    { 0x0165, "bi" },      // CHANGE_BLIP_COLOUR
    { 0x0168, "bi" },      // CHANGE_BLIP_SCALE
    { 0x018B, "bi" },      // CHANGE_BLIP_DISPLAY
    { 0x07E0, "bi" },      // SET_BLIP_AS_FRIENDLY
    { 0x0186, "VB" },      // ADD_BLIP_FOR_CAR
    { 0x0187, "PB" },      // ADD_BLIP_FOR_CHAR
    { 0x018A, "fffB" },    // ADD_BLIP_FOR_COORD
    { 0x02A7, "fffiB" },   // ADD_SPRITE_BLIP_FOR_CONTACT_POINT
    { 0x04CE, "fffiB" },   // ADD_SHORT_RANGE_SPRITE_BLIP_FOR_COORD
    { 0x03BC, "ffffQ" },   // ADD_SPHERE
    { 0x03BD, "q" },       // REMOVE_SPHERE
    { 0x03CF, "ii" },      // LOAD_MISSION_AUDIO
    { 0x03D1, "i" },       // PLAY_MISSION_AUDIO
    { 0x040D, "i" },       // CLEAR_MISSION_AUDIO
    { 0x015F, "ffffff" },  // SET_FIXED_CAMERA_POSITION
    { 0x0160, "fffi" },    // POINT_CAMERA_AT_POINT
    { 0x015A, "" },        // RESTORE_CAMERA
    // Cameras des mini-cinematiques (nombre de parametres lu dans les gestionnaires du jeu, run\saop.py) :
    { 0x0157, "iii" },     // POINT_CAMERA_AT_PLAYER : envoyee comme 0159 vers le pantin de l'hote
    { 0x0158, "Vii" },     // POINT_CAMERA_AT_CAR
    { 0x0159, "Pii" },     // POINT_CAMERA_AT_CHAR
    { 0x0679, "Vfffffffi" }, // ATTACH_CAMERA_TO_VEHICLE (decalage, rotation, inclinaison, transition)
    { 0x067A, "VfffVfi" }, // ATTACH_CAMERA_TO_VEHICLE_LOOK_AT_VEHICLE
    { 0x067B, "VfffPfi" }, // ATTACH_CAMERA_TO_VEHICLE_LOOK_AT_CHAR
    { 0x067C, "Pfffffffi" }, // ATTACH_CAMERA_TO_CHAR
    { 0x067D, "PfffVfi" }, // ATTACH_CAMERA_TO_CHAR_LOOK_AT_VEHICLE
    { 0x067E, "PfffPfi" }, // ATTACH_CAMERA_TO_CHAR_LOOK_AT_CHAR
    { 0x0936, "ffffffii" },// CAMERA_SET_VECTOR_MOVE : la camera glisse d'un point a un autre (duree ms, lissage)
    { 0x0920, "ffffffii" },// CAMERA_SET_VECTOR_TRACK : le point vise glisse
    { 0x0922, "ffii" },    // CAMERA_SET_LERP_FOV
    { 0x092F, "i" },       // CAMERA_PERSIST_TRACK
    { 0x0930, "i" },       // CAMERA_PERSIST_POS
    { 0x02EB, "" },        // RESTORE_CAMERA_JUMPCUT
    { 0x0373, "" },        // SET_CAMERA_BEHIND_PLAYER
    { 0x02E4, "s" },       // LOAD_CUTSCENE (CCutsceneMgr::LoadCutsceneData 0x4D5E80)
    { 0x02E7, "" },        // START_CUTSCENE (0x5B1460) : attend que la cinematique soit chargee
    { 0x02EA, "" },        // CLEAR_CUTSCENE (0x4D5ED0) : attend que celle de l'invite soit finie
    { 0x0055, "ifff" },    // SET_PLAYER_COORDINATES : l'invite est pose a cote (decale selon son numero)
    { 0x0109, "ii" },      // ADD_SCORE : l'argent gagne en mission, pour chacun
    { 0x00A1, "Pfff" },    // SET_CHAR_COORDINATES (joueur de l'hote : l'invite est pose a cote)
    { 0x0860, "Pi" },      // LINK_CHAR_WITH_INTERIOR (joueur de l'hote : l'invite aussi)
    { 0x04BB, "i" },       // SET_AREA_VISIBLE : seulement si l'invite vient d'etre pose avec l'hote, ou est pres de lui
    { 0x0107, "ifffO" },   // CREATE_OBJECT (modele negatif : table des objets utilises 0xA44B70, id a +24)
    { 0x029B, "ifffO" },   // CREATE_OBJECT_NO_OFFSET
    { 0x0108, "o" },       // DELETE_OBJECT
    { 0x01BC, "offf" },    // SET_OBJECT_COORDINATES
    { 0x0177, "of" },      // SET_OBJECT_HEADING
    { 0x0382, "oi" },      // SET_OBJECT_COLLISION
    { 0x0750, "oi" },      // SET_OBJECT_VISIBLE
    { 0x0188, "oB" },      // ADD_BLIP_FOR_OBJECT
    { 0x0213, "iifffK" },  // CREATE_PICKUP (objets de mission : ramasses par un invite, ils comptent chez l'hote, events.cpp)
    { 0x032B, "iiifffK" }, // CREATE_PICKUP_WITH_AMMO
    { 0x0215, "k" },       // REMOVE_PICKUP
    { 0x020C, "fffi" },    // ADD_EXPLOSION
    { 0x0565, "fffi" },    // ADD_EXPLOSION_NO_SOUND
    { 0x0948, "fffif" },   // ADD_EXPLOSION_VARIABLE_SHAKE
    { 0x02CF, "fffiiR" },  // START_SCRIPT_FIRE
    { 0x02D1, "r" },       // REMOVE_SCRIPT_FIRE
};

// Cinematiques : CCutsceneMgr::ms_cutsceneLoadStatus 0xB5F84C (2 = chargee), ms_running 0xB5F851,
// HasCutsceneFinished 0x5B0570.
static bool CutsceneLoaded() { return *(uint8_t *)0xB5F84C == 2; }
static bool CutsceneRunning() { return *(uint8_t *)0xB5F851 != 0; }
static bool CutsceneFinished() { return ((bool(__cdecl *)())0x5B0570)(); }
static const MirrorOp *FindOp(int op)
{
    for (auto &o : kOps) if (o.op == op) return &o;
    return nullptr;
}

// --- Hote : lecture des parametres ---
static int *LocalVar(void *script, int idx)
{
    if (*((uint8_t *)script + 0xDC)) return (int *)0xA48960 + idx;
    return (int *)((uint8_t *)script + 0x3C) + idx;
}
static uint8_t *GlobalVar(int offset) { return (uint8_t *)0xA49960 + offset; }

struct Param { char kind; int value; char text[16]; int *out; };

// Lit un parametre a ip (avance ip) : valeur entiere/reelle ou texte, et l'adresse de la variable si c'en est une.
static bool ReadParam(void *script, uint8_t *&ip, Param &p)
{
    uint8_t t = *ip++;
    p.out = nullptr;
    memset(p.text, 0, sizeof(p.text));
    switch (t) {
    case 1: case 6: p.value = *(int *)ip; ip += 4; return true;
    case 4: p.value = *(int8_t *)ip; ip += 1; return true;
    case 5: p.value = *(int16_t *)ip; ip += 2; return true;
    case 2: p.out = (int *)GlobalVar(*(uint16_t *)ip); p.value = *p.out; ip += 2; return true;
    case 3: p.out = LocalVar(script, *(uint16_t *)ip); p.value = *p.out; ip += 2; return true;
    case 7: case 8: {
        uint16_t base = *(uint16_t *)ip, iv = *(uint16_t *)(ip + 2);
        int16_t flags = *(int16_t *)(ip + 4);
        ip += 6;
        int index = flags < 0 ? *(int *)GlobalVar(iv) : *LocalVar(script, iv);
        p.out = t == 7 ? (int *)GlobalVar(base + index * 4) : LocalVar(script, base + index);
        p.value = *p.out;
        return true;
    }
    case 9: memcpy(p.text, ip, 8); ip += 8; return true;
    case 0xA: memcpy(p.text, GlobalVar(*(uint16_t *)ip), 8); ip += 2; return true;
    case 0xB: memcpy(p.text, LocalVar(script, *(uint16_t *)ip), 8); ip += 2; return true;
    case 0xE: { int n = *ip++; memcpy(p.text, ip, n < 15 ? n : 15); ip += n; return true; }
    case 0xF: memcpy(p.text, ip, 15); ip += 16; return true;
    case 0x10: memcpy(p.text, GlobalVar(*(uint16_t *)ip), 15); ip += 2; return true;
    case 0x11: memcpy(p.text, LocalVar(script, *(uint16_t *)ip), 15); ip += 2; return true;
    }
    return false;   // type inconnu : la commande n'est pas capturee
}

struct Capture { const MirrorOp *op; int n; Param p[10]; };
static Capture g_cap;
static bool g_capturing;

// Hote : commandes encore actives (texte de mission, marqueurs, spheres), rejouees pour un invite qui arrive.
struct Active { int handle; int len; bool persist, after; uint8_t data[256]; };   // after : apres les creations
static Active g_active[160];
static void KeepActive(int op, const uint8_t *buf, int len, int handle)
{
    if (op == 0x0164 || op == 0x03BD || op == 0x0108 || op == 0x0215) {   // retrait
        for (auto &a : g_active) if (a.len && a.handle == handle) a.len = 0;
        if (op == 0x0108) for (auto &a : g_active) if (a.len && a.after && (a.handle & 0xFFFFFF) == (handle & 0xFFFFFF)) a.len = 0;
        return;
    }
    if (op == 0x054C) handle = -1;   // un seul texte de mission
    for (auto &a : g_active) if (a.len && a.handle == handle) a.len = 0;
    for (auto &a : g_active)
        if (!a.len) { a.handle = handle; a.len = len; a.persist = op == 0x04CE || op == 0x02A7; a.after = op == 0x01BC || op == 0x0177 || op == 0x0382 || op == 0x0750; memcpy(a.data, buf, len); return; }
}
static void SendActive(int peer)
{
    for (auto &a : g_active) if (a.len && a.handle == -1) NetSendReliableTo(peer, a.data, a.len);   // texte d'abord
    for (auto &a : g_active) if (a.len && a.handle != -1 && !a.after) NetSendReliableTo(peer, a.data, a.len);
    for (auto &a : g_active) if (a.len && a.handle != -1 && a.after) NetSendReliableTo(peer, a.data, a.len);   // etat des objets
}

// --- Variables globales ---
enum { GLOBALS_BEGIN = 8, GLOBALS_END = 43808, GLOBALS_N = (GLOBALS_END - GLOBALS_BEGIN) / 4 };
static int g_snap[GLOBALS_N];
static bool g_snapValid;
static bool g_fullSent[MAX_PLAYERS];
static int *Globals() { return (int *)(0xA49960 + GLOBALS_BEGIN); }
static bool Shareable(int v) { return v >= -1 && v <= 255; }

// Envoie les globales (toutes, ou celles qui different de l'instantane) ; peer < 0 : a tous les invites.
static int SendGlobals(int peer, bool all)
{
    uint8_t buf[1 + 2 + 190 * 6];
    int n = 0, total = 0;
    const int *g = Globals();
    auto flush = [&]() {
        if (!n) return;
        buf[0] = RL_GLOBALS;
        *(uint16_t *)(buf + 1) = (uint16_t)n;
        if (peer < 0) NetSendReliable(buf, 3 + n * 6); else NetSendReliableTo(peer, buf, 3 + n * 6);
        n = 0;
    };
    for (int i = 0; i < GLOBALS_N; i++) {
        if (!Shareable(g[i]) || (!all && g[i] == g_snap[i])) continue;
        *(uint16_t *)(buf + 3 + n * 6) = (uint16_t)i;
        memcpy(buf + 3 + n * 6 + 2, &g[i], 4);
        n++; total++;
        if (n == 190) flush();
    }
    flush();
    return total;
}

// Hote, chaque image : etat complet pour chaque invite arrive en partie.
static int g_hostSlot = -1;          // partie de l'hote : emplacement charge, -1 nouvelle partie
static bool g_sessionSent[MAX_PLAYERS];

static int g_seenSlot = -1;          // hote au menu : emplacement qu'il charge

static void HostGlobalsFrame()
{
    // La partie que l'hote a lancee depuis le menu (chargement ou nouvelle) est annoncee a chaque invite au menu.
    int &seenSlot = g_seenSlot;
    static bool wasInGame;
    bool inGame = GameState() == 9 && FindPlayerPed();
    if (inGame && !wasInGame) { g_hostSlot = seenSlot; seenSlot = -1; for (auto &b : g_sessionSent) b = false; }
    if (!inGame && wasInGame) seenSlot = -1;
    wasInGame = inGame;
    if (!inGame) return;
    for (int i = 1; i < MAX_PLAYERS; i++) {
        if (!g_players[i].connected) { g_sessionSent[i] = false; continue; }
        if (g_sessionSent[i] || g_players[i].state.inGame) continue;
        g_sessionSent[i] = true;
        uint8_t m[2] = { RL_SESSION, (uint8_t)(int8_t)g_hostSlot };
        NetSendReliableTo(i, m, 2);
        Log("miroir : partie annoncee au joueur %d (%s)", i, g_hostSlot >= 0 ? "sauvegarde" : "nouvelle partie");
    }
    for (int i = 1; i < MAX_PLAYERS; i++) {
        if (!g_players[i].connected) { g_fullSent[i] = false; continue; }
        if (g_fullSent[i] || !g_players[i].state.inGame) continue;
        g_fullSent[i] = true;
        Log("miroir : etat complet de l'histoire envoye au joueur %d (%d variables)", i, SendGlobals(i, true));
        SendActive(i);
    }
}

// Hote : une mission de l'histoire demarre (script.cpp) : position de l'hote pour le regroupement des invites.
void MirrorMissionStart()
{
    memcpy(g_snap, Globals(), sizeof(g_snap));
    g_snapValid = true;
    void *me = FindPlayerPed();
    if (!g_cfg.host || !NetRunning() || !me) return;
    uint8_t buf[1 + 16];
    buf[0] = RL_MISSION_START;
    memcpy(buf + 1, EntityPos(me), 12);
    float h = Field<float>(me, PED_ROTATION);
    memcpy(buf + 13, &h, 4);
    NetSendReliable(buf, sizeof(buf));
}

void MirrorBefore(void *script, int op)
{
    g_capturing = false;
    if (!g_cfg.host || !NetRunning() || !ScriptIsMission(script)) return;
    if (op == 0x00D8) {   // MISSION_HAS_FINISHED
        if (g_snapValid) Log("miroir : %d variables de l'histoire changees envoyees", SendGlobals(-1, false));
        for (auto &a : g_active) if (!a.persist) a.len = 0;
        g_snapValid = false;
        uint8_t m = RL_MISSION_END;
        NetSendReliable(&m, 1);
        Log("miroir : fin de mission envoyee");
        return;
    }
    const MirrorOp *mo = FindOp(op);
    if (!mo) return;
    uint8_t *ip = *(uint8_t **)((uint8_t *)script + 0x14);
    g_cap.op = mo;
    g_cap.n = (int)strlen(mo->sig);
    for (int k = 0; k < g_cap.n; k++) {
        g_cap.p[k].kind = mo->sig[k];
        if (!ReadParam(script, ip, g_cap.p[k])) return;
        if (g_cap.p[k].kind == 'P' && FindPlayerPed() && g_cap.p[k].value == PedRef(FindPlayerPed())) g_cap.p[k].kind = 'H';
    }
    g_capturing = true;
}

void MirrorAfter(void *script, int op)
{
    if (!g_capturing) return;
    g_capturing = false;
    uint8_t buf[256];
    int len = 0;
    buf[len++] = RL_MIRROR;
    if (op == 0x0157) { op = 0x0159; g_cap.p[0].kind = 'H'; }   // camera sur le joueur de l'hote : sur son pantin
    *(uint16_t *)(buf + len) = (uint16_t)op; len += 2;
    buf[len++] = (uint8_t)g_cap.n;
    for (int k = 0; k < g_cap.n; k++) {
        Param &p = g_cap.p[k];
        buf[len++] = (uint8_t)p.kind;
        if (p.kind == 's') {
            int n = (int)strnlen(p.text, 15);
            buf[len++] = (uint8_t)n;
            memcpy(buf + len, p.text, n); len += n;
        } else {
            int v = strchr("BQOKR", p.kind) && p.out ? *p.out : p.value;   // sortie : le handle de l'hote
            memcpy(buf + len, &v, 4); len += 4;
        }
    }
    NetSendReliable(buf, len);
    (void)script;
    // Commandes a garder pour un invite qui arrive : texte de mission, creation/retrait de marqueur ou sphere.
    // (objets : les deplacements apres la creation ne sont pas gardes ; cle distincte de celle des marqueurs)
    int handle = 0;
    for (int k = 0; k < g_cap.n; k++) {
        char kd = g_cap.p[k].kind;
        if (!strchr("BQbqOoKk", kd)) continue;
        handle = (kd == 'B' || kd == 'Q' || kd == 'O' || kd == 'K') && g_cap.p[k].out ? *g_cap.p[k].out : g_cap.p[k].value;
        if (kd == 'O' || kd == 'o') handle ^= 0x40000000;
        if (kd == 'K' || kd == 'k') handle ^= 0x60000000;
        break;
    }
    if (op == 0x0188) handle = 0;   // marqueur d'objet : rejoue avec l'objet... non garde (ordre de recreation)
    // Etat d'un objet de mission (position, cap, collision, visibilite) : le dernier de chaque sorte est garde aussi,
    // renvoye apres les creations a un invite qui arrive en cours de mission (cle : objet + sorte, bit 0x10000000).
    if (handle && (op == 0x01BC || op == 0x0177 || op == 0x0382 || op == 0x0750)) {
        int kind = op == 0x01BC ? 0 : op == 0x0177 ? 1 : op == 0x0382 ? 2 : 3;
        handle = (handle & 0x00FFFFFF) | 0x10000000 | (kind << 24);
    }
    if (op == 0x054C || handle) KeepActive(op, buf, len, handle);
}

// --- Invite : rejeu ---
enum { MAX_MAP = 128 };
struct HandleMap { int host, guest; bool persist; };   // persist : icone radar (04CE, 02A7), gardee apres la mission
static HandleMap g_blips[MAX_MAP], g_spheres[MAX_MAP], g_objects[MAX_MAP], g_pickups[MAX_MAP], g_fires[MAX_MAP];
static int MapGet(HandleMap *m, int host)
{
    for (int i = 0; i < MAX_MAP; i++) if (m[i].host == host && host) return m[i].guest;
    return 0;
}
static void MapSet(HandleMap *m, int host, int guest, bool persist = false)
{
    for (int i = 0; i < MAX_MAP; i++) if (m[i].host == host) { m[i].guest = guest; m[i].persist = persist; return; }
    for (int i = 0; i < MAX_MAP; i++) if (!m[i].host) { m[i].host = host; m[i].guest = guest; m[i].persist = persist; return; }
}
static void MapDel(HandleMap *m, int host)
{
    for (int i = 0; i < MAX_MAP; i++) if (m[i].host == host) m[i].host = m[i].guest = 0;
}

enum { QUEUE = 1024 };
struct Pending { uint8_t data[256]; int len; uint32_t since; };
static Pending g_queue[QUEUE];
static int g_qHead, g_qTail;

static void OnReliable(int from, const uint8_t *data, int len)
{
    (void)from;
    if (SaveSyncReliable(data, len)) return;   // sauvegarde partagee (savesync.cpp) : traitee tout de suite
    if (ChatReliable(from, data, len)) return;  // tchat (chat.cpp)
    if (PoliceReliable(data, len)) return;      // ejection par un policier de l'hote (police.cpp)
    if (EventsReliable(from, data, len)) return;   // pickups, tags (events.cpp)
    if (len >= 2 && data[0] == RL_SESSION) {   // partie de l'hote : l'invite encore au menu la suit
        if (g_cfg.host) return;
        int slot = (int8_t)data[1];
        char path[MAX_PATH];
        wsprintfA(path, "%s%i.b", (const char *)0xC16F18, slot + 1);
        if (slot >= 0 && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) slot = -1;   // pas cette sauvegarde
        if (RequestGameStart(slot)) Log("miroir : partie de l'hote suivie (%s)", slot >= 0 ? path : "nouvelle partie");
        return;
    }
    if (g_cfg.host || len < 1 || len > 256) return;
    if ((g_qTail + 1) % QUEUE == g_qHead) { Log("miroir : file pleine, commande perdue"); return; }
    Pending &p = g_queue[g_qTail];
    memcpy(p.data, data, len);
    p.len = len;
    p.since = 0;
    g_qTail = (g_qTail + 1) % QUEUE;
}

alignas(16) static uint8_t g_script[0x200];   // CRunningScript fantome de l'invite
static uint8_t g_code[300];

static void ExecCommand(int op)
{
    *(uint8_t **)(g_script + 0x10) = g_code;
    *(uint8_t **)(g_script + 0x14) = g_code + 2;
    ScriptOriginalHandler(op / 100)(g_script, op);
}

static void RestoreScreen();
static uint32_t g_teleportedAt;
static uint32_t g_mirrorCamAt;   // invite : derniere commande de camera rejouee (garde-fou CameraWatch)

// Mouvements de camera en cours (CAMERA_SET_VECTOR_MOVE/TRACK 0936/0920, gardes par CAMERA_PERSIST_* 092F/0930) :
// effaces comme le fait le jeu (0x50D2D0 : actifs +0xC8C/+0xCEC, persistants +0xCEE/+0xCEF de TheCamera) au retour de
// la camera derriere le joueur. Apres la cinematique des velos (02/10), la camera de l'invite restait en l'air.
static void CameraVectorReset()
{
    uint8_t *cam = (uint8_t *)0xB6F028;
    cam[0xC8C] = cam[0xCEC] = cam[0xCEE] = cam[0xCEF] = 0;
}   // invite : derniere teleportation avec l'hote (SET_AREA_VISIBLE a suivre)

// Rejoue une commande ; faux si une entite manque encore (on reessaiera).
static bool Replay(const uint8_t *d, int len)
{
    if (d[0] == RL_MISSION_END) {
        VehiclesMissionEnded();
        for (auto &b : g_blips) if (b.host && !b.persist) {
            *(uint16_t *)g_code = 0x0164;
            g_code[2] = 1; memcpy(g_code + 3, &b.guest, 4);
            ExecCommand(0x0164);
            b.host = b.guest = 0;
        }
        for (auto &o : g_objects) if (o.host) {   // objets de mission (chez l'hote, le nettoyage de mission les retire)
            *(uint16_t *)g_code = 0x0108;
            g_code[2] = 1; memcpy(g_code + 3, &o.guest, 4);
            ExecCommand(0x0108);
            o.host = o.guest = 0;
        }
        for (auto &s : g_spheres) if (s.host) {
            *(uint16_t *)g_code = 0x03BD;
            g_code[2] = 1; memcpy(g_code + 3, &s.guest, 4);
            ExecCommand(0x03BD);
            s.host = s.guest = 0;
        }
        for (auto &k : g_pickups) if (k.host) {   // pickups de mission restants
            *(uint16_t *)g_code = 0x0215;
            g_code[2] = 1; memcpy(g_code + 3, &k.guest, 4);
            ExecCommand(0x0215);
            k.host = k.guest = 0;
        }
        for (auto &f : g_fires) if (f.host) {
            *(uint16_t *)g_code = 0x02D1;
            g_code[2] = 1; memcpy(g_code + 3, &f.guest, 4);
            ExecCommand(0x02D1);
            f.host = f.guest = 0;
        }
        RestoreScreen();
        Log("miroir : fin de mission de l'hote, ecran retabli");
        return true;
    }
    if (d[0] == RL_GLOBALS && len >= 3) {
        int n = *(const uint16_t *)(d + 1);
        int *g = Globals();
        for (int k = 0; k < n && 3 + k * 6 + 6 <= len; k++) {
            int idx = *(const uint16_t *)(d + 3 + k * 6);
            if (idx < GLOBALS_N) memcpy(&g[idx], d + 3 + k * 6 + 2, 4);
        }
        return true;
    }
    if (d[0] == RL_MISSION_START && len >= 17) {
        void *me = FindPlayerPed();
        float host[3], h;
        memcpy(host, d + 1, 12);
        memcpy(&h, d + 13, 4);
        const float *p = EntityPos(me);
        float dx = p[0] - host[0], dy = p[1] - host[1], dz = p[2] - host[2];
        if (dx * dx + dy * dy + dz * dz > 40.0f * 40.0f && !PedVehicle(me)) {
            // derriere l'hote (avant = (-sin h, cos h)), en ligne selon le numero du joueur
            float back = 2.0f, side = 1.2f * (g_localId - 2);
            float pos[3] = { host[0] + sinf(h) * back + cosf(h) * side, host[1] - cosf(h) * back + sinf(h) * side, host[2] };
            PlacePuppet(me, pos, h);
            Log("miroir : mission de l'hote, regroupe derriere lui");
        }
        return true;
    }
    if (d[0] != RL_MIRROR || len < 4) return true;
    int op = *(const uint16_t *)(d + 1), n = d[3];
    if (op == 0x02E7 && !CutsceneLoaded()) return false;                           // chargement en cours
    if (op == 0x02EA && CutsceneRunning() && !CutsceneFinished()) {
        // L'hote a fini (ou passe) sa cinematique : on passe celle de l'invite, comme un appui sur Croix
        // (PCTempJoyState de la manette 0, +0x20), une image sur deux.
        static uint32_t frame;
        static const bool noSkip = GetPrivateProfileIntA("SACoop", "TestSansPasser", 0, IniPath()) != 0;   // test : passer impossible
        if (!noSkip) ((int16_t *)(0xB73458 + 0xA8))[0x20 / 2] = (++frame & 2) ? 255 : 0;
        return false;
    }
    if (op == 0x02EA) ((int16_t *)(0xB73458 + 0xA8))[0x20 / 2] = 0;
    if ((op == 0x0107 || op == 0x029B || op == 0x0213 || op == 0x032B) && len >= 9 && d[4] == 'i') {   // modele de l'objet charge avant
        int model;
        memcpy(&model, d + 5, 4);
        if (model < 0) model = *(int *)(0xA44B88 + -model * 28);
        if (model <= 0 || model >= 20000) return true;
        if (!ModelLoaded(model)) { RequestModel(model, 2); LoadAllRequestedModels(false); }
        if (!ModelLoaded(model)) return false;
    }
    int pos = 4, out = 0;
    int c = 0;
    *(uint16_t *)g_code = (uint16_t)op;
    c = 2;
    int outHost[4] = {}, outIdx[4] = {};
    char outKind[4] = {};
    for (int k = 0; k < n; k++) {
        if (pos >= len) return true;
        char kind = (char)d[pos++];
        if (kind == 's') {
            int sl = d[pos++];
            g_code[c++] = 0xE; g_code[c++] = (uint8_t)sl;
            memcpy(g_code + c, d + pos, sl); c += sl; pos += sl;
            continue;
        }
        int v;
        memcpy(&v, d + pos, 4); pos += 4;
        switch (kind) {
        case 'P': {
            void *ped = MissionCopyById((uint32_t)v);
            if (!ped) return false;
            v = PedRef(ped);
            break;
        }
        case 'H': {
            // Joueur de l'hote : pour une teleportation ou un interieur, c'est l'invite lui-meme qui suit ;
            // sinon (marqueur, camera...), son pantin chez l'invite.
            void *ped = op == 0x00A1 || op == 0x0860 ? FindPlayerPed() : PuppetOf(0);
            if (!ped) return false;
            v = PedRef(ped);
            break;
        }
        case 'V': {
            void *veh = NetVehicleByOwnerRef(0, v);
            if (!veh) return false;
            v = VehicleRef(veh);
            break;
        }
        case 'b': v = MapGet(g_blips, v); if (!v) return true; break;      // marqueur inconnu : rien a faire
        case 'q': v = MapGet(g_spheres, v); if (!v) return true; break;
        case 'o': v = MapGet(g_objects, v); if (!v) return true; break;
        case 'k': v = MapGet(g_pickups, v); if (!v) return true; break;
        case 'r': v = MapGet(g_fires, v); if (!v) return true; break;
        case 'B': case 'Q': case 'O': case 'K': case 'R':
            outKind[out] = kind; outHost[out] = v; outIdx[out] = out;
            g_code[c++] = 3; *(uint16_t *)(g_code + c) = (uint16_t)out; c += 2;
            out++;
            continue;
        }
        g_code[c++] = kind == 'f' ? 6 : 1;
        memcpy(g_code + c, &v, 4); c += 4;
    }
    // SET_PLAYER_COORDINATES / SET_CHAR_COORDINATES du joueur de l'hote : a cote de lui, decale selon le numero.
    bool selfTeleport = op == 0x0055 || (op == 0x00A1 && d[4] == 'H');
    if (selfTeleport && c >= 2 + 20) {
        float x;
        memcpy(&x, g_code + 2 + 5 + 1, 4);
        x += 1.5f * g_localId;
        memcpy(g_code + 2 + 5 + 1, &x, 4);
        if (PedVehicle(FindPlayerPed())) return true;   // en voiture : on ne l'arrache pas
        g_teleportedAt = GetTickCount();
    }
    if (op == 0x0860 && d[4] != 'H') return true;   // interieur d'un autre personnage : sa copie suit le reseau
    if (op == 0x04BB) {   // zone visible : seulement avec l'hote, sinon la ville de l'invite se dechargerait
        void *host = PuppetOf(0), *me = FindPlayerPed();
        bool closeBy = false;
        if (host && me) {
            const float *a = EntityPos(host), *b = EntityPos(me);
            float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
            closeBy = dx * dx + dy * dy + dz * dz < 40.0f * 40.0f;
        }
        if (!closeBy && GetTickCount() - g_teleportedAt > 3000) return true;
    }
    if (op == 0x015A || op == 0x02EB || op == 0x0373) CameraVectorReset();
    if (op == 0x015F || op == 0x0160 || (op >= 0x0157 && op <= 0x0159) || (op >= 0x0679 && op <= 0x067E) || op == 0x0936 || op == 0x0920) g_mirrorCamAt = GetTickCount();
    ExecCommand(op);
    static int logged;
    if (logged < 300) { logged++; Log("miroir : %04X rejouee", op); }
    for (int k = 0; k < out; k++) {
        int guest = *(int *)(g_script + 0x3C + outIdx[k] * 4);
        HandleMap *m = outKind[k] == 'B' ? g_blips : outKind[k] == 'O' ? g_objects : outKind[k] == 'K' ? g_pickups : outKind[k] == 'R' ? g_fires : g_spheres;
        MapSet(m, outHost[k], guest, op == 0x04CE || op == 0x02A7);
    }
    if (op == 0x0164) MapDel(g_blips, *(const int *)(d + 5));
    if (op == 0x03BD) MapDel(g_spheres, *(const int *)(d + 5));
    if (op == 0x0108) MapDel(g_objects, *(const int *)(d + 5));
    if (op == 0x0215) MapDel(g_pickups, *(const int *)(d + 5));
    if (op == 0x02D1) MapDel(g_fires, *(const int *)(d + 5));
    return true;
}

// Ecran de l'invite apres une mission de l'hote : fondu d'entree, format normal, controles, camera, textes, sons.
static void RestoreScreen()
{
    CameraVectorReset();
    static const struct { uint16_t op; int args[2]; int n; } cmds[] = {
        { 0x016A, { 500, 1 }, 2 }, { 0x02A3, { 0 }, 1 }, { 0x01B4, { 0, 1 }, 2 }, { 0x02EB, {}, 0 },
        { 0x00BE, {}, 0 }, { 0x03E6, {}, 0 }, { 0x040D, { 1 }, 1 }, { 0x040D, { 2 }, 1 },
    };
    for (auto &cmd : cmds) {
        int c = 2;
        *(uint16_t *)g_code = cmd.op;
        for (int k = 0; k < cmd.n; k++) { g_code[c++] = 1; memcpy(g_code + c, &cmd.args[k], 4); c += 4; }
        ExecCommand(cmd.op);
    }
}

// Ecrit le resultat d'une commande dans sa variable de sortie (parametre suivant du script, qui avance).
bool ScriptStoreResult(void *script, int value)
{
    uint8_t *&ip = *(uint8_t **)((uint8_t *)script + 0x14);
    Param p;
    if (!ReadParam(script, ip, p) || !p.out) return false;
    *p.out = value;
    return true;
}

// Valeurs des n premiers parametres d'une commande (ip juste apres l'opcode), sans avancer le script.
bool ScriptReadValues(void *script, uint8_t *ip, int n, int *vals)
{
    Param p;
    for (int k = 0; k < n; k++) {
        if (!ReadParam(script, ip, p)) return false;
        vals[k] = p.value;
    }
    return true;
}

// Execute une commande dans le script fantome ; types[k] : 'i' entier, 'f' reel.
void RunScriptCommandTyped(int op, int nargs, const char *types, const int *args)
{
    memcpy(g_script + 8, "sacoop", 7);
    int c = 2;
    *(uint16_t *)g_code = (uint16_t)op;
    for (int k = 0; k < nargs; k++) {
        if (types[k] == 's') {   // chaine (args[k] = son adresse) : type 0x0E, longueur puis caracteres
            const char *str = (const char *)(uintptr_t)args[k];
            int len = lstrlenA(str);
            if (len > 31) len = 31;
            g_code[c++] = 0x0E;
            g_code[c++] = (uint8_t)len;
            memcpy(g_code + c, str, len);
            c += len;
            continue;
        }
        if (types[k] == 'o') {   // sortie : variable locale args[k] du script fantome (ScriptGhostVar)
            g_code[c++] = 3; *(uint16_t *)(g_code + c) = (uint16_t)args[k]; c += 2;
            continue;
        }
        g_code[c++] = types[k] == 'f' ? 6 : 1; memcpy(g_code + c, &args[k], 4); c += 4;
    }
    ExecCommand(op);
}
int ScriptGhostVar(int i) { return *(int *)(g_script + 0x3C + i * 4); }

// Execute une commande a parametres entiers dans le script fantome (autotests, retablissement de l'ecran).
void RunScriptCommand(int op, int nargs, const int *args)
{
    memcpy(g_script + 8, "sacoop", 7);
    int c = 2;
    *(uint16_t *)g_code = (uint16_t)op;
    for (int k = 0; k < nargs; k++) { g_code[c++] = 1; memcpy(g_code + c, &args[k], 4); c += 4; }
    ExecCommand(op);
}

// Autotest "objet" (hote, hors mission) : un baril (1225) cree, deplace puis supprime par le script fantome marque
// "mission", comme le ferait une mission : les commandes passent par la capture du miroir.
void MirrorTestObject(int step, const float *pos)
{
    static int handle;
    memcpy(g_script + 8, "sacoop", 7);
    g_script[0xDC] = 1;
    int c = 2, op = step == 0 ? 0x0107 : step == 1 ? 0x01BC : 0x0108;
    auto i32 = [&](uint8_t t, const void *v) { g_code[c++] = t; memcpy(g_code + c, v, 4); c += 4; };
    *(uint16_t *)g_code = (uint16_t)op;
    if (step == 0) {
        int model = 1225;
        if (!ModelLoaded(model)) { RequestModel(model, 2); LoadAllRequestedModels(false); }
        i32(1, &model); i32(6, &pos[0]); i32(6, &pos[1]); i32(6, &pos[2]);
        g_code[c++] = 3; *(uint16_t *)(g_code + c) = 0; c += 2;   // sortie : locale de mission 0
    } else {
        i32(1, &handle);
        if (step == 1) { i32(6, &pos[0]); i32(6, &pos[1]); i32(6, &pos[2]); }
    }
    *(uint8_t **)(g_script + 0x10) = g_code;
    *(uint8_t **)(g_script + 0x14) = g_code + 2;
    MirrorBefore(g_script, op);
    ExecCommand(op);
    MirrorAfter(g_script, op);
    if (step == 0) handle = *(int *)0xA48960;
    g_script[0xDC] = 0;
    Log("autotest : objet %d (etape %d)", handle, step);
}

// Chaque tour de boucle, menu compris : reception du flux fiable (l'invite au menu doit entendre RL_SESSION), et
// hote au menu : emplacement en cours de chargement.
void MirrorMenuFrame()
{
    g_onReliable = OnReliable;
    if (g_cfg.host && GameState() == 7) { int s = MenuLoadingSlot(); if (s >= 0) g_seenSlot = s; }
}

// Invite arrive apres les fondus de l'hote (sa propre INTRO est bloquee) : reste noir alors que l'hote voit clair.
// Si l'ecran est noir sans fondu en cours (CCamera +0xBFC alpha, +0x51 m_bFading) depuis 2 s, joueur vivant, hors
// cinematique, et que l'hote est clair : fondu d'entree (DO_FADE 500 ms).
static void UnstickFade()
{
    static uint32_t stuckSince;
    uint8_t *cam = (uint8_t *)0xB6F028;
    float alpha = *(float *)(cam + 0xBFC);
    // Pendant une cinematique (celle de l'hote, rejouee) : le fondu de l'hote, tel quel (sinon l'invite deja noir au
    // lancement pouvait voir toute la cinematique en noir, son fondu d'entree rejoue ne prenant pas).
    if (CutsceneRunning() && g_players[0].connected && g_players[0].state.inGame) {
        cam[0x51] = 0;
        *(float *)(cam + 0xBFC) = g_players[0].state.fade;
        stuckSince = 0;
        return;
    }
    int st = Field<int>(FindPlayerPed(), PED_STATE);
    const NetPlayer &host = g_players[0];
    bool stuck = alpha > 200.0f && !cam[0x51] && st != 54 && st != 55 && !CutsceneRunning() &&
                 host.connected && host.state.inGame && host.state.fade < 20;
    if (!stuck) { stuckSince = 0; return; }
    if (!stuckSince) stuckSince = GetTickCount();
    if (GetTickCount() - stuckSince < 2000) return;
    stuckSince = 0;
    int args[2] = { 500, 1 };
    RunScriptCommand(0x016A, 2, args);
    Log("miroir : ecran noir alors que l'hote voit clair, fondu d'entree");
}

// Fin de cinematique forcee (CCutsceneMgr, comme CLEAR_CUTSCENE 0x4D5ED0), commandes et camera rendues au joueur.
// Avant, un CLEAR_CUTSCENE qui ne passait pas a temps etait abandonne : la cinematique restait "en cours" et le jeu
// coupait les commandes de l'invite pour de bon (1er test reel, GG : il voyait l'hote bouger sans pouvoir bouger).
static void ForceCutsceneEnd(const char *why)
{
    ((int16_t *)(0xB73458 + 0xA8))[0x20 / 2] = 0;
    if (CutsceneRunning()) ((void(__cdecl *)())0x4D5ED0)();
    int ctl[2] = { 0, 1 };
    RunScriptCommand(0x01B4, 2, ctl);    // SET_PLAYER_CONTROL joueur 0, oui
    RunScriptCommand(0x02EB, 0, nullptr);   // RESTORE_CAMERA_JUMPCUT
    Log("miroir : fin de cinematique forcee (%s), commandes rendues", why);
}

// Garde-fou : la camera de l'invite est tenue (fixe, mouvement de mission) alors que l'hote a retrouve la sienne depuis
// 2,5 s, hors cinematique, et une camera de mission a ete rejouee dans les 3 dernieres minutes (pas celle d'un magasin
// de l'invite) : elle lui est rendue (mouvements effaces, RESTORE_CAMERA_JUMPCUT 02EB, camera dans le dos 0373).
static void CameraWatch()
{
    static uint32_t since;
    uint32_t now = GetTickCount();
    const NetPlayer &h = g_players[0];
    bool stuck = *(uint8_t *)(0xB6F028 + 0x2B) == 0 && h.connected && h.state.inGame && !h.state.camScripted
                 && now - h.lastStateAt < 1000 && !CutsceneRunning() && g_mirrorCamAt && now - g_mirrorCamAt < 180000;
    if (!stuck) { since = 0; return; }
    if (!since) { since = now; return; }
    if (now - since < 2500) return;
    since = 0;
    CameraVectorReset();
    static const uint16_t ops[2] = { 0x02EB, 0x0373 };
    for (uint16_t op : ops) { *(uint16_t *)g_code = op; ExecCommand(op); }
    Log("miroir : camera de l'invite rendue (l'hote n'est plus en camera de mission)");
}

// Autotest "camcoince" (invite) : une camera fixe comme celle d'une mission, que l'hote n'a pas : CameraWatch doit la rendre.
void MirrorTestStuckCamera(const float *pos)
{
    int a[6]; float f[6] = { pos[0], pos[1], pos[2] + 25.0f, 0, 0, 0 }; memcpy(a, f, 24);
    RunScriptCommandTyped(0x015F, 6, "ffffff", a);
    int b[4]; float g[3] = { pos[0], pos[1], pos[2] }; memcpy(b, g, 12); b[3] = 2;
    RunScriptCommandTyped(0x0160, 4, "fffi", b);
    g_mirrorCamAt = GetTickCount();
    Log("autotest : camera fixe en l'air (comme une mission)");
}

void MirrorFrame()
{
    g_onReliable = OnReliable;
    if (g_cfg.host) { if (NetRunning()) HostGlobalsFrame(); return; }
    if (GameState() != 9 || !FindPlayerPed()) return;
    UnstickFade();
    CameraWatch();
    memcpy(g_script + 8, "sacoop", 7);
    uint32_t now = GetTickCount();
    for (int budget = 0; budget < 64 && g_qHead != g_qTail; budget++) {
        Pending &p = g_queue[g_qHead];
        if (!p.since) p.since = now;
        if (!Replay(p.data, p.len)) {
            int op = p.len > 2 ? *(const uint16_t *)(p.data + 1) : 0;
            uint32_t patience = op == 0x02E7 ? 20000 : op == 0x02EA ? 10000 : 3000;
            if (now - p.since < patience) break;   // entite ou cinematique pas encore prete : on attend (dans l'ordre)
            if (op == 0x02EA) ForceCutsceneEnd("la cinematique n'a pas pu etre passee");   // jamais abandonnee
            else Log("miroir : commande %04X abandonnee (entite absente)", op);
        }
        g_qHead = (g_qHead + 1) % QUEUE;
    }
}
