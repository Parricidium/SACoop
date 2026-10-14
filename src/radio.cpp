// Radio sync: the host's radio is the reference, the guests follow it.
//
// What the game does (CAERadioTrackManager at 0x8CB6F8, gta-reversed): each station is a chain of tracks (music, DJ
// talk, adverts, idents...) picked at RANDOM into a queue of 5 (`ChooseTracksForStation`), started at a random
// position (`PlayTime`). Two PCs therefore hear different songs and different adverts on the same station.
//
// What we do (once a second at most, on the game thread, same thread as the game's own radio service):
//  - Host: every 500 ms (and as soon as the track changes) MsgRadio = station, track being played (audio hardware
//    CAEAudioHardware 0xB5F8B8: active track, play time, length), the manager's queue (types, indexes).
//  - Guest, radio on and on the SAME station as the host: if the track being played differs from the host's, or the
//    position is off by more than 2.5 s, for at least 1.5 s, the guest's radio is restarted the way a retune does it
//    (StartRadio, 0x4EB3C0: write the "requested" settings, mode STOPPING -> the game's own service starts them) but
//    with the host's queue and the host's position (plus the time the message took). Adverts are tracks of the queue
//    like the others, so they follow too. At most one restart every 6 s, and three tries per host track.
//  - Custom radio ("User Tracks", station 12): the track ids are indexes into the player's own library, which is
//    only the same when both libraries are the same. Each PC hashes its library (sa-utrax.dat + sa-ufiles.dat in the
//    user files folder: ordered list of file name + size); the host's hash travels in MsgRadio and the guest only
//    follows when they are equal.
// Station changes in a shared vehicle are handled by RadioFrame (coop.cpp, the driver's station). Everything is
// written to the log ("radio:" lines) so that what really happens can be checked. Switch: RadioSync=0 in sacoop.ini.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "hud.h"
using namespace game;
#include <string.h>
#include <stdlib.h>

// tRadioSettings (0x3C bytes): queue of 5 track ids, current / previous track, start position, length, flags, station...
struct RadioSettings {
    int32_t queue[5];
    int32_t curr, prev, playTime, trackLengthMs;
    int8_t flags, station, bass;
    float bassGain;
    int8_t types[5];
    int8_t currType, prevType;
    int8_t idx[5];
    int8_t currIdx, prevIdx;
};
static_assert(sizeof(RadioSettings) == 0x3C, "tRadioSettings");

static uint8_t *const RM = (uint8_t *)0x8CB6F8;   // CAERadioTrackManager (0x370 bytes)
static RadioSettings *RadioActive() { return (RadioSettings *)(RM + 0xC4); }
static RadioSettings *RadioRequested() { return (RadioSettings *)(RM + 0x88); }
static int &RadioMode() { return *(int *)(RM + 0x68); }   // 0 starting, 1 waiting to play, 2 playing, 3 stopping, 7 stopped
static int32_t &RadioStatePauseTime(int station) { return *(int32_t *)(RM + 0x100 + station * 0x2C + 0xC); }

static int HwActiveTrack() { return ((int(__thiscall *)(void *))0x4D8F80)((void *)0xB5F8B8); }   // CAEAudioHardware::GetActiveTrackID
static int HwPlayTime() { return ((int(__thiscall *)(void *))0x4D8F60)((void *)0xB5F8B8); }      // GetTrackPlayTime (ms, < 0 = not ready)
static int HwLength() { return ((int(__thiscall *)(void *))0x4D8F70)((void *)0xB5F8B8); }        // GetTrackLengthMs

enum { RADIO_USER = 12 };

// --- Signature of the custom track library ---
// Whole file into a malloc'ed buffer (caller frees), false if missing / empty / over 64 MB.
static bool ReadWholeFile(const char *path, uint8_t **out, DWORD *n)
{
    *out = nullptr;
    *n = 0;
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(f, NULL), got = 0;
    if (size == INVALID_FILE_SIZE || size == 0 || size > 64u * 1024u * 1024u) { CloseHandle(f); return false; }
    uint8_t *buf = (uint8_t *)malloc(size);
    bool ok = buf && ReadFile(f, buf, size, &got, NULL) && got == size;
    CloseHandle(f);
    if (!ok) { free(buf); return false; }
    *out = buf;
    *n = size;
    return true;
}

