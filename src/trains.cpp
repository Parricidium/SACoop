// Train sync.
//
// Why trains need their own sync: a train is not a free vehicle. The game keeps, for the engine (front carriage), a distance
// along one of the four tracks (m_fCurrentRailDistance) and a speed (m_fTrainSpeed); every other carriage follows the one in
// front of it, and CTrain::ProcessControl puts the whole train on the rails from those two numbers. Each PC used to run its
// own random trains (and the car sync could even delete a single carriage), so the players never saw the same train.
//
// Design (host = reference, like the rest of the mod):
//  - Trains are registered by their engine. The host gives each one an id and sends MSG_TRAIN (15 times a second, 30 while a
//    player drives): track, direction, node, rail distance, speed, the models of the carriages, and the "train type" the game
//    built it from. That type is learnt by hooking the calls to CTrain::CreateMissionTrain (0x6F7550), which is what both the
//    random generator and the CREATE_MISSION_TRAIN script command use.
//  - A guest builds its own copy with the same function and the same type (so the same engine and carriages), then each
//    frame, right before CGame::Process, writes the interpolated rail distance and speed of the owner into the engine. The
//    game places the carriages itself. The same 110 ms delay as the players and vehicles (interp.cpp), so a player riding in a
//    train is where the train is.
//  - The guest's own random trains are switched off (CTrain::bDisableRandomTrains) while the host shares its trains, and any
//    random train that is still there is removed. Mission trains the guest's own scripts make are left alone.
//  - Whoever drives a train owns it: the owner sends, the others follow (a guest who gets in the cab of a copy takes it over;
//    when he gets out he sends a few "release" messages and the host takes over again).
//  - Trains never go through the vehicle sync (vehicles.cpp, population.cpp skip them): their copies are not free vehicles.
// Known limits: the host's generator only builds trains near the host, so a guest far away sees none; a player inside a train is
// shown as a ped standing in it (no seat animation); a derailed train is not synced.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "interp.h"
#include "vehicles.h"
#include "peds.h"
#include "trains.h"
#include <math.h>
#include <string.h>

using namespace game;

