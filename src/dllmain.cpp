// Point d'entree : SACoop se fait passer pour dinput8.dll (le jeu l'importe statiquement)
// et renvoie DirectInput8Create vers la vraie DLL du systeme.
#include "util.h"
#include "sacoop.h"
#include "camera.h"
#include "mods.h"
#include "render.h"
#include "script.h"
#include "widescreen.h"
#include "menu.h"
#include "net.h"
#include "panel.h"
#include "game.h"
#include "gfx.h"
#include "fps.h"
#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>

Config g_cfg;

typedef HRESULT(WINAPI *DirectInput8Create_t)(HINSTANCE, DWORD, const GUID &, LPVOID *, void *);

// Souris et clavier DirectInput : tant que l'interface en jeu est ouverte (menu F10, aide F1, tchat), la souris sert
// a son curseur et le jeu ne voit ni souris ni clavier (camera, tirs, touches immobiles). A la fermeture, les touches
// et boutons encore enfonces restent caches jusqu'a ce qu'ils soient relaches (Echap ne rouvre pas le menu du jeu,
// le clic sur X ne tire pas). IDirectInput8::CreateDevice = vtable[3], IDirectInputDevice8::GetDeviceState = vtable[9].
static const GUID kSysMouse = { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID kSysKeyboard = { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static void *g_diMouse, *g_diKeyboard;
typedef HRESULT(WINAPI *GetDeviceState_t)(void *, DWORD, void *);
typedef HRESULT(WINAPI *CreateDevice_t)(void *, const GUID &, void **, void *);
static GetDeviceState_t o_GetDeviceState;
static CreateDevice_t o_CreateDevice;

static const HRESULT DI_OK_TEST = 0;
static bool AnyDown(const uint8_t *b, DWORD n) { for (DWORD i = 0; i < n; i++) if (b[i] & 0x80) return true; return false; }

static HRESULT WINAPI h_GetDeviceState(void *dev, DWORD size, void *data)
{
    HRESULT hr = o_GetDeviceState(dev, size, data);
    static int testMouseAny = -1;   // (TestSouris : instance de test sans le premier plan, souris non acquise)
    if (testMouseAny < 0) testMouseAny = GetPrivateProfileIntA("SACoop", "TestSouris", 0, IniPath());
    if (FAILED(hr) && testMouseAny > 0 && dev == g_diMouse && data) { memset(data, 0, size); hr = DI_OK_TEST; }
    if (FAILED(hr) || !data) return hr;
    static bool holdMouse, holdKeys;
    bool capture = PanelCapturesKeys();
    if (dev == g_diMouse && size >= 16) {
        LONG *m = (LONG *)data;
        uint8_t *buttons = (uint8_t *)data + 12;
        DWORD nb = size >= 20 ? 8 : 4;
        static int testMouse = -1;   // TestSouris=1 : souris poussee vers l'avant 1 s, 30 s apres l'arrivee en partie
        if (testMouse < 0) testMouse = GetPrivateProfileIntA("SACoop", "TestSouris", 0, IniPath());
        if (testMouse > 0 && game::GameState() == 9) {
            static DWORD since;
            if (!since) since = GetTickCount();
            DWORD t = GetTickCount() - since;
            float *at = nullptr;
            if (void *cam = *(void **)0xC1703C) if (uint8_t *fr = *(uint8_t **)((uint8_t *)cam + 4)) at = (float *)(fr + 0x70);   // RwFrame LTM : at
            static float z0;
            static int phase;
            if (phase == 0 && t > 30000 && at) { phase = 1; z0 = at[2]; }
            if (phase == 1) { m[1] = -15; if (t > 31000) { phase = 2; Log("test souris : vers l'avant, regard z %.3f -> %.3f (%s)", z0, at ? at[2] : 0.0f, at && at[2] > z0 ? "vers le haut" : "vers le bas"); } }
        }
        if (PanelOpen()) { PanelMouseInput(m[0], m[1], m[2], (buttons[0] & 0x80) != 0); holdMouse = true; }
        if (capture || holdMouse) {
            if (!capture && !AnyDown(buttons, nb)) holdMouse = false;
            memset(data, 0, size);
        }
    } else if (dev == g_diKeyboard && size >= 256) {
        if (capture) holdKeys = true;
        if (holdKeys) {
            if (!capture && !AnyDown((uint8_t *)data, 256)) holdKeys = false;
            memset(data, 0, size);
        }
    }
    return hr;
}

static HRESULT WINAPI h_CreateDevice(void *di, const GUID &guid, void **out, void *outer)
{
    HRESULT hr = o_CreateDevice(di, guid, out, outer);
    if (SUCCEEDED(hr) && out && *out) {
        bool mouse = !memcmp(&guid, &kSysMouse, sizeof(GUID)), keyboard = !memcmp(&guid, &kSysKeyboard, sizeof(GUID));
        if (mouse) g_diMouse = *out;
        if (keyboard) g_diKeyboard = *out;
        if ((mouse || keyboard) && !o_GetDeviceState) {
            void **vt = *(void ***)*out;
            o_GetDeviceState = (GetDeviceState_t)vt[9];
            DWORD old;
            VirtualProtect(&vt[9], 4, PAGE_EXECUTE_READWRITE, &old);
            vt[9] = (void *)h_GetDeviceState;
            VirtualProtect(&vt[9], 4, old, &old);
            Log("entrees : souris et clavier DirectInput suivis (interface en jeu)");
        }
    }
    return hr;
}

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD ver, const GUID &riid, LPVOID *out, void *outer)
{
    static DirectInput8Create_t real;
    if (!real) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        lstrcatA(path, "\\dinput8.dll");
        real = (DirectInput8Create_t)GetProcAddress(LoadLibraryA(path), "DirectInput8Create");
    }
    HRESULT hr = real ? real(inst, ver, riid, out, outer) : E_FAIL;
    if (SUCCEEDED(hr) && out && *out && !o_CreateDevice) {
        void **vt = *(void ***)*out;
        o_CreateDevice = (CreateDevice_t)vt[3];
        DWORD old;
        VirtualProtect(&vt[3], 4, PAGE_EXECUTE_READWRITE, &old);
        vt[3] = (void *)h_CreateDevice;
        VirtualProtect(&vt[3], 4, old, &old);
    }
    return hr;
}

