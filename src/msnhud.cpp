// Mission HUD mirror.
//
// What the game does: a mission script puts numbers on the screen with the on-screen timer (COnscreenTimer, 0xBA1788): one
// clock and four counters. Each one is a slot holding the NUMBER OF A SCRIPT VARIABLE (a byte offset from ScriptSpace
// 0xA49960; mission locals are negative offsets, down to 0xA48960), a GXT key for the label, a type (number, bar, "x / max"),
// a colour. Every frame the game reads the variable and draws it. The mission scripts only run on the host (the guests replay
// a ghost of the opcodes that move things, never the ones that fill the HUD), so a guest saw neither Ryder's health bar nor the
// number of crates left in "Robbing Uncle Sam" (14/10).
//
// What we do:
//  - Host, 4 times a second: read the clock and the four slots, read the variable each one points at, send MsgHud (a few more
//    times after everything went off, so the guests clear it even if one message is lost).
//  - Guest: put the host's slots in its own slots, but with the variable replaced by one of OUR four numbers (a static
//    buffer: its offset from ScriptSpace is a plain 32-bit subtraction, the game adds it back). The game then draws them as
//    if a script had put them there. A slot the guest's own game filled is never touched. When the host's slot goes off, or
//    nothing came for 1.5 s, the slots we filled are emptied.
// The label is the GXT key the host's script used: it shows when the guest's mission text table has it (the mission text
// is loaded on the guests by the mirrored LOAD_MISSION_TEXT).
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "msnhud.h"
#include <string.h>

using namespace game;