namespace {

enum { MAX_TRAINS = 8, MAX_CARS = 8, MADE_SLOTS = 16 };
// CTrain fields (plugin-sdk, validated offsets): node +0x5A0 (int16), speed +0x5A4, rail distance +0x5A8, flags +0x5B8 (uint16),
// track +0x5C0 (int8), previous / next carriage +0x5D0 / +0x5D4.
enum : int { T_NODE = 0x5A0, T_SPEED = 0x5A4, T_DIST = 0x5A8, T_FLAGS = 0x5B8, T_TRACK = 0x5C0, T_PREV = 0x5D0, T_NEXT = 0x5D4 };
enum : unsigned { TF_STOPPED = 1u << 1, TF_FRONT = 1u << 3, TF_MISSION = 1u << 5, TF_CLOCKWISE = 1u << 6, TF_DERAILED = 1u << 8 };

struct NetTrain {
    uint32_t id;          // 0 = free slot; (host << 24) | counter
    uint8_t owner;        // player who drives it (0 = the host)
    void *eng;            // our engine (front carriage), nullptr if we have none yet
    int ref;              // its pool reference
    int type;             // CreateMissionTrain "train type" (-1 unknown)
    uint8_t cars;
    uint16_t models[MAX_CARS];
    bool driving;         // the local player drives it
    uint32_t lastRecv, lastSend, relUntil, retryAt;
    uint8_t fails;
    bool haveUnw;
    float unw;            // last received rail distance, unwrapped (continuous over the loop point of the track)
    MsgTrain last;        // newest message (a copy is built from it)
    Track tr;             // received states: pos[0] = unwrapped distance, vel[0] = speed (units per 1/50 s)
};
NetTrain g_tr[MAX_TRAINS];
uint32_t g_counter;
bool g_hookOk;

// Trains the game built since the hook is in: engine, pool reference and type.
struct Made { void *eng; int ref; int type; };
Made g_made[MADE_SLOTS];
int g_madeN;

// --- Game access ---
Pool *VehPool() { return *(Pool **)0xB74494; }
bool InPool(void *v)
{
    Pool *p = VehPool();
    if (!p || !v) return false;
    intptr_t off = (uint8_t *)v - p->objects;
    if (off < 0 || off % 0xA18 || off / 0xA18 >= p->size) return false;
    return !(p->flags[off / 0xA18] & 0x80);
}
int RefOf(void *v)
{
    Pool *p = VehPool();
    int i = (int)(((uint8_t *)v - p->objects) / 0xA18);
    return (i << 8) | p->flags[i];
}
void *FromRef(int ref)
{
    Pool *p = VehPool();
    int i = ref >> 8;
    if (!p || i < 0 || i >= p->size || (p->flags[i] & 0x80) || p->flags[i] != (ref & 0xFF)) return nullptr;
    return p->objects + i * 0xA18;
}
uint16_t &Flags(void *t) { return *(uint16_t *)((uint8_t *)t + T_FLAGS); }
bool IsFront(void *t) { return (Flags(t) & TF_FRONT) != 0; }
void *Next(void *t) { return *(void **)((uint8_t *)t + T_NEXT); }
void *Prev(void *t) { return *(void **)((uint8_t *)t + T_PREV); }
float TrackTotal(int track)
{
    if (track < 0 || track > 3) return 0.0f;
    float t = ((float *)0xC37FEC)[track];   // arrTotalTrackLength
    return t > 100.0f && t < 100000.0f ? t : 0.0f;
}
// The front carriage of the train this carriage belongs to (nullptr if the chain is not valid).
void *FrontOf(void *t)
{
    for (int k = 0; k < 16 && t; k++) {
        if (!InPool(t) || !IsTrain(t)) return nullptr;
        if (IsFront(t) || !Prev(t)) return t;
        t = Prev(t);
    }
    return nullptr;
}
int Chain(void *front, void **out, int max)
{
    int n = 0;
    for (void *c = front; c && n < max; c = Next(c)) {
        if (!InPool(c) || !IsTrain(c)) break;
        out[n++] = c;
    }
    return n;
}

// --- Table ---
NetTrain *FindById(uint32_t id)
{
    for (auto &n : g_tr) if (n.id == id) return &n;
    return nullptr;
}
NetTrain *FindByEng(void *eng)
{
    for (auto &n : g_tr) if (n.id && n.eng == eng) return &n;
    return nullptr;
}
NetTrain *NewSlot()
{
    for (auto &n : g_tr) if (!n.id) { memset(&n, 0, sizeof(n)); n.type = -1; return &n; }
    return nullptr;
}
bool Alive(const NetTrain &n) { return n.id && n.eng && FromRef(n.ref) == n.eng; }

// Is the local player the driver of this train's engine?
bool LocalDrives(void *eng)
{
    void *me = FindPlayerPed();
    return me && eng && PedVehicle(me) == eng && Field<void *>(eng, VEH_DRIVER) == me;
}
// Is the local player in any carriage of this train?
bool LocalInside(void **cars, int n)
{
    void *me = FindPlayerPed();
    void *mv = me ? PedVehicle(me) : nullptr;
    if (!mv) return false;
    for (int i = 0; i < n; i++) if (cars[i] == mv) return true;
    return false;
}

// --- CreateMissionTrain ---
typedef int(__cdecl *CreateTrain_t)(CVector, bool, unsigned, void **, void **, int, int, bool);
const CreateTrain_t kCreateTrain = (CreateTrain_t)0x6F7550;

// Every call of the game to CreateMissionTrain lands here; we only note which type built which engine.
int __cdecl h_CreateMissionTrain(CVector posn, bool clockwise, unsigned type, void **first, void **last, int node, int track, bool mission)
{
    int r = kCreateTrain(posn, clockwise, type, first, last, node, track, mission);
    void *eng = first ? *first : nullptr;
    if (eng && InPool(eng) && IsTrain(eng)) {
        bool known = false;
        for (auto &m : g_made) if (m.eng == eng && m.ref == RefOf(eng)) known = true;
        if (!known) g_made[g_madeN++ % MADE_SLOTS] = { eng, RefOf(eng), (int)type };
    }
    return r;
}
int MadeType(void *eng)
{
    for (auto &m : g_made) if (m.eng == eng && m.ref == RefOf(eng)) return m.type;
    return -1;
}

// --- Removing a train ---
// Every carriage out of the world and deleted; ordinary peds inside (random passengers, driver) go with it. Never while the
// local player is in it.
bool RemoveChain(void *front, const char *why)
{
    void *cars[12];
    int n = Chain(front, cars, 12);
    if (!n || LocalInside(cars, n)) return false;
    for (int i = 0; i < n; i++) {
        void *occ[9] = { Field<void *>(cars[i], VEH_DRIVER) };
        for (int k = 0; k < 8; k++) occ[k + 1] = Field<void *>(cars[i], VEH_PASSENGERS + k * 4);
        WorldRemove(cars[i]);
        RemoveReferencesToDeletedObject(cars[i]);
        DeleteEntity(cars[i]);
        for (void *p : occ)
            if (p && p != FindPlayerPed() && PuppetIndex(p) < 0) ((void(__cdecl *)(void *))0x610F20)(p);   // CPopulation::RemovePed
    }
    Log("trains: %s removed (%d carriages)", why, n);
    return true;
}
void Drop(NetTrain &n, const char *why)
{
    if (Alive(n) && IsFront(n.eng) && !RemoveChain(n.eng, why)) return;   // the local player is inside: later
    memset(&n, 0, sizeof(n));
}

// --- Messages ---
float Unwrap(NetTrain &n, float dist, float total)
{
    if (!n.haveUnw) { n.haveUnw = true; n.unw = dist; return dist; }
    float base = fmodf(n.unw, total);
    if (base < 0) base += total;
    float d = dist - base;
    if (d > total * 0.5f) d -= total;
    else if (d < -total * 0.5f) d += total;
    n.unw += d;
    if (n.unw > total * 3.0f || n.unw < -total) {   // keep the numbers small: shift the whole track
        float shift = n.unw > 0 ? total * 2.0f : -total * 2.0f;
        n.unw -= shift;
        for (int i = 0; i < n.tr.count; i++) n.tr.s[i].pos[0] -= shift;
    }
    return n.unw;
}

void OnTrain(const MsgTrain &m)
{
    if (!g_cfg.syncTrains || m.owner == g_localId || m.cars > MAX_CARS) return;
    NetTrain *n = FindById(m.id);
    if (m.flags & TRF_GONE) {
        if (n && !g_cfg.host && n->owner == m.owner) Drop(*n, "train gone at the host");
        return;
    }
    if (!n) {
        if (g_cfg.host) return;   // the host knows every train
        n = NewSlot();
        if (!n) return;
        n->id = m.id;
        n->owner = m.owner;
    }
    if (m.flags & TRF_RELEASE) {   // its driver got out: the host is the owner again
        if (n->owner == m.owner) { n->owner = 0; n->tr.Clear(); n->haveUnw = false; Log("trains: %08X back to the host", n->id); }
        return;
    }
    if (n->driving || (n->owner == g_localId && g_localId > 0)) return;   // we drive it: the others' states are old news
    if (n->owner != m.owner) {
        n->owner = m.owner;
        n->tr.Clear();
        n->haveUnw = false;
        Log("trains: %08X is now driven by player %d", n->id, m.owner);
    }
    float total = TrackTotal(m.track);
    if (total <= 0.0f) return;
    ClockSample(m.owner, m.time);
    Snap sn = {};
    sn.t = m.time;
    sn.pos[0] = Unwrap(*n, m.dist, total);
    sn.vel[0] = m.speed;
    n->tr.Push(sn);
    n->last = m;
    n->type = m.trainType;
    n->cars = m.cars;
    for (int k = 0; k < MAX_CARS; k++) n->models[k] = m.models[k];
    n->lastRecv = GetTickCount();
}

void SendTrain(NetTrain &n, uint8_t extra)
{
    uint8_t *e = (uint8_t *)n.eng;
    MsgTrain m = {};
    m.type = MSG_TRAIN;
    m.owner = (uint8_t)g_localId;
    m.id = n.id;
    uint16_t fl = Flags(e);
    m.flags = extra;
    if (fl & TF_CLOCKWISE) m.flags |= TRF_CLOCKWISE;
    if (fl & TF_MISSION) m.flags |= TRF_MISSION;
    if (fl & TF_STOPPED) m.flags |= TRF_STOPPED;
    if (fl & TF_DERAILED) m.flags |= TRF_DERAILED;
    m.track = *(int8_t *)(e + T_TRACK);
    m.trainType = (uint8_t)(n.type < 0 || n.type > 254 ? 255 : n.type);
    m.cars = n.cars;
    m.node = *(int16_t *)(e + T_NODE);
    memcpy(m.pos, EntityPos(e), 12);
    m.dist = *(float *)(e + T_DIST);
    m.speed = *(float *)(e + T_SPEED);
    for (int k = 0; k < MAX_CARS; k++) m.models[k] = n.models[k];
    m.time = GetTickCount();
    NetSendToAll(&m, sizeof(m));
}

// --- Host: every train gets an id ---
void HostScan()
{
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (now - last < 500) return;
    last = now;
    Pool *p = VehPool();
    if (!p) return;
    for (int i = 0; i < p->size; i++) {
        if (p->flags[i] & 0x80) continue;
        uint8_t *v = p->objects + i * 0xA18;
        if (!IsTrain(v) || !IsFront(v) || FindByEng(v)) continue;
        NetTrain *n = NewSlot();
        if (!n) { static int said; if (said++ < 3) Log("trains: table full, a train is not shared"); continue; }
        n->id = ((uint32_t)(g_localId < 0 ? 0 : g_localId) << 24) | (++g_counter & 0xFFFFFF);
        n->owner = (uint8_t)(g_localId < 0 ? 0 : g_localId);
        n->eng = v;
        n->ref = RefOf(v);
        n->type = MadeType(v);
        void *cars[MAX_CARS + 4];
        int c = Chain(v, cars, MAX_CARS + 4);
        n->cars = (uint8_t)(c > MAX_CARS ? MAX_CARS : c);
        for (int k = 0; k < n->cars; k++) n->models[k] = (uint16_t)*(int16_t *)((uint8_t *)cars[k] + 0x22);
        n->lastRecv = now;
        Log("trains: %08X registered (track %d, %s, type %d, %d carriages, mission %d, engine model %d)", n->id, *(int8_t *)(v + T_TRACK),
            (Flags(v) & TF_CLOCKWISE) ? "clockwise" : "anticlockwise", n->type, (int)n->cars, (Flags(v) & TF_MISSION) ? 1 : 0, (int)n->models[0]);
        if (n->type < 0) Log("trains: %08X was not built through CreateMissionTrain (cheat, save?): it stays local", n->id);
    }
    for (auto &n : g_tr) {
        if (!n.id) continue;
        if (!Alive(n)) {   // gone: tell the guests so they do not wait for the timeout
            MsgTrain m = {};
            m.type = MSG_TRAIN; m.owner = (uint8_t)g_localId; m.id = n.id; m.flags = TRF_GONE;
            NetSendToAll(&m, sizeof(m));
            Log("trains: %08X is gone", n.id);
            memset(&n, 0, sizeof(n));
            continue;
        }
        if (n.owner != g_localId && now - n.lastRecv > 3000) { n.owner = (uint8_t)g_localId; n.tr.Clear(); n.haveUnw = false; Log("trains: %08X: its driver stopped talking, the host takes it back", n.id); }
    }
}

// --- Guest: build the copy ---
void TryCreate(NetTrain &n)
{
    uint32_t now = GetTickCount();
    if (now < n.retryAt || !n.tr.count) return;
    n.retryAt = now + (n.fails < 3 ? 1500u : 10000u);
    const MsgTrain &m = n.last;
    void *me = FindPlayerPed();
    if (!me) return;
    const float *mp = EntityPos(me);
    float dx = m.pos[0] - mp[0], dy = m.pos[1] - mp[1];
    if (dx * dx + dy * dy > 1200.0f * 1200.0f) return;   // far from us: not worth a copy
    if (m.trainType == 255 || m.cars == 0 || m.track > 3) return;
    for (int k = 0; k < m.cars; k++) if (m.models[k] < 400 || m.models[k] > 611) return;
    bool loaded = true;
    for (int k = 0; k < m.cars; k++) if (!ModelLoaded(m.models[k])) { RequestModel(m.models[k], 2); loaded = false; }
    if (!loaded) {
        LoadAllRequestedModels(false);
        for (int k = 0; k < m.cars; k++) if (!ModelLoaded(m.models[k])) return;
    }
    void *first = nullptr, *last = nullptr;
    CVector pos = { m.pos[0], m.pos[1], m.pos[2] };
    kCreateTrain(pos, (m.flags & TRF_CLOCKWISE) != 0, m.trainType, &first, &last, m.node, m.track, true);
    if (!first || !InPool(first) || !IsTrain(first)) {
        if (n.fails < 255) n.fails++;
        Log("trains: %08X: the game built no train (type %d, track %d, node %d, try %d)", n.id, (int)m.trainType, (int)m.track, (int)m.node, (int)n.fails);
        return;
    }
    n.eng = first;
    n.ref = RefOf(first);
    n.fails = 0;
    void *cars[MAX_CARS + 4];
    int c = Chain(first, cars, MAX_CARS + 4);
    Log("trains: %08X copy built (type %d, track %d, %d carriages, host had %d)", n.id, (int)m.trainType, (int)m.track, c, (int)m.cars);
}

// A random train of our own that has no business here: removed (guest, while the host shares its trains).
bool g_hostTrains;
uint32_t g_hostTrainsAt;
bool g_forcedOff;
uint8_t g_prevRandom;

bool HostSharing() { return g_hostTrains && GetTickCount() - g_hostTrainsAt < 6000; }

void GuestRandomOff()
{
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (now - last < 500) return;
    last = now;
    uint8_t &flag = *(uint8_t *)0xC38010;   // CTrain::bDisableRandomTrains
    if (!g_forcedOff) { g_prevRandom = flag; g_forcedOff = true; Log("trains: the host shares its trains, our own random trains are off"); }
    flag = 1;
    Pool *p = VehPool();
    for (int i = 0; p && i < p->size; i++) {
        if (p->flags[i] & 0x80) continue;
        uint8_t *v = p->objects + i * 0xA18;
        if (!IsTrain(v) || !IsFront(v) || (Flags(v) & TF_MISSION) || FindByEng(v)) continue;   // (mission trains: ours or copies)
        RemoveChain(v, "local random train");
    }
}

void GuestMaintain()
{
    uint32_t now = GetTickCount();
    for (auto &n : g_tr) {
        if (!n.id) continue;
        if (n.eng && !Alive(n)) { n.eng = nullptr; n.ref = 0; }
        if (n.owner == g_localId || n.driving) continue;   // ours
        if (now - n.lastRecv > 3000) { Drop(n, "copy without news"); continue; }
        if (!n.eng) TryCreate(n);
    }
}

// Ownership: whoever sits in the cab of the engine drives it.
void ClaimRelease()
{
    uint32_t now = GetTickCount();
    for (auto &n : g_tr) {
        if (!n.id || !Alive(n)) continue;
        bool driving = LocalDrives(n.eng);
        if (driving && !n.driving) {
            n.driving = true;
            if (n.owner != g_localId) { n.owner = (uint8_t)g_localId; n.tr.Clear(); n.haveUnw = false; Log("trains: %08X: we drive it, we are its owner now", n.id); }
        } else if (!driving && n.driving) {
            n.driving = false;
            if (g_localId > 0) {   // a guest gives the train back to the host
                n.owner = 0;
                n.tr.Clear();
                n.haveUnw = false;
                n.relUntil = now + 600;
                Log("trains: %08X: we got out, back to the host", n.id);
            }
        }
    }
}

void SendOwned()
{
    uint32_t now = GetTickCount();
    for (auto &n : g_tr) {
        if (!n.id || !Alive(n) || n.type < 0 || g_localId < 0) continue;
        if (n.relUntil && (int32_t)(n.relUntil - now) > 0) {   // release messages (UDP: a few of them)
            if (now - n.lastSend >= 100) { n.lastSend = now; SendTrain(n, TRF_RELEASE); }
            continue;
        }
        if (n.owner != g_localId) continue;
        if (now - n.lastSend < (n.driving ? 33u : 66u)) continue;
        n.lastSend = now;
        SendTrain(n, 0);
    }
}

}  // namespace

