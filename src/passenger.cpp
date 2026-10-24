// Passager. SA n'a pas de touche "monter en passager" (sauf avec sa bande), et F sur une voiture conduite par le pantin
// d'un autre joueur l'en ejecterait (vol de voiture) alors qu'il conduit toujours chez lui.
//  - G (en partie, a pied) : monte en passager dans la voiture la plus proche (6 m) conduite par un autre joueur, a la
//    premiere place libre (CTaskComplexEnterCarAsPassenger, porte de la place : 0x64F190).
//  - F vers une voiture conduite par un autre joueur : la tache "monter au volant" du joueur local est remplacee par
//    "monter en passager".
// Le reste suit le chemin habituel : MsgState.carTask = 2 (le pantin joue la montee chez les autres), puis la place.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "chat.h"
#include "passenger.h"
#include "vehicles.h"
#include <math.h>

using namespace game;

static bool DrivenByPlayer(void *veh)
{
    void *drv = Field<void *>(veh, VEH_DRIVER);
    return drv && PuppetIndex(drv) >= 0;
}

// 0.54.92: a player who has started getting in at the wheel (his state says carTask 1 with this vehicle) counts as its
// driver already, so a second player can get in as passenger at once instead of waiting until he is seated.
// 0.54.94 (test of 22/10: both pressed F at the same car at about the same time; each PC saw the other getting in at the
// wheel, so BOTH turned into passengers, both went for the same seat, nobody drove, and the guest froze until he killed
// himself or a phone call replaced his task). Now:
//  - the other player's "at the wheel" must last 250 ms (a player who presses F at a car someone is already getting in
//    sends carTask 1 for a frame or two before his own PassengerFrame turns it into passenger: the second case of 22/10);
//  - we give the wheel up only if he started before us (as seen here), or if we were both getting in at the wheel and
//    he has the lower player number (the host keeps it). Both PCs reach the same answer.
static void *g_otherWheelVeh[MAX_PLAYERS];
static uint32_t g_otherWheelSince[MAX_PLAYERS];
static void *g_myWheelVeh;
static uint32_t g_myWheelSince;

static void TrackWheelEntries(void *me, uint32_t now)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        void *v = nullptr;
        if (i != g_localId && g_players[i].connected && g_players[i].state.inGame && g_players[i].state.carTask == 1 && g_players[i].state.carTaskVeh)
            v = NetVehicleById(g_players[i].state.carTaskVeh);
        if (v != g_otherWheelVeh[i]) { g_otherWheelVeh[i] = v; g_otherWheelSince[i] = now; }
    }
    void *task = ActiveTask(me);
    void *mine = TaskVtable(task) == VT_TaskEnterCarAsDriver ? Field<void *>(task, 0xC) : nullptr;
    if (mine != g_myWheelVeh) { g_myWheelVeh = mine; g_myWheelSince = now; }
}

// Player number of another player who gets this vehicle's wheel before us, or -1.
static int OtherHasWheel(void *veh, uint32_t now)
{
    if (!veh) return -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i == g_localId || g_otherWheelVeh[i] != veh) continue;
        if (now - g_otherWheelSince[i] < 250) continue;
        if (g_myWheelVeh != veh || (int)(g_otherWheelSince[i] - g_myWheelSince) < 0 || i < g_localId) return i;
    }
    return -1;
}

// 0.54.94: a get-in (task 700 passenger / 701 driver) still running after 15 s is stuck (two players for one seat, a seat
// taken under us): it is aborted, the player is free again and can press F / G once more.
static void EntryWatchdog(void *me, uint32_t now)
{
    static uint32_t since;
    if (!FindTaskType(me, 700) && !FindTaskType(me, 701)) { since = 0; return; }
    if (!since) { since = now | 1; return; }
    if (now - since < 15000) return;
    since = 0;
    uintptr_t vt = TaskVtable(ActiveTask(me));
    void *veh = PedVehicle(me);
    DropTaskType(me, 700);
    DropTaskType(me, 701);
    Log("montee bloquee depuis 15 s (tache active %08X, %s) : annulee", (unsigned)vt, veh ? "assis dans un vehicule" : "a pied");
}

static int FreeSeat(void *veh)
{
    int maxPass = Field<uint8_t>(veh, VEH_MAXPASS);
    for (int i = 0; i < maxPass && i < 8; i++) if (!Field<void *>(veh, VEH_PASSENGERS + i * 4)) return i;
    return -1;
}