static void LoadConfig()
{
    char ini[MAX_PATH];
    lstrcpynA(ini, IniPath(), MAX_PATH);
    int fen = GetPrivateProfileIntA("SACoop", "Fenetre", 1, ini);
    g_cfg.windowed = fen != 0;
    g_cfg.borderless = fen == 2;
    g_cfg.winX = GetPrivateProfileIntA("SACoop", "FenetreX", 40, ini);
    g_cfg.winY = GetPrivateProfileIntA("SACoop", "FenetreY", 40, ini);
    {
        char sz[32];
        GetPrivateProfileStringA("SACoop", "TailleFenetre", "1280x720", sz, sizeof(sz), ini);
        if (sscanf(sz, "%dx%d", &g_cfg.winW, &g_cfg.winH) != 2 || g_cfg.winW < 640 || g_cfg.winH < 480) { g_cfg.winW = 1280; g_cfg.winH = 720; }
    }
    g_cfg.background = GetPrivateProfileIntA("SACoop", "ArrierePlan", 0, ini) != 0;
    g_cfg.skipIntro = GetPrivateProfileIntA("SACoop", "SansIntro", 1, ini) != 0;
    g_cfg.localUserFiles = GetPrivateProfileIntA("SACoop", "SauvegardesLocales", 1, ini) != 0;
    g_cfg.maxFps = GetPrivateProfileIntA("SACoop", "ImagesParSeconde", 60, ini);
    g_cfg.autoStart = GetPrivateProfileIntA("SACoop", "AutoDemarrer", 0, ini) != 0;
    g_cfg.netAuto = GetPrivateProfileIntA("SACoop", "Reseau", 0, ini) != 0;
    g_cfg.logScripts = GetPrivateProfileIntA("SACoop", "JournalScripts", 0, ini) != 0;
    GetPrivateProfileStringA("SACoop", "Autotest", "", g_cfg.autotest, sizeof(g_cfg.autotest), ini);
    char role[16];
    GetPrivateProfileStringA("SACoop", "Role", "hote", role, sizeof(role), ini);
    g_cfg.host = _stricmp(role, "invite") != 0;
    GetPrivateProfileStringA("SACoop", "Pseudo", "CJ", g_cfg.playerName, sizeof(g_cfg.playerName), ini);
    GetPrivateProfileStringA("SACoop", "Adresse", "127.0.0.1", g_cfg.address, sizeof(g_cfg.address), ini);
    g_cfg.port = GetPrivateProfileIntA("SACoop", "Port", 7800, ini);
    g_cfg.friendlyFire = GetPrivateProfileIntA("SACoop", "TirAmi", 1, ini) != 0;
    g_cfg.ao = GetPrivateProfileIntA("SACoop", "OcclusionAmbiante", 1, ini) != 0;
    g_cfg.fxaa = GetPrivateProfileIntA("SACoop", "Anticrenelage", 1, ini) != 0;
    g_cfg.drawDistance = GetPrivateProfileIntA("SACoop", "DistanceAffichage", 200, ini);
    if (g_cfg.drawDistance < 100) g_cfg.drawDistance = 100;
    if (g_cfg.drawDistance > 300) g_cfg.drawDistance = 300;
    g_cfg.zonePop = GetPrivateProfileIntA("SACoop", "ZonePopulation", 150, ini);
    if (g_cfg.zonePop < 100) g_cfg.zonePop = 100;
    if (g_cfg.zonePop > 200) g_cfg.zonePop = 200;
    g_cfg.popDensity = GetPrivateProfileIntA("SACoop", "DensitePopulation", 150, ini);
    if (g_cfg.popDensity < 50) g_cfg.popDensity = 50;
    if (g_cfg.popDensity > 300) g_cfg.popDensity = 300;
    g_cfg.aniso = GetPrivateProfileIntA("SACoop", "FiltrageAnisotrope", 1, ini) != 0;
    g_cfg.gameLang = GetPrivateProfileIntA("SACoop", "LangueJeu", -1, ini);
    g_cfg.invertMouseY = GetPrivateProfileIntA("SACoop", "SourisInverseeY", 0, ini) != 0;
    g_cfg.fpsView = GetPrivateProfileIntA("SACoop", "VuePremierePersonne", 1, ini) != 0;
    g_cfg.testFirstPerson = GetPrivateProfileIntA("SACoop", "TestPremierePersonne", 0, ini) != 0;
    {   // ToucheVue : F1-F12 ou une lettre / un chiffre
        char k[8];
        GetPrivateProfileStringA("SACoop", "ToucheVue", "F6", k, sizeof(k), ini);
        g_cfg.fpsKey = VK_F6;
        if ((k[0] == 'F' || k[0] == 'f') && atoi(k + 1) >= 1 && atoi(k + 1) <= 12) g_cfg.fpsKey = VK_F1 + atoi(k + 1) - 1;
        else if (k[0] && !k[1]) g_cfg.fpsKey = toupper((unsigned char)k[0]);
    }
    g_cfg.shareWanted = GetPrivateProfileIntA("SACoop", "RecherchePartagee", 1, ini) != 0;
    g_cfg.hostPolice = GetPrivateProfileIntA("SACoop", "PoliceHote", 1, ini) != 0;
    g_cfg.widescreen = GetPrivateProfileIntA("SACoop", "GrandEcran", 1, ini) != 0;
    g_cfg.testMission = GetPrivateProfileIntA("SACoop", "TestMission", 0, ini);
    g_cfg.testSkip = GetPrivateProfileIntA("SACoop", "TestPasser", 0, ini) != 0;
    g_cfg.testBoard = GetPrivateProfileIntA("SACoop", "TestTableau", 0, ini) != 0;
    g_cfg.testMenuCoop = GetPrivateProfileIntA("SACoop", "TestMenuCoop", 0, ini);
    g_cfg.testLoadSlot = GetPrivateProfileIntA("SACoop", "ChargerEmplacement", 0, ini);
    g_cfg.skin = GetPrivateProfileIntA("SACoop", "Tenue", 0, ini);
    if (g_cfg.skin < 0 || g_cfg.skin > 299) g_cfg.skin = 0;

    // Pseudo, adresse et port : dans sacoop-joueur.ini (absent du paquet, une mise a jour ne les efface pas).
    // Premier lancement : repris de sacoop.ini.
    const char *pj = PlayerIniPath();
    if (GetFileAttributesA(pj) == INVALID_FILE_ATTRIBUTES) {
        char port[16];
        wsprintfA(port, "%d", g_cfg.port);
        WritePrivateProfileStringA("SACoop", "Pseudo", g_cfg.playerName, pj);
        WritePrivateProfileStringA("SACoop", "Adresse", g_cfg.address, pj);
        WritePrivateProfileStringA("SACoop", "Port", port, pj);
        Log("reglages : %s cree (pseudo, adresse et port repris de sacoop.ini)", pj);
    } else {
        GetPrivateProfileStringA("SACoop", "Pseudo", g_cfg.playerName, g_cfg.playerName, sizeof(g_cfg.playerName), pj);
        GetPrivateProfileStringA("SACoop", "Adresse", g_cfg.address, g_cfg.address, sizeof(g_cfg.address), pj);
        g_cfg.port = GetPrivateProfileIntA("SACoop", "Port", g_cfg.port, pj);
    }

    // Ligne de commande (raccourcis Heberger / Rejoindre) : -sacoop hote | -sacoop invite <adresse>
    const char *cmd = GetCommandLineA();
    if (const char *opt = strstr(cmd, "-sacoop ")) {
        g_cfg.netAuto = true;
        char r[16] = "", addr[64] = "";
        sscanf(opt + 8, "%15s %63s", r, addr);
        if (_stricmp(r, "hote") == 0) g_cfg.host = true;
        else if (_stricmp(r, "invite") == 0) {
            g_cfg.host = false;
            if (addr[0] && addr[0] != '-') {
                lstrcpynA(g_cfg.address, addr, sizeof(g_cfg.address));
                WritePrivateProfileStringA("SACoop", "Adresse", g_cfg.address, pj);
            }
        }
    }
    // Salon du lanceur : l'hote lance la partie choisie sans passer par le menu (-sacoop-partie <emplacement 1-8|nouvelle>)
    if (const char *opt = strstr(cmd, "-sacoop-partie ")) {
        g_cfg.autoStart = true;
        g_cfg.testLoadSlot = atoi(opt + 15);   // "nouvelle" -> 0 : nouvelle partie
        if (g_cfg.testLoadSlot < 0 || g_cfg.testLoadSlot > 8) g_cfg.testLoadSlot = 0;
    }
}

