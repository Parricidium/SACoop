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
#include "mirror.h"
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

// --- Mission objects (0.54.29) ---
// The host's mission creates its objects by script (CREATE_OBJECT 0107 / 029B: CObject +0x13C type 2 or 6), and the mirror
// creates the same on the guest. But only the creation and a few commands were repeated: a crate carried by a ped, moved
// by a forklift, attached, slid or made visible later stayed where (and as) the guest's copy began, or unseen (test of
// 15/10: the crates of Home Invasion and the boxes of Robbing Uncle Sam were not seen by the guest). The host sends, for
// each of its mission objects within 150 m of a guest, its script handle, pose, area and visible / collision flags
// (MSG_MOBJECT: moving ones 10 times a second, still ones every 2 s); the guest puts his copy (MirrorGuestObject) there.
static int ObjRef(int i) { return (i << 8) | ObjectPool()->flags[i]; }
static uint8_t *ObjFromRef(int h)
{
    ObjPool *pool = ObjectPool();
    int i = h >> 8;
    if (!pool || h <= 0 || i >= pool->size || pool->flags[i] != (uint8_t)(h & 0xFF)) return nullptr;
    return ObjAt(i);
}
static bool IsMissionObject(uint8_t *o) { return o[0x13C] == 2 || o[0x13C] == 6; }

static void HostMissionObjects()
{
    static uint32_t last;
    static float sentPos[1024][3];
    static uint32_t sentAt[1024];
    uint32_t now = GetTickCount();
    if (now - last < 100) return;
    last = now;
    ObjPool *pool = ObjectPool();
    if (!pool) return;
    int sent = 0;
    for (int i = 0; i < pool->size && i < 1024 && sent < 24; i++) {
        if (pool->flags[i] & 0x80) continue;
        uint8_t *o = ObjAt(i);
        if (!IsMissionObject(o)) continue;
        uint8_t *mat = *(uint8_t **)(o + 0x14);
        if (!mat) continue;
        const float *p = EntityPos(o);
        bool guestNear = false;
        for (int g = 1; g < MAX_PLAYERS && !guestNear; g++)
            guestNear = g_players[g].connected && g_players[g].state.inGame && Dist2(g_players[g].state.pos, p) < 150.0f * 150.0f;
        if (!guestNear) continue;
        bool moved = Dist2(p, sentPos[i]) > 0.03f * 0.03f;
        if (!moved && now - sentAt[i] < 2000) continue;
        MsgMObject m = {};
        m.type = MSG_MOBJECT;
        m.area = o[0x2F];
        m.model = *(int16_t *)(o + 0x22);
        m.handle = ObjRef(i);
        m.flags = *(uint32_t *)(o + 0x1C);
        m.attached = *(void **)(o + 0xFC) != nullptr;
        memcpy(m.pos, p, 12);
        memcpy(m.right, mat, 12);
        memcpy(m.fwd, mat + 0x10, 12);
        memcpy(m.speed, MoveSpeed(o), 12);
        NetSendToAll(&m, sizeof(m));
        memcpy(sentPos[i], p, 12);
        sentAt[i] = now;
        sent++;
    }
}

// Guest: the last state of each host object, applied on every frame (0.54.30). Applied only when a message came, the
// copy was left to the local physics in between: boxes in the back of the van (attached to it on the host, loose on the
// guest) were thrown out by the van copy, which is moved by placement, not by physics (test of 15/10). Now the copy's
// own physics is off while it is followed (CPhysical bDontApplySpeed +0x40 bit 13): it is put at the host's position,
// carried along its speed for up to 0.3 s between messages; an object attached on the host has no collision here.
struct Followed { int hostHandle, guest; MsgMObject m; uint32_t at; };
static Followed g_followed[128];

static void PlaceObject(uint8_t *o, const MsgMObject &m, float ahead)
{
    uint8_t *mat = *(uint8_t **)(o + 0x14);
    if (!mat) return;
    float pos[3];
    for (int k = 0; k < 3; k++) pos[k] = m.pos[k] + m.speed[k] * ahead;
    float off = Dist2(EntityPos(o), pos);
    bool farAway = off > 4.0f;
    if (farAway) WorldRemove(o);
    float *r = (float *)mat, *f = (float *)(mat + 0x10), *u = (float *)(mat + 0x20);
    memcpy(r, m.right, 12);
    memcpy(f, m.fwd, 12);
    u[0] = r[1] * f[2] - r[2] * f[1];
    u[1] = r[2] * f[0] - r[0] * f[2];
    u[2] = r[0] * f[1] - r[1] * f[0];
    memcpy(EntityPos(o), pos, 12);
    memset(MoveSpeed(o), 0, 12);
    memset((uint8_t *)o + 0x50, 0, 12);   // turn speed
    if (farAway) WorldAdd(o);
    if (*(void **)(o + 0x18)) {
        ((void(__thiscall *)(void *))0x446F90)(o);   // CEntity::UpdateRW
        ((void(__thiscall *)(void *))0x532B00)(o);   // CEntity::UpdateRwFrame
    }
}