// --- Public ---
// Every "call rel32" to target in the code of the game (committed pages only).
static int ScanCalls(uintptr_t lo, uintptr_t hi, uintptr_t target, uintptr_t *out, int max)
{
    int hits = 0;
    for (uintptr_t base = lo; base < hi;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void *)base, &mbi, sizeof(mbi))) break;
        uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (end > hi) end = hi;
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            for (uintptr_t a = base; a + 5 <= end; a++) {
                if (*(const uint8_t *)a != 0xE8 || a + 5 + *(const int32_t *)(a + 1) != target) continue;
                if (hits < max) out[hits] = a;
                hits++;
            }
        if (end <= base) break;
        base = end;
    }
    return hits;
}

void InstallTrainHook()
{
    if (!g_cfg.syncTrains) { Log("trains: disabled in sacoop.ini"); return; }
    uintptr_t sites[8];
    int hits = ScanCalls(0x401000, 0x850000, 0x6F7550, sites, 8);
    if (hits < 1 || hits > 8) { Log("trains: %d calls to CreateMissionTrain found (expected 1 to 3): trains are not shared", hits); return; }
    for (int i = 0; i < hits; i++) {
        PatchCall(sites[i], (void *)h_CreateMissionTrain);
        Log("trains: CreateMissionTrain call hooked at %08X", (unsigned)sites[i]);
    }
    g_hookOk = true;
}

