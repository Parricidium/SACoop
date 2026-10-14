// Charge de la machine et echantillonnage du fil du jeu (diagnostic des saccades). Au journal toutes les 10 s :
//  - part de processeur du fil du jeu, du processus et de la machine : un jeu lent avec un fil qui dort (peu de
//    processeur utilise) attend quelque chose ; un fil a 100 % calcule ;
//  - octets lus sur disque par le jeu (streaming) ;
//  - les processus les plus gourmands de la machine ;
//  - Profil=1 dans sacoop.ini : le fil du jeu est en plus echantillonne toutes les 20 ms (module de l'instruction en
//    cours, premiere adresse de retour dans le code du jeu), pour savoir ou il passe son temps ou ce qu'il attend.
// Rien ici n'agit sur le jeu, sauf le retrait de la limitation d'energie de Windows 11 pour le processus.
#include "util.h"
#include "sacoop.h"
#include <tlhelp32.h>
#include <string.h>

static DWORD g_gameThreadId;

static ULONGLONG U64(const FILETIME &f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

// --- Echantillons du fil du jeu ---
struct Sample { uintptr_t eip, c1; };
enum { MAX_SAMPLES = 2048, MAX_PIDS = 2048 };
static Sample g_s[MAX_SAMPLES];
static int g_ns;

static void TakeSample(HANDLE th)
{
    if (g_ns >= MAX_SAMPLES) return;
    DWORD buf[256];
    bool ok = false;
    uintptr_t eip = 0;
    SuspendThread(th);
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_CONTROL;
    if (GetThreadContext(th, &ctx)) {
        eip = ctx.Eip;
        const DWORD *sp = (const DWORD *)ctx.Esp;
        if (!IsBadReadPtr(sp, sizeof(buf))) { memcpy(buf, sp, sizeof(buf)); ok = true; }
    }
    ResumeThread(th);   // (rien d'autre tant que le fil est suspendu : il pourrait tenir un verrou)
    if (!eip) return;
    Sample s = { eip, 0 };
    if (ok) for (int i = 0; i < 256; i++) if (buf[i] >= 0x401000 && buf[i] < 0x857000) { s.c1 = buf[i]; break; }
    g_s[g_ns++] = s;
}

static const char *ModuleName(uintptr_t a)
{
    struct Entry { uintptr_t base; char name[24]; };
    static Entry cache[64];
    static int n;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)a, &mbi, sizeof(mbi))) return "?";
    uintptr_t base = (uintptr_t)mbi.AllocationBase;
    for (int i = 0; i < n; i++) if (cache[i].base == base) return cache[i].name;
    if (n >= 64) return "autre";
    char path[MAX_PATH];
    const char *nm = "code anonyme";
    if (base && GetModuleFileNameA((HMODULE)base, path, MAX_PATH)) {
        const char *b = strrchr(path, '\\');
        nm = b ? b + 1 : path;
    }
    cache[n].base = base;
    lstrcpynA(cache[n].name, nm, sizeof(cache[n].name));
    return cache[n++].name;
}

struct Count { uintptr_t key; char name[24]; int n; };
static void AddCount(Count *arr, int &n, int cap, uintptr_t key, const char *name)
{
    for (int i = 0; i < n; i++) if (arr[i].key == key && (!name || !lstrcmpA(arr[i].name, name))) { arr[i].n++; return; }
    if (n >= cap) return;
    arr[n].key = key;
    lstrcpynA(arr[n].name, name ? name : "", sizeof(arr[n].name));
    arr[n].n = 1;
    n++;
}
static int TakeMax(Count *arr, int n)
{
    int best = -1;
    for (int i = 0; i < n; i++) if (arr[i].n > 0 && (best < 0 || arr[i].n > arr[best].n)) best = i;
    return best;
}

