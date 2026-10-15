// Speech sync (0.54.13): what a character says is heard the same on every PC, the same line at the same moment.
//
// How the game speaks (checked in gta_sa.exe 1.0 US, 15/10): every ped has a CAEPedSpeechAudioEntity at CPed +0x294
// (+4: the ped, +0x92: voice type, 2 = the player; +0x98: talking; +0xA4: sound id, +0xA6: bank id, +0xA8: speech slot,
// +0xAC: volume, +0xB0: context). Three paths choose a line:
//  - CPed::Say (0x5EFFE0) -> AddSayEvent (0x4E6550, event 0x34): the line is picked at RANDOM among the voice's lines for
//    that context (0x4E5920, which writes +0xA4 / +0xA6);
//  - the generic voices (0x4E6100 area, contexts 0xAD-0xAF), same chooser;
//  - pain (0x4E4F70, event 0x35: the grunts when hurt), its own chooser (0x4E4400).
// All three end in LoadAndPlaySpeech (0x4E4840, thiscall, 1 argument: delay), which loads sound +0xA4 of bank +0xA6 into
// slot +0xA8 (slot table 0xB61C38, 0x1C bytes per slot, status 0 = free). Its three calls (0x4E50D2, 0x4E62EB, 0x4E68B8)
// go through SpeechPlay below.
//
// What we do:
//  - A line of the local player (any player: CJ's remarks, his pain) or, on the host, of a ped it shares (MissionPedId:
//    mission and ambient peds near a guest) is sent (MSG_SPEECH: bank, sound, context, voice type), relayed by the host.
//  - The receiver plays that exact bank and sound on the copy (the player's puppet, the ped's copy) the way the game's
//    pain path does it: free slot (0x4E4950), volume, talking flag, then LoadAndPlaySpeech.
//  - The copies' own speech (their random lines, their pain when the local physics hits them) is not played: only what
//    the original said is heard.
// Switch: SyncParole=0 in sacoop.ini. Log lines start with "parole :".
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "entities.h"
#include "speech.h"
#include <string.h>
#include <math.h>

using namespace game;

enum { SP_PLAYER = 0, SP_PED = 1 };

static bool g_installed, g_injecting;
typedef void(__thiscall *Play_t)(void *ent, int delay);
static const Play_t LoadAndPlaySpeech = (Play_t)0x4E4840;
static uint8_t *SlotStatus(int slot) { return (uint8_t *)(0xB61C38 + slot * 0x1C); }

static void *EntityPed(void *ent) { return *(void **)((uint8_t *)ent + 4); }
static uint8_t *SpeechOf(void *ped) { return (uint8_t *)ped + 0x294; }

// The local player's own "talking" state stands for the game's "player is talking" flag (0xB613E5).
static void RestorePlayerTalkFlag()
{
    void *me = FindPlayerPed();
    *(uint8_t *)0xB613E5 = me ? SpeechOf(me)[0x98] : 0;
}

static bool IsCopy(void *ped) { return PuppetIndex(ped) >= 0 || (!g_cfg.host && IsMissionCopy(ped)); }

static void __fastcall SpeechPlay(void *ent, void *, int delay)
{
    uint8_t *e = (uint8_t *)ent;
    void *ped = EntityPed(ent);
    if (g_injecting || !ped || !g_cfg.speechSync || !NetRunning()) { LoadAndPlaySpeech(ent, delay); return; }
    if (IsCopy(ped)) {
        // A copy's own line (random remark, pain from a local hit): not played, the original's comes by the network. The
        // caller already marked it talking and, for the player's voice type, the player as talking: undone.
        e[0x98] = 0;
        if (*(int16_t *)(e + 0x92) == 2) RestorePlayerTalkFlag();
        return;
    }
    LoadAndPlaySpeech(ent, delay);
    int16_t sound = *(int16_t *)(e + 0xA4), bank = *(int16_t *)(e + 0xA6);
    if (sound < 0 || bank < 0) return;
    MsgSpeech m = {};
    m.type = MSG_SPEECH;
    m.from = (uint8_t)(g_localId < 0 ? 0 : g_localId);
    if (ped == FindPlayerPed()) m.kind = SP_PLAYER;
    else if (g_cfg.host && (m.id = MissionPedId(ped)) != 0) m.kind = SP_PED;
    else return;
    m.bank = bank;
    m.sound = sound;
    m.context = *(int16_t *)(e + 0xB0);
    m.voiceType = (uint8_t)*(int16_t *)(e + 0x92);
    NetSendToAll(&m, sizeof(m));
    static int said;
    if (said < 30) { said++; Log("parole : %s dit le son %d de la banque %d (contexte %d), envoye", m.kind == SP_PLAYER ? "le joueur" : "un personnage", sound, bank, m.context); }
}

