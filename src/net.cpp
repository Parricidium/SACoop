#include <winsock2.h>
#include <ws2tcpip.h>
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>

NetPlayer g_players[MAX_PLAYERS];
int g_localId = -1;
void (*g_onWorld)(const MsgWorld &w);
void (*g_onState)(const MsgState &s);
void (*g_onReliable)(int from, const uint8_t *data, int len);
void (*g_onJoin)(int peer);
uint16_t g_myPing;
static MsgRadio g_radio;      // guest: the host's last radio message
static uint32_t g_radioAt;    // ... and when it arrived (0 = none)
bool NetGetRadio(MsgRadio *out, uint32_t *rxAt)
{
    if (!g_radioAt) return false;
    *out = g_radio;
    *rxAt = g_radioAt;
    return true;
}
static volatile float g_pingMs[MAX_PLAYERS];   // measured round trip between us and player i (host: to each guest; guest: to the host, index 0), ms, 0 = unknown
static uint8_t g_pingTable[MAX_PLAYERS];       // guest: every guest's ping to the host as the host measured it (MsgWorld), ms, 0 = unknown
void (*g_onClothes)(const MsgClothes &c);
void (*g_onVehicle)(const MsgVehicle &v);
void (*g_onDamage)(const MsgDamage &d);
void (*g_onPed)(const MsgPed &p);
void (*g_onObject)(const MsgObject &o);
void (*g_onPedHit)(const MsgPedHit &h);
void (*g_onMarker)(const MsgMarker &m);
void (*g_onTrain)(const MsgTrain &t);

static SOCKET g_sock = INVALID_SOCKET;
static sockaddr_in g_hostAddr;               // invite : adresse de l'hote
static sockaddr_in g_peerAddr[MAX_PLAYERS];  // hote : adresse de chaque invite
static uint32_t g_lastHello;
static bool g_everAccepted;   // invite : deja accepte par l'hote pendant cette session

static uint32_t NewSession()
{
    // L'octet bas sert de generation au flux fiable : jamais le meme que la session precedente.
    static uint32_t prev;
    uint32_t s;
    do s = (GetTickCount() * 2654435761u) ^ (GetCurrentProcessId() << 7) ^ ((uint32_t)rand() << 3) ^ (uint32_t)rand();
    while ((uint8_t)s == (uint8_t)prev);
    prev = s;
    return s;
}

// --- Flux fiables : un par pair (hote : un par invite ; invite : un seul, vers l'hote, indice 0) ---
enum { RL_QUEUE = 2048 };
struct RlOut { uint32_t seq; uint16_t len; uint8_t data[MAX_RELIABLE_PAYLOAD]; };
struct RlStream {
    RlOut queue[RL_QUEUE];   // anneau des messages non acquittes
    uint32_t nextSeq;        // prochain numero a attribuer (commence a 1)
    uint32_t acked;          // plus grand numero acquitte (cumulatif)
    uint32_t expected;       // reception : prochain numero attendu
    uint32_t lastResend;
    uint32_t stuckSince;     // emission : depuis quand le meme numero est renvoye sans acquittement
    uint32_t gapSince;       // reception : depuis quand on recoit des numeros au-dela de celui attendu
};
static RlStream g_rl[MAX_PLAYERS];

static void SendTo(const sockaddr_in &to, const void *data, int len);
static void RlReset(RlStream &r) { memset(&r, 0, sizeof(r)); r.nextSeq = 1; r.expected = 1; }
// Un invite qui charge une sauvegarde ne presente plus d'image pendant plusieurs secondes ; les messages de vie
// partent d'un fil a part (NetKeepAlive), le delai n'a donc pas a etre court.
enum { TIMEOUT_MS = 15000 };
// Generation du flux fiable : les 8 bits bas de la session de l'invite (MsgHello.session), connue des deux cotes. Un
// paquet ou un accuse d'une autre generation (ancien flux, avant une reconnexion) est ignore sans acquitter.
static uint32_t g_session;    // invite : numero de la connexion en cours (nouveau a chaque reconnexion)
static uint32_t g_peerSession[MAX_PLAYERS];   // hote : session de chaque invite
static uint8_t RlGen(int peer) { return (uint8_t)(g_cfg.host ? g_peerSession[peer] : g_session); }
enum { RL_HDR = 6 };   // type, generation, seq (4)
static void Disconnect(int i);

bool NetRunning() { return g_sock != INVALID_SOCKET; }