namespace {

const uintptr_t TIMER = 0xBA1788;      // COnscreenTimer
const uint32_t SCRIPT = 0xA49960;      // ScriptSpace
const uint32_t LOCALS = 0xA48960;      // mission local variables (just under ScriptSpace)
const int COUNTERS = 4;

uint8_t *Counter(int i) { return (uint8_t *)TIMER + 0x40 + i * 0x44; }   // varId +0, max +4, key +8 (10), type +0x12 (u16), enabled +0x3E, flash +0x3F, colour +0x40

bool ReadVar(uint32_t varId, int32_t *out)
{
    uint32_t a = SCRIPT + varId;   // (32-bit arithmetic: a negative id wraps to the locals)
    if (a < LOCALS || a > SCRIPT + 0x42000 || (a & 3)) return false;
    if (IsBadReadPtr((void *)(uintptr_t)a, 4)) return false;
    *out = *(int32_t *)(uintptr_t)a;
    return true;
}

// --- Host ---
bool AnyGuest()
{
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (i != g_localId && g_players[i].connected) return true;
    return false;
}

void HostSend()
{
    static uint32_t last;
    static int tail;   // messages still to send after everything went off
    static bool logged;
    uint32_t now = GetTickCount();
    if (now - last < 250 || !AnyGuest()) return;
    last = now;
    uint8_t *t = (uint8_t *)TIMER;
    MsgHud m = {};
    m.type = MSG_HUD;
    bool any = false;
    uint32_t cv = *(uint32_t *)t;
    int32_t val;
    if (cv && ReadVar(cv, &val)) {
        m.clockOn = 1;
        m.clockValue = val;
        m.clockDir = t[0x39];
        m.clockBeep = *(uint32_t *)(t + 0x3C);
        memcpy(m.clockKey, t + 4, 9);
        any = true;
    }
    m.paused = t[0x151];
    for (int i = 0; i < COUNTERS; i++) {
        uint8_t *s = Counter(i);
        uint32_t id = *(uint32_t *)s;
        if (!id || !ReadVar(id, &val)) continue;
        HudCounter &c = m.c[i];
        c.on = 1;
        c.type = (uint8_t)*(uint16_t *)(s + 0x12);
        c.flash = s[0x3F];
        c.color = *(uint32_t *)(s + 0x40);
        c.max = *(uint32_t *)(s + 4);
        c.value = val;
        memcpy(c.key, s + 8, 9);
        any = true;
    }
    if (any) tail = 4;
    else if (tail > 0) tail--;
    else { logged = false; return; }
    if (any && !logged) {
        logged = true;
        Log("mission HUD: sending the host's timer and counters (clock %d, counters %d %d %d %d)", m.clockOn, m.c[0].on, m.c[1].on, m.c[2].on, m.c[3].on);
    }
    NetSendToAll(&m, sizeof(m));
}

// --- Guest ---
int32_t g_val[COUNTERS];   // what our slots point at
int32_t g_clockVal;
uint32_t g_offSince[COUNTERS], g_clockOffSince;   // the host's slot reported off since (a slot is only dropped after 1 s of that)
MsgHud g_msg;
uint32_t g_msgAt;
bool g_have;

void OnHud(const MsgHud &h)
{
    g_msg = h;
    g_msgAt = GetTickCount() | 1;
    g_have = true;
}

uint32_t IdOf(int32_t *p) { return (uint32_t)(uintptr_t)p - SCRIPT; }

void ClearSlot(uint8_t *s)
{
    *(uint32_t *)s = 0;
    *(uint32_t *)(s + 4) = 0;
    s[8] = 0;
    *(uint16_t *)(s + 0x12) = 0;
    s[0x3E] = 0;
}

void ClearClock()
{
    uint8_t *t = (uint8_t *)TIMER;
    *(uint32_t *)t = 0;
    t[4] = 0;
    t[0x38] = 0;
    t[0x39] = 1;
}

// All of ours off.
void DropAll()
{
    uint8_t *t = (uint8_t *)TIMER;
    if (*(uint32_t *)t == IdOf(&g_clockVal)) ClearClock();
    for (int i = 0; i < COUNTERS; i++)
        if (*(uint32_t *)Counter(i) == IdOf(&g_val[i])) ClearSlot(Counter(i));
}

void GuestApply()
{
    uint32_t now = GetTickCount();
    if (!g_have || now - g_msgAt > 1500) { DropAll(); return; }
    const MsgHud &m = g_msg;
    uint8_t *t = (uint8_t *)TIMER;
    uint32_t mineClock = IdOf(&g_clockVal);
    uint32_t cur = *(uint32_t *)t;
    if (m.clockOn) {
        bool mine = cur == mineClock;
        bool expired = m.clockDir == 1 && m.clockValue <= 0;
        if ((mine || cur == 0) && !(expired && !mine)) {
            int32_t d = m.clockValue - g_clockVal;
            if (!mine || d > 800 || d < -800) g_clockVal = m.clockValue;   // (the game's own tick keeps it smooth in between)
            if (!mine) Log("mission HUD: clock %d ms", (int)m.clockValue);
            *(uint32_t *)t = mineClock;
            memcpy(t + 4, m.clockKey, 9);
            t[4 + 9] = 0;
            t[0x39] = m.clockDir;
            *(uint32_t *)(t + 0x3C) = m.clockBeep;
            t[0x151] = 0;   // (never the host's: copied 4 times a second it made the guest's HUD blink, test of 15/10)
        }
    } else if (cur == mineClock) {
        if (!g_clockOffSince) g_clockOffSince = now;
        if (now - g_clockOffSince >= 1000) ClearClock();
    }
    if (m.clockOn) g_clockOffSince = 0;
    for (int i = 0; i < COUNTERS; i++) {
        uint8_t *s = Counter(i);
        const HudCounter &c = m.c[i];
        uint32_t mineId = IdOf(&g_val[i]);
        uint32_t id = *(uint32_t *)s;
        bool mine = id == mineId;
        if (c.on) {
            if (id != 0 && !mine) continue;   // (filled by our own game: left alone)
            g_val[i] = c.value;
            *(uint32_t *)s = mineId;
            *(uint32_t *)(s + 4) = c.max;
            memcpy(s + 8, c.key, 9);
            s[8 + 9] = 0;
            *(uint16_t *)(s + 0x12) = c.type;
            *(uint32_t *)(s + 0x40) = c.color;
            s[0x3F] = 0;   // steady: no "flash when first displayed" on the guest (it blinked at the update rate, test of 15/10)
            if (!mine) {
                static int claims;
                if (++claims <= 20) Log("mission HUD: counter %d (%s) type %d, value %d (claim %d)", i, c.key, c.type, (int)c.value, claims);
            }
            g_offSince[i] = 0;
        } else if (mine) {
            // The host's script clears and shows its counters again from time to time: one message saying "off" made the
            // guest drop the counter and the next one put it back, the HUD blinked at the update rate (client log of 15/10:
            // 550 re-claims, all at multiples of the 265 ms message interval). Dropped only after 1 s of "off".
            if (!g_offSince[i]) g_offSince[i] = now;
            if (now - g_offSince[i] >= 1000) { ClearSlot(s); g_offSince[i] = 0; }
        }
    }
}

}   // namespace

void MissionHudFrame()
{
    g_onHud = OnHud;
    if (!NetRunning()) { if (!g_cfg.host && g_have) { DropAll(); g_have = false; } return; }
    if (g_cfg.host) HostSend(); else GuestApply();
}
