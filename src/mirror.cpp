// Miroir des missions (la methode de VCCoop). Les missions ne tournent que chez l'hote (script.cpp) ; ce qu'elles
// montrent a l'ecran est rejoue chez les invites : l'hote capture une liste blanche de commandes de ses scripts de
// mission, avec les parametres evalues, et les envoie sur le flux fiable (ordonne, acquitte) ; l'invite les execute
// dans un script fantome a lui (meme gestionnaire du jeu, parametres reecrits en constantes).
//  - Traduction : personnage de l'hote (sa reference de pool = id MsgPed) -> sa copie ; vehicule (reference de pool
//    chez l'hote, MsgVehicle.ownerRef) -> sa copie ; marqueurs et spheres crees -> ceux de l'invite.
//  - Une commande dont l'entite n'existe pas encore chez l'invite attend en tete de file (3 s au plus).
//  - Debut de mission (START_MISSION de l'hote) : un invite a plus de 40 m est pose derriere l'hote.
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
#include <string.h>
#include <math.h>

using namespace game;

enum : uint8_t { RL_MIRROR = 1, RL_MISSION_END = 2, RL_MISSION_START = 3 };

// Signature de chaque commande : i entier, f reel, s texte, P personnage, V vehicule, b/B marqueur (entree/sortie),
// q/Q sphere (entree/sortie). (A l'envoi, un P qui designe le joueur de l'hote devient H : son pantin chez l'invite.)
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
    { 0x02EB, "" },        // RESTORE_CAMERA_JUMPCUT
    { 0x0373, "" },        // SET_CAMERA_BEHIND_PLAYER
    { 0x02E4, "s" },       // LOAD_CUTSCENE (CCutsceneMgr::LoadCutsceneData 0x4D5E80)
    { 0x02E7, "" },        // START_CUTSCENE (0x5B1460) : attend que la cinematique soit chargee
    { 0x02EA, "" },        // CLEAR_CUTSCENE (0x4D5ED0) : attend que celle de l'invite soit finie
    { 0x0055, "ifff" },    // SET_PLAYER_COORDINATES : l'invite est pose a cote (decale selon son numero)
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

struct Capture { const MirrorOp *op; int n; Param p[8]; };
static Capture g_cap;
static bool g_capturing;

// Hote : une mission de l'histoire demarre (script.cpp) : position de l'hote pour le regroupement des invites.
void MirrorMissionStart()
{
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
            int v = (p.kind == 'B' || p.kind == 'Q') && p.out ? *p.out : p.value;   // sortie : le handle de l'hote
            memcpy(buf + len, &v, 4); len += 4;
        }
    }
    NetSendReliable(buf, len);
    (void)script;
}

// --- Invite : rejeu ---
enum { MAX_MAP = 128 };
struct HandleMap { int host, guest; };
static HandleMap g_blips[MAX_MAP], g_spheres[MAX_MAP];
static int MapGet(HandleMap *m, int host)
{
    for (int i = 0; i < MAX_MAP; i++) if (m[i].host == host && host) return m[i].guest;
    return 0;
}
static void MapSet(HandleMap *m, int host, int guest)
{
    for (int i = 0; i < MAX_MAP; i++) if (m[i].host == host) { m[i].guest = guest; return; }
    for (int i = 0; i < MAX_MAP; i++) if (!m[i].host) { m[i].host = host; m[i].guest = guest; return; }
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

// Rejoue une commande ; faux si une entite manque encore (on reessaiera).
static bool Replay(const uint8_t *d, int len)
{
    if (d[0] == RL_MISSION_END) {
        for (auto &b : g_blips) if (b.host) {
            *(uint16_t *)g_code = 0x0164;
            g_code[2] = 1; memcpy(g_code + 3, &b.guest, 4);
            ExecCommand(0x0164);
            b.host = b.guest = 0;
        }
        for (auto &s : g_spheres) if (s.host) {
            *(uint16_t *)g_code = 0x03BD;
            g_code[2] = 1; memcpy(g_code + 3, &s.guest, 4);
            ExecCommand(0x03BD);
            s.host = s.guest = 0;
        }
        RestoreScreen();
        Log("miroir : fin de mission de l'hote, ecran retabli");
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
    if (op == 0x02EA && CutsceneRunning() && !CutsceneFinished()) return false;   // l'invite a demarre un peu apres
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
            void *ped = PuppetOf(0);
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
        case 'B': case 'Q':
            outKind[out] = kind; outHost[out] = v; outIdx[out] = out;
            g_code[c++] = 3; *(uint16_t *)(g_code + c) = (uint16_t)out; c += 2;
            out++;
            continue;
        }
        g_code[c++] = kind == 'f' ? 6 : 1;
        memcpy(g_code + c, &v, 4); c += 4;
    }
    if (op == 0x0055 && c >= 2 + 20) {   // SET_PLAYER_COORDINATES : a cote de l'hote, decale selon le numero
        float x;
        memcpy(&x, g_code + 2 + 5 + 1, 4);
        x += 1.5f * g_localId;
        memcpy(g_code + 2 + 5 + 1, &x, 4);
    }
    ExecCommand(op);
    static int logged;
    if (logged < 300) { logged++; Log("miroir : %04X rejouee", op); }
    for (int k = 0; k < out; k++) {
        int guest = *(int *)(g_script + 0x3C + outIdx[k] * 4);
        MapSet(outKind[k] == 'B' ? g_blips : g_spheres, outHost[k], guest);
    }
    if (op == 0x0164) MapDel(g_blips, *(const int *)(d + 5));
    if (op == 0x03BD) MapDel(g_spheres, *(const int *)(d + 5));
    return true;
}

// Ecran de l'invite apres une mission de l'hote : fondu d'entree, format normal, controles, camera, textes, sons.
static void RestoreScreen()
{
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

// Execute une commande a parametres entiers dans le script fantome (autotests, retablissement de l'ecran).
void RunScriptCommand(int op, int nargs, const int *args)
{
    memcpy(g_script + 8, "sacoop", 7);
    int c = 2;
    *(uint16_t *)g_code = (uint16_t)op;
    for (int k = 0; k < nargs; k++) { g_code[c++] = 1; memcpy(g_code + c, &args[k], 4); c += 4; }
    ExecCommand(op);
}

void MirrorFrame()
{
    g_onReliable = OnReliable;
    if (g_cfg.host || GameState() != 9 || !FindPlayerPed()) return;
    memcpy(g_script + 8, "sacoop", 7);
    uint32_t now = GetTickCount();
    for (int budget = 0; budget < 64 && g_qHead != g_qTail; budget++) {
        Pending &p = g_queue[g_qHead];
        if (!p.since) p.since = now;
        if (!Replay(p.data, p.len)) {
            int op = p.len > 2 ? *(const uint16_t *)(p.data + 1) : 0;
            uint32_t patience = op == 0x02E7 ? 20000 : op == 0x02EA ? 10000 : 3000;
            if (now - p.since < patience) break;   // entite ou cinematique pas encore prete : on attend (dans l'ordre)
            Log("miroir : commande %04X abandonnee (entite absente)", p.len > 2 ? *(const uint16_t *)(p.data + 1) : 0);
        }
        g_qHead = (g_qHead + 1) % QUEUE;
    }
}