static volatile long g_sentPackets, g_sentBytes;   // statistique (journal toutes les 30 s, NetPoll)
static void SendTo(const sockaddr_in &to, const void *data, int len)
{
    sendto(g_sock, (const char *)data, len, 0, (const sockaddr *)&to, sizeof(to));
    InterlockedIncrement(&g_sentPackets);
    InterlockedExchangeAdd(&g_sentBytes, len);
}

// --- Ping: high resolution time and reception thread ---
// MsgPing.time is a microsecond clock (QueryPerformanceCounter, 32 bits, wraps every 71 minutes; differences are fine).
// Before, ping and pong were handled by NetPoll, once per game frame (30 FPS cap = up to 33 ms of waiting on EACH side,
// plus the 15 ms granularity of GetTickCount): 50 ms shown between two programs on the same PC, 147 ms instead of
// ~15 ms over the internet (tests of 14/10). Now the socket is read by its own thread: a PING is answered the moment it
// arrives and a PONG is timed the moment it arrives. Every packet (PING and PONG too, they count as signs of life) then
// goes into a ring that NetPoll empties on the game thread, in order, exactly as before.
static uint32_t NowUs32()
{
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint32_t)((uint64_t)c.QuadPart * 1000000ull / (uint64_t)freq.QuadPart);
}

static void RecordPing(int id, uint32_t sentUs)
{
    if (id < 0 || id >= MAX_PLAYERS) return;
    uint32_t rtt = NowUs32() - sentUs;
    if (rtt > 5000000u) return;   // more than 5 s: an old packet
    float ms = rtt / 1000.0f;
    float old = g_pingMs[id];
    g_pingMs[id] = old > 0.0f ? old * 0.6f + ms * 0.4f : ms;   // smoothed
    if (!g_cfg.host && id == 0) g_myPing = (uint16_t)(g_pingMs[0] + 0.5f);
}

struct RxPkt { sockaddr_in from; int len; uint8_t buf[1500]; };
enum { RX_QUEUE = 1024 };
static RxPkt *g_rx;
static volatile LONG g_rxHead, g_rxTail, g_rxRun, g_rxDropped;
static HANDLE g_rxThread;
static bool SameAddr(const sockaddr_in &a, const sockaddr_in &b);

static DWORD WINAPI RxThread(LPVOID)
{
    static uint8_t buf[1500];
    sockaddr_in from;
    while (g_rxRun && g_sock != INVALID_SOCKET) {
        SOCKET sock = g_sock;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv = { 0, 100000 };
        if (select(0, &fds, NULL, NULL, &tv) <= 0) continue;
        int fromLen = sizeof(from);
        int len = recvfrom(sock, (char *)buf, sizeof(buf), 0, (sockaddr *)&from, &fromLen);
        if (len <= 0) continue;   // (also ICMP "port unreachable" errors on Windows)
        if (len >= (int)sizeof(MsgPing) && buf[0] == MSG_PING) {
            MsgPing pong = *(const MsgPing *)buf;
            pong.type = MSG_PONG;
            sendto(sock, (const char *)&pong, sizeof(pong), 0, (const sockaddr *)&from, sizeof(from));
        } else if (len >= (int)sizeof(MsgPing) && buf[0] == MSG_PONG) {
            int id = -1;
            if (g_cfg.host) { for (int i = 1; i < MAX_PLAYERS; i++) if (g_players[i].connected && SameAddr(g_peerAddr[i], from)) id = i; }
            else if (SameAddr(from, g_hostAddr)) id = 0;
            RecordPing(id, ((const MsgPing *)buf)->time);
        }
        LONG head = g_rxHead;
        if (!g_rx || head - g_rxTail >= RX_QUEUE) { InterlockedIncrement(&g_rxDropped); continue; }
        RxPkt &q = g_rx[head % RX_QUEUE];
        q.from = from;
        q.len = len;
        memcpy(q.buf, buf, len);
        InterlockedExchange(&g_rxHead, head + 1);
    }
    return 0;
}

static void RxStart()
{
    g_rx = new RxPkt[RX_QUEUE];
    g_rxHead = g_rxTail = g_rxDropped = 0;
    g_rxRun = 1;
    g_rxThread = CreateThread(NULL, 0, RxThread, NULL, 0, NULL);
    if (!g_rxThread) Log("network: could not start the reception thread (error %lu)", GetLastError());
}

static void RxStop()
{
    g_rxRun = 0;
    if (g_rxThread) { WaitForSingleObject(g_rxThread, 1000); CloseHandle(g_rxThread); g_rxThread = NULL; }
    RxPkt *q = g_rx;
    g_rx = nullptr;
    delete[] q;
    g_rxHead = g_rxTail = 0;
}