bool TrainsHostReady() { return g_hookOk && g_cfg.syncTrains; }

void TrainsSetHost(bool on)
{
    g_hostTrains = on;
    if (on) g_hostTrainsAt = GetTickCount();
}

void TrainsIdle()
{
    g_onTrain = OnTrain;
    if (!NetRunning()) {   // no session: the copies built for a session go (the host's own trains are the game's)
        for (auto &n : g_tr) {
            if (!n.id) continue;
            if (g_cfg.host) memset(&n, 0, sizeof(n)); else Drop(n, "copy after the session");
        }
    }
    // Session over (or the host stopped sharing): our random trains come back.
    if (g_forcedOff && (!NetRunning() || (!g_cfg.host && !HostSharing()))) {
        *(uint8_t *)0xC38010 = g_prevRandom;
        g_forcedOff = false;
        Log("trains: our random trains are back");
    }
}

void TrainsFrame()
{
    if (!g_cfg.syncTrains || GameState() != 9 || !FindPlayerPed()) return;
    if (g_cfg.host) {
        if (g_hookOk) HostScan();
    } else if (HostSharing()) {
        GuestRandomOff();
        GuestMaintain();
    }
    ClaimRelease();
    SendOwned();
    if (!InterpActive()) TrainsApply();
}

void TrainsApply()
{
    if (!g_cfg.syncTrains || GameState() != 9) return;
    uint32_t now = GetTickCount();
    float ts = *(float *)0xB7CB5C;   // CTimer::ms_fTimeStep (1 = 1/50 s)
    if (!(ts > 0.0f && ts < 20.0f)) ts = 1.0f;
    for (auto &n : g_tr) {
        if (!n.id || !Alive(n) || n.owner == g_localId || n.driving || !n.tr.count) continue;
        if (now - n.lastRecv > 1500) continue;   // no news: the train goes on by itself
        uint8_t *e = (uint8_t *)n.eng;
        if (!IsFront(e) || (Flags(e) & TF_DERAILED)) continue;
        float total = TrackTotal(*(int8_t *)(e + T_TRACK));
        if (total <= 0.0f) continue;
        Snap s;
        if (!TrackSample(n.tr, n.owner, s)) continue;
        // ProcessControl is about to add speed * time step: write the distance one step early so the train ends up where
        // the owner had it (shown 110 ms late, like everything else) at the end of this frame.
        float d = s.pos[0] - s.vel[0] * ts;
        d = fmodf(d, total);
        if (d < 0) d += total;
        float cur = *(float *)(e + T_DIST), diff = fabsf(d - cur);
        if (diff > total * 0.5f) diff = total - diff;
        // A jump (the copy was just built, a long silence): also take the owner's node so the game does not walk the whole
        // track node by node (one clack sound each).
        if (diff > 20.0f) *(int16_t *)(e + T_NODE) = n.last.node;
        *(float *)(e + T_DIST) = d;
        *(float *)(e + T_SPEED) = s.vel[0];
    }
}
