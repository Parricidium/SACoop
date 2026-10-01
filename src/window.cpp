// Mode fenetre force et comportement en arriere-plan : deux instances doivent pouvoir tourner cote a cote
// sans voler la souris ni le premier plan. San Andreas cree son peripherique en Direct3D 9 (d3d9.dll importee).
#include "util.h"
#include "sacoop.h"
#include "chat.h"
#include "panel.h"
#include "menu.h"
#include "fps.h"
#include "widescreen.h"
#include "passenger.h"
#include "camera.h"
#include "render.h"
#include "net.h"
#include <d3d9.h>
#include <mmsystem.h>

enum { VT_D3D_CREATEDEVICE = 16, VT_DEV_RESET = 16, VT_DEV_PRESENT = 17 };

typedef IDirect3D9 *(WINAPI *Direct3DCreate9_t)(UINT);
typedef HRESULT(WINAPI *CreateDevice_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT(WINAPI *Reset_t)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
typedef HRESULT(WINAPI *Present_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);

static Direct3DCreate9_t o_Direct3DCreate9;
static CreateDevice_t o_CreateDevice;
static Reset_t o_Reset;
static Present_t o_Present;

static HWND g_hwnd;
static void SubclassGameWindow();
static HWND g_prevForeground;   // fenetre qui avait le premier plan au lancement (instances de test)

HWND GameWindow() { return g_hwnd; }

bool GameHasFocus()
{
    return g_hwnd && GetForegroundWindow() == g_hwnd;
}

static void MakeWindowed(D3DPRESENT_PARAMETERS *pp)
{
    if (!g_cfg.windowed) return;
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    if (pp->BackBufferFormat != D3DFMT_X8R8G8B8 && pp->BackBufferFormat != D3DFMT_A8R8G8B8)
        pp->BackBufferFormat = D3DFMT_X8R8G8B8;
}

static BOOL(WINAPI *o_SetWindowPos)(HWND, HWND, int, int, int, int, UINT) = SetWindowPos;

static void FitWindow(HWND hwnd, int w, int h)
{
    if (!g_cfg.windowed || !hwnd) return;
    if (g_cfg.borderless) {
        // Sans bordure : la fenetre couvre l'ecran ; le jeu rend a sa resolution (celle de l'ecran, widescreen.cpp).
        // Instances de test (FenetreX hors ecran) : meme taille, mais posee hors de l'ecran.
        SetWindowLongA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongA(hwnd, GWL_EXSTYLE, 0);
        bool test = g_cfg.winX < -1000;
        o_SetWindowPos(hwnd, test ? HWND_NOTOPMOST : HWND_TOP, test ? g_cfg.winX : 0, test ? g_cfg.winY : 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                       SWP_FRAMECHANGED | (test ? SWP_NOACTIVATE : 0));
        return;
    }
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    SetWindowLongA(hwnd, GWL_STYLE, style);
    SetWindowLongA(hwnd, GWL_EXSTYLE, 0);
    RECT r = { 0, 0, w, h };
    AdjustWindowRect(&r, style, FALSE);
    o_SetWindowPos(hwnd, HWND_NOTOPMOST, g_cfg.winX, g_cfg.winY, r.right - r.left, r.bottom - r.top,
                   SWP_FRAMECHANGED | SWP_NOACTIVATE);
    char title[128];
    wsprintfA(title, "GTA San Andreas - SACoop (%s)", g_cfg.playerName);
    SetWindowTextA(hwnd, title);
}

// Limiteur d'images : sans lui le jeu tourne tres vite en fenetre. Chaque image part a intervalle regulier :
// minuterie Windows a 1 ms, Sleep tant qu'il reste plus de 3 ms, puis attente active.
static double g_presentMs;   // temps passe dans le vrai Present (statistique)
static void LimitFrameRate()
{
    if (g_cfg.maxFps <= 0) return;
    FpsFrame();   // (plafond du moment, limiteur du jeu neutralise : fps.cpp)
    // Seulement au menu (7) et en partie (9) : les ecrans de chargement comptent leurs images, ils seraient lents.
    int st = *(int *)sa::gGameState;
    if (st != 7 && st != 9) return;
    static LARGE_INTEGER freq, next;
    if (!freq.QuadPart) {
        QueryPerformanceFrequency(&freq);
        timeBeginPeriod(1);
    }
    LONGLONG step = freq.QuadPart / FpsCurrentCap();
    LARGE_INTEGER now, entry;
    QueryPerformanceCounter(&now);
    entry = now;
    if (next.QuadPart == 0 || now.QuadPart - next.QuadPart > step * 4) next = now;   // gros retard : on repart
    for (;;) {
        LONGLONG left = next.QuadPart - now.QuadPart;
        if (left <= 0) break;
        if (left * 1000 > freq.QuadPart * 3) Sleep(1);   // Sleep(1) peut deborder de ~2 ms
        else YieldProcessor();
        QueryPerformanceCounter(&now);
    }
    next.QuadPart += step;
    static double waitMs;
    waitMs += (now.QuadPart - entry.QuadPart) * 1000.0 / freq.QuadPart;

    // Statistique de regularite (journal toutes les 10 s) : ecart min / max entre deux images.
    static LARGE_INTEGER last, since;
    static double minMs = 1e9, maxMs = 0;
    static int count;
    if (last.QuadPart) {
        double ms = (now.QuadPart - last.QuadPart) * 1000.0 / freq.QuadPart;
        if (ms < minMs) minMs = ms;
        if (ms > maxMs) maxMs = ms;
        count++;
    } else since = now;
    last = now;
    if ((now.QuadPart - since.QuadPart) > freq.QuadPart * 10) {
        MEMORYSTATUSEX ms = { sizeof(ms) };
        GlobalMemoryStatusEx(&ms);
        unsigned usedMb = (unsigned)((ms.ullTotalVirtual - ms.ullAvailVirtual) >> 20), totalMb = (unsigned)(ms.ullTotalVirtual >> 20);
        Log("images : %.1f/s, ecart %.1f a %.1f ms, attente du limiteur %.1f ms, Present %.1f ms ; memoire %u Mo sur %u", count * (double)freq.QuadPart / (now.QuadPart - since.QuadPart), minMs, maxMs,
            count ? waitMs / count : 0.0, count ? g_presentMs / count : 0.0, usedMb, totalMb);
        since = now; count = 0; minMs = 1e9; maxMs = 0; waitMs = 0; g_presentMs = 0;
    }
}

// Windows donne le premier plan a la fenetre d'un processus qu'on vient de lancer, malgre SW_SHOWNOACTIVATE.
// Une instance d'arriere-plan le rend aussitot a la fenetre qui l'avait (pendant ses premieres secondes).
static void GiveBackForeground()
{
    static DWORD start;
    if (!g_cfg.background || !g_prevForeground) return;
    if (!start) start = GetTickCount();
    if (GetTickCount() - start > 15000) { g_prevForeground = NULL; return; }
    if (GetForegroundWindow() == g_hwnd && IsWindow(g_prevForeground)) {
        SetForegroundWindow(g_prevForeground);
        Log("premier plan rendu a la fenetre precedente");
    }
}

static HRESULT WINAPI h_Present(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty)
{
    WatchdogFrame();
    GiveBackForeground();
    static bool inFrame;
    if (!inFrame) { inFrame = true; OnFrame(); inFrame = false; }
    WidescreenFrame();
    MenuFrame();
    // Menus resserres (widescreen.cpp) : bandes laterales effacees (sinon des restes d'anciennes images y restent).
    float barW = MenuBarWidth();
    if (barW > 0.5f) {
        int w = *(int *)0xC17044, h = *(int *)0xC17048;
        D3DRECT bars[2] = { { 0, 0, (LONG)barW, h }, { w - (LONG)barW, 0, w, h } };
        dev->Clear(2, bars, D3DCLEAR_TARGET, 0, 1.0f, 0);
    }
    LimitFrameRate();
    LARGE_INTEGER p0, p1, pf;
    QueryPerformanceCounter(&p0);
    HRESULT hr = o_Present(dev, src, dst, wnd, dirty);
    QueryPerformanceCounter(&p1);
    QueryPerformanceFrequency(&pf);
    g_presentMs += (p1.QuadPart - p0.QuadPart) * 1000.0 / pf.QuadPart;
    return hr;
}

static HRESULT WINAPI h_Reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
    MakeWindowed(pp);
    // Fenetre : le jeu demande la taille EXTERIEURE de la fenetre (+6 x +40 avec la bordure) mais dessine a la taille
    // de son mode (RsGlobal 0xC17044 / 0xC17048) : une bande noire restait en bas et a droite. Plein ecran fenetre :
    // le jeu demandait 640x480 (taille de la fenetre a sa creation) pour un mode a la taille de l'ecran : l'image ne
    // couvrait qu'un coin de l'ecran (retour de JD et GG, 01/10). Dans les deux cas, l'image prend la taille du mode.
    if (g_cfg.windowed) {
        int w = *(int *)0xC17044, h = *(int *)0xC17048;
        if (w >= 320 && h >= 240) { pp->BackBufferWidth = w; pp->BackBufferHeight = h; }
    }
    RenderBeforeReset();
    HRESULT hr = o_Reset(dev, pp);
    if (SUCCEEDED(hr)) RenderAfterReset(dev, pp);
    Log("Reset %ux%u fenetre=%d -> 0x%08lX", pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed, hr);
    FitWindow(g_hwnd, pp->BackBufferWidth, pp->BackBufferHeight);
    return hr;
}