float NetPing(int player)
{
    if (player < 0 || player >= MAX_PLAYERS || player == g_localId || !g_players[player].connected) return -1.0f;
    if (g_cfg.host) return g_pingMs[player] > 0.0f ? g_pingMs[player] : -1.0f;
    if (player == 0) return g_pingMs[0] > 0.0f ? g_pingMs[0] : -1.0f;
    return g_pingTable[player] ? g_pingTable[player] : -1.0f;   // another guest: his ping to the host
}

// Host: the guests' pings, for MsgWorld (1 ms steps, 255 at most, 0 = unknown).
void NetFillPings(uint8_t *out)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        float p = i > 0 && g_players[i].connected ? g_pingMs[i] : 0.0f;
        out[i] = p <= 0.0f ? 0 : p < 1.0f ? 1 : p > 255.0f ? 255 : (uint8_t)(p + 0.5f);
    }
}

void NetStop()
{
    if (g_sock == INVALID_SOCKET) return;
    NetSendBye();
    SOCKET s = g_sock;
    g_sock = INVALID_SOCKET;
    closesocket(s);
    RxStop();
    for (int i = 0; i < MAX_PLAYERS; i++) g_pingMs[i] = 0.0f;
    memset(g_pingTable, 0, sizeof(g_pingTable));
    g_radioAt = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) g_players[i].connected = false;
    for (auto &r : g_rl) RlReset(r);
    g_localId = -1;
    WSACleanup();
    Log("reseau : arrete");
}

static const sockaddr_in *PeerAddr(int peer);

static void RlSendOne(int peer, const RlOut &o)
{
    uint8_t pkt[RL_HDR + MAX_RELIABLE_PAYLOAD];
    pkt[0] = MSG_RELIABLE;
    pkt[1] = RlGen(peer);
    memcpy(pkt + 2, &o.seq, 4);
    memcpy(pkt + RL_HDR, o.data, o.len);
    if (const sockaddr_in *a = PeerAddr(peer)) SendTo(*a, pkt, RL_HDR + o.len);
}

static void RlPush(int peer, const void *data, int len)
{
    RlStream &r = g_rl[peer];
    if (r.nextSeq - r.acked > RL_QUEUE) {
        // Perdre un message au milieu d'un flux ordonne le casse pour de bon : on coupe le pair, il reviendra avec une
        // nouvelle session.
        Log("reseau : file fiable pleine pour %d, coupure pour resynchroniser", peer);
        Disconnect(peer);
        return;
    }
    RlOut &o = r.queue[r.nextSeq % RL_QUEUE];
    o.seq = r.nextSeq++;
    o.len = (uint16_t)len;
    memcpy(o.data, data, len);
    RlSendOne(peer, o);
}

static void RlResend(int peer, uint32_t now)
{
    RlStream &r = g_rl[peer];
    if (r.acked + 1 >= r.nextSeq) { r.stuckSince = 0; return; }
    if (!r.stuckSince) r.stuckSince = now;
    // Le meme premier message renvoye depuis 10 s sans accuse : le flux du pair est perdu.
    if (now - r.stuckSince > 10000) {
        Log("reseau : flux fiable vers %d bloque (n%u sans accuse depuis 10 s)", peer, r.acked + 1);
        r.stuckSince = now;
        if (g_cfg.host) { uint8_t m = MSG_RESYNC; if (const sockaddr_in *a = PeerAddr(peer)) SendTo(*a, &m, 1); }
        else Disconnect(0);
        return;
    }
    if (now - r.lastResend < 150) return;
    r.lastResend = now;
    int sent = 0;
    for (uint32_t s = r.acked + 1; s < r.nextSeq && sent < 32; s++, sent++) RlSendOne(peer, r.queue[s % RL_QUEUE]);
}

