// Interpolation of remote entities: clocks, state tracks, hook after the game's physics, world sanitizer.
// (Port of VCCoop's interp.cpp; the addresses are San Andreas 1.0 US.)
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "interp.h"
#include "trains.h"
#include <math.h>
#include <string.h>

using namespace game;

// --- Clocks ---
// Smallest offset (our time - sender's time) seen: that of the packets that arrived fastest. It creeps up by 2 ms
// per second to follow clock drift, and restarts from scratch if it jumps (other machine).
static uint32_t g_offset[MAX_PLAYERS];
static bool g_haveOffset[MAX_PLAYERS];
static uint32_t g_relaxAt[MAX_PLAYERS];

void ClockSample(int src, uint32_t senderTime)
{
    if (src < 0 || src >= MAX_PLAYERS) return;
    uint32_t now = GetTickCount(), off = now - senderTime;
    if (!g_haveOffset[src] || (int32_t)(off - g_offset[src]) < 0 || (int32_t)(off - g_offset[src]) > 2000) {
        g_offset[src] = off;
        g_haveOffset[src] = true;
        g_relaxAt[src] = now;
    } else if (now - g_relaxAt[src] >= 1000) {
        g_offset[src] += 2;
        g_relaxAt[src] = now;
    }
}

// --- Tracks ---
void Track::Push(const Snap &n)
{
    if (count && (int32_t)(n.t - s[count - 1].t) <= 0) return;   // late or duplicated (UDP)
    // Long silence (parked vehicle, sent once a second): the entity did not move in between; repeat the old state
    // just before the new one, otherwise it would be seen sliding slowly across the whole gap.
    if (count && n.t - s[count - 1].t > 300) {
        Snap hold = s[count - 1];
        hold.t = n.t - 60;
        hold.vel[0] = hold.vel[1] = hold.vel[2] = 0;
        if (count == TRACK_SNAPS) { for (int i = 1; i < count; i++) s[i - 1] = s[i]; count--; }
        s[count++] = hold;
    }
    if (count == TRACK_SNAPS) { for (int i = 1; i < count; i++) s[i - 1] = s[i]; count--; }
    s[count++] = n;
}