static HRESULT WINAPI h_CreateDevice(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                     D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    MakeWindowed(pp);
    g_hwnd = pp->hDeviceWindow ? pp->hDeviceWindow : focus;
    SubclassGameWindow();
    HRESULT hr = o_CreateDevice(d3d, adapter, type, focus, flags, pp, out);
    Log("CreateDevice %ux%u fmt=%u fenetre=%d -> 0x%08lX", pp->BackBufferWidth, pp->BackBufferHeight,
        pp->BackBufferFormat, pp->Windowed, hr);
    if (SUCCEEDED(hr) && *out) {
        void **vt = *(void ***)*out;
        if (!o_Reset) {
            o_Reset = (Reset_t)PatchPointer(&vt[VT_DEV_RESET], (void *)h_Reset);
            o_Present = (Present_t)PatchPointer(&vt[VT_DEV_PRESENT], (void *)h_Present);
        }
        FitWindow(g_hwnd, pp->BackBufferWidth, pp->BackBufferHeight);
        RenderDeviceCreated(*out, pp);
    }
    return hr;
}

static IDirect3D9 *WINAPI h_Direct3DCreate9(UINT sdk)
{
    IDirect3D9 *d3d = o_Direct3DCreate9(sdk);
    if (d3d) {
        void **vt = *(void ***)d3d;
        if (vt[VT_D3D_CREATEDEVICE] != (void *)h_CreateDevice)
            o_CreateDevice = (CreateDevice_t)PatchPointer(&vt[VT_D3D_CREATEDEVICE], (void *)h_CreateDevice);
    }
    Log("Direct3DCreate9(%u) -> %p", sdk, d3d);
    return d3d;
}

