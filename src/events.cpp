// Evenements du monde partages par le flux fiable (envoyes par celui qui les cause ; l'hote relaie aux autres invites) :
//  - Pickups (armes, sante, argent, collectibles, objets de mission) : CPickups::aPickUps 0x9788C0, 620 x 0x20
//    (+0x4 objet, +0xC heure de reapparition, +0x10 position compressee x3 en 1/8 m, +0x18 modele, +0x1A numero de
//    reference, +0x1C type, +0x1D drapeaux : 1 desactive, 8 visible). Ramasse par le joueur local : CPickup::Update
//    (0x457410, appel 0x45902E dans CPickups::Update) renvoie vrai. Chez les autres, le meme pickup (meme modele, meme
//    place) est retire, ou desactive le temps de sa reapparition (types 2 et 15 : 30 s / 6 min, comme le jeu :
//    GetRidOfObjects 0x454CF0 puis drapeau 1), et sa poignee est ajoutee a la liste des pickups ramasses (anneau de 20
//    a 0x978628, index 0x978624) : HAS_PICKUP_BEEN_COLLECTED (0214) d'un script de l'hote la voit, un fer a cheval ou
//    un objet de mission ramasse par un invite compte donc chez l'hote.
//  - Tags (graffitis) : CTagManager::ms_tagDesc 0xA9A8C0 (150 x {CEntity*, alpha}), ms_numTags 0xA9AD70 ; un tag
//    peint par l'un est peint chez tous (SetAlpha 0x49CEC0 : met a jour le compte et affiche "tags x/100").
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "events.h"
#include <string.h>
#include <math.h>

using namespace game;

enum : uint8_t { RL_PICKUP = 30, RL_TAG = 31 };

#pragma pack(push, 1)
struct EvPickup { uint8_t type, from; int16_t model, pos[3]; uint8_t ptype; };
struct EvTag { uint8_t type, from; uint16_t index; uint8_t alpha; float x, y; };
#pragma pack(pop)

static bool InGame() { return GameState() == 9 && FindPlayerPed() != nullptr; }
static uint8_t *Pickup(int i) { return (uint8_t *)0x9788C0 + i * 0x20; }
enum { PICKUPS = 620 };

static void Broadcast(const void *d, int len, int except)
{
    if (!NetRunning()) return;
    if (!g_cfg.host) { NetSendReliable(d, len); return; }
    for (int i = 1; i < MAX_PLAYERS; i++)
        if (i != except && g_players[i].connected) NetSendReliableTo(i, d, len);
}

// --- Pickups ---
static EvPickup g_pickQueue[16];
static int g_pickQueued;

typedef bool(__thiscall *PickupUpdate_t)(void *pickup, void *ped, void *veh, int playerId);
static bool __fastcall h_PickupUpdate(uint8_t *pk, void *, void *ped, void *veh, int playerId)
{
    EvPickup e = { RL_PICKUP, (uint8_t)(g_localId < 0 ? 0 : g_localId), *(int16_t *)(pk + 0x18),
                   { *(int16_t *)(pk + 0x10), *(int16_t *)(pk + 0x12), *(int16_t *)(pk + 0x14) }, pk[0x1C] };
    bool r = ((PickupUpdate_t)0x457410)(pk, ped, veh, playerId);
    if (r && g_pickQueued < 16) g_pickQueue[g_pickQueued++] = e;
    return r;
}

static void ApplyPickup(const EvPickup &e)
{
    int best = -1, bestD = 9;   // meme modele, a moins d'un metre
    for (int i = 0; i < PICKUPS; i++) {
        uint8_t *p = Pickup(i);
        if (!p[0x1C] || *(int16_t *)(p + 0x18) != e.model) continue;
        int d = 0;
        for (int k = 0; k < 3; k++) { int a = abs(*(int16_t *)(p + 0x10 + k * 2) - e.pos[k]); if (a > d) d = a; }
        if (d < bestD) { bestD = d; best = i; }
    }
    static int said;
    if (best < 0) { if (said < 30) { said++; Log("pickup ramasse par le joueur %d (modele %d) : pas chez nous", e.from, e.model); } return; }
    uint8_t *p = Pickup(best);
    int t = p[0x1C];
    if (t == 2 || t == 15) {   // reapparait : desactive comme le jeu
        if (!(p[0x1D] & 1)) {
            *(uint32_t *)(p + 0xC) = *(uint32_t *)0xB7CB84 + (t == 15 ? 360000 : 30000);
            ((void(__thiscall *)(void *))0x454CF0)(p);   // CPickup::GetRidOfObjects
            p[0x1D] |= 1;
        }
    } else if (t == 1 || t == 7 || (t >= 9 && t <= 12) || (t >= 16 && t <= 18) || t == 21) {
        return;   // magasins, mines, proprietes : chacun les siens
    } else {
        uint32_t handle = ((uint32_t)*(uint16_t *)(p + 0x1A) << 16) | (uint32_t)best;
        uint16_t &n = *(uint16_t *)0x978624;
        ((uint32_t *)0x978628)[n] = handle;   // vu par HAS_PICKUP_BEEN_COLLECTED des scripts
        if (++n >= 20) n = 0;
        ((void(__cdecl *)(uint32_t))0x4573D0)(handle);   // CPickups::RemovePickUp (0215)
    }
    if (said < 30) { said++; Log("pickup ramasse par le joueur %d (modele %d, type %d) : retire chez nous", e.from, e.model, t); }
}

