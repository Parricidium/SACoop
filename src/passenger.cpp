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
#include <string.h>
#include <stdio.h>

using namespace game;

static bool DrivenByPlayer(void *veh)
{
    void *drv = Field<void *>(veh, VEH_DRIVER);
    return drv && PuppetIndex(drv) >= 0;
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
    // F vers la voiture d'un joueur : en passager plutot qu'au volant.
    void *task = ActiveTask(me);
    if (TaskVtable(task) == VT_TaskEnterCarAsDriver) {
        void *veh = Field<void *>(task, 0xC);
        if (veh && DrivenByPlayer(veh)) {
            if (!EnterAsPassenger(me, veh)) SetPrimaryTask(me, nullptr, 3);
        }
    }
    // (0.54.46 diagnostic, 16/10: "pressing F by a car with several NPCs, CJ jolts for a second and stops") A get-in at a
    // copy that ends with us still on foot: how long it ran, the vehicle's people, door lock (+0x4F8), status, speed.
    {
        static void *watchVeh;
        static uint32_t since;
        static int said;
        void *t = ActiveTask(me);
        uintptr_t tv = TaskVtable(t);
        void *target = (tv == VT_TaskEnterCarAsDriver || tv == VT_TaskEnterCarAsPassenger) ? Field<void *>(t, 0xC) : nullptr;
        if (target && target != watchVeh && IsNetVehicle(target)) { watchVeh = target; since = GetTickCount(); }
        else if (!target && watchVeh) {
            if (!PedVehicle(me) && said < 30) {
                said++;
                uint8_t *v = (uint8_t *)watchVeh;
                const float *sp = (const float *)(v + 0x44);
                char who[96] = "";
                for (int k = -1; k < 8; k++) {
                    void *o = k < 0 ? Field<void *>(v, VEH_DRIVER) : Field<void *>(v, VEH_PASSENGERS + k * 4);
                    if (o) sprintf(who + strlen(who), " %s%d:%d", k < 0 ? "V" : "P", k + 1, PuppetIndex(o) >= 0 ? -1 : *(int16_t *)((uint8_t *)o + 0x22));
                }
                Log("vol : montee interrompue apres %u ms, vehicule modele %d (%s), occupants%s, verrou %d, statut %d, vitesse %.0f km/h, physique %08X",
                    GetTickCount() - since, *(int16_t *)(v + 0x22), IsRemoteVehicle(v) ? "copie" : "a nous", who, *(int *)(v + 0x4F8), v[0x36] >> 3,
                    sqrtf(sp[0] * sp[0] + sp[1] * sp[1] + sp[2] * sp[2]) * 180.0f, *(uint32_t *)(v + 0x40));
            }
            watchVeh = nullptr;
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
        if (!DrivenByPlayer(v) || FreeSeat(v) < 0) continue;
        const float *vp = EntityPos(v);
        float dx = vp[0] - mp[0], dy = vp[1] - mp[1], dz = vp[2] - mp[2], d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) { bestD = d; best = v; }
    }
    if (best) EnterAsPassenger(me, best);
    else Log("passager : aucune voiture de joueur avec une place libre a moins de 6 m (moi %.1f %.1f %.1f)", mp[0], mp[1], mp[2]);
}

// Autotest "passager" : demande comme un appui sur G.
void PassengerRequest() { g_wantG = true; }