// Le jeu recentre le curseur en permanence : on ne le laisse faire que s'il a le premier plan.
static BOOL(WINAPI *o_SetCursorPos)(int, int);
static BOOL WINAPI h_SetCursorPos(int x, int y)
{
    if (!GameHasFocus()) return TRUE;
    return o_SetCursorPos(x, y);
}

// --- Perte d'activation : le jeu met alors son son en pause (et la cinematique d'intro attend pour toujours une piste
// audio qui ne se charge plus : instance figee dans 0x4DC0E0 le 30/09). En coop le jeu ne s'arrete jamais quand on
// change de fenetre : les messages de desactivation ne lui sont pas transmis.
static WNDPROC o_WndProc;
static LRESULT CALLBACK h_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) && MenuWindowMessage(msg, wp)) return 0;   // saisie du menu COOP
    if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) && PanelWindowMessage(msg, wp)) return 0;  // panneau F10
    if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) && ChatWindowMessage(msg, wp)) return 0;   // saisie du tchat
    if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) && CameraWindowMessage(msg, wp)) return 0; // vue a la premiere personne
    if (msg == WM_KEYDOWN) PassengerWindowMessage(msg, wp);                                    // G : passager
    if ((msg == WM_ACTIVATEAPP && !wp) || (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) || msg == WM_KILLFOCUS) {
        static int logged;
        if (logged++ < 5) Log("fenetre : desactivation ignoree (message 0x%X)", msg);
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
    // Reprise du focus : le jeu demande le menu Pause (0x53BC60 : m_bActivateMenuNextFrame, 0xBA677B = 1). En coop,
    // revenir sur la fenetre ne doit rien mettre en pause.
    if (msg == WM_SETFOCUS && NetRunning()) {
        uint8_t before = *(uint8_t *)0xBA677B;
        LRESULT r = CallWindowProcA(o_WndProc, hwnd, msg, wp, lp);
        *(uint8_t *)0xBA677B = before;
        return r;
    }
    return CallWindowProcA(o_WndProc, hwnd, msg, wp, lp);
}

// La fenetre du jeu existe deja quand dinput8.dll est charge : on la sous-classe a la creation du peripherique.
static void SubclassGameWindow()
{
    if (o_WndProc || !g_hwnd) return;
    o_WndProc = (WNDPROC)SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)h_WndProc);
}

// --- Fenetre en arriere-plan (instances de test) : ouverte a la position voulue, jamais activee ---
static HWND(WINAPI *o_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static HWND WINAPI h_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
                                     HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    if (g_cfg.windowed) { x = g_cfg.winX; y = g_cfg.winY; }
    HWND hwnd = o_CreateWindowExA(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    if (!parent && !g_hwnd && hwnd) { g_hwnd = hwnd; SubclassGameWindow(); }
    return hwnd;
}

static BOOL WINAPI h_SetWindowPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags)
{
    if (g_cfg.windowed && hwnd == g_hwnd) {
        if (g_cfg.borderless) { bool test = g_cfg.winX < -1000; x = test ? g_cfg.winX : 0; y = test ? g_cfg.winY : 0; w = GetSystemMetrics(SM_CXSCREEN); h = GetSystemMetrics(SM_CYSCREEN); flags &= ~(SWP_NOMOVE | SWP_NOSIZE); }
        else { x = g_cfg.winX; y = g_cfg.winY; flags &= ~SWP_NOMOVE; }
    }
    if (g_cfg.background) flags |= SWP_NOACTIVATE;
    return o_SetWindowPos(hwnd, after, x, y, w, h, flags);
}

