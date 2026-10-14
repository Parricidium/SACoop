// Mission HUD mirror (msnhud.cpp): the host's on-screen timer and counters (Ryder's health, the crates left, the time left)
// are shown on the guests' screens too.
#pragma once

void MissionHudFrame();   // every game loop turn, in game: host sends, guests apply (and drop what they put up when it ends)
