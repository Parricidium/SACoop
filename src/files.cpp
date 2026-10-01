// Dossier des reglages et sauvegardes : le jeu lit "Personal" (Mes documents) dans le registre et y ajoute
// "\GTA San Andreas User Files". On lui donne le dossier du jeu :
// chaque instance a ses propres reglages et ses sauvegardes coop restent a part de celles du solo.
#include "util.h"
#include "sacoop.h"
#include <string.h>

static LSTATUS(WINAPI *o_RegQueryValueExA)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);

static LSTATUS WINAPI h_RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    if (g_cfg.localUserFiles && name && _stricmp(name, "Personal") == 0 && data && size) {
        char dir[MAX_PATH];
        lstrcpynA(dir, GameDir(), MAX_PATH);
        size_t n = strlen(dir);
        if (n && dir[n - 1] == '\\') dir[--n] = 0;
        if (n + 1 <= *size) {
            memcpy(data, dir, n + 1);
            *size = (DWORD)(n + 1);
            if (type) *type = REG_SZ;
            static bool logged;
            if (!logged) { Log("dossier des sauvegardes : %s\\GTA San Andreas User Files", dir); logged = true; }
            return ERROR_SUCCESS;
        }
    }
    return o_RegQueryValueExA(key, name, reserved, type, data, size);
}

// Le jeu a deja calcule son dossier au demarrage, avant le chargement du mod (dinput8.dll arrive tard) : dossier
// "...\GTA San Andreas User Files" en cache en 0xC92368 (256 octets) et base des sauvegardes "<dossier>\GTASAsf" en
// 0xC16F18 (C_PcSave, completee par numero + ".b"). On les refait sur le dossier du jeu, sinon les sauvegardes
// partiraient quand meme dans Mes documents.
static void LocalUserFilesNow()
{
    if (!g_cfg.localUserFiles) return;
    char dir[MAX_PATH];
    lstrcpynA(dir, GameDir(), MAX_PATH);
    size_t n = strlen(dir);
    if (n && dir[n - 1] == '\\') dir[--n] = 0;
    lstrcatA(dir, "\\GTA San Andreas User Files");
    if (strlen(dir) >= 255) return;
    CreateDirectoryA(dir, NULL);
    lstrcpyA((char *)0xC92368, dir);
    wsprintfA((char *)0xC16F18, "%s\\GTASAsf", dir);
    Log("sauvegardes : %s<n>.b", (const char *)0xC16F18);
}

// CdStreamInit (0x4068F0) cree un semaphore NOMME "CdStream" (gCdStreamSema, 0x8E4004) : deux instances du jeu
// partagent alors le meme objet systeme, se volent leurs signaux et se figent ensemble au chargement (vu le 30/09).
// dinput8.dll est charge apres CdStreamInit : on ne peut pas changer le nom a la creation. Au chargement du mod, le fil
// de lecture (0x406560, poignee 0x8E4008) attend encore, file vide : on le remplace par un fil neuf qui attend sur un
// semaphore anonyme, propre a ce processus.
static void PrivateCdStreamSemaphore()
{
    HANDLE &sema = *(HANDLE *)0x8E4004, &thread = *(HANDLE *)0x8E4008;
    DWORD &threadId = *(DWORD *)0x8E4000;
    int head = *(int *)0x8E3FF0, tail = *(int *)0x8E3FF4;
    if (!sema || !thread || head != tail) { Log("CdStream : pas remplace (sema %p, fil %p, file %d/%d)", sema, thread, head, tail); return; }
    HANDLE fresh = CreateSemaphoreA(NULL, 0, 5, NULL);
    if (!fresh) return;
    int prio = GetThreadPriority(thread);
    TerminateThread(thread, 0);
    WaitForSingleObject(thread, 1000);
    CloseHandle(thread);
    CloseHandle(sema);
    sema = fresh;
    thread = CreateThread(NULL, 0x10000, (LPTHREAD_START_ROUTINE)0x406560, NULL, CREATE_SUSPENDED, &threadId);
    SetThreadPriority(thread, prio);
    ResumeThread(thread);
    Log("CdStream : semaphore prive et fil de lecture relance");
}

// Une seule copie du jeu (0x7468E0) : evenement nomme "Grand theft auto San Andreas" ; s'il existe deja, le jeu met
// l'autre fenetre au premier plan et quitte. Ce controle passe AVANT le chargement de dinput8.dll (charge a la
// premiere utilisation de DirectInput, plus loin dans WinMain) : on ne peut pas l'empecher, mais on ferme l'evenement
// de notre instance des qu'on est charge, pour que la suivante (deuxieme joueur sur le meme PC, tests) demarre.
typedef LONG(NTAPI *NtQueryObject_t)(HANDLE, int, void *, ULONG, ULONG *);
struct UStr { USHORT Length, MaximumLength; PWSTR Buffer; };

static void CloseSingleInstanceEvent()
{
    NtQueryObject_t q = (NtQueryObject_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
    if (!q) return;
    static uint8_t buf[1024];
    int closed = 0;
    for (uintptr_t h = 4; h < 0x4000; h += 4) {
        ULONG len;
        // Type d'abord (2 = ObjectTypeInformation, jamais bloquant) : seulement les evenements.
        if (q((HANDLE)h, 2, buf, sizeof(buf), &len) < 0) continue;
        UStr *type = (UStr *)buf;
        if (!type->Buffer || type->Length != 10 || wcsncmp(type->Buffer, L"Event", 5) != 0) continue;
        if (q((HANDLE)h, 1, buf, sizeof(buf), &len) < 0) continue;   // 1 = ObjectNameInformation
        UStr *name = (UStr *)buf;
        if (name->Buffer && name->Length && wcsstr(name->Buffer, L"Grand theft auto San Andreas")) {
            CloseHandle((HANDLE)h);
            closed++;
        }
    }
    Log("controle d'instance unique : %d evenement(s) ferme(s)", closed);
}

void InstallFileHooks()
{
    LocalUserFilesNow();
    CloseSingleInstanceEvent();
    PrivateCdStreamSemaphore();
    o_RegQueryValueExA = (decltype(o_RegQueryValueExA))HookImport("advapi32.dll", "RegQueryValueExA", (void *)h_RegQueryValueExA);
}