static void Normalize(float *v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static float LerpAngle(float a, float b, float u)
{
    float d = b - a;
    while (d > 3.14159265f) d -= 6.28318531f;
    while (d < -3.14159265f) d += 6.28318531f;
    return a + d * u;
}

bool TrackSample(const Track &tr, int src, Snap &o, bool linear)
{
    if (!tr.count) return false;
    uint32_t rt = GetTickCount() - (src >= 0 && src < MAX_PLAYERS ? g_offset[src] : 0) - (uint32_t)g_cfg.interpDelay;
    const Snap &newest = tr.s[tr.count - 1];
    if ((int32_t)(rt - newest.t) >= 0) {
        // Nothing recent: carry on with the velocity.
        float dt = (float)(rt - newest.t);
        if (dt > 250.0f) dt = 250.0f;
        o = newest;
        if (linear) {
            if (tr.count >= 2 && newest.t - tr.s[tr.count - 2].t > 0) {
                const Snap &p = tr.s[tr.count - 2];
                float f = 20.0f / (float)(newest.t - p.t);
                for (int k = 0; k < 3; k++) o.vel[k] = (newest.pos[k] - p.pos[k]) * f;
            } else o.vel[0] = o.vel[1] = o.vel[2] = 0;
        }
        for (int k = 0; k < 3; k++) o.pos[k] += o.vel[k] * dt / 20.0f;
        return true;
    }
    if ((int32_t)(rt - tr.s[0].t) <= 0) { o = tr.s[0]; return true; }
    int i = tr.count - 1;
    while (i > 1 && (int32_t)(rt - tr.s[i - 1].t) < 0) i--;
    const Snap &a = tr.s[i - 1], &b = tr.s[i];
    float span = (float)(b.t - a.t), u = (float)(rt - a.t) / span;
    o = b;
    o.t = rt;
    // Hermite (positions and velocities of both states): the path follows the bends instead of cutting them.
    float D = span / 20.0f, u2 = u * u, u3 = u2 * u;
    float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    for (int k = 0; k < 3; k++) {
        if (linear) {
            o.pos[k] = a.pos[k] + (b.pos[k] - a.pos[k]) * u;
            o.vel[k] = (b.pos[k] - a.pos[k]) / D;
        } else {
            o.pos[k] = h00 * a.pos[k] + h10 * D * a.vel[k] + h01 * b.pos[k] + h11 * D * b.vel[k];
            o.vel[k] = a.vel[k] + (b.vel[k] - a.vel[k]) * u;
        }
        o.right[k] = a.right[k] + (b.right[k] - a.right[k]) * u;
        o.fwd[k] = a.fwd[k] + (b.fwd[k] - a.fwd[k]) * u;
    }
    Normalize(o.right);
    Normalize(o.fwd);
    o.heading = LerpAngle(a.heading, b.heading, u);
    return true;
}

// --- Cinematiques ---
bool CutsceneNow() { return *(const uint8_t *)0xB5F851 != 0; }

void CutsceneHide(void *e, bool hide, bool &mine)
{
    if (!e) return;
    uint8_t &f = *((uint8_t *)e + 0x1C);   // CEntity::m_nFlags, octet 0 : bit 7 = bIsVisible
    if (hide) {
        if (f & 0x80) { f &= (uint8_t)~0x80; mine = true; }
    } else if (mine) {
        f |= 0x80;
        mine = false;
    }
}

// --- RenderWare frame ---
// CEntity::UpdateRwMatrix (0x446F90) copies the CMatrix into the RwMatrix of the model, CEntity::UpdateRwFrame
// (0x532B00) tells RenderWare the frame changed. CWorld::Process does both for every moving entity at the end of its
// pass; a pose written after it needs them again.
void EntityRefresh(void *e)
{
    if (!e || !*(void **)((uint8_t *)e + 0x18)) return;   // CEntity::m_pRwObject
    ((void(__thiscall *)(void *))0x446F90)(e);
    ((void(__thiscall *)(void *))0x532B00)(e);
}

// --- World sanitizer ---
// Guard before the physics: a vehicle or ped whose matrix, position or speed is no longer a number (NaN) or is absurd
// makes the game compute a world sector outside its grid, which corrupts its lists (crashes inside
// CPhysical::ProcessCollisionSectorList, seen on VCCoop guests during a chase). Its last sane matrix is put back, its
// speeds are zeroed, and it is noted in the log.
struct SaneState { float m[16]; uint16_t handle; bool valid; };
static SaneState g_saneVeh[256], g_sanePed[512];

static bool Finite(const float *v, int n, float lim)
{
    for (int i = 0; i < n; i++) if (!(fabsf(v[i]) <= lim)) return false;   // NaN: the comparison fails
    return true;
}

static int SanitizePool(Pool *pool, int entry, SaneState *saved, int maxSlots, const char *what)
{
    int fixed = 0;
    if (!pool || !pool->objects || !pool->flags) return 0;
    for (int i = 0; i < pool->size && i < maxSlots; i++) {
        if (pool->flags[i] & 0x80) { saved[i].valid = false; continue; }
        uint8_t *e = pool->objects + i * entry;
        uint8_t *mat = *(uint8_t **)(e + 0x14);   // CMatrix: right, forward, up, position (rows of 4 floats)
        if (!mat) continue;
        float *m = (float *)mat;
        uint16_t h = (uint16_t)((i << 8) | pool->flags[i]);
        bool ok = Finite(m, 3, 4.0f) && Finite(m + 4, 3, 4.0f) && Finite(m + 8, 3, 4.0f) && Finite(m + 12, 3, 12000.0f) &&
                  Finite((float *)(e + 0x44), 3, 50.0f) && Finite((float *)(e + 0x50), 3, 50.0f);
        if (ok) { memcpy(saved[i].m, m, 64); saved[i].handle = h; saved[i].valid = true; continue; }
        fixed++;
        static int said;
        if (said++ < 60)
            Log("sanitizer: %s %d (model %d) absurd: pos %.1f %.1f %.1f, axes %.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f, speed %.2f %.2f %.2f, turn %.2f %.2f %.2f; %s",
                what, i, *(int16_t *)(e + 0x22), m[12], m[13], m[14], m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10],
                *(float *)(e + 0x44), *(float *)(e + 0x48), *(float *)(e + 0x4C), *(float *)(e + 0x50), *(float *)(e + 0x54), *(float *)(e + 0x58),
                saved[i].valid && saved[i].handle == h ? "back to its last sane pose" : "laid flat");
        if (saved[i].valid && saved[i].handle == h) memcpy(m, saved[i].m, 64);
        else {
            bool posOk = Finite(m + 12, 3, 12000.0f);
            float px = posOk ? m[12] : 0, py = posOk ? m[13] : 0, pz = posOk ? m[14] : 20;
            memset(m, 0, 64);
            m[0] = 1; m[5] = 1; m[10] = 1;
            m[12] = px; m[13] = py; m[14] = pz;
        }
        memset(e + 0x44, 0, 12);
        memset(e + 0x50, 0, 12);
        if (*(void **)(e + 0x18)) EntityRefresh(e);
    }
    return fixed;
}