// Reception : n'accepte que le numero attendu (les autres seront renvoyes), acquitte toujours ; une autre
// generation est ignoree sans accuse.
static void RlReceive(int peer, const uint8_t *buf, int len)
{
    if (len < RL_HDR + 1 || len - RL_HDR > MAX_RELIABLE_PAYLOAD) return;
    RlStream &r = g_rl[peer];
    if (buf[1] != RlGen(peer)) return;
    uint32_t seq, now = GetTickCount();
    memcpy(&seq, buf + 2, 4);
    if (seq == r.expected) {
        r.expected++;
        r.gapSince = 0;
        if (g_onReliable) g_onReliable(peer, buf + RL_HDR, len - RL_HDR);
    } else if (seq > r.expected) {
        if (!r.gapSince) r.gapSince = now;
        else if (now - r.gapSince > 5000) {
            Log("reseau : flux fiable de %d desynchronise (attendu n%u, recu n%u depuis 5 s)", peer, r.expected, seq);
            r.gapSince = 0;
            if (!g_cfg.host) { Disconnect(0); return; }
            uint8_t m = MSG_RESYNC;
            if (const sockaddr_in *a = PeerAddr(peer)) SendTo(*a, &m, 1);
        }
    }
    uint8_t ack[RL_HDR] = { MSG_ACK, RlGen(peer) };
    uint32_t a = r.expected - 1;
    memcpy(ack + 2, &a, 4);
    if (const sockaddr_in *addr = PeerAddr(peer)) SendTo(*addr, ack, RL_HDR);
}

static void RlAck(int peer, const uint8_t *buf, int len)
{
    if (len < RL_HDR || buf[1] != RlGen(peer)) return;
    uint32_t a;
    memcpy(&a, buf + 2, 4);
    if (a > g_rl[peer].acked && a < g_rl[peer].nextSeq) { g_rl[peer].acked = a; g_rl[peer].stuckSince = 0; }
}

void NetSendReliableTo(int peer, const void *data, int len)
{
    if (g_sock == INVALID_SOCKET || !g_cfg.host || peer <= 0 || peer >= MAX_PLAYERS || !g_players[peer].connected) return;
    if (len <= MAX_RELIABLE_PAYLOAD) RlPush(peer, data, len);
}

void NetSendReliable(const void *data, int len)
{
    if (g_sock == INVALID_SOCKET || g_localId < 0 || len > MAX_RELIABLE_PAYLOAD) return;
    if (g_cfg.host) {
        for (int i = 1; i < MAX_PLAYERS; i++) if (g_players[i].connected) RlPush(i, data, len);
    } else {
        RlPush(0, data, len);
    }
}

bool NetStart()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { Log("reseau : WSAStartup a echoue"); return false; }
    for (auto &r : g_rl) RlReset(r);
    srand(GetTickCount());
    g_session = NewSession();
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u_long nb = 1;
    ioctlsocket(g_sock, FIONBIO, &nb);
    int bufSize = 1 << 20;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVBUF, (const char *)&bufSize, sizeof(bufSize));
    setsockopt(g_sock, SOL_SOCKET, SO_SNDBUF, (const char *)&bufSize, sizeof(bufSize));

    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(g_cfg.host ? (u_short)g_cfg.port : 0);
    if (bind(g_sock, (sockaddr *)&local, sizeof(local)) != 0) {
        Log("reseau : impossible d'ouvrir le port %d (erreur %d)", g_cfg.port, WSAGetLastError());
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        return false;
    }
    if (g_cfg.host) {
        g_localId = 0;
        g_players[0].connected = true;
        Log("reseau : hote sur le port UDP %d", g_cfg.port);
    } else {
        addrinfo hints = {}, *res = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(g_cfg.address, NULL, &hints, &res) != 0 || !res) {
            Log("reseau : adresse '%s' introuvable", g_cfg.address);
            return false;
        }
        g_hostAddr = *(sockaddr_in *)res->ai_addr;
        g_hostAddr.sin_port = htons((u_short)g_cfg.port);
        freeaddrinfo(res);
        Log("reseau : invite, connexion a %s:%d", g_cfg.address, g_cfg.port);
    }
    RxStart();
    return true;
}

static const sockaddr_in *PeerAddr(int peer)
{
    if (g_cfg.host) return peer > 0 && peer < MAX_PLAYERS && g_players[peer].connected ? &g_peerAddr[peer] : NULL;
    return &g_hostAddr;
}