// The free seat on the side the player stands (0.54.43: G always took the first free seat, the back left one; a guest by
// the back right door of the host's car, Big Smoke in front, could never get in there). Passenger seats: 0 front right,
// 1 back left, 2 back right (more on buses: any).
static int SeatNear(void *me, void *veh)
{
    int maxPass = Field<uint8_t>(veh, VEH_MAXPASS);
    uint8_t *mat = *(uint8_t **)((uint8_t *)veh + 0x14);
    if (!mat || maxPass > 3) return FreeSeat(veh);
    const float *vp = EntityPos(veh), *mp = EntityPos(me), *r = (const float *)mat, *f = (const float *)(mat + 0x10);
    float dx = mp[0] - vp[0], dy = mp[1] - vp[1];
    bool right = dx * r[0] + dy * r[1] > 0, front = dx * f[0] + dy * f[1] > 0;
    static const int rightFront[] = { 0, 2, 1 }, rightBack[] = { 2, 0, 1 }, left[] = { 1, 0, 2 };
    const int *order = right ? (front ? rightFront : rightBack) : left;
    for (int k = 0; k < 3; k++) if (order[k] < maxPass && !Field<void *>(veh, VEH_PASSENGERS + order[k] * 4)) return order[k];
    return -1;
}

static bool EnterAsPassenger(void *me, void *veh)
{
    int seat = SeatNear(me, veh);
    if (seat < 0) return false;
    int door = ((int(__cdecl *)(void *, int))0x64F190)(veh, seat);
    void *task = NewEnterCarTask(veh, door);
    if (!task) return false;
    SetPrimaryTask(me, task, 3);
    Log("passager : monte dans la voiture d'un joueur (place %d, porte %d)", seat + 1, door);
    return true;
}

// 0.54.96 (report of 24/10: "passengers on bikes are not coming off when the driver stacks it and comes off, in any
// circumstance"): the game throws the passenger off with the driver (CBike::DamageKnockOffRider), but on the passenger's
// PC the driver is a puppet on a copy, the crash happens on the driver's PC only. Now, as passenger of a bike or quad
// whose rider was a player: once his state has said for 0.3 s that he is off it, we come off too - jumping off
// (CTaskComplexLeaveCar, not sensible, forced: what pressing F at speed does) when he fell, getting off when he got off.
static void BikePassengerFrame(void *me, uint32_t now)
{
    static void *bike;
    static int rider = -1;
    static uint32_t goneSince, leavingAt;
    void *veh = PedVehicle(me);
    int type = veh ? Field<int>(veh, VEH_TYPE) : -1;
    if (!veh || (type != 9 && type != 10 && type != 2) || Field<void *>(veh, VEH_DRIVER) == me) { bike = nullptr; rider = -1; goneSince = 0; return; }
    if (veh != bike) { bike = veh; rider = -1; goneSince = 0; }
    void *drv = Field<void *>(veh, VEH_DRIVER);
    int id = drv ? PuppetIndex(drv) : -1;
    if (id >= 0 && id < MAX_PLAYERS) { rider = id; goneSince = 0; if (g_players[id].state.carTask == 3) leavingAt = now | 1; return; }
    if (rider < 0 || FindTaskType(me, 704)) return;   // no player rode it, or we are getting off already
    const MsgState &s = g_players[rider].state;
    if (g_players[rider].connected && s.vehicleId && NetVehicleById(s.vehicleId) == veh) {
        if (s.carTask == 3) leavingAt = now | 1;   // (getting off the normal way)
        goneSince = 0;
        return;
    }
    if (!goneSince) { goneSince = now | 1; return; }
    if (now - goneSince < 300) return;
    bool fell = s.carTask != 3 && !(leavingAt && now - leavingAt < 2500);
    void *mem = ((void *(__cdecl *)(unsigned))0x61A5A0)(0x34);
    void *task = mem ? ((void *(__thiscall *)(void *, void *, int, int, bool, bool))0x63B8C0)(mem, veh, 0, 0, !fell, fell) : nullptr;   // CTaskComplexLeaveCar
    if (task) SetPrimaryTask(me, task, 3);
    Log("passager : le joueur %d n'est plus sur la moto (%s) : on %s aussi", rider, fell ? "tombe" : "descendu", fell ? "saute" : "descend");
    rider = -1;
    goneSince = leavingAt = 0;
}

static bool g_wantG;
static uint32_t g_fAt;   // F (or Enter) pressed on foot, for the forced jack below (0.54.45)