static void OnSpeech(const MsgSpeech &m)
{
    if (!g_installed || !g_cfg.speechSync || m.from == g_localId || GameState() != 9) return;
    void *ped = m.kind == SP_PLAYER ? PuppetOf(m.from) : m.kind == SP_PED && !g_cfg.host && m.from == 0 ? MissionCopyById(m.id) : nullptr;
    static int said;
    if (!ped) { if (said < 30) { said++; Log("parole : son %d banque %d du joueur %d : pas de copie ici", m.sound, m.bank, m.from); } return; }
    void *me = FindPlayerPed();
    if (me) {   // (too far to be heard: the game would not have said it either)
        const float *a = EntityPos(me), *b = EntityPos(ped);
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        if (dx * dx + dy * dy + dz * dz > 60.0f * 60.0f) return;
    }
    uint8_t *e = SpeechOf(ped);
    if (!e[0x90] || e[0x98]) return;   // speech not set up on this ped, or still saying the previous line
    int slot = (int16_t)((int(__thiscall *)(void *))0x4E4950)(e);   // a free speech slot among the 5 (the player's own slot 5 stays his)
    if (slot < 0 || slot >= 5 || *SlotStatus(slot) != 0) return;
    float vol = (float)*(int8_t *)(*(uint8_t **)0xBD00F8 + (m.context == -1 ? 0x35 : 0x34));   // the game's speech / pain volume
    if (m.voiceType == 2) vol += *(float *)0x8C80E8;   // the player's voice is a little louder, as in the game
    *(float *)(e + 0xAC) = vol;
    e[0x9B] = 0;   // not forced audible
    e[0x9C] = 0;   // not front end (heard at the ped's place)
    *(int16_t *)(e + 0xA8) = (int16_t)slot;
    *(int16_t *)(e + 0xB0) = m.context;
    *(int16_t *)(e + 0xA4) = m.sound;
    *(int16_t *)(e + 0xA6) = m.bank;
    e[0x98] = 1;
    g_injecting = true;
    LoadAndPlaySpeech(e, 0);
    g_injecting = false;
    if (said < 30) { said++; Log("parole : son %d banque %d du joueur %d joue sur sa copie (case %d)", m.sound, m.bank, m.from, slot); }
}

void InstallSpeech()
{
    g_onSpeech = OnSpeech;
    if (!g_cfg.speechSync) { Log("parole : synchronisation desactivee (SyncParole=0)"); return; }
    static const uintptr_t calls[3] = { 0x4E50D2, 0x4E62EB, 0x4E68B8 };
    for (uintptr_t a : calls) {   // each one must be "call 0x4E4840" (other exe: nothing is patched)
        if (*(const uint8_t *)a != 0xE8 || a + 5 + *(const int32_t *)(a + 1) != 0x4E4840) {
            Log("parole : appel inattendu en 0x%06X, synchronisation de la parole non installee", (unsigned)a);
            return;
        }
    }
    for (uintptr_t a : calls) PatchCall(a, (void *)SpeechPlay);
    g_installed = true;
    Log("parole : synchronisation installee (3 appels de LoadAndPlaySpeech)");
}
