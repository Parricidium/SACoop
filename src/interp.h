// Interpolation of remote entities (interp.cpp) -- port of VCCoop's interp module to San Andreas.
// Every state received (player, vehicle, mission ped) carries the sender's send time. Remote entities are shown a
// little in the past (g_cfg.interpDelay) and interpolated between two received states, so the motion stays smooth
// despite network jitter. With no recent state the last velocity is extrapolated (250 ms at most).
// Placement happens right after CGame::Process (physics done, nothing drawn yet): that is the only moment where a
// position written by the mod is what the camera and the renderer will use.
#pragma once
#include <stdint.h>

enum { TRACK_SNAPS = 10 };

struct Snap {
    uint32_t t;             // sender's GetTickCount at send time
    float pos[3], vel[3];   // velocity: game units per 1/50 s
    float right[3], fwd[3]; // orientation (vehicles)
    float heading;          // heading (peds)
};

struct Track {
    Snap s[TRACK_SNAPS];    // oldest to newest
    int count;
    void Clear() { count = 0; }
    void Push(const Snap &n);
};

// Clock of each remote player (0..3): offset between his GetTickCount and ours. Call on every reception.
void ClockSample(int src, uint32_t senderTime);
// State to show now for this track (source: player src). False if the track is empty.
// linear: for peds, whose game velocity does not follow the real motion (it comes from the animations): the
// velocity is then derived from the received positions.
bool TrackSample(const Track &tr, int src, Snap &out, bool linear = false);

// Hook right after CGame::Process (the call in Idle, found at run time). Does nothing when Interpolation=0.
void InstallInterp();
bool InterpActive();            // hook installed and enabled: callers stop placing entities themselves
// Copies the entity's matrix into its RenderWare frame (what CWorld::Process does at the end of its pass). Needed
// for every pose written after CGame::Process, or the renderer draws the old one.
void EntityRefresh(void *e);

// Cinematiques : une tourne chez nous (CCutsceneMgr::ms_running, 0xB5F851).
bool CutsceneNow();
// Cache (CEntity::m_bIsVisible, +0x1C bit 7) un double reseau pendant une cinematique : pantin d'un autre joueur, copie
// d'un PNJ ou d'un vehicule de mission se tiennent dans la scene, par-dessus les acteurs de la cinematique, et
// scintillent (deux modeles aux memes endroits). mine memorise qu'on l'a cache : on ne remet visible que ce qu'on a cache.
void CutsceneHide(void *e, bool hide, bool &mine);

// Provided by the modules:
void PuppetsAfterProcess();    // coop.cpp
void VehiclesAfterProcess();   // vehicles.cpp
void GhostsAfterProcess();     // entities.cpp