static bool SameAddr(const sockaddr_in &a, const sockaddr_in &b)
{
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

// Etats de joueur arrives dans le desordre (UDP) : on ne garde que les plus recents. Un numero bien plus petit
// que le dernier vient d'un jeu relance (le compteur repart de 1).
static bool StaleState(NetPlayer &p, const MsgState &s)
{
    if (p.connected && s.seq <= p.lastSeq && p.lastSeq - s.seq < 100000) return true;
    p.lastSeq = s.seq;
    return false;
}

static void Disconnect(int i)
{
    g_players[i].connected = false;
    // L'hote a disparu (ou notre flux est perdu) : on refrappe avec une nouvelle session et un flux fiable neuf.
    if (!g_cfg.host && i == 0) { g_localId = -1; g_session = NewSession(); RlReset(g_rl[0]); }
}

static void HostReceive(const uint8_t *buf, int len, const sockaddr_in &from)
{
    int id = -1;
    for (int i = 1; i < MAX_PLAYERS; i++)
        if (g_players[i].connected && SameAddr(g_peerAddr[i], from)) id = i;

    if (buf[0] == MSG_HELLO && len >= 2 && len < (int)sizeof(MsgHello)) {   // autre version (message plus court)
        MsgFull f = { MSG_FULL, 2, NET_VERSION };
        SendTo(from, &f, sizeof(f));
        return;
    }
    if (buf[0] == MSG_HELLO && len >= (int)sizeof(MsgHello)) {
        const MsgHello *h = (const MsgHello *)buf;
        // Invite deja connu qui refrappe avec une nouvelle session : il repart d'un flux fiable neuf, nous aussi.
        if (id >= 0 && h->session != g_peerSession[id]) {
            g_peerSession[id] = h->session;
            RlReset(g_rl[id]);
            g_players[id].lastSeq = 0;
            Log("reseau : %s (joueur %d) se reconnecte, flux fiable remis a zero", g_players[id].state.name, id);
            if (g_onJoin) g_onJoin(id);
        }
        if (id < 0 && h->version == NET_VERSION)
            for (int i = 1; i < MAX_PLAYERS && id < 0; i++)
                if (!g_players[i].connected) {
                    id = i;
                    memset(&g_players[i], 0, sizeof(g_players[i]));
                    g_peerAddr[i] = from;
                    g_players[i].connected = true;
                    g_peerSession[i] = h->session;
                    RlReset(g_rl[i]);
                    lstrcpynA(g_players[i].state.name, h->name, sizeof(g_players[i].state.name));
                    Log("reseau : %s entre (joueur %d)", g_players[i].state.name, i);
                    if (g_onJoin) g_onJoin(i);
                }
        if (id < 0) {
            MsgFull f = { MSG_FULL, (uint8_t)(h->version == NET_VERSION ? 1 : 2), NET_VERSION };
            SendTo(from, &f, sizeof(f));
            static uint32_t lastLog;
            if (GetTickCount() - lastLog > 5000) { lastLog = GetTickCount(); Log("reseau : %s refuse (%s, version %d)", h->name, f.reason == 1 ? "partie pleine" : "version differente", h->version); }
            return;
        }
        g_players[id].lastSeen = GetTickCount();
        MsgWelcome w = { MSG_WELCOME, (uint8_t)id };
        SendTo(from, &w, sizeof(w));
        return;
    }
    if (id < 0) return;
    g_players[id].lastSeen = GetTickCount();
    if (buf[0] == MSG_STATE && len >= (int)sizeof(MsgState)) {
        MsgState s = *(const MsgState *)buf;
        s.id = (uint8_t)id;
        if (StaleState(g_players[id], s)) return;
        g_players[id].state = s;
        g_players[id].lastStateAt = GetTickCount();
        if (g_onState) g_onState(s);
        for (int i = 1; i < MAX_PLAYERS; i++)   // relais aux autres invites
            if (i != id && g_players[i].connected) SendTo(g_peerAddr[i], &s, sizeof(s));
    } else if (buf[0] == MSG_CLOTHES && len >= (int)sizeof(MsgClothes)) {
        MsgClothes c = *(const MsgClothes *)buf;
        c.id = (uint8_t)id;
        if (g_onClothes) g_onClothes(c);
        for (int i = 1; i < MAX_PLAYERS; i++)   // relais aux autres invites
            if (i != id && g_players[i].connected) SendTo(g_peerAddr[i], &c, sizeof(c));
    } else if (buf[0] == MSG_DAMAGE && len >= (int)sizeof(MsgDamage)) {
        MsgDamage d = *(const MsgDamage *)buf;
        d.from = (uint8_t)id;
        if (g_onDamage) g_onDamage(d);
        for (int i = 1; i < MAX_PLAYERS; i++)   // relais aux autres invites (seul le joueur touche l'applique)
            if (i != id && g_players[i].connected) SendTo(g_peerAddr[i], &d, sizeof(d));
    } else if (buf[0] == MSG_PEDHIT && len >= (int)sizeof(MsgPedHit)) {
        MsgPedHit h = *(const MsgPedHit *)buf;
        h.from = (uint8_t)id;
        if (g_onPedHit) g_onPedHit(h);
    } else if (buf[0] == MSG_VEHICLE && len >= (int)sizeof(MsgVehicle)) {
        if (g_onVehicle) g_onVehicle(*(const MsgVehicle *)buf);
        for (int i = 1; i < MAX_PLAYERS; i++)   // relais aux autres invites
            if (i != id && g_players[i].connected) SendTo(g_peerAddr[i], buf, len);
    } else if (buf[0] == MSG_TRAIN && len >= (int)sizeof(MsgTrain)) {
        MsgTrain t = *(const MsgTrain *)buf;
        t.owner = (uint8_t)id;   // (the sender is who the host knows, whatever the message says)
        if (g_onTrain) g_onTrain(t);
        for (int i = 1; i < MAX_PLAYERS; i++)   // relay to the other guests
            if (i != id && g_players[i].connected) SendTo(g_peerAddr[i], &t, sizeof(t));
    } else if (buf[0] == MSG_OBJECT && len >= (int)sizeof(MsgObject)) {
        MsgObject o = *(const MsgObject *)buf;
        o.from = (uint8_t)id;
        if (g_onObject) g_onObject(o);
        for (int i = 1; i < MAX_PLAYERS; i++)   // relais aux autres invites
            if (i != id && g_players[i].connected) SendTo(g_peerAddr[i], &o, sizeof(o));
    } else if (buf[0] == MSG_PING || buf[0] == MSG_PONG) {
        // (answered / timed by the reception thread; here it only counts as a sign of life, done above)
    } else if (buf[0] == MSG_RELIABLE) {
        RlReceive(id, buf, len);
    } else if (buf[0] == MSG_ACK) {
        RlAck(id, buf, len);
    } else if (buf[0] == MSG_BYE) {
        Log("reseau : joueur %d parti", id);
        Disconnect(id);
        MsgBye b = { MSG_BYE, (uint8_t)id };
        for (int i = 1; i < MAX_PLAYERS; i++)   // les autres invites n'attendent pas le delai
            if (i != id && g_players[i].connected) SendTo(g_peerAddr[i], &b, sizeof(b));
    }
}

static void GuestReceive(const uint8_t *buf, int len, const sockaddr_in &from)
{
    if (!SameAddr(from, g_hostAddr)) return;
    g_players[0].lastSeen = GetTickCount();
    switch (buf[0]) {
    case MSG_RELIABLE:
        RlReceive(0, buf, len);
        break;
    case MSG_PONG:   // (timed by the reception thread)
    case MSG_PING:   // (answered by the reception thread)
        break;
    case MSG_ACK:
        RlAck(0, buf, len);
        break;
    case MSG_RESYNC:
        if (g_localId >= 0) { Log("reseau : l'hote demande une resynchronisation"); Disconnect(0); }
        break;
    case MSG_WELCOME:
        if (len >= (int)sizeof(MsgWelcome) && g_localId != ((const MsgWelcome *)buf)->id) {
            g_localId = ((const MsgWelcome *)buf)->id;
            g_players[0].connected = true;
            Log("reseau : accepte par l'hote, joueur %d", g_localId);
            g_everAccepted = true;
        }
        break;
    case MSG_FULL: {
        static uint32_t lastLog;
        const MsgFull *f = (const MsgFull *)buf;
        if (GetTickCount() - lastLog > 5000) {
            lastLog = GetTickCount();
            if (len >= (int)sizeof(MsgFull) && f->reason == 2) Log("reseau : l'hote refuse : version differente (la sienne %d, la notre %d)", f->hostVersion, NET_VERSION);
            else Log("reseau : l'hote refuse : partie pleine");
        }
        break;
    }
    case MSG_STATE:
        if (len >= (int)sizeof(MsgState)) {
            const MsgState *s = (const MsgState *)buf;
            if (s->id < MAX_PLAYERS && s->id != g_localId) {
                NetPlayer &p = g_players[s->id];
                if (!p.connected) Log("reseau : %s (joueur %d) est la", s->name, s->id);
                if (StaleState(p, *s)) break;
                p.connected = true;
                p.state = *s;
                p.lastSeen = p.lastStateAt = GetTickCount();
                if (g_onState) g_onState(*s);
            }
        }
        break;
    case MSG_WORLD:
        if (len >= (int)sizeof(MsgWorld)) {
            memcpy(g_pingTable, ((const MsgWorld *)buf)->ping, sizeof(g_pingTable));
            if (g_onWorld) g_onWorld(*(const MsgWorld *)buf);
        }
        break;
    case MSG_DAMAGE:
        if (len >= (int)sizeof(MsgDamage) && g_onDamage) g_onDamage(*(const MsgDamage *)buf);
        break;
    case MSG_PED:
        if (len >= (int)sizeof(MsgPed) && g_onPed) g_onPed(*(const MsgPed *)buf);
        break;
    case MSG_MARKER:
        if (len >= (int)sizeof(MsgMarker) && g_onMarker) g_onMarker(*(const MsgMarker *)buf);
        break;
    case MSG_VEHICLE:
        if (len >= (int)sizeof(MsgVehicle) && g_onVehicle) g_onVehicle(*(const MsgVehicle *)buf);
        break;
    case MSG_OBJECT:
        if (len >= (int)sizeof(MsgObject) && g_onObject) g_onObject(*(const MsgObject *)buf);
        break;
    case MSG_TRAIN:
        if (len >= (int)sizeof(MsgTrain) && g_onTrain) g_onTrain(*(const MsgTrain *)buf);
        break;
    case MSG_RADIO:
        if (len >= (int)sizeof(MsgRadio)) { g_radio = *(const MsgRadio *)buf; g_radioAt = GetTickCount() | 1; }
        break;
    case MSG_CLOTHES:
        if (len >= (int)sizeof(MsgClothes) && ((const MsgClothes *)buf)->id != g_localId && g_onClothes) g_onClothes(*(const MsgClothes *)buf);
        break;
    case MSG_BYE:
        if (len >= (int)sizeof(MsgBye) && ((const MsgBye *)buf)->id < MAX_PLAYERS && ((const MsgBye *)buf)->id != g_localId) {
            int who = ((const MsgBye *)buf)->id;
            Log("reseau : joueur %d parti", who);
            Disconnect(who);
        }
        break;
    }
}

static long g_recvPackets, g_recvBytes;
void NetPoll()
{
    if (g_sock == INVALID_SOCKET) return;
    while (g_rx && g_sock != INVALID_SOCKET && g_rxTail != g_rxHead) {   // packets read by the reception thread, in order
        RxPkt &q = g_rx[(uint32_t)g_rxTail % RX_QUEUE];
        const uint8_t *buf = q.buf;
        int len = q.len;
        const sockaddr_in from = q.from;
        g_recvPackets++;
        g_recvBytes += len;
        if (buf[0] == MSG_BATCH && len >= 2) {   // grouped packet: each message in turn
            int n = buf[1], p = 2;
            for (int k = 0; k < n && p + 2 <= len; k++) {
                int l = *(const uint16_t *)(buf + p);
                p += 2;
                if (l <= 0 || p + l > len) break;
                if (g_cfg.host) HostReceive(buf + p, l, from);
                else GuestReceive(buf + p, l, from);
                p += l;
            }
        } else if (g_cfg.host) HostReceive(buf, len, from);
        else GuestReceive(buf, len, from);
        InterlockedExchange(&g_rxTail, g_rxTail + 1);
    }

    uint32_t now = GetTickCount();
    static uint32_t statSince;
    if (!statSince) statSince = now;
    if (now - statSince >= 30000) {   // debit et ping (diagnostic des desynchronisations)
        float sec = (now - statSince) / 1000.0f;
        Log("network: ping %.1f %.1f %.1f %.1f ms (players 0-3, 0 = unknown), received %.0f packets/s (%.1f kB/s), sent %.0f packets/s (%.1f kB/s), %ld dropped by the queue",
            g_pingMs[0], g_pingMs[1], g_pingMs[2], g_pingMs[3], g_recvPackets / sec, g_recvBytes / sec / 1024.0f, g_sentPackets / sec, g_sentBytes / sec / 1024.0f, (long)g_rxDropped);
        g_recvPackets = g_recvBytes = 0;
        g_sentPackets = g_sentBytes = 0;
        statSince = now;
    }
    if (g_cfg.host) { for (int i = 1; i < MAX_PLAYERS; i++) if (g_players[i].connected) RlResend(i, now); }
    else if (g_localId >= 0) RlResend(0, now);
    static uint32_t lastPing;
    if (g_localId >= 0 && now - lastPing > 1000) {   // guest: to the host; host: to every guest (their pings go out in MsgWorld)
        lastPing = now;
        MsgPing p = { MSG_PING, NowUs32() };
        if (g_cfg.host) { for (int i = 1; i < MAX_PLAYERS; i++) if (g_players[i].connected) SendTo(g_peerAddr[i], &p, sizeof(p)); }
        else SendTo(g_hostAddr, &p, sizeof(p));
    }
    if (!g_cfg.host && g_localId < 0 && now - g_lastHello > 1000) {   // on frappe a la porte chaque seconde
        MsgHello h = { MSG_HELLO, NET_VERSION };
        lstrcpynA(h.name, g_cfg.playerName, sizeof(h.name));
        h.rejoin = g_everAccepted && *(int *)sa::gGameState == 9;   // deja dans la partie : simple coupure
        h.session = g_session;
        SendTo(g_hostAddr, &h, sizeof(h));
        g_lastHello = now;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i == g_localId || !g_players[i].connected) continue;
        if (now - g_players[i].lastSeen > TIMEOUT_MS) {
            Log("reseau : joueur %d ne repond plus", i);
            Disconnect(i);
        }
    }
}