// --- Tags ---
static uint8_t g_tagSeen[150];
static bool g_tagInit;
static uint8_t *TagDesc(int i) { return (uint8_t *)0xA9A8C0 + i * 8; }
static int NumTags() { int n = *(int *)0xA9AD70; return n < 0 ? 0 : n > 150 ? 150 : n; }

static void TagsFrame()
{
    static uint32_t last;
    if (GetTickCount() - last < 300) return;
    last = GetTickCount();
    int n = NumTags();
    for (int i = 0; i < n; i++) {
        uint8_t a = TagDesc(i)[4];
        if (!g_tagInit) { g_tagSeen[i] = a; continue; }
        // peint un peu plus (ou fini) depuis le dernier envoi
        if (a > g_tagSeen[i] && (a - g_tagSeen[i] >= 40 || (a > 228 && g_tagSeen[i] <= 228))) {
            void *ent = *(void **)TagDesc(i);
            const float *pos = ent ? EntityPos(ent) : nullptr;
            EvTag e = { RL_TAG, (uint8_t)(g_localId < 0 ? 0 : g_localId), (uint16_t)i, a, pos ? pos[0] : 0, pos ? pos[1] : 0 };
            Broadcast(&e, sizeof(e), -1);
            g_tagSeen[i] = a;
        }
    }
    g_tagInit = true;
}

static void ApplyTag(const EvTag &e)
{
    int n = NumTags(), i = e.index;
    auto isNear = [&](int k) {
        void *ent = *(void **)TagDesc(k);
        if (!ent) return false;
        const float *p = EntityPos(ent);
        return fabsf(p[0] - e.x) < 2.0f && fabsf(p[1] - e.y) < 2.0f;
    };
    if (i >= n || !isNear(i)) { i = -1; for (int k = 0; k < n; k++) if (isNear(k)) { i = k; break; } }
    if (i < 0) return;
    void *ent = *(void **)TagDesc(i);
    if (TagDesc(i)[4] >= e.alpha) return;
    ((void(__cdecl *)(void *, uint8_t))0x49CEC0)(ent, e.alpha);   // CTagManager::SetAlpha
    g_tagSeen[i] = e.alpha;
    static int said;
    if (said < 20 && e.alpha > 228) { said++; Log("tag %d peint par le joueur %d", i, e.from); }
}

// --- Commun ---
bool EventsReliable(int from, const uint8_t *d, int len)
{
    if (len < 2) return false;
    if (d[0] == RL_PICKUP && len >= (int)sizeof(EvPickup)) {
        EvPickup e = *(const EvPickup *)d;
        if (g_cfg.host) { e.from = (uint8_t)from; Broadcast(&e, sizeof(e), from); }
        if (InGame()) ApplyPickup(e);
        return true;
    }
    if (d[0] == RL_TAG && len >= (int)sizeof(EvTag)) {
        EvTag e = *(const EvTag *)d;
        if (g_cfg.host) { e.from = (uint8_t)from; Broadcast(&e, sizeof(e), from); }
        if (InGame()) ApplyTag(e);
        return true;
    }
    return false;
}

void EventsFrame()
{
    if (!InGame()) { g_pickQueued = 0; g_tagInit = false; return; }
    for (int k = 0; k < g_pickQueued; k++) {
        Broadcast(&g_pickQueue[k], sizeof(EvPickup), -1);
        static int said;
        if (said < 30) { said++; Log("pickup ramasse (modele %d, type %d) : envoye aux autres", g_pickQueue[k].model, g_pickQueue[k].ptype); }
    }
    g_pickQueued = 0;
    if (NetRunning()) TagsFrame();
}

void InstallEvents()
{
    const uint8_t *c = (const uint8_t *)0x45902E;
    if (c[0] == 0xE8 && (uintptr_t)(c + 5) + *(int32_t *)(c + 1) == 0x457410) PatchCall(0x45902E, (void *)h_PickupUpdate);
    else Log("evenements : appel de CPickup::Update introuvable, pickups non partages");
}
