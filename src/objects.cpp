// Objets du decor deplaces (caisses, barils, poteaux renverses, poubelles...) : meme place chez tous.
// Un objet de la carte (CObject, pool 0xB7449C, 0x19C par case) garde son objet factice d'origine (m_pDummyObject
// +0x170) : sa position d'origine et son modele l'identifient sur chaque PC. Celui qui est le plus pres d'un objet
// deplace (il l'a pousse, renverse) envoie sa position, son orientation et sa vitesse (MSG_OBJECT, relaye par l'hote) ;
// les autres posent le leur la (rendu mobile : CEntity::SetIsStatic, vtable[4]) et ne le renvoient pas pendant 1 s.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "objects.h"
#include <string.h>
#include <math.h>

using namespace game;

static bool InGame() { return GameState() == 9 && FindPlayerPed() != nullptr; }

struct ObjPool { uint8_t *objects, *flags; int size; };
static ObjPool *ObjectPool() { return *(ObjPool **)0xB7449C; }
static uint8_t *ObjAt(int i) { return ObjectPool()->objects + i * 0x19C; }
static void *Dummy(uint8_t *o) { return *(void **)(o + 0x170); }

struct Track { void *obj; uint32_t remoteUntil, lastSend; float lastPos[3]; };
static Track g_track[256];

static Track &TrackOf(void *obj)
{
    Track *freeSlot = nullptr;
    for (auto &t : g_track) { if (t.obj == obj) return t; if (!t.obj && !freeSlot) freeSlot = &t; }
    if (!freeSlot) freeSlot = &g_track[GetTickCount() % 256];
    *freeSlot = { obj };
    return *freeSlot;
}

static float Dist2(const float *a, const float *b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

// Le joueur local est-il le plus proche de cet objet parmi les joueurs ?
static bool Closest(const float *p, float myD2)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i == g_localId || !g_players[i].connected || !g_players[i].state.inGame) continue;
        if (Dist2(g_players[i].state.pos, p) < myD2) return false;
    }
    return true;
}

static void SendFrame()
{
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (now - last < 100) return;
    last = now;
    ObjPool *pool = ObjectPool();
    void *me = FindPlayerPed();
    if (!pool || !me) return;
    const float *mp = EntityPos(me);
    int sent = 0;
    for (int i = 0; i < pool->size && sent < 12; i++) {
        if (pool->flags[i] & 0x80) continue;
        uint8_t *o = ObjAt(i);
        void *d = Dummy(o);
        if (!d) continue;
        const float *p = EntityPos(o), *orig = EntityPos(d);
        float myD2 = Dist2(p, mp);
        if (myD2 > 60.0f * 60.0f) continue;
        const float *sp = MoveSpeed(o);
        bool moving = sp[0] * sp[0] + sp[1] * sp[1] + sp[2] * sp[2] > 0.0001f;
        bool displaced = Dist2(p, orig) > 0.3f * 0.3f;
        if (!moving && !displaced) continue;
        Track &t = TrackOf(o);
        if ((int)(t.remoteUntil - now) > 0) continue;   // pose par un autre joueur : on ne le renvoie pas
        // en mouvement : 10 fois par seconde ; au repos deplace : une fois a l'arret puis toutes les 3 s
        bool changed = Dist2(p, t.lastPos) > 0.02f * 0.02f;
        if (!(moving || changed) && now - t.lastSend < 3000) continue;
        if (!Closest(p, myD2)) continue;
        MsgObject m = {};
        m.type = MSG_OBJECT;
        m.from = (uint8_t)(g_localId < 0 ? 0 : g_localId);
        m.model = *(int16_t *)(o + 0x22);
        memcpy(m.orig, orig, 12);
        memcpy(m.pos, p, 12);
        if (uint8_t *mat = *(uint8_t **)(o + 0x14)) { memcpy(m.right, mat, 12); memcpy(m.fwd, mat + 0x10, 12); }
        else continue;
        memcpy(m.speed, sp, 12);
        NetSendToAll(&m, sizeof(m));
        t.lastSend = now;
        memcpy(t.lastPos, p, 12);
        sent++;
    }
}

static void OnObject(const MsgObject &m)
{
    if (!InGame() || m.from == g_localId) return;
    ObjPool *pool = ObjectPool();
    if (!pool) return;
    uint8_t *best = nullptr;
    float bestD = 0.5f * 0.5f;
    for (int i = 0; i < pool->size; i++) {
        if (pool->flags[i] & 0x80) continue;
        uint8_t *o = ObjAt(i);
        void *d = Dummy(o);
        if (!d || *(int16_t *)(o + 0x22) != m.model) continue;
        float dd = Dist2(EntityPos(d), m.orig);
        if (dd < bestD) { bestD = dd; best = o; }
    }
    if (!best) return;   // pas charge chez nous (trop loin) : il sera pose au prochain envoi
    uint8_t *mat = *(uint8_t **)(best + 0x14);
    if (!mat) return;
    Track &t = TrackOf(best);
    t.remoteUntil = GetTickCount() + 1000;
    memcpy(t.lastPos, m.pos, 12);
    if (best[0x1C] & 4) ((void(__thiscall *)(void *, bool))(*(void ***)best)[4])(best, false);   // SetIsStatic(false)
    float *r = (float *)mat, *f = (float *)(mat + 0x10), *u = (float *)(mat + 0x20);
    bool farAway = Dist2(EntityPos(best), m.pos) > 2.0f * 2.0f;
    if (farAway) WorldRemove(best);
    memcpy(r, m.right, 12);
    memcpy(f, m.fwd, 12);
    u[0] = r[1] * f[2] - r[2] * f[1];
    u[1] = r[2] * f[0] - r[0] * f[2];
    u[2] = r[0] * f[1] - r[1] * f[0];
    memcpy(EntityPos(best), m.pos, 12);
    memcpy(MoveSpeed(best), m.speed, 12);
    if (farAway) WorldAdd(best);
    static int said;
    if (said < 20) { said++; Log("objet du decor (modele %d) deplace par le joueur %d", m.model, m.from); }
}

void ObjectsFrame()
{
    g_onObject = OnObject;
    if (!InGame() || !NetRunning()) return;
    SendFrame();
}