bool PassengerWindowMessage(UINT msg, WPARAM wp)
{
    if (msg == WM_KEYDOWN && wp == 'G' && !ChatTyping() && NetRunning() && GameState() == 9) { g_wantG = true; return false; }
    if (msg == WM_KEYDOWN && (wp == 'F' || wp == VK_RETURN) && !ChatTyping() && NetRunning() && GameState() == 9) g_fAt = GetTickCount() | 1;
    return false;
}

void PassengerFrame()
{
    void *me = FindPlayerPed();
    if (!me || GameState() != 9) { g_wantG = false; return; }
    uint32_t now = GetTickCount();
    TrackWheelEntries(me, now);
    EntryWatchdog(me, now);
    BikePassengerFrame(me, now);
    // F vers la voiture d'un joueur : en passager plutot qu'au volant.
    void *task = ActiveTask(me);
    if (TaskVtable(task) == VT_TaskEnterCarAsDriver) {
        void *veh = Field<void *>(task, 0xC);
        int other = OtherHasWheel(veh, now);
        if (veh && (DrivenByPlayer(veh) || other >= 0)) {
            if (other >= 0 && g_myWheelVeh == veh && now - g_myWheelSince > 100)
                Log("passager : le joueur %d montait aussi au volant, il le garde", other);
            if (!EnterAsPassenger(me, veh)) SetPrimaryTask(me, nullptr, 3);
        }
    }
    // F at a copy of the host's vehicle with people inside, and the game started no get-in at all (16/10: "can't jack
    // cars with several NPCs inside, nothing starts, from either side"): the game's "get in at the wheel" task is given
    // to the nearest such copy within 4 m (it picks the door, pulls the driver out, the others flee).
    if (g_fAt && GetTickCount() - g_fAt > 350) {
        g_fAt = 0;
        int vt = (int)TaskVtable(ActiveTask(me));
        if (!PedVehicle(me) && vt != VT_TaskEnterCarAsDriver && vt != VT_TaskEnterCarAsPassenger) {
            const float *mp = EntityPos(me);
            Pool *vp = *(Pool **)0xB74494;
            void *best = nullptr;
            float bestD = 4.0f * 4.0f;
            for (int i = 0; i < vp->size; i++) {
                if (vp->flags[i] & 0x80) continue;
                void *v = vp->objects + i * 0xA18;
                if (!IsRemoteVehicle(v) || DrivenByPlayer(v) || IsTrain(v)) continue;
                int people = Field<void *>(v, VEH_DRIVER) ? 1 : 0;
                for (int k = 0; k < 8; k++) if (Field<void *>(v, VEH_PASSENGERS + k * 4)) people++;
                if (!people) continue;
                const float *p = EntityPos(v);
                float dx = p[0] - mp[0], dy = p[1] - mp[1], dz = p[2] - mp[2], d = dx * dx + dy * dy + dz * dz;
                if (d < bestD) { bestD = d; best = v; }
            }
            if (best) {
                if (void *t = NewEnterCarTask(best, -1)) {
                    SetPrimaryTask(me, t, 3);
                    Log("vol : le jeu ne lancait pas la montee, donnee vers le vehicule modele %d (%.1f m, conducteur %s)",
                        *(int16_t *)((uint8_t *)best + 0x22), sqrtf(bestD), Field<void *>(best, VEH_DRIVER) ? "oui" : "non");
                }
            }
        }
    }
    if (!g_wantG) return;
    g_wantG = false;
    if (PedVehicle(me)) return;
    // Voiture d'un joueur la plus proche, a moins de 6 m.
    const float *mp = EntityPos(me);
    Pool *p = *(Pool **)0xB74494;
    void *best = nullptr;
    float bestD = 6.0f * 6.0f;
    for (int i = 0; i < p->size; i++) {
        if (p->flags[i] & 0x80) continue;
        void *v = p->objects + i * 0xA18;
        if (!(DrivenByPlayer(v) || OtherHasWheel(v, now) >= 0) || FreeSeat(v) < 0) continue;
        const float *vp = EntityPos(v);
        float dx = vp[0] - mp[0], dy = vp[1] - mp[1], dz = vp[2] - mp[2], d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) { bestD = d; best = v; }
    }
    if (best) EnterAsPassenger(me, best);
    else Log("passager : aucune voiture de joueur avec une place libre a moins de 6 m (moi %.1f %.1f %.1f)", mp[0], mp[1], mp[2]);
}

// Autotest "passager" : demande comme un appui sur G.
void PassengerRequest() { g_wantG = true; }