static BOOL(WINAPI *o_ShowWindow)(HWND, int);
static BOOL WINAPI h_ShowWindow(HWND hwnd, int cmd)
{
    if (g_cfg.background && (cmd == SW_SHOW || cmd == SW_SHOWNORMAL || cmd == SW_SHOWDEFAULT)) cmd = SW_SHOWNOACTIVATE;
    return o_ShowWindow(hwnd, cmd);
}

static HWND(WINAPI *o_SetFocus)(HWND);
static HWND WINAPI h_SetFocus(HWND hwnd)
{
    if (g_cfg.background && !GameHasFocus()) return NULL;
    return o_SetFocus(hwnd);
}

// Sans le premier plan, le jeu ne doit ni capturer ni enfermer la souris.
static BOOL(WINAPI *o_ClipCursor)(const RECT *);
static BOOL WINAPI h_ClipCursor(const RECT *r)
{
    if (r && !GameHasFocus()) return TRUE;
    return o_ClipCursor(r);
}

static HWND(WINAPI *o_SetCapture)(HWND);
static HWND WINAPI h_SetCapture(HWND hwnd)
{
    if (!GameHasFocus()) return NULL;
    return o_SetCapture(hwnd);
}

// Une instance d'arriere-plan ne prend jamais le premier plan (ni ne le donne a une autre fenetre du jeu).
static BOOL(WINAPI *o_SetForegroundWindow)(HWND);
static BOOL WINAPI h_SetForegroundWindow(HWND hwnd)
{
    if (g_cfg.background) return TRUE;
    return o_SetForegroundWindow(hwnd);
}

void InstallWindowHooks()
{
    SetProcessDPIAware();
    g_prevForeground = GetForegroundWindow();
    o_CreateWindowExA = (decltype(o_CreateWindowExA))HookImport("user32.dll", "CreateWindowExA", (void *)h_CreateWindowExA);
    o_SetWindowPos = (decltype(o_SetWindowPos))HookImport("user32.dll", "SetWindowPos", (void *)h_SetWindowPos);
    o_ShowWindow = (decltype(o_ShowWindow))HookImport("user32.dll", "ShowWindow", (void *)h_ShowWindow);
    o_SetFocus = (decltype(o_SetFocus))HookImport("user32.dll", "SetFocus", (void *)h_SetFocus);
    o_ClipCursor = (decltype(o_ClipCursor))HookImport("user32.dll", "ClipCursor", (void *)h_ClipCursor);
    o_SetCapture = (decltype(o_SetCapture))HookImport("user32.dll", "SetCapture", (void *)h_SetCapture);
    o_SetCursorPos = (BOOL(WINAPI *)(int, int))HookImport("user32.dll", "SetCursorPos", (void *)h_SetCursorPos);
    o_SetForegroundWindow = (BOOL(WINAPI *)(HWND))HookImport("user32.dll", "SetForegroundWindow", (void *)h_SetForegroundWindow);
    // Le chargeur du 1.0 US resout d3d9 lui-meme (pas de table d'importation utilisable au moment de DllMain) : on
    // detourne directement Direct3DCreate9 dans d3d9.dll. Sans ca, la fenetre n'est pas forcee et le jeu part en plein
    // ecran (vu le 30/09 sur l'ecran de JD pendant un test).
    HMODULE d3d9 = LoadLibraryA("d3d9.dll");
    uint8_t *f = d3d9 ? (uint8_t *)GetProcAddress(d3d9, "Direct3DCreate9") : nullptr;
    static const uint8_t hot[] = { 0x8B, 0xFF, 0x55, 0x8B, 0xEC };   // mov edi,edi / push ebp / mov ebp,esp
    if (f) o_Direct3DCreate9 = (Direct3DCreate9_t)MakeDetour((uintptr_t)f, hot, sizeof(hot), (void *)h_Direct3DCreate9);
    if (!o_Direct3DCreate9) {
        Log("Direct3DCreate9 : crochet impossible (%p : %02X %02X %02X %02X %02X)", f, f ? f[0] : 0, f ? f[1] : 0, f ? f[2] : 0, f ? f[3] : 0, f ? f[4] : 0);
        // Une instance de test ne doit jamais ouvrir un plein ecran sur l'ecran de JD.
        if (g_cfg.background) TerminateProcess(GetCurrentProcess(), 4);
    }
}