static void ReportSamples()
{
    if (!g_ns) return;
    static Count mods[16], eips[128], calls[128];
    int nm = 0, ne = 0, nc = 0, total = g_ns;
    for (int i = 0; i < g_ns; i++) {
        AddCount(mods, nm, 16, 0, ModuleName(g_s[i].eip));
        if (g_s[i].eip >= 0x401000 && g_s[i].eip < 0x857000) AddCount(eips, ne, 128, g_s[i].eip >> 4, nullptr);
        if (g_s[i].c1) AddCount(calls, nc, 128, g_s[i].c1, nullptr);
    }
    char line[900];
    int len = wsprintfA(line, "profil : %d echantillons ; en cours dans :", total);
    for (int k = 0; k < 5; k++) {
        int b = TakeMax(mods, nm);
        if (b < 0) break;
        len += wsprintfA(line + len, " %s %d%c", mods[b].name, mods[b].n * 100 / total, '%');
        mods[b].n = 0;
    }
    len += wsprintfA(line + len, " ; instructions du jeu :");
    for (int k = 0; k < 5; k++) {
        int b = TakeMax(eips, ne);
        if (b < 0) break;
        len += wsprintfA(line + len, " %06X x%d", (unsigned)(eips[b].key << 4), eips[b].n);
        eips[b].n = 0;
    }
    len += wsprintfA(line + len, " ; premiers appelants :");
    for (int k = 0; k < 6; k++) {
        int b = TakeMax(calls, nc);
        if (b < 0) break;
        len += wsprintfA(line + len, " %06X x%d", (unsigned)calls[b].key, calls[b].n);
        calls[b].n = 0;
    }
    Log("%s", line);
    g_ns = 0;
}

// --- Processus les plus gourmands ---
struct ProcPrev { DWORD pid; ULONGLONG cpu; };
static ProcPrev g_pp[MAX_PIDS], g_pn[MAX_PIDS];
static int g_npp;

static void TopProcesses(ULONGLONG wall100ns, char *out, int cap)
{
    out[0] = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    struct Top { char name[32]; ULONGLONG d; } top[4] = {};
    int nn = 0;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) do {
        if (nn >= MAX_PIDS) break;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        FILETIME c, e, k, u;
        if (GetProcessTimes(h, &c, &e, &k, &u)) {
            ULONGLONG cpu = U64(k) + U64(u), prev = 0;
            bool had = false;
            g_pn[nn].pid = pe.th32ProcessID;
            g_pn[nn].cpu = cpu;
            nn++;
            for (int i = 0; i < g_npp; i++) if (g_pp[i].pid == pe.th32ProcessID) { prev = g_pp[i].cpu; had = true; break; }
            if (had && cpu >= prev) {
                ULONGLONG d = cpu - prev;
                int slot = -1;
                for (int i = 0; i < 4; i++) if (d > top[i].d && (slot < 0 || top[i].d < top[slot].d)) slot = i;
                if (slot >= 0) { top[slot].d = d; lstrcpynA(top[slot].name, pe.szExeFile, sizeof(top[slot].name)); }
            }
        }
        CloseHandle(h);
    } while (Process32Next(snap, &pe));
    CloseHandle(snap);
    memcpy(g_pp, g_pn, nn * sizeof(ProcPrev));
    g_npp = nn;
    int len = 0;
    for (int k = 0; k < 4; k++) {
        int b = -1;
        for (int i = 0; i < 4; i++) if (top[i].d > 0 && (b < 0 || top[i].d > top[b].d)) b = i;
        if (b < 0 || len > cap - 64) break;
        len += wsprintfA(out + len, "%s%s %d%c", len ? ", " : "", top[b].name, (int)(top[b].d * 100 / (wall100ns ? wall100ns : 1)), '%');
        top[b].d = 0;
    }
}

// --- Meteo (diagnostic) : chaque changement de la meteo du jeu est journalise, avec l'heure du jeu et la cinematique ---
// CWeather : ancienne 0xC81320, nouvelle 0xC8131C, forcee 0xC81318, fondu 0xC8130C ; CClock 0xB70153 / 0xB70152 ;
// CCutsceneMgr::ms_running 0xB5F851 ; gGameState 0xC8D4C0. Lecture seule, depuis ce fil.
static void WeatherWatch()
{
    static short o = -2, n = -2, f = -2;
    static int said;
    if (*(int *)0xC8D4C0 != 9 || said >= 300) return;
    short wo = *(short *)0xC81320, wn = *(short *)0xC8131C, wf = *(short *)0xC81318;
    if (wo == o && wn == n && wf == f) return;
    o = wo; n = wn; f = wf;
    said++;
    Log("meteo : ancienne %d, nouvelle %d, forcee %d, fondu %.2f ; heure du jeu %02d:%02d ; cinematique %d",
        (int)wo, (int)wn, (int)wf, *(float *)0xC8130C, (int)*(uint8_t *)0xB70153, (int)*(uint8_t *)0xB70152, (int)*(uint8_t *)0xB5F851);
}