static uint32_t Fnv(uint32_t h, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

// 0 = no library / unreadable. Computed once (the game only rewrites the files when the player rescans).
static uint32_t LibrarySignature()
{
    static bool done;
    static uint32_t sig;
    if (done) return sig;
    done = true;
    uint32_t t0 = GetTickCount();
    char dir[MAX_PATH], p1[MAX_PATH], p2[MAX_PATH];
    lstrcpynA(dir, (const char *)0xC92368, MAX_PATH - 24);   // ".../GTA San Andreas User Files", cached by the game
    wsprintfA(p1, "%s\\sa-utrax.dat", dir);
    wsprintfA(p2, "%s\\sa-ufiles.dat", dir);
    uint8_t *utrax, *ufiles;
    DWORD nUtrax, nUfiles;
    if (!ReadWholeFile(p1, &utrax, &nUtrax) || !ReadWholeFile(p2, &ufiles, &nUfiles) || nUtrax < 12) {
        if (utrax) free(utrax);
        Log("radio: no custom track library found in %s (sa-utrax.dat / sa-ufiles.dat)", dir);
        return sig = 0;
    }
    uint32_t count = nUtrax / 12, h = 2166136261u;
    h = Fnv(h, &count, 4);
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t *e = (const uint32_t *)(utrax + i * 12);   // start in sa-ufiles.dat, path length, file type
        uint32_t start = e[0], len = e[1];
        if ((uint64_t)start + len > nUfiles || len == 0 || len > 1024) { h = Fnv(h, &i, 4); continue; }
        char path[1100];
        memcpy(path, ufiles + start, len);
        path[len] = 0;
        const char *name = path;
        for (const char *c = path; *c; c++) if (*c == '/' || *c == '\\') name = c + 1;
        char lower[1100];
        size_t ln = strlen(name);
        for (size_t k = 0; k <= ln; k++) lower[k] = name[k] >= 'A' && name[k] <= 'Z' ? (char)(name[k] + 32) : name[k];
        wchar_t wide[1100];
        int wn = MultiByteToWideChar(CP_UTF8, 0, path, (int)len, wide, 1099);
        uint64_t size = 0;
        if (wn > 0) {
            wide[wn] = 0;
            WIN32_FILE_ATTRIBUTE_DATA fa;
            if (GetFileAttributesExW(wide, GetFileExInfoStandard, &fa)) size = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        }
        h = Fnv(h, lower, ln);
        h = Fnv(h, &size, 8);
    }
    free(utrax);
    free(ufiles);
    if (!h) h = 1;
    Log("radio: custom track library of %u tracks, signature %08X (%u ms)", count, h, GetTickCount() - t0);
    return sig = h;
}

// --- Host: what is playing ---
static bool HostSample(MsgRadio &m)
{
    memset(&m, 0, sizeof(m));
    m.type = MSG_RADIO;
    m.station = -1;
    RadioSettings *a = RadioActive();
    if (RadioMode() != 2 || a->station < 0 || a->station > RADIO_USER) return false;   // radio off, retuning, or not started yet
    int track = HwActiveTrack(), pos = HwPlayTime();
    if (track < 0 || pos < 0) return false;
    m.station = a->station;
    m.curType = a->currType;
    m.track = track;
    m.playMs = pos;
    m.lengthMs = HwLength();
    memcpy(m.queue, a->queue, sizeof(m.queue));
    memcpy(m.types, a->types, sizeof(m.types));
    memcpy(m.idx, a->idx, sizeof(m.idx));
    if (a->station == RADIO_USER) m.userSig = LibrarySignature();
    return true;
}

static void HostFrame(uint32_t now)
{
    static uint32_t lastSend, lastLog;
    static int lastStation = -2, lastTrack = -2;
    MsgRadio m;
    bool on = HostSample(m);
    bool changed = m.station != lastStation || (on && m.track != lastTrack);
    if (!changed && now - lastSend < 500) return;
    lastSend = now;
    if (changed && (on || lastStation >= 0))
        Log("radio: host %s, station %d, track %d (type %d), position %d / %d ms, queue %d %d %d", on ? "plays" : "silent", m.station, m.track, m.curType, m.playMs, m.lengthMs, m.queue[0], m.queue[1], m.queue[2]);
    lastStation = m.station;
    lastTrack = on ? m.track : -2;
    NetSendToGuests(&m, sizeof(m));
    (void)lastLog;
}