static void SanitizeWorld()
{
    SanitizePool(*(Pool **)0xB74494, 0xA18, g_saneVeh, 256, "vehicle");
    SanitizePool(PedPool(), 0x7C4, g_sanePed, 512, "ped");
}

// --- Hook after CGame::Process ---
static bool g_hooked;
bool InterpActive() { return g_hooked && g_cfg.interp; }

static void __cdecl h_GameProcess()
{
    bool playing = GameState() == 9 && FindPlayerPed();
    if (playing && g_cfg.guard) SanitizeWorld();
    // Vehicle copies are placed BEFORE the physics too: CWorld::Process seats their occupants and the camera of a
    // passenger follows; placed only afterwards, driver and passengers were one frame behind the vehicle.
    if (playing && g_cfg.placeVeh) VehiclesAfterProcess();
    if (playing) TrainsApply();   // trains: the owner's rail distance and speed, written before the game moves them (trains.cpp)
    ((void(__cdecl *)())0x53BEE0)();   // CGame::Process
    if (GameState() != 9 || !FindPlayerPed()) return;
    if (g_cfg.placeFoot) PuppetsAfterProcess();
    if (g_cfg.placeVeh) VehiclesAfterProcess();
    if (g_cfg.placeFoot) GhostsAfterProcess();
}

// The call to CGame::Process (0x53BEE0, cdecl, no argument) lives in Idle (0x53E920). Its exact address is found at
// run time: the only E8 rel32 in Idle that lands on it. If there is not exactly one, nothing is hooked and the
// previous behaviour (placement after the frame, in CoopFrame) stays in force.
void InstallInterp()
{
    if (!g_cfg.interp && !g_cfg.guard) { Log("interpolation: disabled in sacoop.ini"); g_cfg.placeFoot = g_cfg.placeVeh = false; return; }
    const uintptr_t idle = 0x53E920, process = 0x53BEE0;
    uintptr_t found = 0;
    int hits = 0;
    for (uintptr_t a = idle; a < idle + 0x400; a++) {
        if (*(const uint8_t *)a != 0xE8) continue;
        int32_t rel = *(const int32_t *)(a + 1);
        if (a + 5 + rel == process) { found = a; hits++; }
    }
    if (hits != 1) {
        Log("interpolation: call to CGame::Process not found in Idle (%d candidates): module off", hits);
        g_cfg.interp = g_cfg.placeFoot = g_cfg.placeVeh = false;
        return;
    }
    if (!g_cfg.interp) g_cfg.placeFoot = g_cfg.placeVeh = false;   // only the sanitizer was asked for
    PatchCall(found, (void *)h_GameProcess);
    g_hooked = true;
    Log("interpolation: hooked after CGame::Process (call at %08X); interp=%d delay=%d ms foot=%d vehicles=%d freeze=%d guard=%d",
        (unsigned)found, g_cfg.interp, g_cfg.interpDelay, g_cfg.placeFoot, g_cfg.placeVeh, g_cfg.freezeCopies, g_cfg.guard);
}
