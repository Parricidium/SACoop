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
#include <math.h>

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

static bool EnterAsPassenger(void *me, void *veh)
{
    int seat = FreeSeat(veh);
    if (seat < 0) return false;
    int door = ((int(__cdecl *)(void *, int))0x64F190)(veh, seat);
    void *task = NewEnterCarTask(veh, door);
    if (!task) return false;
    SetPrimaryTask(me, task, 3);
    Log("passager : monte dans la voiture d'un joueur (place %d, porte %d)", seat + 1, door);
    return true;
}

static bool g_wantG;

bool PassengerWindowMessage(UINT msg, WPARAM wp)
{
    if (msg == WM_KEYDOWN && wp == 'G' && !ChatTyping() && NetRunning() && GameState() == 9) { g_wantG = true; return false; }
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
