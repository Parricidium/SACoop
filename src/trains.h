// Train sync (trains.cpp): the host's trains (random ones and mission ones) run on every guest's rails the same way.
#pragma once
#include <stdint.h>

void InstallTrainHook();      // dllmain: learn which "train type" the game builds each train from (CTrain::CreateMissionTrain 0x6F7550)
void TrainsIdle();            // every game loop turn, in menu or in game: handlers, and random trains given back when the session ends
void TrainsFrame();           // in game, once per turn: register (host), send, create / remove the copies (guests)
void TrainsApply();           // right before CGame::Process: put every followed train where its owner has it (interp.cpp)
bool TrainsHostReady();       // host: the hook is in place, so the guests may switch their own random trains off (MsgWorld.trains)
void TrainsSetHost(bool on);  // guest: the host says it shares its trains (MsgWorld.trains)