// --- Windows 11 : limitation d'energie du processus (EcoQoS) et minuterie ---
// Un processus juge "en arriere-plan" peut etre ralenti et voir la resolution de sa minuterie ignoree : on demande a
// ne pas l'etre (ProcessPowerThrottling = 4, ControlMask = vitesse d'execution 1 + resolution de minuterie 4,
// StateMask = 0). Sans effet sur Windows 10 (l'appel echoue, journalise).
static void NoPowerThrottling()
{
    struct State { ULONG Version, ControlMask, StateMask; } st = { 1, 1 | 4, 0 };
    typedef BOOL(WINAPI * SetProcessInformation_t)(HANDLE, int, LPVOID, DWORD);
    SetProcessInformation_t fn = (SetProcessInformation_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessInformation");
    if (!fn) { Log("energie : SetProcessInformation absent (Windows plus ancien que 8)"); return; }
    BOOL ok = fn(GetCurrentProcess(), 4, &st, sizeof(st));
    Log("energie : limitation du processus retiree -> %s (code %lu)", ok ? "ok" : "refuse", ok ? 0UL : GetLastError());
}

static DWORD WINAPI ChargeThread(LPVOID)
{
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_gameThreadId);
    if (!th) return 0;
    const bool sampling = GetPrivateProfileIntA("SACoop", "Profil", 0, IniPath()) != 0;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    NoPowerThrottling();
    Log("charge : %lu coeurs logiques ; journal toutes les 10 s%s", si.dwNumberOfProcessors, sampling ? ", echantillonnage du fil du jeu actif (Profil=1)" : " (Profil=1 pour echantillonner le fil du jeu)");

    FILETIME c, e, k, u, idle, ker, usr;
    ULONGLONG prevWall = GetTickCount64(), prevThread = 0, prevProc = 0, prevIdle = 0, prevSys = 0, prevRead = 0;
    if (GetThreadTimes(th, &c, &e, &k, &u)) prevThread = U64(k) + U64(u);
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) prevProc = U64(k) + U64(u);
    if (GetSystemTimes(&idle, &ker, &usr)) { prevIdle = U64(idle); prevSys = U64(ker) + U64(usr); }
    IO_COUNTERS io = {};
    if (GetProcessIoCounters(GetCurrentProcess(), &io)) prevRead = io.ReadTransferCount;
    char top[256];
    TopProcesses(1, top, sizeof(top));   // (premier releve : sert de point de depart)

    for (;;) {
        ULONGLONG deadline = GetTickCount64() + 10000;
        while (GetTickCount64() < deadline) {
            if (sampling) { TakeSample(th); Sleep(20); }
            else Sleep(100);
            WeatherWatch();
        }
        ULONGLONG nowWall = GetTickCount64(), wall100 = (nowWall - prevWall) * 10000;
        if (!wall100) continue;
        ULONGLONG thr = prevThread, prc = prevProc, idl = prevIdle, sys = prevSys, rd = prevRead;
        if (GetThreadTimes(th, &c, &e, &k, &u)) thr = U64(k) + U64(u);
        if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) prc = U64(k) + U64(u);
        if (GetSystemTimes(&idle, &ker, &usr)) { idl = U64(idle); sys = U64(ker) + U64(usr); }
        if (GetProcessIoCounters(GetCurrentProcess(), &io)) rd = io.ReadTransferCount;
        int pctThread = (int)((thr - prevThread) * 100 / wall100), pctProc = (int)((prc - prevProc) * 100 / wall100);
        int pctSys = sys > prevSys ? (int)(100 - (idl - prevIdle) * 100 / (sys - prevSys)) : 0;
        DWORD fgPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
        TopProcesses(wall100, top, sizeof(top));
        Log("charge : fil du jeu %d %% d'un coeur, processus %d %%, machine %d %% ; disque lu par le jeu %u Mo ; premier plan : %s ; plus gourmands : %s",
            pctThread, pctProc, pctSys, (unsigned)((rd - prevRead) >> 20), fgPid == GetCurrentProcessId() ? "le jeu" : "autre fenetre", top);
        ReportSamples();
        prevWall = nowWall; prevThread = thr; prevProc = prc; prevIdle = idl; prevSys = sys; prevRead = rd;
    }
}

void StartCharge()
{
    g_gameThreadId = GetCurrentThreadId();
    CreateThread(NULL, 0, ChargeThread, NULL, 0, NULL);
}