static void OnMObject(const MsgMObject &m)
{
    if (g_cfg.host || !InGame()) return;
    int h = MirrorGuestObject(m.handle);
    static int saidNone, saidFix;
    if (!h) {   // (not created here: the creation was not repeated or is still waiting for its model)
        static int last[32];
        bool seen = false;
        for (int x : last) if (x == m.handle) seen = true;
        if (!seen && saidNone < 30) { last[saidNone % 32] = m.handle; saidNone++; Log("objet de mission de l'hote %d (modele %d) : pas de copie chez l'invite", m.handle, m.model); }
        return;
    }
    uint8_t *o = ObjFromRef(h);
    if (!o || *(void **)(o + 0xFC)) return;   // (attached to something here: the game places it)
    uint32_t fl = *(uint32_t *)(o + 0x1C);
    float off = Dist2(EntityPos(o), m.pos);
    if ((o[0x2F] != m.area || (fl & 0x80) != (m.flags & 0x80) || off > 1.0f) && saidFix < 40) {
        saidFix++;
        Log("objet de mission %d (hote %d, modele %d) : chez l'invite zone %d visible %d a %.1f m, chez l'hote zone %d visible %d%s -> corrige",
            h, m.handle, m.model, o[0x2F], (fl & 0x80) ? 1 : 0, sqrtf(off), m.area, (m.flags & 0x80) ? 1 : 0, m.attached ? ", attache" : "");
    }
    Followed *slot = nullptr, *freeSlot = nullptr;
    for (auto &f : g_followed) { if (f.hostHandle == m.handle) { slot = &f; break; } if (!f.hostHandle && !freeSlot) freeSlot = &f; }
    if (!slot) slot = freeSlot;
    if (!slot) return;
    slot->hostHandle = m.handle;
    slot->guest = h;
    slot->m = m;
    slot->at = GetTickCount();
}

static void GuestFollowObjects()
{
    uint32_t now = GetTickCount();
    float step = *(float *)0xB7CB5C;   // CTimer::ms_fTimeStep (speeds are per 1/50 s)
    for (auto &f : g_followed) {
        if (!f.hostHandle) continue;
        uint8_t *o = ObjFromRef(f.guest);
        bool gone = !o || MirrorGuestObject(f.hostHandle) != f.guest;
        if (gone || now - f.at > 3000) {   // no news for 3 s (the host's object is still, or too far): the game takes it back
            if (!gone) *(uint32_t *)(o + 0x40) &= ~0x2000u;
            f.hostHandle = 0;
            continue;
        }
        if (*(void **)(o + 0xFC)) continue;
        o[0x2F] = f.m.area;
        uint32_t &fl = *(uint32_t *)(o + 0x1C);
        fl = (fl & ~0x81u) | (f.m.flags & 0x80) | (f.m.attached ? 0 : (f.m.flags & 1));   // visible; collision unless attached
        *(uint32_t *)(o + 0x40) |= 0x2000;   // bDontApplySpeed: we move it
        float dt = (now - f.at) / 20.0f;     // in 1/50 s steps
        if (dt > 15.0f) dt = 15.0f;
        (void)step;
        PlaceObject(o, f.m, dt);
    }
}

// Host and guest: CObject::Render (0x59FD50, checked in gta_sa.exe 1.0 US) with no RW object reads it anyway when the
// object is scaled (+0x15C != 1) or flagged 0x20000 (0x59FE47): the host crashed there after Robbing Uncle Sam (15/10).
// An object with no RW object has nothing to draw: skipped.
typedef void(__thiscall *ObjRender_t)(void *);
static ObjRender_t o_ObjRender;
static void __fastcall h_ObjRender(void *obj, void *)
{
    if (!*(void **)((uint8_t *)obj + 0x18)) {
        static int said;
        if (said < 5) { said++; Log("objet (modele %d) a dessiner sans modele 3D : ignore", *(int16_t *)((uint8_t *)obj + 0x22)); }
        return;
    }
    o_ObjRender(obj);
}
void InstallObjects()
{
    static const uint8_t pro[] = { 0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1 };
    o_ObjRender = (ObjRender_t)MakeDetour(0x59FD50, pro, sizeof(pro), (void *)h_ObjRender);
}

void ObjectsFrame()
{
    g_onObject = OnObject;
    g_onMObject = OnMObject;
    if (!InGame() || !NetRunning()) return;
    SendFrame();
    if (g_cfg.host) HostMissionObjects(); else GuestFollowObjects();
}