// Depuis le fil du chien de garde (toutes les 3 s) : un signe de vie meme quand le fil du jeu ne presente plus
// d'image (chargement, gel) ; sinon l'autre cote nous declarait parti.
void NetKeepAlive()
{
    if (g_sock == INVALID_SOCKET || g_localId < 0) return;
    MsgPing p = { MSG_PING, NowUs32() };
    if (g_cfg.host) { for (int i = 1; i < MAX_PLAYERS; i++) if (g_players[i].connected) SendTo(g_peerAddr[i], &p, sizeof(p)); }
    else SendTo(g_hostAddr, &p, sizeof(p));
}

// Regroupement (fil du jeu) : les messages d'etat d'une image vers un meme joueur partent dans un seul paquet
// MSG_BATCH (jusqu'a 1200 octets), envoye a la fin de l'image (NetFlush) ou quand il est plein. Avec une forte
// population partagee, c'etaient des centaines de paquets par seconde (un par pieton ou voiture), trop pour un tunnel
// VPN : pertes, pantins qui se teleportent (essai de JD et GG du 01/10, densite 300 %).
struct Batch { uint8_t buf[1200]; int len, n; };
static Batch g_batch[MAX_PLAYERS];   // hote : par invite ; invite : case 0 = l'hote
static const sockaddr_in &BatchAddr(int i) { return g_cfg.host ? g_peerAddr[i] : g_hostAddr; }
static void BatchFlush(int i)
{
    Batch &b = g_batch[i];
    if (b.n == 1) SendTo(BatchAddr(i), b.buf + 4, b.len - 4);   // un seul message : tel quel
    else if (b.n > 1) { b.buf[0] = MSG_BATCH; b.buf[1] = (uint8_t)b.n; SendTo(BatchAddr(i), b.buf, b.len); }
    b.n = 0;
    b.len = 2;
}
static void BatchAdd(int i, const void *data, int len)
{
    if (len > 1000) { SendTo(BatchAddr(i), data, len); return; }
    Batch &b = g_batch[i];
    if (b.n == 0) b.len = 2;
    if (b.len + 2 + len > (int)sizeof(b.buf) || b.n == 255) BatchFlush(i);
    *(uint16_t *)(b.buf + b.len) = (uint16_t)len;
    memcpy(b.buf + b.len + 2, data, len);
    b.len += 2 + len;
    b.n++;
}
void NetFlush()
{
    if (g_sock == INVALID_SOCKET) return;
    for (int i = 0; i < MAX_PLAYERS; i++) if (g_batch[i].n) BatchFlush(i);
}

