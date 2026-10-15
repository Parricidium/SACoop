// Garage doors (0.54.18, request of 15/10): a door opened for one player is open for the others.
//
// The game's garages (checked in gta_sa.exe 1.0 US): CGarages::aGarages 0x96C048, 0xD8 bytes each, CGarages::NumGarages
// 0x96C024. CGarage: +0 position (CVector), +0x38 door position (0 closed .. 1 open), +0x4C type, +0x4D door state
// (0 closed, 1 open, 2 closing, 3 opening, 4 waiting for the player to get out, 5 closed with a car dropped off).
// Each PC runs its own garages, for its own player: a door opened by the host's car stayed shut on the guest's screen.
//
// The player nearest to a garage (within 30 m, same as the others' announced positions say) is its reference: twice a
// second (and at once when its door changes) he sends the door state and position of the garages he is the nearest to
// (MSG_GARAGE, relayed by the host). The others nudge their own door the same way: open-ish (1, 3, 4) -> opening (3) if
// theirs is not open-ish, closed-ish (0, 2, 5) -> closing (2); the game then moves the door and its objects itself.
// Switch: SyncGarages=0 in sacoop.ini. Log lines start with "garage :".
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "garages.h"
#include <string.h>
#include <math.h>

using namespace game;

static const int MAX_GARAGES = 50;
static uint8_t *Garage(int i) { return (uint8_t *)0x96C048 + i * 0xD8; }
static int NumGarages() { int n = *(int *)0x96C024; return n < 0 ? 0 : n > MAX_GARAGES ? MAX_GARAGES : n; }
static bool OpenIsh(int st) { return st == 1 || st == 3 || st == 4; }

struct Remote { uint8_t state, from; float pos; uint32_t at; };
static Remote g_remote[MAX_GARAGES];
static uint8_t g_sentState[MAX_GARAGES];
static uint32_t g_sentAt[MAX_GARAGES];

static float Dist2D(const float *a, const float *b) { float dx = a[0] - b[0], dy = a[1] - b[1]; return dx * dx + dy * dy; }

// Player nearest to this garage (within 30 m, outside interiors), -1 if none.
static int Nearest(const float *g)
{
    int best = -1;
    float bestD = 30.0f * 30.0f;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const float *p;
        // (A passenger is never the reference: the driver's game runs the doors for his car.)
        if (i == g_localId) {
            void *me = FindPlayerPed();
            if (!me || EntityArea(me) != 0) continue;
            if (void *veh = PedVehicle(me)) if (Field<void *>(veh, VEH_DRIVER) != me) continue;
            p = EntityPos(me);
        } else {
            const NetPlayer &np = g_players[i];
            if (!np.connected || !np.state.inGame || np.state.area != 0) continue;
            if (np.state.vehicleId && np.state.seat != 0) continue;
            p = np.state.pos;
        }
        float d = Dist2D(p, g);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

static void OnGarage(const MsgGarage &m)
{
    if (!g_cfg.syncGarages || m.from == g_localId) return;
    int n = m.count > 8 ? 8 : m.count;
    for (int k = 0; k < n; k++) {
        int i = m.e[k].idx;
        if (i >= MAX_GARAGES) continue;
        g_remote[i] = { m.e[k].state, m.from, m.e[k].pos, GetTickCount() };
    }
}

void GaragesFrame()
{
    g_onGarage = OnGarage;
    if (!g_cfg.syncGarages || !NetRunning() || GameState() != 9 || !FindPlayerPed() || g_localId < 0) return;
    uint32_t now = GetTickCount();
    static uint32_t lastScan;
    bool scan = now - lastScan >= 100;
    if (scan) lastScan = now;
    MsgGarage m = {};
    m.type = MSG_GARAGE;
    m.from = (uint8_t)g_localId;
    int count = NumGarages();
    for (int i = 0; i < count; i++) {
        uint8_t *g = Garage(i);
        const float *pos = (const float *)g;
        int st = g[0x4D];
        // The reference found at the last scan is kept in between (0.54.36 review: on the frames between two scans any
        // remote state was applied, even to a garage this PC is the reference of: two PCs that each thought they were
        // the nearest wrote each other's door state on most frames, the door flapped).
        static int8_t refOf[MAX_GARAGES];
        static bool refInit;
        if (!refInit) { refInit = true; memset(refOf, -1, sizeof(refOf)); }
        if (scan) refOf[i] = (int8_t)Nearest(pos);
        int ref = refOf[i];
        if (ref == g_localId) {
            bool changed = OpenIsh(st) != OpenIsh(g_sentState[i]);
            if ((changed || now - g_sentAt[i] >= 500) && m.count < 8) {
                m.e[m.count++] = { (uint8_t)i, (uint8_t)st, *(float *)(g + 0x38) };
                if (changed) {
                    static int said;
                    if (said++ < 20) Log("garage : porte %d (type %d) %s, envoyee aux autres", i, g[0x4C], OpenIsh(st) ? "ouverte" : "fermee");
                }
                g_sentState[i] = (uint8_t)st;
                g_sentAt[i] = now;
            }
            continue;
        }
        const Remote &r = g_remote[i];
        if (!r.at || now - r.at > 1500) continue;
        if (ref != r.from) continue;   // (only the reference's doors: nobody near, or someone else is nearer now)
        bool want = OpenIsh(r.state);
        if (want == OpenIsh(st)) continue;
        g[0x4D] = want ? 3 : 2;   // opening / closing: the game moves the door and its objects
        static int said;
        if (said++ < 20) Log("garage : porte %d (type %d) %s comme chez le joueur %d", i, g[0x4C], want ? "ouverte" : "fermee", r.from);
    }
    if (m.count) NetSendToAll(&m, sizeof(m));
}