// --- Guest: follow ---
static void ApplyHost(const MsgRadio &h, int pos)
{
    RadioSettings *rs = RadioRequested();
    int k = 0;
    while (k < 5 && h.queue[k] != h.track) k++;   // the host's queue may not have moved on yet: start from the track being played
    int32_t q[5];
    int8_t t[5], ix[5];
    for (int i = 0; i < 5; i++) {
        int s = k < 5 ? k + i : -1;
        if (s >= 0 && s < 5) { q[i] = h.queue[s]; t[i] = h.types[s]; ix[i] = h.idx[s]; }
        else { q[i] = -1; t[i] = 6; ix[i] = -1; }   // 6 = TYPE_NONE: the game picks the following tracks itself
    }
    if (k >= 5) { q[0] = h.track; t[0] = h.curType; }
    rs->station = h.station;
    memcpy(rs->queue, q, sizeof(q));
    memcpy(rs->types, t, sizeof(t));
    memcpy(rs->idx, ix, sizeof(ix));
    rs->playTime = pos;
    RadioStatePauseTime(h.station) = -1;
    RadioMode() = 3;   // stopping: the game's service then starts the requested settings (what StartRadio does)
    *(uint8_t *)RM = 1;   // m_bInitialised
}

static void GuestFrame(uint32_t now)
{
    static uint32_t mismatchSince, lastApply, giveUpUntil, lastLog;
    static int applyTrack = -2, applyCount;
    static bool libWarned;
    MsgRadio h;
    uint32_t rxAt;
    if (!NetGetRadio(&h, &rxAt) || now - rxAt > 4000 || h.station < 0) { mismatchSince = 0; return; }   // the host's radio is silent
    RadioSettings *a = RadioActive();
    int mode = RadioMode();
    if (now - lastLog > 10000) {
        lastLog = now;
        Log("radio: host station %d track %d pos %d/%d | me station %d track %d pos %d mode %d", h.station, h.track, h.playMs, h.lengthMs, a->station, HwActiveTrack(), HwPlayTime(), mode);
    }
    if (a->station != h.station || mode != 2) { mismatchSince = 0; return; }
    if (*(uint8_t *)(0xBA6748 + 0x5C)) return;   // game menu open
    if (h.station == RADIO_USER) {
        uint32_t mine = LibrarySignature();
        if (!h.userSig || h.userSig != mine) {
            if (!libWarned) {
                libWarned = true;
                Log("radio: custom track libraries differ (host %08X, mine %08X): not following the host on this station", h.userSig, mine);
                HudToast("Custom radio: your track libraries differ, not synced", 5000);
            }
            return;
        }
    }
    int gTrack = HwActiveTrack(), gPos = HwPlayTime();
    if (gTrack < 0 || gPos < 0) return;   // not ready
    float half = NetPing(0);
    int hostPos = h.playMs + (int)(now - rxAt) + (half > 0.0f ? (int)(half * 0.5f) : 0);
    if (h.lengthMs > 0 && (h.lengthMs < 6000 || hostPos > h.lengthMs - 1000)) { mismatchSince = 0; return; }   // short idents, end of track: the next message tells
    bool same = gTrack == h.track;
    int err = same ? (gPos > hostPos ? gPos - hostPos : hostPos - gPos) : 0;
    if (same && err <= 2500) { mismatchSince = 0; return; }
    if (!mismatchSince) mismatchSince = now;
    if (now - mismatchSince < 1500 || now - lastApply < 6000 || now < giveUpUntil) return;
    if (h.track == applyTrack) {
        if (++applyCount > 3) {
            giveUpUntil = now + 60000;
            applyCount = 0;
            Log("radio: track %d did not stay in sync after 3 tries, pausing the sync for 60 s", h.track);
            return;
        }
    } else { applyTrack = h.track; applyCount = 1; }
    lastApply = now;
    mismatchSince = 0;
    if (h.lengthMs > 0 && hostPos > h.lengthMs - 1500) hostPos = h.lengthMs - 1500;
    Log("radio: following the host: station %d, track %d at %d ms (mine: track %d at %d ms, %s), queue %d %d %d", h.station, h.track, hostPos, gTrack, gPos,
        same ? "position off" : "other track", h.queue[0], h.queue[1], h.queue[2]);
    ApplyHost(h, hostPos);
}

void RadioSyncFrame()
{
    static uint32_t last;
    if (!g_cfg.radioSync || !NetRunning() || GameState() != 9) return;
    uint32_t now = GetTickCount();
    if (now - last < 250) return;
    last = now;
    if (g_cfg.host) HostFrame(now);
    else GuestFrame(now);
}