void NetSendToGuests(const void *data, int len)
{
    if (g_sock == INVALID_SOCKET || !g_cfg.host) return;
    for (int i = 1; i < MAX_PLAYERS; i++)
        if (g_players[i].connected) BatchAdd(i, data, len);
}

void NetSendToAll(const void *data, int len)
{
    if (g_sock == INVALID_SOCKET || g_localId < 0) return;
    if (g_cfg.host) NetSendToGuests(data, len);
    else BatchAdd(0, data, len);
}

void NetSendBye()
{
    if (g_sock == INVALID_SOCKET || g_localId < 0) return;
    MsgBye b = { MSG_BYE, (uint8_t)g_localId };
    for (int k = 0; k < 2; k++) {   // deux fois : c'est de l'UDP et on ne reviendra pas
        if (g_cfg.host) NetSendToGuests(&b, sizeof(b)); else SendTo(g_hostAddr, &b, sizeof(b));
    }
}

void NetSendState(const MsgState &s)
{
    if (g_sock == INVALID_SOCKET || g_localId < 0) return;
    if (g_cfg.host) {
        for (int i = 1; i < MAX_PLAYERS; i++)
            if (g_players[i].connected) SendTo(g_peerAddr[i], &s, sizeof(s));
    } else {
        SendTo(g_hostAddr, &s, sizeof(s));
    }
}