// Verifie qu'on tourne bien sur le 1.0 US : l'octet de tete de CRunningScript::ProcessOneCommand
// (inc word [CTheScripts::CommandsExecuted]) n'existe qu'a cette adresse dans cette version.
static bool IsVersion10US()
{
    static const unsigned char sig[] = { 0x66, 0xFF, 0x05, 0xF4, 0x47, 0xA4, 0x00 };
    return memcmp((void *)0x469FB0, sig, sizeof(sig)) == 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID lp)
{
    if (reason == DLL_PROCESS_DETACH) { NetSendBye(); return TRUE; }   // fermeture du jeu : les autres le savent tout de suite
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    // Le chargeur du jeu charge dinput8.dll, le libere puis le recharge : sans epingle, nos crochets pointaient dans
    // une DLL dechargee et le jeu s'arretait des le demarrage.
    HMODULE self;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)&DllMain, &self);

    char logPath[MAX_PATH];
    wsprintfA(logPath, "%ssacoop.log", GameDir());
    LogInit(logPath);
    LoadConfig();
    Log("SACoop %s charge, dossier %s, processus %lu", SACOOP_VERSION, GameDir(), GetCurrentProcessId());
    InstallCrashLog();

    if (!IsVersion10US()) {
        Log("gta_sa.exe n'est pas la version 1.0 US : SACoop desactive");
        MessageBoxA(NULL, "SACoop a besoin de gta_sa.exe en version 1.0 US.\nLe mod est desactive.", "SACoop", MB_ICONWARNING);
        return TRUE;
    }
    StartWatchdog();
    InstallFileHooks();
    InstallWindowHooks();
    InstallGamePatches();
    InstallPuppetRender();
    InstallScripts();
    InstallWidescreen();
    InstallResolution();
    InstallMenu();
    InstallCamera();
    InstallMods();
    InstallRender();
    InstallGfx();
    InstallFps();
    InstallPrefs();
    return TRUE;
}
