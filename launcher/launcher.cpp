// SACoop - lanceur (SACoop.exe, a poser dans le dossier du jeu, livre dans le paquet). Meme principe que celui de VCCoop.
//
//  - Fenetre sans cadre, forme et transparence prises du PNG SACoop\interface\launcher.png (fenetre "layered", alpha
//    par pixel) ; textes, champs et boutons dessines par-dessus avec GDI+.
//  - Cible gta_sa.exe : celui du dossier, ou celui choisi (retenu dans sacoop-launcher.ini). Seul l'exe 1.0 US est
//    accepte (meme signature que dllmain.cpp IsVersion10US) ; Steam 3.0 et autres : message, sans lien.
//  - A chaque lancement : derniere version publiee sur GitHub (Parricidium/SACoop, pre-versions comprises). Plus
//    recente que SACoop\version.txt (ou mod absent) : telechargement du zip, extraction (tar.exe de Windows), copie
//    dans le dossier du jeu. sacoop.ini de l'utilisateur garde ses valeurs (seules les cles nouvelles sont ajoutees) ;
//    sacoop-joueur.ini n'est pas dans le paquet. Le lanceur se remplace lui-meme (renomme en .old) puis se relance.
//  - Heberger ouvre un salon (TCP, port du jeu) ; Rejoindre y entre (sinon, si l'hote joue deja, lance le jeu
//    directement en invite). Quand tout le monde est pret, l'hote lance : chaque lanceur demarre son jeu
//    (-sacoop hote -sacoop-partie <emplacement|nouvelle> | -sacoop invite <adresse>), puis reste affiche en ecran
//    d'attente jusqu'a ce que la fenetre du jeu apparaisse.
//
// Options de ligne de commande (tests) : /capture <png> <menu|attente|sansexe|maj|options|notes> ; /check <exe> (code
// de sortie : 0 = 1.0 US, 1 = absent, 2 = autre) ; /maj <exe> <journal> ; /testlancer <1|2> <journal> ;
// /testsalon <hote|invite> <journal>.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <algorithm>
using std::min;
using std::max;
#include <objidl.h>
#include <gdiplus.h>
#include <winhttp.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <atomic>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

using namespace Gdiplus;

static const wchar_t *kReleasesApi = L"https://api.github.com/repos/Parricidium/SACoop/releases?per_page=40";
static const wchar_t *kStoreUrl = L"https://store.rockstargames.com/fr/game/buy-grand-theft-auto-the-trilogy";   // Trilogy (meme lien que le README et VCCoop)
static const float kImgW = 1000, kImgH = 620;   // mise en page (coordonnees de launcher.png)

// ---------------------------------------------------------------- etat
enum { ST_IDLE, ST_LAUNCH, ST_CLOSING };
enum { K_NORMAL, K_OK, K_WARN, K_ERR };
enum ExeKind { EXE_OK, EXE_MISSING, EXE_OTHER };

static HWND g_wnd;
static bool g_fr;
static std::wstring g_dir, g_self, g_iniLauncher;   // dossier du lanceur (avec \), chemin du lanceur
static std::wstring g_exe, g_gameDir;               // gta_sa.exe choisi, son dossier (avec \)
static ExeKind g_exeKind = EXE_MISSING;
static std::wstring g_localVer;
static int g_state = ST_IDLE;
static float g_scale = 1, g_alpha = 0, g_time = 0;
static Bitmap *g_bg, *g_bgDark;
static int g_winW, g_winH;
static HDC g_memDC;
static HBITMAP g_dib;
static void *g_bits;

static CRITICAL_SECTION g_cs;
static std::wstring g_status;
static int g_statusKind = K_NORMAL;
static std::atomic<float> g_progress(-1.0f);   // -1 = pas de barre, -2 = indeterminee, 0..1
static std::atomic<bool> g_busy(false);

static HANDLE g_proc;
static DWORD g_pid, g_launchT, g_winSeenT;
static std::wstring g_launchInfo;

#define WM_APP_RELAUNCH (WM_APP + 1)
#define WM_APP_GO (WM_APP + 2)          // invite : l'hote a lance la partie
#define WM_APP_LOBBYEND (WM_APP + 3)    // invite : salon ferme ou refuse (wParam : 1 = refuse)

// Salon : etat partage entre la fenetre et les fils reseau (sous g_lcs)
enum { LB_NONE, LB_HOST, LB_CONNECTING, LB_GUEST };
struct LobbyPeer { int id; std::string name, skin; bool ready; int ping; };
static std::atomic<int> g_lobby(LB_NONE);
static CRITICAL_SECTION g_lcs;
static std::vector<LobbyPeer> g_peers;
static int g_myId, g_lobbyChoice;                  // choix de l'hote : 0 = nouvelle partie, sinon index dans g_saves + 1
static std::string g_lobbyChoiceLabel;              // invite : sauvegarde choisie par l'hote ("" = nouvelle partie)
static std::atomic<bool> g_meReady(false);          // invite : pret
static bool g_goWait;                               // invite : GO recu, lancement dans un instant
static bool g_joinFallback;                          // invite : pas de salon chez l'hote -> rejoindre directement en jeu

// Sons du salon : arrivee, depart, pret, plus pret. Petites notes synthetisees (WAV en memoire, PlaySound).
enum { SND_JOIN, SND_LEAVE, SND_READY, SND_UNREADY, SND_COUNT };
static std::vector<uint8_t> g_snd[SND_COUNT];
static void MakeSound(std::vector<uint8_t> &w, std::initializer_list<float> notes, float noteMs, float vol)
{
    const int rate = 22050, per = (int)(rate * noteMs / 1000.0f), tail = rate / 5;
    int total = per * (int)notes.size() + tail;
    std::vector<float> buf(total, 0.0f);
    int k = 0;
    for (float f : notes) {
        int start = k++ * per;
        for (int i = 0; i < per + tail && start + i < total; i++) {
            float t = i / (float)rate;
            float env = (i < rate / 200 ? i / (rate / 200.0f) : 1.0f) * expf(-t * 9.0f);
            buf[start + i] += env * (sinf(6.2831853f * f * t) + 0.25f * sinf(6.2831853f * f * 2 * t));
        }
    }
    uint32_t data = total * 2;
    w.resize(44 + data);
    uint8_t *p = w.data();
    auto u32 = [&](int at, uint32_t v) { memcpy(p + at, &v, 4); };
    auto u16 = [&](int at, uint16_t v) { memcpy(p + at, &v, 2); };
    memcpy(p, "RIFF", 4); u32(4, 36 + data); memcpy(p + 8, "WAVEfmt ", 8); u32(16, 16); u16(20, 1); u16(22, 1);
    u32(24, rate); u32(28, rate * 2); u16(32, 2); u16(34, 16); memcpy(p + 36, "data", 4); u32(40, data);
    for (int i = 0; i < total; i++) {
        float v = max(-1.0f, min(1.0f, buf[i] * vol));
        int16_t sv = (int16_t)(v * 32767);
        memcpy(p + 44 + i * 2, &sv, 2);
    }
}
static void LobbySound(int which)
{
    if (g_snd[0].empty()) {
        MakeSound(g_snd[SND_JOIN], { 523.3f, 659.3f, 784.0f }, 90, 0.30f);     // do mi sol : quelqu'un arrive
        MakeSound(g_snd[SND_LEAVE], { 784.0f, 659.3f, 523.3f }, 90, 0.26f);    // sol mi do : il part
        MakeSound(g_snd[SND_READY], { 987.8f, 1318.5f }, 70, 0.24f);           // si mi aigus : pret
        MakeSound(g_snd[SND_UNREADY], { 659.3f, 493.9f }, 80, 0.22f);          // mi si graves : plus pret
    }
    PlaySoundW((LPCWSTR)g_snd[which].data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

static std::wstring g_testLog;          // /testfenetre, /testlancer : fenetre hors ecran, sans activation, journal
static int g_ulwOk = -1, g_frames;
static int g_testLaunch = -1;

static const wchar_t *T(const wchar_t *fr, const wchar_t *en) { return g_fr ? fr : en; }

static void SetStatus(int kind, const wchar_t *fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_cs);
    g_status = buf;
    g_statusKind = kind;
    LeaveCriticalSection(&g_cs);
}

// ---------------------------------------------------------------- utilitaires
static std::wstring Widen(const std::string &s, UINT cp = CP_UTF8)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string Narrow(const std::wstring &w, UINT cp = CP_ACP)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
static bool FileExists(const std::wstring &p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static std::wstring DirOf(const std::wstring &p) { size_t k = p.find_last_of(L"\\/"); return k == std::wstring::npos ? L"" : p.substr(0, k + 1); }
static bool ReadAll(const std::wstring &p, std::vector<unsigned char> &out)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(f, NULL), got = 0;
    out.resize(size);
    bool ok = size == INVALID_FILE_SIZE ? false : (size == 0 || (ReadFile(f, out.data(), size, &got, NULL) && got == size));
    CloseHandle(f);
    return ok;
}
static std::wstring Trim(std::wstring s)
{
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    size_t k = 0;
    while (k < s.size() && (s[k] == L' ' || s[k] == L'\t' || s[k] == 0xFEFF)) k++;
    return s.substr(k);
}

// ---------------------------------------------------------------- gta_sa.exe 1.0 US
// Octets de tete de CRunningScript::ProcessOneCommand (inc word [CTheScripts::CommandsExecuted]) a 0x469FB0 : n'existent
// qu'a cette adresse dans le 1.0 US (meme test que le mod, dllmain.cpp).
static ExeKind CheckExe(const std::wstring &path)
{
    std::vector<unsigned char> d;
    if (path.empty() || !ReadAll(path, d) || d.size() < 0x400) return EXE_MISSING;
    static const unsigned char sig[] = { 0x66, 0xFF, 0x05, 0xF4, 0x47, 0xA4, 0x00 };
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)d.data();
    if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 && (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS32) < d.size()) {
        const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(d.data() + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 && nt->OptionalHeader.ImageBase == 0x400000) {
            const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
            DWORD rva = 0x469FB0 - 0x400000;
            for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                if ((const unsigned char *)(sec + i + 1) > d.data() + d.size()) break;
                DWORD va = sec[i].VirtualAddress, sz = max(sec[i].Misc.VirtualSize, sec[i].SizeOfRawData);
                if (rva >= va && rva < va + sz) {
                    size_t off = sec[i].PointerToRawData + (rva - va);
                    if (off + sizeof(sig) <= d.size() && rva - va + sizeof(sig) <= sec[i].SizeOfRawData && !memcmp(d.data() + off, sig, sizeof(sig)))
                        return EXE_OK;
                }
            }
        }
    }
    return EXE_OTHER;
}

static void BadExeMessage()
{
    MessageBoxW(g_wnd, T(L"Ce gta_sa.exe n'est pas la version 1.0 US.\n\n"
                         L"SACoop ne fonctionne qu'avec le gta_sa.exe de la version 1.0 US. La version Steam actuelle (3.0) "
                         L"et les autres ne sont pas compatibles : il faut d'abord r\u00E9trograder le jeu en 1.0 US.\n\n"
                         L"Ensuite, choisis le bon gta_sa.exe dans le lanceur.",
                         L"This gta_sa.exe is not version 1.0 US.\n\n"
                         L"SACoop only works with the version 1.0 US gta_sa.exe. The current Steam version (3.0) and the "
                         L"others are not compatible: the game must be downgraded to 1.0 US first.\n\n"
                         L"Then pick the right gta_sa.exe in the launcher."),
                L"SACoop", MB_ICONWARNING | MB_OK);
}

// ---------------------------------------------------------------- reglages
struct Field { std::wstring text; RectF r; size_t maxLen; bool address; };
static Field g_fields[2];   // 0 = pseudo, 1 = adresse
static int g_focus = -1;

static std::wstring PlayerIni() { return g_gameDir + L"sacoop-joueur.ini"; }

static void LoadPlayer()
{
    char v[128];
    std::string pj = Narrow(PlayerIni()), main = Narrow(g_gameDir + L"sacoop.ini");
    GetPrivateProfileStringA("SACoop", "Pseudo", "CJ", v, sizeof(v), main.c_str());
    GetPrivateProfileStringA("SACoop", "Pseudo", v, v, sizeof(v), pj.c_str());
    g_fields[0].text = Widen(v, CP_ACP);
    GetPrivateProfileStringA("SACoop", "Adresse", "127.0.0.1", v, sizeof(v), main.c_str());
    GetPrivateProfileStringA("SACoop", "Adresse", v, v, sizeof(v), pj.c_str());
    g_fields[1].text = Widen(v, CP_ACP);
}

static void SavePlayer()
{
    if (g_gameDir.empty()) return;
    std::string pj = Narrow(PlayerIni());
    std::wstring name = Trim(g_fields[0].text);
    if (name.empty()) name = L"CJ";
    WritePrivateProfileStringA("SACoop", "Pseudo", Narrow(name).c_str(), pj.c_str());
    WritePrivateProfileStringA("SACoop", "Adresse", Narrow(Trim(g_fields[1].text)).c_str(), pj.c_str());
}

static void LoadLocalVersion()
{
    std::vector<unsigned char> d;
    g_localVer.clear();
    if (!g_gameDir.empty() && ReadAll(g_gameDir + L"SACoop\\version.txt", d))
        g_localVer = Trim(Widen(std::string(d.begin(), d.end())));
    if (!g_gameDir.empty() && !FileExists(g_gameDir + L"dinput8.dll")) g_localVer.clear();   // mod pas installe ici
}

static void SetExe(const std::wstring &path)
{
    g_exe = path;
    g_exeKind = CheckExe(path);
    g_gameDir = g_exeKind == EXE_OK ? DirOf(path) : L"";
    LoadLocalVersion();
    if (!g_gameDir.empty()) LoadPlayer();
}

// "0.1.0-prealpha" : nombres compares un a un, puis le suffixe (a egalite, sans suffixe = plus recent).
static int CmpVer(std::wstring a, std::wstring b)
{
    if (!a.empty() && (a[0] == L'v' || a[0] == L'V')) a.erase(0, 1);
    if (!b.empty() && (b[0] == L'v' || b[0] == L'V')) b.erase(0, 1);
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        bool da = i < a.size() && iswdigit(a[i]), db = j < b.size() && iswdigit(b[j]);
        if (!da || !db) break;
        long na = wcstol(a.c_str() + i, NULL, 10), nb = wcstol(b.c_str() + j, NULL, 10);
        if (na != nb) return na < nb ? -1 : 1;
        while (i < a.size() && iswdigit(a[i])) i++;
        while (j < b.size() && iswdigit(b[j])) j++;
        if (i < a.size() && a[i] == L'.' && j < b.size() && b[j] == L'.') { i++; j++; continue; }
        break;
    }
    std::wstring sa = a.substr(min(i, a.size())), sb = b.substr(min(j, b.size()));
    if (sa == sb) return 0;
    if (sa.empty() != sb.empty()) return sa.empty() ? 1 : -1;
    return sa < sb ? -1 : 1;
}

// ---------------------------------------------------------------- HTTP (WinHTTP)
// GET sur une URL https ; corps dans out (ou dans le fichier toFile), progression 0..1 si progress.
static bool HttpGet(const std::wstring &url, std::string *out, const std::wstring &toFile, bool progress)
{
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = _countof(path);
    wchar_t extra[1024] = {};
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = _countof(extra);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    std::wstring full = std::wstring(path) + extra;

    HINTERNET s = WinHttpOpen(L"SACoop-Launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!s) s = WinHttpOpen(L"SACoop-Launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 8000, 8000, 15000, 30000);
    bool ok = false;
    HANDLE f = INVALID_HANDLE_VALUE;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", full.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    // en-tete de l'API seulement pour l'API (le zip : requete nue, comme un navigateur)
    const wchar_t *hdr = toFile.empty() ? L"Accept: application/vnd.github+json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
    if (r && WinHttpSendRequest(r, hdr, toFile.empty() ? (DWORD)-1 : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
          && WinHttpReceiveResponse(r, NULL)) {
        DWORD code = 0, len = sizeof(code);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        DWORD total = 0; len = sizeof(total);
        if (!WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &total, &len, WINHTTP_NO_HEADER_INDEX)) total = 0;
        if (code == 200) {
            if (!toFile.empty()) f = CreateFileW(toFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            ok = toFile.empty() || f != INVALID_HANDLE_VALUE;
            DWORD got = 0;
            std::vector<char> buf(64 * 1024);
            while (ok) {
                DWORD n = 0;
                if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &n)) { ok = false; break; }
                if (!n) break;
                if (f != INVALID_HANDLE_VALUE) { DWORD w = 0; if (!WriteFile(f, buf.data(), n, &w, NULL) || w != n) ok = false; }
                else out->append(buf.data(), n);
                got += n;
                if (progress && total) g_progress = (float)got / total;
            }
            if (ok && total && got != total) ok = false;
        }
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

// Chaine JSON a partir de son guillemet ouvrant, echappements decodes (en UTF-8).
static std::string JsonDecode(const std::string &json, size_t q)
{
    std::string v;
    for (size_t i = q + 1; i < json.size() && json[i] != '"'; i++) {
        char c = json[i];
        if (c != '\\' || i + 1 >= json.size()) { v += c; continue; }
        char e = json[++i];
        if (e == 'n') v += '\n';
        else if (e == 'r') {}
        else if (e == 't') v += ' ';
        else if (e == 'u' && i + 4 < json.size()) {
            unsigned cp = strtoul(json.substr(i + 1, 4).c_str(), NULL, 16);
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;   // emoji (paires) : laisses
            if (cp < 0x80) v += (char)cp;
            else if (cp < 0x800) { v += (char)(0xC0 | (cp >> 6)); v += (char)(0x80 | (cp & 0x3F)); }
            else { v += (char)(0xE0 | (cp >> 12)); v += (char)(0x80 | ((cp >> 6) & 0x3F)); v += (char)(0x80 | (cp & 0x3F)); }
        } else v += e;
    }
    return v;
}
// Valeur texte d'une cle, cherchee dans [from, to).
static std::string JsonField(const std::string &json, const char *key, size_t from, size_t to)
{
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k, from);
    if (p == std::string::npos || p >= to) return "";
    p = json.find(':', p + k.size());
    size_t q = p == std::string::npos ? p : json.find_first_not_of(" \t\r\n", p + 1);
    return q != std::string::npos && json[q] == '"' ? JsonDecode(json, q) : "";
}

// ---------------------------------------------------------------- installation
static bool RunHidden(const std::wstring &cmd, DWORD *exitCode)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    std::vector<wchar_t> c(cmd.begin(), cmd.end());
    c.push_back(0);
    if (!CreateProcessW(NULL, c.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 120000);
    GetExitCodeProcess(pi.hProcess, exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static void DeleteTree(const std::wstring &dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring p = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(p);
            else { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(p.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

// sacoop.ini deja la : garde les valeurs de l'utilisateur, ajoute seulement les cles apparues dans la nouvelle version.
static void MergeIni(const std::wstring &newIni, const std::wstring &userIni)
{
    std::vector<unsigned char> d;
    if (!ReadAll(newIni, d)) return;
    std::string text(d.begin(), d.end()), section, u = Narrow(userIni);
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { section = line.substr(1, line.find(']') - 1); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty()) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        char cur[8];
        GetPrivateProfileStringA(section.c_str(), key.c_str(), "\x01", cur, sizeof(cur), u.c_str());
        if (cur[0] == 1 && !cur[1]) WritePrivateProfileStringA(section.c_str(), key.c_str(), val.c_str(), u.c_str());
    }
}

// Copie l'arbre extrait dans le dossier du jeu. selfReplaced : le lanceur en cours a ete remplace.
static DWORD g_copyError;
static bool CopyTree(const std::wstring &src, const std::wstring &dst, bool *selfReplaced, std::wstring *failed)
{
    CreateDirectoryW(dst.c_str(), NULL);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool ok = true;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring s = src + L"\\" + n, d = dst + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { ok = CopyTree(s, d, selfReplaced, failed) && ok; continue; }
        if (!_wcsicmp(d.c_str(), (g_gameDir + L"sacoop.ini").c_str()) && FileExists(d)) { MergeIni(s, d); continue; }
        if (!_wcsicmp(d.c_str(), (g_gameDir + L"SACoop\\version.txt").c_str())) continue;   // en dernier (UpdateThread)
        if (!_wcsicmp(d.c_str(), g_self.c_str())) {
            std::wstring old = g_self + L".old";
            DeleteFileW(old.c_str());
            if (!MoveFileExW(g_self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) { g_copyError = GetLastError(); ok = false; *failed = n; continue; }
            *selfReplaced = true;
        }
        SetFileAttributesW(d.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!CopyFileW(s.c_str(), d.c_str(), FALSE)) { g_copyError = GetLastError(); ok = false; *failed = n; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

// Le dossier accepte-t-il l'ecriture (Program Files sans droits, par exemple) ?
static bool IsWritableDir(const std::wstring &dir)
{
    std::wstring p = dir + L"sacoop-ecriture.tmp";
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    CloseHandle(f);
    return true;
}

// ---------------------------------------------------------------- notes des versions (onglet NOUVEAUTES)
// Texte de chaque release GitHub : en francais, puis une ligne "---", puis en anglais (dist\make-release.ps1 -Notes).
// Gardees dans SACoop\notes-maj.json pour les lire hors ligne.
struct Note { std::wstring ver, date, fr, en, zip; };
static std::vector<Note> g_notes;
static volatile bool g_notesDone;
static float g_notesH;

// Markdown simple : titres, gras et code retires ; puces "- " -> "\u2022".
static std::wstring CleanNote(const std::wstring &s)
{
    std::wstring o;
    for (size_t i = 0; i < s.size(); i++) {
        bool lineStart = i == 0 || s[i - 1] == L'\n';
        if (s[i] == L'`') continue;
        if (s[i] == L'*' && i + 1 < s.size() && s[i + 1] == L'*') { i++; continue; }
        if (lineStart && s[i] == L'#') { while (i < s.size() && (s[i] == L'#' || s[i] == L' ')) i++; i--; continue; }
        if (lineStart && (s[i] == L'-' || s[i] == L'*') && i + 1 < s.size() && s[i + 1] == L' ') { o += L"\u2022"; continue; }
        o += s[i];
    }
    size_t b = o.find_first_not_of(L"\n "), e = o.find_last_not_of(L"\n ");
    return b == std::wstring::npos ? L"" : o.substr(b, e - b + 1);
}

// Liste des releases (les plus recentes d'abord), brouillons exclus.
static std::vector<Note> ParseReleases(const std::string &json)
{
    std::vector<Note> list;
    for (size_t at = 0;;) {
        size_t p = json.find("\"tag_name\"", at);
        if (p == std::string::npos) break;
        size_t next = json.find("\"tag_name\"", p + 10), end = next == std::string::npos ? json.size() : next;
        // l'objet de la release commence avant "tag_name" (url, id...) : on cherche les assets dans [p, fin)
        Note n;
        n.ver = Widen(JsonField(json, "tag_name", p, end));
        if (!n.ver.empty() && (n.ver[0] == L'v' || n.ver[0] == L'V')) n.ver.erase(0, 1);
        std::string d = JsonField(json, "published_at", p, end);
        if (d.size() >= 10) n.date = Widen(d.substr(8, 2) + "/" + d.substr(5, 2) + "/" + d.substr(0, 4));
        std::wstring body = Widen(JsonField(json, "body", p, end));
        size_t sep = body.find(L"\n---");
        if (sep == std::wstring::npos) n.fr = n.en = CleanNote(body);
        else {
            size_t enAt = body.find(L'\n', sep + 1);
            n.fr = CleanNote(body.substr(0, sep));
            n.en = CleanNote(enAt == std::wstring::npos ? L"" : body.substr(enAt + 1));
            if (n.en.empty()) n.en = n.fr;
        }
        for (size_t k = p; k < end;) {
            size_t u = json.find("\"browser_download_url\"", k);
            if (u == std::string::npos || u >= end) break;
            std::string url = JsonField(json, "browser_download_url", u, end);
            k = u + 20;
            if (url.size() > 4 && !_stricmp(url.c_str() + url.size() - 4, ".zip")) { n.zip = Widen(url); break; }
        }
        bool draft = json.find("\"draft\": true", p) < end || json.find("\"draft\":true", p) < end;
        if (!n.ver.empty() && !draft) list.push_back(n);
        at = end;
    }
    return list;
}

static std::string g_releasesJson;   // derniere reponse de l'API (partagee entre la mise a jour et les notes)

static bool FetchReleases(std::string &json)
{
    std::wstring cache = g_dir + L"SACoop\\notes-maj.json";
    if (HttpGet(kReleasesApi, &json, L"", false) && json.find("\"tag_name\"") != std::string::npos) {
        CreateDirectoryW((g_dir + L"SACoop").c_str(), NULL);
        HANDLE f = CreateFileW(cache.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, json.data(), (DWORD)json.size(), &w, NULL); CloseHandle(f); }
        return true;
    }
    json.clear();
    HANDLE f = CreateFileW(cache.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(f, NULL), r = 0;
        if (size != INVALID_FILE_SIZE && size < 8u << 20) { json.resize(size); ReadFile(f, &json[0], size, &r, NULL); json.resize(r); }
        CloseHandle(f);
    }
    return false;   // hors ligne (json : cache, pour les notes seulement)
}

// ---------------------------------------------------------------- mise a jour
static DWORD WINAPI UpdateThread(void *)
{
    SetStatus(K_NORMAL, T(L"Recherche de mises \u00E0 jour\u2026", L"Checking for updates\u2026"));
    g_progress = -2;
    std::string json;
    std::wstring local = g_localVer;
    bool online = FetchReleases(json);
    std::vector<Note> rel = json.empty() ? std::vector<Note>() : ParseReleases(json);
    EnterCriticalSection(&g_cs);
    g_notes = rel;
    LeaveCriticalSection(&g_cs);
    g_notesDone = true;
    if (!online) {
        g_progress = -1;
        if (local.empty()) SetStatus(K_ERR, T(L"Hors ligne : SACoop n'est pas install\u00E9 ici", L"Offline: SACoop is not installed here"));
        else SetStatus(K_WARN, T(L"Hors ligne \u00B7 SACoop %s", L"Offline \u00B7 SACoop %s"), local.c_str());
        g_busy = false;
        return 0;
    }
    std::wstring remote, zipUrl;
    for (const Note &n : rel) if (!n.zip.empty()) { remote = n.ver; zipUrl = n.zip; break; }   // la plus recente avec un zip
    if (remote.empty() || zipUrl.empty() || (!local.empty() && CmpVer(remote, local) <= 0)) {
        g_progress = -1;
        SetStatus(K_OK, T(L"SACoop %s \u00B7 \u00E0 jour", L"SACoop %s \u00B7 up to date"), local.empty() ? L"?" : local.c_str());
        g_busy = false;
        return 0;
    }

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring work = std::wstring(tmp) + L"SACoop-maj";
    DeleteTree(work);
    CreateDirectoryW(work.c_str(), NULL);
    std::wstring zip = work + L"\\SACoop.zip", ext = work + L"\\x";
    SetStatus(K_NORMAL, T(L"T\u00E9l\u00E9chargement de SACoop %s\u2026", L"Downloading SACoop %s\u2026"), remote.c_str());
    g_progress = 0;
    if (!HttpGet(zipUrl, NULL, zip, true)) {
        g_progress = -1;
        SetStatus(K_ERR, T(L"Mise \u00E0 jour impossible (t\u00E9l\u00E9chargement)", L"Update failed (download)"));
        DeleteTree(work);
        g_busy = false;
        return 0;
    }
    SetStatus(K_NORMAL, T(L"Installation de SACoop %s\u2026", L"Installing SACoop %s\u2026"), remote.c_str());
    g_progress = -2;
    CreateDirectoryW(ext.c_str(), NULL);
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    DWORD code = 1;
    std::wstring cmd = L"\"" + std::wstring(sys) + L"\\tar.exe\" -xf \"" + zip + L"\" -C \"" + ext + L"\"";
    if (!RunHidden(cmd, &code) || code != 0) {
        g_progress = -1;
        SetStatus(K_ERR, T(L"Mise \u00E0 jour impossible (archive)", L"Update failed (archive)"));
        DeleteTree(work);
        g_busy = false;
        return 0;
    }
    bool self = false;
    std::wstring failed;
    std::wstring gd = g_gameDir.substr(0, g_gameDir.size() - 1);
    bool ok = CopyTree(ext, gd, &self, &failed);
    // version.txt seulement si tout est en place : une installation interrompue sera reprise au prochain lancement
    CreateDirectoryW((g_gameDir + L"SACoop").c_str(), NULL);
    if (ok && !CopyFileW((ext + L"\\SACoop\\version.txt").c_str(), (g_gameDir + L"SACoop\\version.txt").c_str(), FALSE)) { ok = false; failed = L"version.txt"; }
    DeleteTree(work);
    g_progress = -1;
    if (!ok && g_copyError == ERROR_ACCESS_DENIED && !IsWritableDir(g_gameDir)) {
        SetStatus(K_ERR, T(L"Dossier du jeu prot\u00E9g\u00E9 : lance SACoop.exe en administrateur pour mettre \u00E0 jour", L"Game folder is protected: run SACoop.exe as administrator to update"));
        g_busy = false;
        return 0;
    }
    if (!ok) {
        SetStatus(K_ERR, T(L"%s est occup\u00E9 : ferme le jeu, puis relance le lanceur", L"%s is in use: close the game, then restart the launcher"), failed.c_str());
        g_busy = false;
        return 0;
    }
    LoadLocalVersion();
    SetStatus(K_OK, T(L"Mis \u00E0 jour \u00B7 SACoop %s", L"Updated \u00B7 SACoop %s"), g_localVer.empty() ? remote.c_str() : g_localVer.c_str());
    g_busy = false;
    if (self) PostMessageW(g_wnd, WM_APP_RELAUNCH, 0, 0);
    return 0;
}

static void StartUpdate()
{
    if (g_exeKind != EXE_OK || g_busy) return;
    g_busy = true;
    HANDLE t = CreateThread(NULL, 0, UpdateThread, NULL, 0, NULL);
    if (t) CloseHandle(t); else g_busy = false;
}

// Sans exe valable : les notes quand meme (pas de mise a jour possible).
static DWORD WINAPI NotesOnlyThread(void *)
{
    std::string json;
    FetchReleases(json);
    std::vector<Note> rel = json.empty() ? std::vector<Note>() : ParseReleases(json);
    EnterCriticalSection(&g_cs);
    g_notes = rel;
    LeaveCriticalSection(&g_cs);
    g_notesDone = true;
    return 0;
}

// ---------------------------------------------------------------- boutons
enum { B_HOST, B_JOIN, B_EXE, B_BUY, B_THEME, B_CLOSE, B_MIN, B_LOGS, B_COUNT };
struct Button { RectF r; float hover; bool visible, enabled; };
static Button g_btn[B_COUNT];
static int g_hot = -1, g_pressed = -1;

static void Layout()
{
    g_fields[0].r = RectF(76, 276, 304, 36); g_fields[0].maxLen = 23; g_fields[0].address = false;
    g_fields[1].r = RectF(76, 338, 304, 36); g_fields[1].maxLen = 63; g_fields[1].address = true;
    g_btn[B_HOST].r = RectF(76, 390, 148, 46);
    g_btn[B_JOIN].r = RectF(232, 390, 148, 46);
    g_btn[B_EXE].r = RectF(250, 452, 130, 20);
    g_btn[B_BUY].r = RectF(236, 554, 144, 26);
    g_btn[B_CLOSE].r = RectF(938, 76, 28, 28);
    g_btn[B_MIN].r = RectF(904, 76, 28, 28);
    g_btn[B_THEME].r = RectF(62, 100, 26, 26);   // coin du panneau, a gauche du logo
    g_btn[B_LOGS].r = RectF(368, 100, 26, 26);   // coin oppose : page des journaux (comme VCCoop)
}

static void UpdateButtons()
{
    bool menu = g_state == ST_IDLE, exeOk = g_exeKind == EXE_OK, busy = g_busy;
    for (int i = 0; i < B_COUNT; i++) g_btn[i].visible = true;
    g_btn[B_HOST].visible = g_btn[B_JOIN].visible = g_btn[B_EXE].visible = menu;
    g_btn[B_HOST].enabled = g_btn[B_JOIN].enabled = menu && exeOk && !busy && !g_localVer.empty();
    g_btn[B_EXE].enabled = menu && !busy;
    if (g_lobby != LB_NONE) {
        extern bool LobbyCanStartPublic();
        g_btn[B_EXE].enabled = false;
        g_btn[B_JOIN].enabled = menu;
        extern bool GuestModsReady();
        g_btn[B_HOST].enabled = menu && (g_lobby == LB_HOST ? LobbyCanStartPublic() : g_lobby == LB_GUEST && GuestModsReady());
    }
    if (g_goWait) g_btn[B_HOST].enabled = g_btn[B_JOIN].enabled = false;
    g_btn[B_CLOSE].enabled = g_btn[B_MIN].enabled = g_btn[B_BUY].enabled = g_btn[B_THEME].enabled = true;
    g_btn[B_LOGS].visible = menu && !g_gameDir.empty();
    g_btn[B_LOGS].enabled = true;
}

// ---------------------------------------------------------------- dessin
static void RoundRect(GraphicsPath &p, RectF r, float rad)
{
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

// Noir et blanc (demande de JD, 30/09) : theme sombre = fond noir, textes et accents blancs ; theme clair = fond blanc,
// textes et accents noirs. Seuls les etats gardent une couleur (erreur en rouge, attention en orange, PRE-ALPHA en rouge).
// Themes : bouton lune / soleil ; Theme=clair|sombre dans sacoop-launcher.ini, sinon celui de Windows.
struct Theme {
    Color ink, grey, panel, panelBorder, sep, card, cardSel, choiceBorder, toggleOff, field, fieldBorder, placeholder,
          tab, tabHot, pill, btn2, btn2Hot, circle, circleHot, fallA, fallB, accent, accent2, onAccent, pillHot;
};
static const Theme kLight = {
    Color(255, 20, 20, 20), Color(255, 110, 110, 110), Color(255, 250, 250, 250), Color(150, 255, 255, 255), Color(255, 225, 225, 225),
    Color(255, 255, 255, 255), Color(255, 236, 236, 236), Color(255, 200, 200, 200), Color(255, 210, 210, 210),
    Color(235, 255, 255, 255), Color(255, 210, 210, 210), Color(255, 175, 175, 175),
    Color(185, 255, 255, 255), Color(240, 255, 255, 255), Color(235, 20, 20, 20), Color(215, 255, 255, 255), Color(240, 238, 238, 238),
    Color(150, 255, 255, 255), Color(235, 255, 255, 255), Color(255, 250, 250, 250), Color(255, 210, 210, 210),
    Color(255, 18, 18, 18), Color(255, 70, 70, 70), Color(255, 255, 255, 255), Color(255, 70, 70, 70) };
static const Theme kDark = {
    Color(255, 242, 242, 242), Color(255, 160, 160, 160), Color(255, 16, 16, 16), Color(60, 255, 255, 255), Color(255, 48, 48, 48),
    Color(255, 30, 30, 30), Color(255, 44, 44, 44), Color(255, 80, 80, 80), Color(255, 64, 64, 64),
    Color(235, 26, 26, 26), Color(255, 70, 70, 70), Color(255, 110, 110, 110),
    Color(200, 30, 30, 30), Color(240, 48, 48, 48), Color(235, 60, 60, 60), Color(215, 24, 24, 24), Color(240, 44, 44, 44),
    Color(170, 30, 30, 30), Color(235, 50, 50, 50), Color(255, 20, 20, 20), Color(255, 40, 40, 40),
    Color(255, 245, 245, 245), Color(255, 190, 190, 190), Color(255, 12, 12, 12), Color(255, 95, 95, 95) };
static bool g_dark;
#define TH(x) ((g_dark ? kDark : kLight).x)
#define kInk TH(ink)
#define kGrey TH(grey)
#define kGreen TH(accent)
#define kGold TH(accent2)
#define kOnAcc TH(onAccent)

static Color Mix(Color a, Color b, float t)
{
    auto L = [&](BYTE x, BYTE y) { return (BYTE)(x + (y - x) * t); };
    return Color(L(a.GetA(), b.GetA()), L(a.GetR(), b.GetR()), L(a.GetG(), b.GetG()), L(a.GetB(), b.GetB()));
}
static Color WithA(Color c, float a) { return Color((BYTE)(c.GetA() * a), c.GetR(), c.GetG(), c.GetB()); }

static void Text(Graphics &g, const std::wstring &s, RectF r, float px, int style, Color c, StringAlignment h = StringAlignmentCenter)
{
    FontFamily fam(L"Segoe UI");
    Font font(&fam, px, style, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(h);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, &font, r, &sf, &b);
}

static float MeasureW(Graphics &g, const std::wstring &s, float px, int style)
{
    FontFamily fam(L"Segoe UI");
    Font font(&fam, px, style, UnitPixel);
    RectF box;
    g.MeasureString(s.c_str(), -1, &font, PointF(0, 0), &box);
    return box.Width;
}

static void DrawButton(Graphics &g, int id, const wchar_t *label, bool primary)
{
    Button &b = g_btn[id];
    if (!b.visible) return;
    float a = b.enabled ? 1.0f : 0.38f;
    RectF r = b.r;
    if (g_pressed == id && g_hot == id) r.Offset(0, 1);
    GraphicsPath p;
    RoundRect(p, r, 12);
    if (primary) {
        GraphicsPath sp;   // ombre coloree
        RoundRect(sp, RectF(r.X + 2, r.Y + 5, r.Width - 4, r.Height), 12);
        SolidBrush sb(Color((BYTE)(45 * a), 0, 0, 0));
        g.FillPath(&sb, &sp);
        LinearGradientBrush lg(r, WithA(kGreen, a), WithA(kGold, a), LinearGradientModeHorizontal);
        g.FillPath(&lg, &p);
        SolidBrush hi(Color((BYTE)(60 * b.hover * a), 255, 255, 255));
        g.FillPath(&hi, &p);
        Text(g, label, r, 15, FontStyleBold, WithA(kOnAcc, a));
    } else {
        SolidBrush fill(Mix(WithA(TH(btn2), a), WithA(TH(btn2Hot), a), b.hover));
        g.FillPath(&fill, &p);
        Pen pen(WithA(kGreen, a), 1.6f);
        g.DrawPath(&pen, &p);
        Text(g, label, r, 15, FontStyleBold, WithA(kGreen, a));
    }
}

static void DrawField(Graphics &g, int i, const wchar_t *label)
{
    Field &f = g_fields[i];
    Text(g, label, RectF(f.r.X + 2, f.r.Y - 18, f.r.Width, 16), 10.5f, FontStyleBold, kGrey, StringAlignmentNear);
    GraphicsPath p;
    RoundRect(p, f.r, 9);
    SolidBrush fill(TH(field));
    g.FillPath(&fill, &p);
    Pen pen(g_focus == i ? kGreen : TH(fieldBorder), g_focus == i ? 2.0f : 1.2f);
    g.DrawPath(&pen, &p);
    RectF tr(f.r.X + 12, f.r.Y, f.r.Width - 24, f.r.Height);
    std::wstring shown = f.text;
    bool placeholder = shown.empty() && g_focus != i;
    if (placeholder) shown = f.address ? L"ex. 26.12.34.56" : L"CJ";
    Text(g, shown, tr, 15, FontStyleRegular, placeholder ? TH(placeholder) : kInk, StringAlignmentNear);
    if (g_focus == i && fmodf(g_time, 1.0f) < 0.55f) {
        FontFamily fam(L"Segoe UI");
        Font font(&fam, 15, FontStyleRegular, UnitPixel);
        StringFormat sf(StringFormat::GenericTypographic());
        sf.SetFormatFlags(StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
        RectF box;
        g.MeasureString(f.text.c_str(), -1, &font, PointF(0, 0), &sf, &box);
        float x = min(tr.X + box.Width + 1, tr.X + tr.Width);
        Pen cp(kGreen, 1.6f);
        g.DrawLine(&cp, x, f.r.Y + 9, x, f.r.Y + f.r.Height - 9);
    }
}

static void DrawBar(Graphics &g, RectF r, float p)
{
    GraphicsPath bg;
    RoundRect(bg, r, r.Height / 2);
    SolidBrush b(WithA(kGreen, 0.22f));
    g.FillPath(&b, &bg);
    RectF fr = r;
    if (p >= 0) fr.Width = max(r.Height, r.Width * min(p, 1.0f));
    else {   // indeterminee : un segment qui glisse
        float w = r.Width * 0.3f, t = fmodf(g_time * 0.8f, 1.0f);
        fr.X = r.X - w + (r.Width + w) * t;
        fr.Width = w;
        g.SetClip(&bg);
    }
    GraphicsPath fp;
    RoundRect(fp, fr, r.Height / 2);
    LinearGradientBrush lg(RectF(fr.X - 1, fr.Y, fr.Width + 2, fr.Height), kGreen, kGold, LinearGradientModeHorizontal);
    g.FillPath(&lg, &fp);
    g.ResetClip();
}

// ---------------------------------------------------------------- options (sacoop.ini du jeu)
// Les memes cles et valeurs par defaut que dllmain.cpp LoadConfig ; ecrites tout de suite, prises au prochain lancement.
enum { TAB_VIDEO, TAB_COOP, TAB_NOTES, TAB_LOBBY, TAB_LOGS, TAB_RENDER, TAB_MODS, TAB_COUNT };   // (TAB_LOGS : page du bouton journaux, pas d'onglet)   // (TAB_LOBBY : seulement pendant un salon)
enum { O_TOGGLE, O_CHOICE };
struct Opt {
    int tab; const char *key; int def; int kind; std::vector<int> vals;
    const wchar_t *fr, *en;
    std::vector<const wchar_t *> labFr, labEn;   // vide : la valeur + suffixe
    const wchar_t *suffix;
    const wchar_t *dFr, *dEn;
};
static std::vector<Opt> g_opts;
static int g_tab = -1, g_optHot = -1, g_optPart = 0, g_tabHot = -1;
static float g_scroll[TAB_COUNT];
static RectF g_tabR[TAB_COUNT];
static const RectF kOptPanel(440, 116, 512, 472), kOptList(452, 128, 488, 396);
static const float kRowH = 34;

static void BuildOptions()
{
    auto T2 = [](int tab, const char *key, int def, const wchar_t *fr, const wchar_t *en, const wchar_t *dFr, const wchar_t *dEn) {
        Opt o; o.tab = tab; o.key = key; o.def = def; o.kind = O_TOGGLE; o.vals = { 0, 1 }; o.fr = fr; o.en = en; o.suffix = L""; o.dFr = dFr; o.dEn = dEn;
        g_opts.push_back(o);
    };
    auto C = [](int tab, const char *key, int def, std::vector<int> vals, const wchar_t *fr, const wchar_t *en, std::vector<const wchar_t *> lf,
                std::vector<const wchar_t *> le, const wchar_t *suffix, const wchar_t *dFr, const wchar_t *dEn) {
        Opt o; o.tab = tab; o.key = key; o.def = def; o.kind = O_CHOICE; o.vals = vals; o.fr = fr; o.en = en; o.labFr = lf; o.labEn = le;
        o.suffix = suffix; o.dFr = dFr; o.dEn = dEn;
        g_opts.push_back(o);
    };
    // VIDEO
    C(TAB_VIDEO, "Fenetre", 2, { 2, 1, 0 }, L"Affichage", L"Display", { L"Plein \u00E9cran fen\u00EAtr\u00E9", L"Fen\u00EAtre", L"Plein \u00E9cran" },
      { L"Borderless", L"Windowed", L"Fullscreen" }, L"",
      L"Plein \u00E9cran fen\u00EAtr\u00E9 (conseill\u00E9) : la fen\u00EAtre couvre l'\u00E9cran, changer de fen\u00EAtre ne met pas le jeu en pause.",
      L"Borderless (recommended): the window covers the screen, switching windows does not pause the game.");
    C(TAB_VIDEO, "ImagesParSeconde", 30, { 25, 30, 45, 60 }, L"Images par seconde", L"Frame rate", {}, {}, L" i/s",
      L"30 = jeu d'origine (conseill\u00E9). Au-dessus, le jeu d'origine a des bogues de physique connus.",
      L"30 = original game (recommended). Above it, the original game has known physics bugs.");
    T2(TAB_VIDEO, "GrandEcran", 1, L"Grand \u00E9cran", L"Widescreen",
       L"La 3D et le HUD gardent leurs proportions sur un \u00E9cran large, avec un champ de vision \u00E9largi.",
       L"The 3D and the HUD keep their proportions on wide screens, with a wider field of view.");
    T2(TAB_RENDER, "OcclusionAmbiante", 1, L"Occlusion ambiante", L"Ambient occlusion",
       L"Rendu moderne : coins, pieds des murs, dessous des voitures et des personnages assombris.",
       L"Modern rendering: corners, wall bases, under cars and characters get darker.");
    T2(TAB_RENDER, "Anticrenelage", 1, L"Anticr\u00E9nelage (FXAA)", L"Anti-aliasing (FXAA)",
       L"Bords des objets adoucis sur la sc\u00E8ne 3D (l'interface reste nette).",
       L"Smoother object edges on the 3D scene (the HUD stays sharp).");
    T2(TAB_VIDEO, "VuePremierePersonne", 1, L"Vue \u00E0 la 1re personne (F6)", L"First-person view (F6)",
       L"F6 en jeu : la cam\u00E9ra \u00E0 pied passe dans les yeux de CJ, et revient avec F6.",
       L"F6 in game: the on-foot camera goes into CJ's eyes, and back with F6.");
    T2(TAB_VIDEO, "SansIntro", 1, L"Passer les logos", L"Skip logos", L"Pas de logos ni de vid\u00E9o d'ouverture au d\u00E9marrage.", L"No logos or intro video at startup.");
    T2(TAB_VIDEO, "SauvegardesLocales", 1, L"Sauvegardes \u00E0 part", L"Separate saves",
       L"R\u00E9glages et sauvegardes du jeu dans son dossier, s\u00E9par\u00E9s de vos sauvegardes solo.",
       L"Game settings and saves in its folder, apart from your solo saves.");
    // COOP
    C(TAB_COOP, "Tenue", 0, { 0, 105, 106, 107, 102, 103, 104, 108, 109, 110, 114, 115, 116 }, L"Personnage vu par les autres", L"Character others see",
      { L"CJ (tes v\u00EAtements)", L"Grove Street 1", L"Grove Street 2", L"Grove Street 3", L"Ballas 1", L"Ballas 2", L"Ballas 3", L"Vagos 1", L"Vagos 2", L"Vagos 3", L"Aztecas 1", L"Aztecas 2", L"Aztecas 3" },
      { L"CJ (your clothes)", L"Grove Street 1", L"Grove Street 2", L"Grove Street 3", L"Ballas 1", L"Ballas 2", L"Ballas 3", L"Vagos 1", L"Vagos 2", L"Vagos 3", L"Aztecas 1", L"Aztecas 2", L"Aztecas 3" }, L"",
      L"CJ : les autres joueurs vous voient avec vos v\u00EAtements (magasins compris). Sinon, sous le personnage choisi.",
      L"CJ: the other players see you with your clothes (shops included). Otherwise, as the chosen character.");
    T2(TAB_COOP, "TirAmi", 1, L"Tir ami", L"Friendly fire",
       L"Les autres joueurs peuvent vous blesser (balles, coups, voitures). Chacun choisit pour lui.",
       L"The other players can hurt you (bullets, hits, cars). Each player chooses for themselves.");
    T2(TAB_COOP, "RecherchePartagee", 1, L"Recherche partag\u00E9e", L"Shared wanted level",
       L"Un seul niveau de recherche pour tous : le crime d'un joueur attire la police sur le groupe, la semer la retire \u00E0 tous.",
       L"One wanted level for everybody: one player's crime brings the police on the group, losing it clears it for all.");
    T2(TAB_COOP, "PoliceHote", 1, L"Police de l'h\u00F4te", L"Host's police",
       L"La police de l'h\u00F4te poursuit aussi les invit\u00E9s recherch\u00E9s (r\u00E9glage de l'h\u00F4te ; chez un invit\u00E9 : pas de police locale \u00E0 c\u00F4t\u00E9 de l'h\u00F4te).",
       L"The host's police also chases wanted guests (host setting; on a guest: no local police near the host).");
}

static std::string GameIni() { return Narrow(g_gameDir + L"sacoop.ini"); }
static int OptGet(const Opt &o) { return GetPrivateProfileIntA("SACoop", o.key, o.def, GameIni().c_str()); }
static void OptSet(const Opt &o, int v) { char b[16]; wsprintfA(b, "%d", v); WritePrivateProfileStringA("SACoop", o.key, b, GameIni().c_str()); }

static const wchar_t *TabName(int t)
{
    static const wchar_t *fr[] = { L"VID\u00C9O", L"COOP", L"NOUVEAUT\u00C9S", L"SALON", L"JOURNAUX", L"RENDU", L"MODS" }, *en[] = { L"VIDEO", L"CO-OP", L"UPDATES", L"LOBBY", L"LOGS", L"RENDERING", L"MODS" };
    return g_fr ? fr[t] : en[t];
}
static bool TabVisible(int t)
{
    if (t == TAB_LOBBY) return g_lobby != LB_NONE;
    if (t == TAB_MODS) return g_lobby != LB_GUEST && g_lobby != LB_CONNECTING;   // (l'invite prend ceux de l'hote)
    return t != TAB_LOGS;
}
static float g_tabFont = 11.5f;   // police des onglets (plus petite quand ils ne tiennent pas sur la ligne)
// Meme ordre et memes regles que VCCoop : marges et ecarts resserres quand les onglets ne tiennent pas jusqu'aux
// boutons reduire / fermer.
static void LayoutTabs()
{
    float x = 440;
    static const int order[] = { TAB_LOBBY, TAB_MODS, TAB_VIDEO, TAB_RENDER, TAB_COOP, TAB_NOTES };
    float tw[TAB_COUNT] = {}, total = 0, pad = 12, gap = 6;
    int n = 0;
    {
        Bitmap bm(1, 1);
        Graphics mg(&bm);
        for (int t : order) if (TabVisible(t)) { tw[t] = MeasureW(mg, TabName(t), 11.5f, FontStyleBold); total += tw[t]; n++; }
    }
    while (pad > 4 && total + n * pad + (n - 1) * gap > 458) { pad -= 2; gap = 4; }
    g_tabFont = 11.5f;
    float room = 458 - n * pad - (n - 1) * gap;
    if (total > room && total > 0) {
        float k = max(0.72f, room / total);
        g_tabFont = 11.5f * k;
        for (int t = 0; t < TAB_COUNT; t++) tw[t] *= k;
    }
    for (int t = 0; t < TAB_COUNT; t++) g_tabR[t] = RectF(0, 0, 0, 0);
    for (int t : order) {
        if (!TabVisible(t)) continue;
        float w = pad + tw[t];
        g_tabR[t] = RectF(x, 78, w, 26);
        x += w + gap;
    }
    if (g_tab >= 0 && g_tab != TAB_LOGS && !TabVisible(g_tab)) g_tab = -1;
}
static std::vector<int> TabRows(int t) { std::vector<int> r; for (int i = 0; i < (int)g_opts.size(); i++) if (g_opts[i].tab == t) r.push_back(i); return r; }
static float NotesMaxScroll();
static float LogsMaxScroll();
static float ModsMaxScroll();
static float MaxScroll(int t) { return t == TAB_LOBBY ? 0.0f : t == TAB_LOGS ? LogsMaxScroll() : t == TAB_MODS ? ModsMaxScroll() : t == TAB_NOTES ? NotesMaxScroll() : max(0.0f, TabRows(t).size() * kRowH - kOptList.Height); }

static int ValueIndex(const Opt &o, int v)
{
    for (int i = 0; i < (int)o.vals.size(); i++) if (o.vals[i] == v) return i;
    for (int i = 0; i < (int)o.vals.size(); i++) if (o.vals[i] > v) return i;   // valeur hors liste : la suivante
    return (int)o.vals.size() - 1;
}
static std::wstring ValueText(const Opt &o, int v)
{
    int i = ValueIndex(o, v);
    const std::vector<const wchar_t *> &lab = g_fr ? o.labFr : o.labEn;
    if (!lab.empty()) return lab[i];
    wchar_t b[32];
    swprintf_s(b, L"%d%s", v, (!wcscmp(o.suffix, L" i/s") && !g_fr) ? L" fps" : o.suffix);
    return b;
}
static void OptStep(int idx, int dir)
{
    const Opt &o = g_opts[idx];
    int v = OptGet(o);
    if (o.kind == O_TOGGLE) { OptSet(o, v ? 0 : 1); return; }
    int i = ValueIndex(o, v);
    if (o.vals[i] != v && dir > 0) i--;
    i = (i + dir + (int)o.vals.size()) % (int)o.vals.size();
    OptSet(o, o.vals[i]);
}

static bool NotesUnseen();

static void DrawTabs(Graphics &g)
{
    if (g_gameDir.empty()) return;
    for (int t = 0; t < TAB_COUNT; t++) {
        if (!TabVisible(t)) continue;
        RectF r = g_tabR[t];
        GraphicsPath p;
        RoundRect(p, r, r.Height / 2);
        bool on = g_tab == t, hot = g_tabHot == t;
        if (on) { LinearGradientBrush lg(r, kGreen, kGold, LinearGradientModeHorizontal); g.FillPath(&lg, &p); }
        else { SolidBrush b(hot ? TH(tabHot) : TH(tab)); g.FillPath(&b, &p); }
        Text(g, TabName(t), r, g_tabFont, FontStyleBold, on ? kOnAcc : Mix(kGrey, kInk, hot ? 1.0f : 0.0f));
        if (t == TAB_NOTES && !on && NotesUnseen()) {   // pastille : des notes pas encore lues
            SolidBrush dot(Color(255, 214, 48, 72));
            g.FillEllipse(&dot, r.X + r.Width - 7, r.Y - 1, 8.0f, 8.0f);
        }
    }
}

static void DrawPanel(Graphics &g)
{
    GraphicsPath pp;
    RoundRect(pp, kOptPanel, 18);
    SolidBrush bg(TH(panel));
    g.FillPath(&bg, &pp);
    Pen border(TH(panelBorder), 1.5f);
    g.DrawPath(&border, &pp);
}

static void DrawNotes(Graphics &g);
static void DrawLobby(Graphics &g);
static void DrawLogs(Graphics &g);
static void DrawMods(Graphics &g);

static void DrawOptions(Graphics &g)
{
    if (g_tab < 0 || g_gameDir.empty()) return;
    if (g_tab == TAB_NOTES) { DrawNotes(g); return; }
    if (g_tab == TAB_LOBBY) { DrawLobby(g); return; }
    if (g_tab == TAB_LOGS) { DrawLogs(g); return; }
    if (g_tab == TAB_MODS) { DrawMods(g); return; }
    DrawPanel(g);
    std::vector<int> rows = TabRows(g_tab);
    float sc = g_scroll[g_tab];
    g.SetClip(kOptList);
    for (int k = 0; k < (int)rows.size(); k++) {
        const Opt &o = g_opts[rows[k]];
        RectF r(kOptList.X, kOptList.Y + k * kRowH - sc, kOptList.Width - 10, kRowH);
        if (r.Y + r.Height < kOptList.Y || r.Y > kOptList.Y + kOptList.Height) continue;
        bool hot = g_optHot == rows[k];
        if (hot) { GraphicsPath hp; RoundRect(hp, RectF(r.X, r.Y + 2, r.Width, r.Height - 4), 9); SolidBrush hb(TH(cardSel)); g.FillPath(&hb, &hp); }
        Text(g, g_fr ? o.fr : o.en, RectF(r.X + 12, r.Y, 280, r.Height), 13.5f, FontStyleRegular, kInk, StringAlignmentNear);
        int v = OptGet(o);
        if (o.kind == O_TOGGLE) {
            RectF tr(r.X + r.Width - 54, r.Y + 7, 42, 20);
            GraphicsPath tp; RoundRect(tp, tr, 10);
            if (v) { LinearGradientBrush lg(tr, kGreen, kGold, LinearGradientModeHorizontal); g.FillPath(&lg, &tp); }
            else { SolidBrush ob(TH(toggleOff)); g.FillPath(&ob, &tp); }
            SolidBrush knob(v ? kOnAcc : Color(255, 255, 255, 255));
            g.FillEllipse(&knob, v ? tr.X + 24 : tr.X + 2, tr.Y + 2, 16.0f, 16.0f);
        } else {
            RectF cr(r.X + r.Width - 190, r.Y + 5, 178, 24);
            GraphicsPath cp; RoundRect(cp, cr, 12);
            SolidBrush cb(TH(card)); g.FillPath(&cb, &cp);
            Pen cpen(TH(choiceBorder), 1.2f); g.DrawPath(&cpen, &cp);
            Color al = (hot && g_optPart < 0) ? kGreen : WithA(kGreen, 0.78f), ar = (hot && g_optPart > 0) ? kGreen : WithA(kGreen, 0.78f);
            Text(g, L"\u2039", RectF(cr.X + 4, cr.Y - 2, 18, cr.Height), 18, FontStyleBold, al);
            Text(g, L"\u203A", RectF(cr.X + cr.Width - 22, cr.Y - 2, 18, cr.Height), 18, FontStyleBold, ar);
            Text(g, ValueText(o, v), RectF(cr.X + 20, cr.Y, cr.Width - 40, cr.Height), 12.5f, FontStyleBold, kInk);
        }
    }
    g.ResetClip();
    // description de la ligne survolee
    Pen sep(TH(sep), 1);
    g.DrawLine(&sep, kOptPanel.X + 18, 532.0f, kOptPanel.X + kOptPanel.Width - 18, 532.0f);
    std::wstring d = g_optHot >= 0 ? (g_fr ? g_opts[g_optHot].dFr : g_opts[g_optHot].dEn)
                                   : T(L"Pris au prochain lancement du jeu.", L"Applied the next time the game starts.");
    FontFamily fam(L"Segoe UI");
    Font font(&fam, 12, FontStyleRegular, UnitPixel);
    StringFormat sf;
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisWord);
    SolidBrush db(kGrey);
    g.DrawString(d.c_str(), -1, &font, RectF(kOptPanel.X + 20, 536, kOptPanel.Width - 40, 44), &sf, &db);
}

// Survol : ligne d'option et cote du selecteur (-1 gauche, +1 droite, 0 libelle)
static void HitOption(float x, float y, int *row, int *part)
{
    *row = -1; *part = 0;
    if (g_tab < 0 || g_tab == TAB_NOTES || g_tab == TAB_LOBBY || g_tab == TAB_LOGS || g_tab == TAB_MODS || !kOptList.Contains(x, y)) return;
    std::vector<int> rows = TabRows(g_tab);
    int k = (int)((y - kOptList.Y + g_scroll[g_tab]) / kRowH);
    if (k < 0 || k >= (int)rows.size()) return;
    *row = rows[k];
    const Opt &o = g_opts[*row];
    float right = kOptList.X + kOptList.Width - 10;
    if (o.kind == O_CHOICE && x >= right - 190) *part = x < right - 190 + 89 ? -1 : 1;
}
static int HitTab(float x, float y)
{
    if (g_state != ST_IDLE || g_gameDir.empty()) return -1;
    for (int t = 0; t < TAB_COUNT; t++) if (TabVisible(t) && g_tabR[t].Contains(x, y)) return t;
    return -1;
}

// ---------------------------------------------------------------- onglet NOUVEAUTES
static std::wstring NewestNote()
{
    EnterCriticalSection(&g_cs);
    std::wstring v = g_notes.empty() ? L"" : g_notes[0].ver;
    LeaveCriticalSection(&g_cs);
    return v;
}
static bool NotesUnseen()
{
    std::wstring v = NewestNote();
    if (v.empty()) return false;
    wchar_t seen[64] = {};
    GetPrivateProfileStringW(L"Lanceur", L"NotesVues", L"", seen, 64, g_iniLauncher.c_str());
    return !seen[0] || CmpVer(v, seen) > 0;
}
static void NotesMarkSeen()
{
    std::wstring v = NewestNote();
    if (!v.empty()) WritePrivateProfileStringW(L"Lanceur", L"NotesVues", v.c_str(), g_iniLauncher.c_str());
}

static RectF NotesArea() { return RectF(kOptList.X, kOptList.Y, kOptList.Width, kOptPanel.Y + kOptPanel.Height - 14 - kOptList.Y); }
static float NotesMaxScroll() { return max(0.0f, g_notesH - NotesArea().Height); }

static void DrawNotes(Graphics &g)
{
    DrawPanel(g);
    RectF area = NotesArea();
    std::vector<Note> notes;
    EnterCriticalSection(&g_cs);
    notes = g_notes;
    LeaveCriticalSection(&g_cs);
    if (notes.empty()) {
        Text(g, g_notesDone ? T(L"Notes de version indisponibles (hors ligne).", L"Release notes unavailable (offline).")
                            : T(L"Chargement des notes de version\u2026", L"Loading release notes\u2026"), area, 13, FontStyleRegular, kGrey);
        return;
    }
    FontFamily fam(L"Segoe UI");
    Font fh(&fam, 14.5f, FontStyleBold, UnitPixel), fd(&fam, 11.5f, FontStyleRegular, UnitPixel), fb(&fam, 12.5f, FontStyleRegular, UnitPixel);
    StringFormat sf;
    SolidBrush ink(kInk), grey(kGrey), green(kGreen);
    Pen sep(TH(sep), 1);
    float sc = g_scroll[TAB_NOTES], y = area.Y - sc, w = area.Width - 14;
    g.SetClip(area);
    for (size_t i = 0; i < notes.size(); i++) {
        const Note &n = notes[i];
        const std::wstring &body = g_fr ? n.fr : n.en;
        RectF box;
        g.MeasureString(body.c_str(), -1, &fb, RectF(0, 0, w - 16, 100000), &sf, &box);
        float h = 26 + (body.empty() ? 0 : box.Height) + 16;
        if (y + h >= area.Y && y <= area.Y + area.Height) {
            std::wstring title = L"SACoop " + n.ver;
            g.DrawString(title.c_str(), -1, &fh, PointF(area.X + 6, y), &green);
            float tw = MeasureW(g, title, 14.5f, FontStyleBold);
            g.DrawString(n.date.c_str(), -1, &fd, PointF(area.X + 12 + tw, y + 3), &grey);
            int cmp = g_localVer.empty() ? 1 : CmpVer(n.ver, g_localVer);
            if (cmp >= 0 && !g_localVer.empty()) {   // version installee, ou plus recente (a venir)
                const wchar_t *lab = cmp == 0 ? T(L"INSTALL\u00C9E", L"INSTALLED") : T(L"NOUVELLE", L"NEW");
                RectF br(area.X + w - 12 - 7.0f * (float)wcslen(lab), y + 2, 12 + 7.0f * (float)wcslen(lab), 17);
                GraphicsPath bp; RoundRect(bp, br, 8.5f);
                SolidBrush bb(cmp == 0 ? WithA(kInk, 0.12f) : WithA(kInk, 0.22f));
                g.FillPath(&bb, &bp);
                Text(g, lab, br, 9.5f, FontStyleBold, kInk);
            }
            if (!body.empty()) g.DrawString(body.c_str(), -1, &fb, RectF(area.X + 10, y + 26, w - 16, box.Height + 4), &sf, &ink);
            if (i + 1 < notes.size()) g.DrawLine(&sep, area.X + 6, y + h - 8, area.X + w, y + h - 8);
        }
        y += h;
    }
    g.ResetClip();
    g_notesH = y + sc - area.Y;
    float ms = NotesMaxScroll();
    if (ms > 0) {
        float bh = area.Height * area.Height / (area.Height + ms), by = area.Y + (area.Height - bh) * sc / ms;
        GraphicsPath sp; RoundRect(sp, RectF(area.X + area.Width - 5, by, 4, bh), 2);
        SolidBrush sb(WithA(kGreen, 0.5f)); g.FillPath(&sb, &sp);
    }
}

// ---------------------------------------------------------------- interface
static void DrawUI(Graphics &g)
{
    UpdateButtons();
    std::wstring status;
    int kind;
    EnterCriticalSection(&g_cs);
    status = g_status; kind = g_statusKind;
    LeaveCriticalSection(&g_cs);
    Color sc = kind == K_OK ? kInk : kind == K_WARN ? Color(255, 205, 120, 30) : kind == K_ERR ? Color(255, 214, 48, 72) : kGrey;
    float prog = g_progress;

    {   // theme : lune (passer en sombre) ou soleil (passer en clair)
        Button &b = g_btn[B_THEME];
        SolidBrush cb(Mix(TH(circle), TH(circleHot), b.hover));
        g.FillEllipse(&cb, b.r);
        Color ic = Mix(kInk, kGreen, b.hover);
        float cx = b.r.X + b.r.Width / 2, cy = b.r.Y + b.r.Height / 2;
        if (!g_dark) {
            SolidBrush moon(ic);
            GraphicsPath mp;
            mp.AddEllipse(cx - 6.5f, cy - 6.5f, 13.0f, 13.0f);
            Region rg(&mp);
            GraphicsPath cut;
            cut.AddEllipse(cx - 2.5f, cy - 9.0f, 13.0f, 13.0f);
            rg.Exclude(&cut);
            g.FillRegion(&moon, &rg);
        } else {
            SolidBrush sun(ic);
            g.FillEllipse(&sun, cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
            Pen ray(ic, 1.6f);
            ray.SetStartCap(LineCapRound); ray.SetEndCap(LineCapRound);
            for (int k = 0; k < 8; k++) { float a = k * 0.7854f; g.DrawLine(&ray, cx + cosf(a) * 6.5f, cy + sinf(a) * 6.5f, cx + cosf(a) * 9.0f, cy + sinf(a) * 9.0f); }
        }
    }
    if (g_btn[B_LOGS].visible) {   // journaux : feuille lignee ; libelle au survol
        Button &b = g_btn[B_LOGS];
        bool on = g_tab == TAB_LOGS;
        if (on) { SolidBrush ob(kInk); g.FillEllipse(&ob, b.r); }
        else { SolidBrush cb(Mix(TH(circle), TH(circleHot), b.hover)); g.FillEllipse(&cb, b.r); }
        Color ic = on ? TH(panel) : Mix(kInk, kGreen, b.hover);
        float cx = b.r.X + b.r.Width / 2, cy = b.r.Y + b.r.Height / 2;
        Pen pen(ic, 1.5f);
        pen.SetLineJoin(LineJoinRound); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        GraphicsPath sheet;
        RoundRect(sheet, RectF(cx - 5.5f, cy - 7.0f, 11.0f, 14.0f), 2.0f);
        g.DrawPath(&pen, &sheet);
        for (int k = 0; k < 3; k++) g.DrawLine(&pen, cx - 3.0f, cy - 3.5f + k * 3.5f, k == 2 ? cx + 1.0f : cx + 3.0f, cy - 3.5f + k * 3.5f);
        if (b.hover > 0.02f && !on) Text(g, T(L"Journaux", L"Logs"), RectF(b.r.X - 84, b.r.Y, 78, b.r.Height), 11.5f, FontStyleBold, WithA(kInk, b.hover), StringAlignmentFar);
    }
    for (int id : { B_MIN, B_CLOSE }) {
        Button &b = g_btn[id];
        SolidBrush cb(Mix(TH(circle), TH(circleHot), b.hover));
        g.FillEllipse(&cb, b.r);
        Pen pen(Mix(kInk, kGreen, b.hover), 1.8f);
        float cx = b.r.X + b.r.Width / 2, cy = b.r.Y + b.r.Height / 2;
        if (id == B_CLOSE) { g.DrawLine(&pen, cx - 5, cy - 5, cx + 5, cy + 5); g.DrawLine(&pen, cx + 5, cy - 5, cx - 5, cy + 5); }
        else g.DrawLine(&pen, cx - 5, cy, cx + 5, cy);
    }

    // PRE-ALPHA : pastille bien visible sous le logo
    {
        RectF pr(170, 184, 116, 20);
        GraphicsPath pp; RoundRect(pp, pr, 10);
        SolidBrush pb(Color(230, 206, 52, 52));
        g.FillPath(&pb, &pp);
        Text(g, L"PRE-ALPHA", pr, 11, FontStyleBold, Color(255, 255, 255, 255));
    }

    if (g_state == ST_LAUNCH || g_state == ST_CLOSING) {
        int dots = (int)(g_time * 2.5f) % 4;
        std::wstring title = T(L"San Andreas se lance", L"San Andreas is starting");
        title += std::wstring(dots, L'.') + std::wstring(3 - dots, L' ');
        Text(g, title, RectF(60, 300, 336, 40), 24, FontStyleBold, kInk);
        Text(g, g_launchInfo, RectF(60, 340, 336, 26), 14, FontStyleRegular, kGrey);
        DrawBar(g, RectF(96, 390, 264, 6), -2);
        Text(g, T(L"La fen\u00EAtre du jeu va appara\u00EEtre.", L"The game window will appear shortly."), RectF(60, 410, 336, 24), 12.5f, FontStyleRegular, kGrey);
    } else {
        DrawTabs(g);
        DrawOptions(g);
        Text(g, status, RectF(60, 212, 336, 22), 13, FontStyleBold, sc);
        if (prog != -1.0f) DrawBar(g, RectF(96, 238, 264, 5), prog);
        DrawField(g, 0, T(L"PSEUDO", L"NICKNAME"));
        DrawField(g, 1, T(L"ADRESSE DE L'H\u00D4TE", L"HOST ADDRESS"));
        const wchar_t *hostLabel = T(L"H\u00C9BERGER", L"HOST"), *joinLabel = g_joinFallback ? T(L"REJOINDRE EN JEU", L"JOIN IN GAME") : T(L"REJOINDRE", L"JOIN");
        if (g_lobby == LB_HOST) { hostLabel = T(L"LANCER", L"START"); joinLabel = T(L"FERMER LE SALON", L"CLOSE LOBBY"); }
        else if (g_lobby == LB_GUEST) { hostLabel = g_meReady ? T(L"PR\u00CAT \u2713", L"READY \u2713") : T(L"PR\u00CAT ?", L"READY?"); joinLabel = T(L"QUITTER", L"LEAVE"); }
        else if (g_lobby == LB_CONNECTING) { hostLabel = T(L"CONNEXION\u2026", L"CONNECTING\u2026"); joinLabel = T(L"ANNULER", L"CANCEL"); }
        DrawButton(g, B_HOST, hostLabel, true);
        DrawButton(g, B_JOIN, joinLabel, false);
        std::wstring exeLine;
        Color ec = kGrey;
        if (g_exeKind == EXE_OK) { exeLine = L"gta_sa.exe 1.0 US \u2713"; ec = kInk; }
        else if (g_exeKind == EXE_MISSING) { exeLine = T(L"gta_sa.exe introuvable", L"gta_sa.exe not found"); ec = Color(255, 214, 48, 72); }
        else { exeLine = T(L"exe pas en 1.0 US", L"exe is not 1.0 US"); ec = Color(255, 214, 48, 72); }
        Text(g, exeLine, RectF(78, 452, 170, 20), 12, FontStyleBold, ec, StringAlignmentNear);
        Button &eb = g_btn[B_EXE];
        const wchar_t *el = g_exeKind == EXE_OK ? T(L"Changer d'exe", L"Change exe") : T(L"Choisir l'exe\u2026", L"Choose exe\u2026");
        Color lc = Mix(g_exeKind == EXE_OK ? kGrey : kGreen, kGreen, eb.hover);
        Text(g, el, eb.r, 12, g_exeKind == EXE_OK ? FontStyleUnderline : FontStyleBold | FontStyleUnderline, WithA(lc, eb.enabled ? 1.0f : 0.4f), StringAlignmentFar);
    }
    const Color lg = WithA(kGrey, 0.8f);
    Text(g, T(L"Mod non officiel et non commercial.", L"Unofficial, non-commercial mod."), RectF(56, 510, 344, 14), 10, FontStyleRegular, lg);
    Text(g, T(L"Non affili\u00E9 \u00E0 Rockstar Games ni \u00E0 Take-Two.", L"Not affiliated with Rockstar Games or Take-Two."), RectF(56, 523, 344, 14), 10, FontStyleRegular, lg);
    Text(g, T(L"N\u00E9cessite une copie l\u00E9gale de GTA: San Andreas.", L"Requires a legal copy of GTA: San Andreas."), RectF(56, 536, 344, 14), 10, FontStyleRegular, lg);
    {   // Acheter le jeu : pastille sombre avec un panier, vers la boutique Rockstar
        Button &b = g_btn[B_BUY];
        Text(g, T(L"Achetez GTA: San Andreas :", L"Buy GTA: San Andreas:"), RectF(56, b.r.Y, b.r.X - 56 - 8, b.r.Height), 12, FontStyleBold, kInk, StringAlignmentFar);
        GraphicsPath p;
        RoundRect(p, b.r, b.r.Height / 2);
        SolidBrush fill(Mix(TH(pill), TH(pillHot), b.hover));
        g.FillPath(&fill, &p);
        Pen cart(Color(255, 255, 255, 255), 1.6f);
        cart.SetLineJoin(LineJoinRound);
        cart.SetStartCap(LineCapRound);
        cart.SetEndCap(LineCapRound);
        float x = b.r.X + 13, y = b.r.Y + 7;
        PointF basket[] = { PointF(x - 2, y), PointF(x + 1, y), PointF(x + 3.5f, y + 9), PointF(x + 12, y + 9), PointF(x + 14, y + 3), PointF(x + 2.2f, y + 3) };
        g.DrawLines(&cart, basket, 6);
        SolidBrush white(Color(255, 255, 255, 255));
        g.FillEllipse(&white, x + 3.2f, y + 10.4f, 3.2f, 3.2f);
        g.FillEllipse(&white, x + 9.8f, y + 10.4f, 3.2f, 3.2f);
        Text(g, L"Rockstar Store", RectF(b.r.X + 30, b.r.Y, b.r.Width - 36, b.r.Height), 12, FontStyleBold, Color(255, 255, 255, 255), StringAlignmentNear);
    }
}

static void RenderTo(Bitmap &target, float scale)
{
    Graphics g(&target);
    g.Clear(Color(0, 0, 0, 0));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.ScaleTransform(scale, scale);
    Bitmap *bgi = (g_dark && g_bgDark) ? g_bgDark : g_bg;
    if (bgi) g.DrawImage(bgi, RectF(0, 0, kImgW, kImgH));
    else {   // pas d'image : carte simple
        GraphicsPath p;
        RoundRect(p, RectF(20, 60, 960, 540), 26);
        LinearGradientBrush lg(RectF(20, 60, 960, 540), TH(fallA), TH(fallB), LinearGradientModeVertical);
        g.FillPath(&lg, &p);
    }
    DrawUI(g);
}

static void Present()
{
    if (!g_wnd || !g_memDC) return;
    {
        Bitmap frame(g_winW, g_winH, g_winW * 4, PixelFormat32bppPARGB, (BYTE *)g_bits);
        RenderTo(frame, g_scale);
    }
    GdiFlush();
    RECT wr;
    GetWindowRect(g_wnd, &wr);
    POINT dst = { wr.left, wr.top }, src = { 0, 0 };
    SIZE sz = { g_winW, g_winH };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(255 * min(max(g_alpha, 0.0f), 1.0f)), AC_SRC_ALPHA };
    HDC screen = GetDC(NULL);
    BOOL ok = UpdateLayeredWindow(g_wnd, screen, &dst, &sz, g_memDC, &src, 0, &bf, ULW_ALPHA);
    if (g_ulwOk != 0) g_ulwOk = ok ? 1 : 0;
    g_frames++;
    ReleaseDC(NULL, screen);
}

// ---------------------------------------------------------------- actions
static void ChooseExe()
{
    wchar_t file[MAX_PATH] = L"gta_sa.exe";
    OPENFILENAMEW of = { sizeof(of) };
    of.hwndOwner = g_wnd;
    of.lpstrFilter = L"gta_sa.exe\0gta_sa*.exe;gta-sa*.exe\0*.exe\0*.exe\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    std::wstring init = !g_exe.empty() ? DirOf(g_exe) : g_dir;
    of.lpstrInitialDir = init.c_str();
    of.lpstrTitle = T(L"Choisir gta_sa.exe (version 1.0 US)", L"Choose gta_sa.exe (version 1.0 US)");
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&of)) return;
    if (CheckExe(file) != EXE_OK) { BadExeMessage(); return; }
    SetExe(file);
    WritePrivateProfileStringW(L"Lanceur", L"Exe", file, g_iniLauncher.c_str());
    StartUpdate();
}

static BOOL CALLBACK FindGameWindow(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    RECT r;
    if (pid == g_pid && IsWindowVisible(h) && GetWindowRect(h, &r) && r.right - r.left >= 320) { *(bool *)lp = true; return FALSE; }
    return TRUE;
}

static void Launch(int mode, const std::wstring &extra = L"")
{
    if (g_exeKind != EXE_OK || g_busy) return;
    std::wstring addr = Trim(g_fields[1].text);
    if (mode == 2 && addr.empty()) {
        SetStatus(K_ERR, T(L"Entre l'adresse de l'h\u00F4te", L"Enter the host address"));
        g_focus = 1;
        return;
    }
    SavePlayer();
    std::wstring args = (mode == 1 ? L"-sacoop hote" : L"-sacoop invite " + addr) + extra;
    // Lance comme un double-clic dans l'explorateur : un mode de compatibilite de l'exe peut exiger l'administrateur ;
    // CreateProcess echoue alors (erreur 740), ShellExecuteEx affiche la demande de Windows.
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = g_wnd;
    sei.lpVerb = L"open";
    sei.lpFile = g_exe.c_str();
    sei.lpParameters = args.c_str();
    sei.lpDirectory = g_gameDir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        DWORD e = GetLastError();
        if (e == ERROR_CANCELLED) SetStatus(K_WARN, T(L"Lancement annul\u00E9 (demande d'administrateur refus\u00E9e)", L"Launch cancelled (administrator prompt declined)"));
        else SetStatus(K_ERR, T(L"Impossible de lancer gta_sa.exe (erreur %lu)", L"Could not start gta_sa.exe (error %lu)"), e);
        return;
    }
    g_proc = sei.hProcess;
    g_pid = GetProcessId(sei.hProcess);
    g_launchT = GetTickCount();
    g_winSeenT = 0;
    std::wstring name = Trim(g_fields[0].text);
    wchar_t info[160];
    if (mode == 1) swprintf_s(info, T(L"%s h\u00E9berge la partie", L"%s is hosting"), name.c_str());
    else swprintf_s(info, T(L"%s rejoint %s", L"%s joins %s"), name.c_str(), addr.c_str());
    g_launchInfo = info;
    g_focus = -1;
    g_state = ST_LAUNCH;
}


// ---------------------------------------------------------------- salon
// Le lanceur de l'hote ouvre un salon en TCP sur le port du jeu (le jeu, lui, est en UDP). Chaque connexion commence
// par "SAL1", puis des messages [u16 longueur][u8 type][...] : HELLO (version, pseudo, tenue) -> WELCOME (numero) ou
// REJECT (raison) ; STATE (joueurs : pret, ping ; choix de partie) ; READY ; PING / PONG ; GO (l'hote lance : chaque
// lanceur demarre son jeu, l'hote avec -sacoop-partie, les invites avec -sacoop invite ; ils suivent ensuite l'hote).
enum { LB_PROTO = 2, M_HELLO = 1, M_WELCOME, M_REJECT, M_STATE, M_READY, M_GO, M_PING, M_PONG, M_MODS, M_GETFILE, M_FILEDATA, M_FILEEND };
static std::atomic<bool> g_hostModsReady(false);   // hote : manifeste des mods pret (plus bas : mods partages)
static void SendManifest(SOCKET s);
static void SendModFile(SOCKET s, int index);
static DWORD WINAPI HostModsThread(void *);
struct SaveInfo { int slot; std::string label; };
static std::vector<SaveInfo> g_saves;
static SOCKET g_listen = INVALID_SOCKET, g_guestSock = INVALID_SOCKET;
struct Conn { SOCKET s; int id; };
static std::vector<Conn> g_conns;                   // hote : invites du salon (sous g_lcs)
static std::atomic<bool> g_goSent(false);
static std::wstring g_lobbyAddr;
static int g_lobbyPort = 7800;
static std::wstring g_testSalon, g_testSalonLog;     // /testsalon hote|invite <journal>

static void TestLog(const char *fmt, ...)
{
    if (g_testSalonLog.empty()) return;
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(b, _countof(b), _TRUNCATE, fmt, ap);
    va_end(ap);
    FILE *f = _wfopen(g_testSalonLog.c_str(), L"a");
    if (f) { fprintf(f, "[%lu] %s\n", GetTickCount(), b); fclose(f); }
}

struct Wr {
    std::string d;
    void u8(int v) { d += (char)(uint8_t)v; }
    void u16(int v) { uint16_t x = (uint16_t)v; d.append((const char *)&x, 2); }
    void u32(uint32_t v) { d.append((const char *)&v, 4); }
    void str(const std::string &s) { size_t n = min<size_t>(s.size(), 255); u8((int)n); d.append(s.data(), n); }
};
struct Rd {
    const std::string &d; size_t p = 0; bool ok = true;
    Rd(const std::string &x) : d(x) {}
    int u8() { if (p + 1 > d.size()) { ok = false; return 0; } return (uint8_t)d[p++]; }
    int u16() { if (p + 2 > d.size()) { ok = false; return 0; } uint16_t x; memcpy(&x, d.data() + p, 2); p += 2; return x; }
    uint32_t u32() { if (p + 4 > d.size()) { ok = false; return 0; } uint32_t x; memcpy(&x, d.data() + p, 4); p += 4; return x; }
    std::string str() { int n = u8(); if (!ok || p + n > d.size()) { ok = false; return ""; } std::string s = d.substr(p, n); p += n; return s; }
};
static bool SendAllS(SOCKET s, const void *d, int n)
{
    const char *p = (const char *)d;
    while (n > 0) { int r = send(s, p, n, 0); if (r <= 0) return false; p += r; n -= r; }
    return true;
}
static bool RecvAllS(SOCKET s, void *d, int n)
{
    char *p = (char *)d;
    while (n > 0) { int r = recv(s, p, n, 0); if (r <= 0) return false; p += r; n -= r; }
    return true;
}
static bool SendMsg(SOCKET s, const Wr &w)
{
    uint16_t n = (uint16_t)w.d.size();
    return SendAllS(s, &n, 2) && SendAllS(s, w.d.data(), n);
}
static bool RecvMsg(SOCKET s, std::string &out)
{
    uint16_t n;
    if (!RecvAllS(s, &n, 2)) return false;
    out.resize(n);
    return n == 0 || RecvAllS(s, &out[0], n);
}

static int LobbyPort()
{
    std::string pj = Narrow(PlayerIni()), main = Narrow(g_gameDir + L"sacoop.ini");
    int port = GetPrivateProfileIntA("SACoop", "Port", 7800, main.c_str());
    return GetPrivateProfileIntA("SACoop", "Port", port, pj.c_str());
}
static std::string MySkin()   // tenue lisible (meme liste que l'onglet COOP et le panneau F10 du jeu)
{
    int v = GetPrivateProfileIntA("SACoop", "Tenue", 0, Narrow(g_gameDir + L"sacoop.ini").c_str());
    static const int ids[] = { 0, 105, 106, 107, 102, 103, 104, 108, 109, 110, 114, 115, 116 };
    static const char *names[] = { "CJ", "Grove 1", "Grove 2", "Grove 3", "Ballas 1", "Ballas 2", "Ballas 3", "Vagos 1", "Vagos 2", "Vagos 3", "Aztecas 1", "Aztecas 2", "Aztecas 3" };
    for (int i = 0; i < 13; i++) if (ids[i] == v) return names[i];
    return "CJ";
}
static std::string MyName() { std::wstring n = Trim(g_fields[0].text); return Narrow(n.empty() ? L"CJ" : n); }

// --- sauvegardes (hote) : <jeu>\GTA San Andreas User Files (SauvegardesLocales=1) ou Mes documents\...
static void ReadSaves()
{
    g_saves.clear();
    std::wstring dir;
    if (GetPrivateProfileIntA("SACoop", "SauvegardesLocales", 1, Narrow(g_gameDir + L"sacoop.ini").c_str())) dir = g_gameDir + L"GTA San Andreas User Files\\";
    else {
        wchar_t docs[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs))) dir = std::wstring(docs) + L"\\GTA San Andreas User Files\\";
    }
    for (int slot = 1; slot <= 8; slot++) {
        wchar_t name[32];
        swprintf_s(name, L"GTASAsf%d.b", slot);
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExW((dir + name).c_str(), GetFileExInfoStandard, &a) || a.nFileSizeLow < 1000) continue;
        FILETIME lt;
        SYSTEMTIME st;
        FileTimeToLocalFileTime(&a.ftLastWriteTime, &lt);
        FileTimeToSystemTime(&lt, &st);
        wchar_t lab[128];
        swprintf_s(lab, T(L"Emplacement %d \u00B7 %02d/%02d %02d:%02d", L"Slot %d \u00B7 %02d/%02d %02d:%02d"), slot, st.wDay, st.wMonth, st.wHour, st.wMinute);
        g_saves.push_back({ slot, Narrow(lab, CP_UTF8) });
    }
}
static std::string ChosenSave()   // sauvegarde choisie ("" = nouvelle partie)
{
    if (g_lobby == LB_GUEST) return g_lobbyChoiceLabel;
    if (g_lobbyChoice <= 0 || g_lobbyChoice > (int)g_saves.size()) return "";
    return g_saves[g_lobbyChoice - 1].label;
}
static std::string ChoiceLabel()
{
    std::string save = ChosenSave();
    if (save.empty()) return Narrow(T(L"Nouvelle partie", L"New game"), CP_UTF8);
    return Narrow(T(L"Charger ", L"Load "), CP_UTF8) + save;
}

// --- hote
static void BroadcastState()
{
    Wr w;
    w.u8(M_STATE);
    EnterCriticalSection(&g_lcs);
    w.u8((int)g_peers.size());
    for (auto &p : g_peers) { w.u8(p.id); w.str(p.name); w.str(p.skin); w.u8(p.ready); w.u16(min(p.ping, 9999)); }
    w.str(ChosenSave());
    for (auto &c : g_conns) SendMsg(c.s, w);
    LeaveCriticalSection(&g_lcs);
}
static LobbyPeer *PeerById(int id) { for (auto &p : g_peers) if (p.id == id) return &p; return NULL; }
static void DropConn(SOCKET s)
{
    EnterCriticalSection(&g_lcs);
    for (size_t i = 0; i < g_conns.size(); i++)
        if (g_conns[i].s == s) {
            int id = g_conns[i].id;
            g_conns.erase(g_conns.begin() + i);
            for (size_t k = 0; k < g_peers.size(); k++) if (g_peers[k].id == id) { TestLog("salon : %s part", g_peers[k].name.c_str()); g_peers.erase(g_peers.begin() + k); break; }
            break;
        }
    LeaveCriticalSection(&g_lcs);
    closesocket(s);
}
static void LobbySession(SOCKET s)
{
    std::string m;
    if (!RecvMsg(s, m)) { closesocket(s); return; }
    Rd r(m);
    int type = r.u8(), proto = r.u8();
    std::string ver = r.str(), name = r.str(), skin = r.str();
    auto reject = [&](const std::string &why) { Wr w; w.u8(M_REJECT); w.str(why); SendMsg(s, w); closesocket(s); };
    if (!r.ok || type != M_HELLO || proto != LB_PROTO) { reject("proto"); return; }
    std::string mine = Narrow(g_localVer, CP_UTF8);
    if (ver != mine) { reject("version " + mine); return; }
    if (g_goSent || g_lobby != LB_HOST) { reject("started"); return; }
    int id = -1;
    EnterCriticalSection(&g_lcs);
    for (int k = 1; k < 4 && id < 0; k++) if (!PeerById(k)) id = k;
    if (id > 0) { g_peers.push_back({ id, name, skin, false, 0 }); g_conns.push_back({ s, id }); }
    LeaveCriticalSection(&g_lcs);
    if (id < 0) { reject("full"); return; }
    { Wr w; w.u8(M_WELCOME); w.u8(id); SendMsg(s, w); }
    TestLog("salon : %s arrive (joueur %d)", name.c_str(), id);
    BroadcastState();
    SendManifest(s);
    DWORD to = 60000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof(to));
    while (RecvMsg(s, m)) {
        Rd q(m);
        int t = q.u8();
        EnterCriticalSection(&g_lcs);
        LobbyPeer *p = PeerById(id);
        if (p && t == M_READY) { p->ready = q.u8() != 0; TestLog("salon : joueur %d pret=%d", id, (int)p->ready); }
        else if (p && t == M_PONG) { uint32_t sent = q.u32(); p->ping = (int)(GetTickCount() - sent); }
        LeaveCriticalSection(&g_lcs);
        if (t == M_READY) BroadcastState();
        if (t == M_GETFILE) SendModFile(s, q.u16());
    }
    DropConn(s);
    BroadcastState();
}
static DWORD WINAPI ConnThread(void *param)
{
    SOCKET s = (SOCKET)param;
    DWORD to = 30000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof(to));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&to, sizeof(to));
    char magic[4];
    if (RecvAllS(s, magic, 4) && !memcmp(magic, "SAL1", 4)) {
        DWORD sto = 3000;   // un invite bloque ne fige pas la fenetre (envois sous g_lcs)
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&sto, sizeof(sto));
        LobbySession(s);   // (ferme la socket)
        return 0;
    }
    closesocket(s);
    return 0;
}
static DWORD WINAPI AcceptThread(void *)
{
    for (;;) {
        SOCKET c = accept(g_listen, NULL, NULL);
        if (c == INVALID_SOCKET) break;
        HANDLE t = CreateThread(NULL, 0, ConnThread, (void *)c, 0, NULL);
        if (t) CloseHandle(t); else closesocket(c);
    }
    return 0;
}
static std::wstring LocalAddresses()
{
    char host[256];
    std::wstring out;
    if (gethostname(host, sizeof(host)) != 0) return out;
    addrinfo hints = {}, *res = NULL;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) return out;
    int n = 0;
    for (addrinfo *a = res; a && n < 3; a = a->ai_next) {
        char ip[64];
        inet_ntop(AF_INET, &((sockaddr_in *)a->ai_addr)->sin_addr, ip, sizeof(ip));
        if (!strncmp(ip, "127.", 4)) continue;
        if (!out.empty()) out += L" \u00B7 ";
        out += Widen(ip);
        n++;
    }
    freeaddrinfo(res);
    return out;
}
static std::wstring g_myAddresses;

static void LobbyHost()
{
    SavePlayer();
    g_lobbyPort = LobbyPort();
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((u_short)g_lobbyPort);
    if (bind(g_listen, (sockaddr *)&a, sizeof(a)) != 0 || listen(g_listen, 8) != 0) {
        int e = WSAGetLastError();
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        SetStatus(K_ERR, T(L"Port %d occup\u00E9 (erreur %d)", L"Port %d in use (error %d)"), g_lobbyPort, e);
        return;
    }
    ReadSaves();
    EnterCriticalSection(&g_lcs);
    g_peers.clear();
    g_conns.clear();
    g_peers.push_back({ 0, MyName(), MySkin(), true, 0 });
    g_lobbyChoice = 0;
    LeaveCriticalSection(&g_lcs);
    g_goSent = false;
    g_myId = 0;
    g_myAddresses = LocalAddresses();
    g_hostModsReady = false;
    if (HANDLE mt = CreateThread(NULL, 0, HostModsThread, NULL, 0, NULL)) CloseHandle(mt);
    g_lobby = LB_HOST;
    g_tab = TAB_LOBBY;
    LayoutTabs();
    HANDLE t = CreateThread(NULL, 0, AcceptThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    SetStatus(K_OK, T(L"Salon ouvert \u00B7 port %d", L"Lobby open \u00B7 port %d"), g_lobbyPort);
    TestLog("salon : ouvert sur le port %d, %d sauvegardes", g_lobbyPort, (int)g_saves.size());
}

static bool LobbyCanStart()
{
    if (g_lobby != LB_HOST || g_goSent) return false;
    bool ok = true;
    EnterCriticalSection(&g_lcs);
    for (auto &p : g_peers) if (p.id != 0 && !p.ready) ok = false;
    LeaveCriticalSection(&g_lcs);
    return ok;
}

static void LobbyClose()
{
    g_lobby = LB_NONE;
    if (g_listen != INVALID_SOCKET) { closesocket(g_listen); g_listen = INVALID_SOCKET; }
    EnterCriticalSection(&g_lcs);
    for (auto &c : g_conns) shutdown(c.s, SD_BOTH);   // les fils des sessions ferment leurs sockets
    g_conns.clear();
    g_peers.clear();
    if (g_guestSock != INVALID_SOCKET) shutdown(g_guestSock, SD_BOTH);   // GuestThread la ferme
    LeaveCriticalSection(&g_lcs);
    g_meReady = false;
    if (g_tab == TAB_LOBBY) g_tab = -1;
    LayoutTabs();
}

static void HostStart()
{
    if (!LobbyCanStart()) return;
    int slot = (g_lobbyChoice > 0 && g_lobbyChoice <= (int)g_saves.size()) ? g_saves[g_lobbyChoice - 1].slot : 0;
    g_goSent = true;
    Wr w;
    w.u8(M_GO);
    w.u8(slot);
    EnterCriticalSection(&g_lcs);
    for (auto &c : g_conns) SendMsg(c.s, w);
    LeaveCriticalSection(&g_lcs);
    TestLog("salon : GO (emplacement %d)", slot);
    for (int i = 0; i < 30; i++) {   // les invites ferment les premiers en recevant GO
        EnterCriticalSection(&g_lcs);
        bool empty = g_conns.empty();
        LeaveCriticalSection(&g_lcs);
        if (empty) break;
        Sleep(50);
    }
    LobbyClose();
    wchar_t extra[48];
    if (slot > 0) swprintf_s(extra, L" -sacoop-partie %d", slot); else wcscpy_s(extra, L" -sacoop-partie nouvelle");
    Launch(1, extra);
}

// --- invite
static void GuestSend(const Wr &w)
{
    EnterCriticalSection(&g_lcs);
    if (g_guestSock != INVALID_SOCKET) SendMsg(g_guestSock, w);
    LeaveCriticalSection(&g_lcs);
}

// ---------------------------------------------------------------- mods partages (salon)
// L'hote envoie a chaque invite le manifeste de son SACoop\mods\ (chemin, taille, empreinte FNV-1a) ; l'invite
// compare au sien, demande les fichiers manquants ou differents (morceaux de 32 Ko sur la connexion du salon), les
// range dans son SACoop\mods\, puis ecrit SACoop\cache\mods-liste.txt (la liste de l'hote) : son jeu, lance avec
// -sacoop-mods, ne charge que ceux-la (mods.cpp). "Pret" attend la fin du telechargement.
struct ModEntry { std::string rel; uint32_t size, hash; };
static std::vector<ModEntry> g_hostMods;            // hote : son manifeste
static uint64_t g_hostModsBytes;
static std::vector<ModEntry> g_wantMods;            // invite : manifeste de l'hote
enum { MS_NONE, MS_WAIT, MS_DOWNLOAD, MS_READY, MS_FAILED };
static std::atomic<int> g_modsState(MS_NONE);
static std::atomic<uint64_t> g_modsTotal(0), g_modsDone(0);
static int g_modsCount;

static std::wstring ModsDir() { return g_gameDir + L"SACoop\\mods\\"; }
static uint32_t FnvFile(const std::wstring &path, uint32_t *size)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    uint32_t h = 2166136261u, total = 0;
    static uint8_t buf[1 << 16];
    DWORD n;
    while (ReadFile(f, buf, sizeof(buf), &n, NULL) && n) {
        for (DWORD i = 0; i < n; i++) { h ^= buf[i]; h *= 16777619u; }
        total += n;
    }
    CloseHandle(f);
    if (size) *size = total;
    return h;
}
static void ScanMods(const std::wstring &dir, const std::string &rel, std::vector<ModEntry> &out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        std::string r = rel + Narrow(fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { ScanMods(dir + fd.cFileName + L"\\", r + "\\", out); continue; }
        if (!_wcsicmp(fd.cFileName + max(0, (int)wcslen(fd.cFileName) - 5), L".part")) continue;   // telechargement interrompu
        if (!_wcsicmp(fd.cFileName + max(0, (int)wcslen(fd.cFileName) - 4), L".txt")) continue;    // notes (LISEZMOI-MODS.txt)
        uint32_t size = 0, hash = FnvFile(dir + fd.cFileName, &size);
        out.push_back({ r, size, hash });
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static DWORD WINAPI HostModsThread(void *)
{
    std::vector<ModEntry> mods;
    if (GetPrivateProfileIntA("SACoop", "ModsPartages", 1, Narrow(g_gameDir + L"sacoop.ini").c_str())) ScanMods(ModsDir(), "", mods);
    uint64_t bytes = 0;
    for (auto &m : mods) bytes += m.size;
    EnterCriticalSection(&g_lcs);
    g_hostMods = mods;
    g_hostModsBytes = bytes;
    LeaveCriticalSection(&g_lcs);
    g_hostModsReady = true;
    TestLog("mods : %d fichiers (%llu octets)", (int)mods.size(), (unsigned long long)bytes);
    return 0;
}
static bool SafeRel(const std::string &r)
{
    return !r.empty() && r.size() < 400 && r.find("..") == std::string::npos && r.find(':') == std::string::npos && r[0] != '\\' && r[0] != '/';
}
// Hote : manifeste envoye par paquets de 100 (le dernier marque).
static void SendManifest(SOCKET s)
{
    for (int i = 0; i < 600 && !g_hostModsReady; i++) Sleep(100);
    EnterCriticalSection(&g_lcs);
    std::vector<ModEntry> mods = g_hostMods;
    LeaveCriticalSection(&g_lcs);
    size_t i = 0;
    do {
        Wr w;
        w.u8(M_MODS);
        size_t n = min<size_t>(100, mods.size() - i);
        w.u8(i + n >= mods.size() ? 1 : 0);
        w.u16((int)n);
        for (size_t k = 0; k < n; k++) { const ModEntry &m = mods[i + k]; w.u16((int)m.rel.size()); w.d += m.rel; w.u32(m.size); w.u32(m.hash); }
        EnterCriticalSection(&g_lcs);
        SendMsg(s, w);
        LeaveCriticalSection(&g_lcs);
        i += n;
    } while (i < mods.size());
}
// Hote : un fichier demande, en morceaux de 32 Ko (chaque envoi sous g_lcs : l'etat du salon passe entre deux).
static void SendModFile(SOCKET s, int index)
{
    EnterCriticalSection(&g_lcs);
    std::string rel = index >= 0 && index < (int)g_hostMods.size() ? g_hostMods[index].rel : "";
    LeaveCriticalSection(&g_lcs);
    bool ok = false;
    HANDLE f = rel.empty() ? INVALID_HANDLE_VALUE : CreateFileW((ModsDir() + Widen(rel, CP_ACP)).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        static char buf[32768];
        DWORD n;
        uint32_t off = 0;
        ok = true;
        while (ok && ReadFile(f, buf, sizeof(buf), &n, NULL) && n) {
            Wr w;
            w.u8(M_FILEDATA); w.u16(index); w.u32(off);
            w.d.append(buf, n);
            EnterCriticalSection(&g_lcs);
            ok = SendMsg(s, w);
            LeaveCriticalSection(&g_lcs);
            off += n;
        }
        CloseHandle(f);
    }
    Wr e;
    e.u8(M_FILEEND); e.u16(index); e.u8(ok ? 1 : 0);
    EnterCriticalSection(&g_lcs);
    SendMsg(s, e);
    LeaveCriticalSection(&g_lcs);
    TestLog("mods : %s envoye au joueur (%s)", rel.c_str(), ok ? "ok" : "echec");
}

// Invite : apres le manifeste, ce qui manque ; puis un fichier a la fois.
static std::vector<int> g_need;
static size_t g_needPos;
static HANDLE g_partFile = INVALID_HANDLE_VALUE;
static void WriteModsList()
{
    CreateDirectoryW((g_gameDir + L"SACoop\\cache").c_str(), NULL);
    FILE *f = _wfopen((g_gameDir + L"SACoop\\cache\\mods-liste.txt").c_str(), L"w");
    if (!f) return;
    for (auto &m : g_wantMods) fprintf(f, "%s\n", m.rel.c_str());
    fclose(f);
}
static void RequestNextMod()
{
    if (g_needPos >= g_need.size()) {
        WriteModsList();
        g_modsState = MS_READY;
        TestLog("mods : a jour (%d fichiers de l'hote)", (int)g_wantMods.size());
        return;
    }
    const ModEntry &m = g_wantMods[g_need[g_needPos]];
    std::wstring path = ModsDir() + Widen(m.rel, CP_ACP);
    CreateDirectoryW(ModsDir().c_str(), NULL);
    for (size_t p = path.find(L'\\', ModsDir().size()); p != std::wstring::npos; p = path.find(L'\\', p + 1))   // sous-dossiers
        CreateDirectoryW(path.substr(0, p).c_str(), NULL);
    g_partFile = CreateFileW((path + L".part").c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    Wr w; w.u8(M_GETFILE); w.u16(g_need[g_needPos]);
    GuestSend(w);
}
static void OnModsMessage(int type, Rd &q)
{
    if (type == M_MODS) {
        int last = q.u8(), n = q.u16();
        for (int k = 0; k < n && q.ok; k++) {
            int len = q.u16();
            if (q.p + len > q.d.size()) { q.ok = false; break; }
            ModEntry m;
            m.rel = q.d.substr(q.p, len); q.p += len;
            m.size = q.u32(); m.hash = q.u32();
            if (q.ok && SafeRel(m.rel)) g_wantMods.push_back(m);
        }
        if (!last) return;
        g_need.clear();
        g_needPos = 0;
        uint64_t total = 0;
        for (size_t i = 0; i < g_wantMods.size(); i++) {
            uint32_t size = 0;
            std::wstring p = ModsDir() + Widen(g_wantMods[i].rel, CP_ACP);
            WIN32_FILE_ATTRIBUTE_DATA a;
            bool same = GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a) && a.nFileSizeLow == g_wantMods[i].size &&
                        FnvFile(p, &size) == g_wantMods[i].hash;
            if (!same) { g_need.push_back((int)i); total += g_wantMods[i].size; }
        }
        g_modsCount = (int)g_wantMods.size();
        g_modsTotal = total;
        g_modsDone = 0;
        TestLog("mods : manifeste de l'hote, %d fichiers, %d a recevoir (%llu octets)", (int)g_wantMods.size(), (int)g_need.size(), (unsigned long long)total);
        g_modsState = g_need.empty() ? MS_READY : MS_DOWNLOAD;
        if (g_need.empty()) WriteModsList(); else RequestNextMod();
    } else if (type == M_FILEDATA) {
        q.u16(); q.u32();
        if (!q.ok || g_partFile == INVALID_HANDLE_VALUE) return;
        DWORD n = (DWORD)(q.d.size() - q.p), w = 0;
        WriteFile(g_partFile, q.d.data() + q.p, n, &w, NULL);
        g_modsDone += n;
    } else if (type == M_FILEEND) {
        int index = q.u16(), ok = q.u8();
        if (g_partFile != INVALID_HANDLE_VALUE) { CloseHandle(g_partFile); g_partFile = INVALID_HANDLE_VALUE; }
        if (g_needPos >= g_need.size() || index != g_need[g_needPos]) return;
        std::wstring path = ModsDir() + Widen(g_wantMods[index].rel, CP_ACP);
        if (!ok || !MoveFileExW((path + L".part").c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW((path + L".part").c_str());
            g_modsState = MS_FAILED;
            TestLog("mods : echec de %s", g_wantMods[index].rel.c_str());
            return;
        }
        g_needPos++;
        RequestNextMod();
    }
}
static void ModsReset()
{
    g_wantMods.clear();
    g_need.clear();
    g_needPos = 0;
    if (g_partFile != INVALID_HANDLE_VALUE) { CloseHandle(g_partFile); g_partFile = INVALID_HANDLE_VALUE; }
    g_modsState = MS_WAIT;
    g_modsTotal = g_modsDone = 0;
    g_modsCount = 0;
}
static std::wstring ModsLine()
{
    wchar_t b[160];
    if (g_lobby == LB_HOST) {
        if (!g_hostModsReady) return T(L"Mods partag\u00E9s : lecture\u2026", L"Shared mods: reading\u2026");
        EnterCriticalSection(&g_lcs);
        int n = (int)g_hostMods.size();
        uint64_t bytes = g_hostModsBytes;
        LeaveCriticalSection(&g_lcs);
        if (!n) return T(L"Mods partag\u00E9s : aucun (dossier SACoop\\mods)", L"Shared mods: none (SACoop\\mods folder)");
        swprintf_s(b, T(L"Mods partag\u00E9s : %d fichiers (%.1f Mo), envoy\u00E9s aux invit\u00E9s", L"Shared mods: %d files (%.1f MB), sent to the guests"), n, bytes / 1048576.0);
        return b;
    }
    switch ((int)g_modsState) {
    case MS_WAIT: return T(L"Mods de l'h\u00F4te : en attente\u2026", L"Host's mods: waiting\u2026");
    case MS_DOWNLOAD:
        swprintf_s(b, T(L"Mods de l'h\u00F4te : t\u00E9l\u00E9chargement %.1f / %.1f Mo", L"Host's mods: downloading %.1f / %.1f MB"), g_modsDone / 1048576.0, g_modsTotal / 1048576.0);
        return b;
    case MS_READY:
        if (!g_modsCount) return T(L"Mods de l'h\u00F4te : aucun", L"Host's mods: none");
        swprintf_s(b, T(L"Mods de l'h\u00F4te : %d fichiers, \u00E0 jour", L"Host's mods: %d files, up to date"), g_modsCount);
        return b;
    case MS_FAILED: return T(L"Mods de l'h\u00F4te : t\u00E9l\u00E9chargement impossible", L"Host's mods: download failed");
    }
    return L"";
}

static SOCKET ConnectTo(const std::wstring &addr, int port, int timeoutMs)
{
    addrinfo hints = {}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(Narrow(addr).c_str(), NULL, &hints, &res) != 0 || !res) return INVALID_SOCKET;
    sockaddr_in a = *(sockaddr_in *)res->ai_addr;
    freeaddrinfo(res);
    a.sin_port = htons((u_short)port);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    connect(s, (sockaddr *)&a, sizeof(a));
    fd_set wr, ex;
    FD_ZERO(&wr); FD_SET(s, &wr);
    FD_ZERO(&ex); FD_SET(s, &ex);
    timeval tv = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    int err = 0, len = sizeof(err);
    if (select(0, NULL, &wr, &ex, &tv) <= 0 || !FD_ISSET(s, &wr) || getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0 || err) { closesocket(s); return INVALID_SOCKET; }
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    DWORD to = 30000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&to, sizeof(to));
    return s;
}
static DWORD WINAPI GuestThread(void *)
{
    SOCKET s = ConnectTo(g_lobbyAddr, g_lobbyPort, 5000);
    if (s == INVALID_SOCKET) { TestLog("salon : pas de salon chez l'hote"); PostMessageW(g_wnd, WM_APP_LOBBYEND, 2, 0); return 0; }
    Wr hello;
    hello.u8(M_HELLO); hello.u8(LB_PROTO); hello.str(Narrow(g_localVer, CP_UTF8)); hello.str(MyName()); hello.str(MySkin());
    std::string m;
    if (!SendAllS(s, "SAL1", 4) || !SendMsg(s, hello) || !RecvMsg(s, m)) { closesocket(s); PostMessageW(g_wnd, WM_APP_LOBBYEND, 2, 0); return 0; }
    Rd r(m);
    int t = r.u8();
    if (t == M_REJECT) {
        static std::string why;
        why = r.str();
        closesocket(s);
        TestLog("salon : refuse (%s)", why.c_str());
        PostMessageW(g_wnd, WM_APP_LOBBYEND, 1, (LPARAM)why.c_str());
        return 0;
    }
    if (t != M_WELCOME) { closesocket(s); PostMessageW(g_wnd, WM_APP_LOBBYEND, 2, 0); return 0; }
    g_myId = r.u8();
    EnterCriticalSection(&g_lcs);
    g_guestSock = s;
    LeaveCriticalSection(&g_lcs);
    ModsReset();
    g_lobby = LB_GUEST;
    SetStatus(K_OK, T(L"Dans le salon de %s", L"In %s's lobby"), g_lobbyAddr.c_str());
    TestLog("salon : entre (joueur %d)", g_myId);
    DWORD to = 60000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof(to));
    bool go = false;
    while (!go && RecvMsg(s, m)) {
        Rd q(m);
        int type = q.u8();
        if (type == M_STATE) {
            int n = q.u8();
            std::vector<LobbyPeer> peers;
            for (int i = 0; i < n; i++) {
                LobbyPeer p;
                p.id = q.u8(); p.name = q.str(); p.skin = q.str(); p.ready = q.u8() != 0; p.ping = q.u16();
                peers.push_back(p);
            }
            std::string label = q.str();
            if (q.ok) { EnterCriticalSection(&g_lcs); g_peers = peers; g_lobbyChoiceLabel = label; LeaveCriticalSection(&g_lcs); }
        } else if (type == M_PING) {
            Wr w; w.u8(M_PONG); w.u32(q.u32());
            GuestSend(w);
        } else if (type == M_MODS || type == M_FILEDATA || type == M_FILEEND) {
            OnModsMessage(type, q);
        } else if (type == M_GO) {
            go = true;
            TestLog("salon : GO recu");
            PostMessageW(g_wnd, WM_APP_GO, q.u8(), 0);
        }
    }
    EnterCriticalSection(&g_lcs);
    g_guestSock = INVALID_SOCKET;
    LeaveCriticalSection(&g_lcs);
    closesocket(s);
    if (!go) PostMessageW(g_wnd, WM_APP_LOBBYEND, 0, 0);
    return 0;
}
static void LobbyJoin()
{
    std::wstring addr = Trim(g_fields[1].text);
    if (addr.empty()) { SetStatus(K_ERR, T(L"Entre l'adresse de l'h\u00F4te", L"Enter the host address")); g_focus = 1; return; }
    size_t colon = addr.find(L':');   // adresse:port accepte
    g_lobbyPort = LobbyPort();
    g_lobbyAddr = addr;
    if (colon != std::wstring::npos) { g_lobbyAddr = addr.substr(0, colon); g_lobbyPort = _wtoi(addr.c_str() + colon + 1); }
    SavePlayer();
    g_meReady = false;
    EnterCriticalSection(&g_lcs);
    g_peers.clear();
    g_lobbyChoiceLabel.clear();
    LeaveCriticalSection(&g_lcs);
    g_lobby = LB_CONNECTING;
    g_tab = TAB_LOBBY;
    LayoutTabs();
    SetStatus(K_NORMAL, T(L"Connexion au salon de %s\u2026", L"Connecting to %s's lobby\u2026"), g_lobbyAddr.c_str());
    HANDLE t = CreateThread(NULL, 0, GuestThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
static void GuestToggleReady()
{
    if (g_modsState != MS_READY) return;   // mods de l'hote pas encore recus
    g_meReady = !g_meReady;
    Wr w; w.u8(M_READY); w.u8(g_meReady ? 1 : 0);
    GuestSend(w);
}

// Toutes les 2 s, l'hote mesure le ping de chacun.
static void LobbyTick()
{
    static DWORD last;
    DWORD now = GetTickCount();
    if (g_lobby != LB_HOST || now - last < 2000) return;
    last = now;
    Wr w; w.u8(M_PING); w.u32(now);
    EnterCriticalSection(&g_lcs);
    for (auto &c : g_conns) SendMsg(c.s, w);
    LeaveCriticalSection(&g_lcs);
    BroadcastState();
}

static void LobbySoundsTick()
{
    static std::vector<std::pair<int, bool>> prev;
    static bool had;
    std::vector<std::pair<int, bool>> now;
    bool in = g_lobby == LB_HOST || g_lobby == LB_GUEST;
    if (in) {
        EnterCriticalSection(&g_lcs);
        for (auto &p : g_peers) now.push_back({ p.id, p.ready });
        LeaveCriticalSection(&g_lcs);
    }
    if (!had || !in) { prev = now; had = in; return; }
    int sound = -1;
    for (auto &n : now) {
        bool found = false;
        for (auto &o : prev) if (o.first == n.first) { found = true; if (o.second != n.second && sound < 0) sound = n.second ? SND_READY : SND_UNREADY; }
        if (!found) sound = SND_JOIN;
    }
    for (auto &o : prev) {
        bool still = false;
        for (auto &n : now) still |= n.first == o.first;
        if (!still && sound != SND_JOIN) sound = SND_LEAVE;
    }
    prev = now;
    if (sound >= 0 && g_testSalonLog.empty() && g_testLog.empty()) LobbySound(sound);   // (modes de test : silencieux)
}

// --- dessin du salon (panneau de droite)
static const Color kPlayerCol[4] = { Color(255, 245, 245, 245), Color(255, 200, 200, 200), Color(255, 155, 155, 155), Color(255, 115, 115, 115) };   // gris : lanceur noir et blanc
static const RectF kChoiceR(456, 452, 480, 30);

static void DrawLobby(Graphics &g)
{
    std::vector<LobbyPeer> peers;
    std::string choice;
    EnterCriticalSection(&g_lcs);
    peers = g_peers;
    choice = ChoiceLabel();
    LeaveCriticalSection(&g_lcs);
    int lobby = g_lobby;
    DrawPanel(g);

    wchar_t head[96];
    swprintf_s(head, T(L"%d / 4 joueurs", L"%d / 4 players"), (int)peers.size());
    Text(g, T(L"SALON", L"LOBBY"), RectF(460, 126, 200, 26), 17, FontStyleBold, kInk, StringAlignmentNear);
    Text(g, head, RectF(700, 126, 232, 26), 13, FontStyleBold, kGrey, StringAlignmentFar);
    std::wstring sub;
    if (lobby == LB_HOST) sub = std::wstring(T(L"Adresse \u00E0 donner : ", L"Address to share: ")) + (g_myAddresses.empty() ? L"?" : g_myAddresses) + L" \u00B7 port " + std::to_wstring(g_lobbyPort);
    else sub = std::wstring(T(L"H\u00F4te : ", L"Host: ")) + g_lobbyAddr + L":" + std::to_wstring(g_lobbyPort);
    Text(g, sub, RectF(460, 150, 476, 20), 11.5f, FontStyleRegular, kGrey, StringAlignmentNear);

    if (lobby == LB_CONNECTING) {
        Text(g, T(L"Connexion au salon\u2026", L"Connecting to the lobby\u2026"), RectF(460, 280, 476, 30), 16, FontStyleBold, kInk);
        DrawBar(g, RectF(560, 320, 276, 5), -2);
    }
    for (int i = 0; i < 4 && lobby != LB_CONNECTING; i++) {
        RectF r(456, 178 + i * 64.0f, 480, 56);
        GraphicsPath rp;
        RoundRect(rp, r, 12);
        if (i >= (int)peers.size()) {
            Pen dash(WithA(kGrey, 0.6f), 1.4f);
            dash.SetDashStyle(DashStyleDash);
            g.DrawPath(&dash, &rp);
            Text(g, T(L"En attente d'un joueur\u2026", L"Waiting for a player\u2026"), r, 12.5f, FontStyleRegular, WithA(kGrey, 0.8f));
            continue;
        }
        const LobbyPeer &p = peers[i];
        bool me = p.id == g_myId;
        SolidBrush rb(me ? TH(tabHot) : TH(tab));
        g.FillPath(&rb, &rp);
        Pen rpen(me ? kInk : WithA(kGrey, 0.5f), 1.2f);
        g.DrawPath(&rpen, &rp);
        // pastille a la couleur du joueur en jeu, avec son initiale
        std::wstring nm = Widen(p.name, CP_UTF8);
        RectF av(r.X + 10, r.Y + 8, 40, 40);
        SolidBrush ab(kPlayerCol[p.id & 3]);
        g.FillEllipse(&ab, av);
        Text(g, nm.empty() ? L"?" : nm.substr(0, 1), av, 18, FontStyleBold, Color(255, 20, 20, 20));
        Text(g, nm, RectF(r.X + 60, r.Y + 7, 230, 22), 15, FontStyleBold, kInk, StringAlignmentNear);
        std::wstring line;
        if (p.id == 0) line = T(L"H\u00F4te", L"Host");
        else line = p.ready ? T(L"Pr\u00EAt \u2713", L"Ready \u2713") : T(L"Pas pr\u00EAt", L"Not ready");
        Text(g, line, RectF(r.X + 60, r.Y + 29, 90, 20), 12, FontStyleBold, p.id == 0 || p.ready ? kInk : kGrey, StringAlignmentNear);
        Text(g, Widen(p.skin, CP_UTF8), RectF(r.X + 160, r.Y + 29, 140, 20), 12, FontStyleRegular, kGrey, StringAlignmentNear);
        if (p.id != 0) {
            wchar_t pb[32];
            swprintf_s(pb, L"%d ms", p.ping);
            Text(g, pb, RectF(r.X + r.Width - 90, r.Y + 8, 78, 40), 12, FontStyleRegular, kGrey, StringAlignmentFar);
        }
    }
    // partie
    Text(g, T(L"PARTIE", L"GAME"), RectF(460, 432, 200, 18), 10.5f, FontStyleBold, kGrey, StringAlignmentNear);
    GraphicsPath cp;
    RoundRect(cp, kChoiceR, 15);
    SolidBrush cb(TH(tab));
    g.FillPath(&cb, &cp);
    Pen cpen(WithA(kGrey, 0.6f), 1.2f);
    g.DrawPath(&cpen, &cp);
    bool host = lobby == LB_HOST;
    if (host && (int)g_saves.size() > 0) {
        Text(g, L"\u2039", RectF(kChoiceR.X + 6, kChoiceR.Y - 2, 20, kChoiceR.Height), 20, FontStyleBold, kInk);
        Text(g, L"\u203A", RectF(kChoiceR.X + kChoiceR.Width - 26, kChoiceR.Y - 2, 20, kChoiceR.Height), 20, FontStyleBold, kInk);
    }
    Text(g, choice.empty() ? L"\u2026" : Widen(choice, CP_UTF8), RectF(kChoiceR.X + 28, kChoiceR.Y, kChoiceR.Width - 56, kChoiceR.Height), 13, FontStyleBold, kInk);
    Text(g, ModsLine(), RectF(460, 490, 476, 18), 11.5f, g_modsState == MS_FAILED && !host ? FontStyleBold : FontStyleRegular, kGrey, StringAlignmentNear);
    if (!host && g_modsState == MS_DOWNLOAD && g_modsTotal > 0) DrawBar(g, RectF(460, 512, 476, 5), (float)((double)g_modsDone / (double)g_modsTotal));
    Pen sep(WithA(kGrey, 0.4f), 1);
    g.DrawLine(&sep, kOptPanel.X + 18, 532.0f, kOptPanel.X + kOptPanel.Width - 18, 532.0f);
    const wchar_t *hint = host ? T(L"Quand tout le monde est pr\u00EAt, \u00AB Lancer \u00BB d\u00E9marre le jeu de chacun ; les invit\u00E9s suivent ta partie.",
                                   L"Once everyone is ready, \"Start\" launches everyone's game; the guests follow your game.")
                               : T(L"Clique sur \u00AB Pr\u00EAt \u00BB. Ton jeu d\u00E9marre tout seul quand l'h\u00F4te lance la partie.",
                                   L"Click \"Ready\". Your game starts by itself when the host starts the session.");
    FontFamily fam(L"Segoe UI");
    Font font(&fam, 12, FontStyleRegular, UnitPixel);
    StringFormat sf;
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush db(kGrey);
    g.DrawString(hint, -1, &font, RectF(kOptPanel.X + 20, 536, kOptPanel.Width - 40, 44), &sf, &db);
}

static bool LobbyClick(float x, float y)
{
    if (g_lobby != LB_HOST || !kChoiceR.Contains(x, y) || g_saves.empty()) return kOptPanel.Contains(x, y);
    int n = (int)g_saves.size() + 1;
    int dir = x < kChoiceR.X + kChoiceR.Width / 2 ? -1 : 1;
    EnterCriticalSection(&g_lcs);
    g_lobbyChoice = (g_lobbyChoice + dir + n) % n;
    LeaveCriticalSection(&g_lcs);
    BroadcastState();
    return true;
}

// /testsalon hote|invite : l'hote ouvre le salon et lance des que tout le monde est pret ; l'invite rejoint et se met
// pret. Abandon au bout de 90 s.
static void TestSalonStep()
{
    static DWORD start = GetTickCount(), allReadySince;
    DWORD t = GetTickCount() - start;
    if (g_state != ST_IDLE || g_goWait) return;
    if (t > 90000) { TestLog("test : abandon (90 s)"); DestroyWindow(g_wnd); return; }
    if (g_testSalon == L"hote") {
        if (g_lobby == LB_NONE && t > 1500) { LobbyHost(); if (!g_saves.empty()) g_lobbyChoice = (int)g_saves.size(); }   // (la derniere : test du chargement)
        EnterCriticalSection(&g_lcs);
        size_t n = g_peers.size();
        LeaveCriticalSection(&g_lcs);
        bool can = n >= 2 && LobbyCanStart();
        if (!can) allReadySince = 0;
        else if (!allReadySince) allReadySince = GetTickCount();
        else if (GetTickCount() - allReadySince > 2000) HostStart();
    } else {
        static bool tried;
        if (g_lobby == LB_NONE && t > 3000 && !tried) { tried = true; LobbyJoin(); }
        if (g_lobby == LB_GUEST && !g_meReady && t > 6000 && g_modsState == MS_READY) { TestLog("test : pret"); GuestToggleReady(); }
    }
}

bool LobbyCanStartPublic() { return LobbyCanStart(); }
bool GuestModsReady() { return g_modsState == MS_READY; }

// ---------------------------------------------------------------- onglet JOURNAUX (porte de VCCoop)
// Les parties gardees par le mod dans <jeu>\logs (sacoop-AAAA-MM-JJ_HH-MM-SS.log, les 50 derniers) : liste (date,
// duree, version, plantage), ouverture d'un clic, dossier, suppression (un 2e clic confirme).
struct LogEntry { std::wstring name; uint64_t bytes; std::wstring ver; DWORD seconds; bool crash; };
static std::vector<LogEntry> g_logList;
static int g_logRowHot = -1, g_logPart = 0;          // g_logPart : 0 la ligne (ouvrir), 1 dossier, 2 corbeille
static std::wstring g_logArm;                         // corbeille armee : nom du journal, "*" pour tout supprimer
static DWORD g_logArmT;
static const RectF kLogsFolderR(826, 124, 110, 22), kLogsAllR(696, 124, 120, 22), kLogsR(452, 152, 488, 374);
static const float kLogRowH = 50;

static std::wstring LogsDir() { return g_gameDir + L"logs\\"; }

static void LogInfo(const std::wstring &path, LogEntry &e)
{
    e.crash = false; e.seconds = 0;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    // En entier jusqu'a 4 Mo ; au-dela, le debut et les 256 derniers Ko (le plantage est ecrit a la fin).
    std::string head, tail;
    DWORD r = 0;
    if (e.bytes <= (4u << 20)) {
        head.resize((size_t)e.bytes);
        if (!head.empty()) ReadFile(f, &head[0], (DWORD)head.size(), &r, NULL);
        head.resize(r);
        tail = head;
    } else {
        head.resize(4096);
        ReadFile(f, &head[0], 4096, &r, NULL); head.resize(r);
        LARGE_INTEGER at; at.QuadPart = (LONGLONG)e.bytes - (256 << 10);
        SetFilePointerEx(f, at, NULL, FILE_BEGIN);
        tail.resize(256 << 10);
        ReadFile(f, &tail[0], (DWORD)tail.size(), &r, NULL); tail.resize(r);
        e.crash = head.find("PLANTAGE") != std::string::npos;
    }
    CloseHandle(f);
    e.crash = e.crash || tail.find("PLANTAGE") != std::string::npos;
    size_t v = head.find("] SACoop ");
    if (v != std::string::npos) {
        v += 9;
        size_t end = head.find_first_of(" \r\n", v);
        std::string ver = head.substr(v, end == std::string::npos ? 0 : end - v);
        if (ver.size() < 24) e.ver.assign(ver.begin(), ver.end());
    }
    unsigned long t0 = 0, t1 = 0;
    if (sscanf_s(head.c_str(), "[%lu]", &t0) == 1) {
        size_t nl = tail.size() > 1 ? tail.rfind("\n[", tail.size() - 2) : std::string::npos;
        if (nl != std::string::npos && sscanf_s(tail.c_str() + nl + 1, "[%lu]", &t1) == 1 && t1 >= t0) e.seconds = (t1 - t0) / 1000;
    }
}

static void LogsScan()
{
    std::vector<LogEntry> list;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((LogsDir() + L"sacoop-*.log").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            LogEntry e;
            e.name = fd.cFileName;
            e.bytes = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            LogInfo(LogsDir() + e.name, e);
            list.push_back(e);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // noms horodates : l'ordre alphabetique est chronologique ; la plus recente en haut
    std::sort(list.begin(), list.end(), [](const LogEntry &a, const LogEntry &b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) > 0; });
    g_logList.swap(list);
    g_logArm.clear();
    g_scroll[TAB_LOGS] = min(g_scroll[TAB_LOGS], LogsMaxScroll());
}

static float LogsMaxScroll() { return max(0.0f, g_logList.size() * kLogRowH - kLogsR.Height); }

static std::wstring LogDate(const std::wstring &name)
{
    int y, mo, d, hh, mi, ss;
    if (swscanf_s(name.c_str(), L"sacoop-%d-%d-%d_%d-%d-%d", &y, &mo, &d, &hh, &mi, &ss) != 6) return name;
    SYSTEMTIME st = {};
    st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d;
    FILETIME ft; SystemTimeToFileTime(&st, &ft); FileTimeToSystemTime(&ft, &st);   // (jour de la semaine)
    static const wchar_t *jfr[] = { L"dim.", L"lun.", L"mar.", L"mer.", L"jeu.", L"ven.", L"sam." }, *jen[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
    wchar_t b[64];
    if (g_fr) swprintf_s(b, L"%s %02d/%02d/%04d \u00B7 %02dh%02d", jfr[st.wDayOfWeek % 7], d, mo, y, hh, mi);
    else swprintf_s(b, L"%s %04d-%02d-%02d \u00B7 %02d:%02d", jen[st.wDayOfWeek % 7], y, mo, d, hh, mi);
    return b;
}

static bool LogArmed(const std::wstring &key) { return !g_logArm.empty() && g_logArm == key && GetTickCount() - g_logArmT < 4000; }

static void DrawIconCircle(Graphics &g, RectF c, bool hot, bool armed, int icon)
{
    if (armed) { SolidBrush ab(Color(255, 214, 48, 72)); g.FillEllipse(&ab, c); }
    else { SolidBrush cb(hot ? TH(circleHot) : TH(circle)); g.FillEllipse(&cb, c); }
    Color ic = armed ? Color(255, 255, 255, 255) : hot ? kInk : kInk;
    Pen pen(ic, 1.4f);
    pen.SetLineJoin(LineJoinRound); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
    float cx = c.X + c.Width / 2, cy = c.Y + c.Height / 2;
    if (icon == 1) {   // dossier
        PointF p[] = { PointF(cx - 7, cy - 4.5f), PointF(cx - 2.5f, cy - 4.5f), PointF(cx - 1, cy - 2.5f), PointF(cx + 7, cy - 2.5f),
                       PointF(cx + 7, cy + 5), PointF(cx - 7, cy + 5) };
        g.DrawPolygon(&pen, p, 6);
    } else {           // corbeille
        g.DrawLine(&pen, cx - 6.5f, cy - 4, cx + 6.5f, cy - 4);
        g.DrawLine(&pen, cx - 2, cy - 6, cx + 2, cy - 6);
        PointF p[] = { PointF(cx - 5, cy - 4), PointF(cx - 4, cy + 6), PointF(cx + 4, cy + 6), PointF(cx + 5, cy - 4) };
        g.DrawLines(&pen, p, 4);
        g.DrawLine(&pen, cx - 1.5f, cy - 1, cx - 1.5f, cy + 3.5f);
        g.DrawLine(&pen, cx + 1.5f, cy - 1, cx + 1.5f, cy + 3.5f);
    }
}

static RectF LogRowRect(int i) { return RectF(kLogsR.X, kLogsR.Y + i * kLogRowH - g_scroll[TAB_LOGS], kLogsR.Width - 10, kLogRowH - 6); }
static RectF LogIconRect(const RectF &r, int part) { return RectF(r.X + r.Width - (part == 2 ? 34.0f : 64.0f), r.Y + 8, 26, 26); }

static void DrawLogs(Graphics &g)
{
    GraphicsPath pp;
    RoundRect(pp, kOptPanel, 18);
    SolidBrush bg(TH(panel));
    g.FillPath(&bg, &pp);
    Pen border(TH(panelBorder), 1.5f);
    g.DrawPath(&border, &pp);
    Text(g, T(L"JOURNAUX", L"LOGS"), RectF(460, 122, 120, 26), 17, FontStyleBold, kInk, StringAlignmentNear);
    uint64_t total = 0;
    int crashes = 0;
    for (auto &e : g_logList) { total += e.bytes; crashes += e.crash; }
    wchar_t cnt[96];
    swprintf_s(cnt, T(L"%d \u00B7 %.1f Mo", L"%d \u00B7 %.1f MB"), (int)g_logList.size(), total / 1048576.0);
    Text(g, cnt, RectF(572, 122, 120, 26), 12.5f, FontStyleBold, kGrey, StringAlignmentNear);
    Text(g, T(L"Ouvrir le dossier", L"Open folder"), kLogsFolderR, 12, FontStyleUnderline, kInk, StringAlignmentFar);
    if (!g_logList.empty()) {
        bool armed = LogArmed(L"*");
        Text(g, armed ? T(L"Confirmer ?", L"Confirm?") : T(L"Tout supprimer", L"Delete all"), kLogsAllR, 12,
             armed ? FontStyleBold | FontStyleUnderline : FontStyleUnderline, armed ? Color(255, 214, 48, 72) : kGrey, StringAlignmentFar);
    }

    if (g_logList.empty())
        Text(g, T(L"Aucun journal pour l'instant : chaque partie en \u00E9crit un ici.", L"No logs yet: every game session writes one here."), kLogsR, 13, FontStyleRegular, kGrey);
    float sc = g_scroll[TAB_LOGS];
    g.SetClip(kLogsR);
    for (int i = 0; i < (int)g_logList.size(); i++) {
        const LogEntry &e = g_logList[i];
        RectF r = LogRowRect(i);
        if (r.Y + r.Height < kLogsR.Y || r.Y > kLogsR.Y + kLogsR.Height) continue;
        bool hot = i == g_logRowHot;
        GraphicsPath rp;
        RoundRect(rp, r, 10);
        SolidBrush rb(hot && g_logPart == 0 ? TH(cardSel) : TH(card));
        g.FillPath(&rb, &rp);
        Pen rpen(hot ? kInk : TH(choiceBorder), 1.2f);
        g.DrawPath(&rpen, &rp);
        // pastille : grise partie terminee, rouge plantage
        SolidBrush dot(e.crash ? Color(255, 214, 48, 72) : kGrey);
        g.FillEllipse(&dot, r.X + 12, r.Y + r.Height / 2 - 4, 8.0f, 8.0f);
        Text(g, LogDate(e.name), RectF(r.X + 28, r.Y + 4, r.Width - 170, 20), 12.5f, FontStyleBold, kInk, StringAlignmentNear);
        wchar_t info[128], dur[32];
        if (e.seconds >= 3600) swprintf_s(dur, L"%u h %02u", e.seconds / 3600, e.seconds / 60 % 60);
        else if (e.seconds >= 60) swprintf_s(dur, L"%u min", e.seconds / 60);
        else swprintf_s(dur, L"%u s", e.seconds);
        if (e.bytes < 1048576) swprintf_s(info, L"%s \u00B7 %.0f %s%s%s", dur, e.bytes / 1024.0, T(L"Ko", L"KB"), e.ver.empty() ? L"" : L" \u00B7 v", e.ver.c_str());
        else swprintf_s(info, L"%s \u00B7 %.1f %s%s%s", dur, e.bytes / 1048576.0, T(L"Mo", L"MB"), e.ver.empty() ? L"" : L" \u00B7 v", e.ver.c_str());
        Text(g, info, RectF(r.X + 28, r.Y + 23, r.Width - 170, 18), 10.5f, FontStyleRegular, kGrey, StringAlignmentNear);
        if (e.crash || i == 0) {   // etiquette : plantage, ou derniere partie
            const wchar_t *lab = e.crash ? T(L"PLANTAGE", L"CRASH") : T(L"DERNI\u00C8RE", L"LATEST");
            float lw = 14 + 6.6f * (float)wcslen(lab);
            RectF br(r.X + r.Width - 74 - lw, r.Y + 13, lw, 17);
            GraphicsPath bp; RoundRect(bp, br, 8.5f);
            SolidBrush bb(e.crash ? Color(45, 214, 48, 72) : WithA(kGrey, 0.18f));
            g.FillPath(&bb, &bp);
            Text(g, lab, br, 9.5f, FontStyleBold, e.crash ? Color(255, 214, 48, 72) : kGrey);
        }
        DrawIconCircle(g, LogIconRect(r, 1), hot && g_logPart == 1, false, 1);
        DrawIconCircle(g, LogIconRect(r, 2), hot && g_logPart == 2, LogArmed(e.name), 2);
    }
    g.ResetClip();
    float ms = LogsMaxScroll();
    if (ms > 0) {
        float h = kLogsR.Height * kLogsR.Height / (kLogsR.Height + ms), y = kLogsR.Y + (kLogsR.Height - h) * sc / ms;
        GraphicsPath sp; RoundRect(sp, RectF(kLogsR.X + kLogsR.Width - 4, y, 4, h), 2);
        SolidBrush sb(WithA(kInk, 0.4f)); g.FillPath(&sb, &sp);
    }
    Pen sep(TH(sep), 1);
    g.DrawLine(&sep, kOptPanel.X + 18, 536.0f, kOptPanel.X + kOptPanel.Width - 18, 536.0f);
    FontFamily fam(L"Segoe UI");
    Font font(&fam, 12, FontStyleRegular, UnitPixel);
    StringFormat sf;
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush db(kGrey);
    wchar_t hint[400];
    swprintf_s(hint, T(L"Un clic ouvre le journal. Un souci en jeu : envoyez celui de la partie concern\u00E9e%s. Les 50 derniers sont gard\u00E9s.",
                       L"Click a log to open it. Trouble in game: send the one from that session%s. The last 50 are kept."),
              crashes ? T(L" (rouge : le jeu a plant\u00E9)", L" (red: the game crashed)") : L"");
    g.DrawString(hint, -1, &font, RectF(kOptPanel.X + 20, 540, kOptPanel.Width - 40, 42), &sf, &db);
}

static int LogRowAt(float x, float y, int *part)
{
    *part = 0;
    if (!kLogsR.Contains(x, y)) return -1;
    int i = (int)((y - kLogsR.Y + g_scroll[TAB_LOGS]) / kLogRowH);
    if (i < 0 || i >= (int)g_logList.size()) return -1;
    RectF r = LogRowRect(i);
    if (!r.Contains(x, y)) return -1;
    RectF f = LogIconRect(r, 1), t = LogIconRect(r, 2);
    f.Inflate(2, 2); t.Inflate(2, 2);
    *part = f.Contains(x, y) ? 1 : t.Contains(x, y) ? 2 : 0;
    return i;
}

static void LogsDelete(bool all, const std::wstring &one)
{
    int done = 0, locked = 0;
    for (auto &e : g_logList) {
        if (!all && e.name != one) continue;
        if (DeleteFileW((LogsDir() + e.name).c_str())) done++; else locked++;
    }
    LogsScan();
    if (locked) SetStatus(K_WARN, T(L"%d journal(aux) supprim\u00E9(s), %d en cours d'utilisation (jeu lanc\u00E9 ?)", L"%d log(s) deleted, %d in use (game running?)"), done, locked);
    else SetStatus(K_OK, T(L"%d journal(aux) supprim\u00E9(s)", L"%d log(s) deleted"), done);
}

static bool LogsMouseDown(float x, float y)
{
    if (kLogsFolderR.Contains(x, y)) {
        CreateDirectoryW(LogsDir().c_str(), NULL);
        ShellExecuteW(g_wnd, L"open", LogsDir().c_str(), NULL, NULL, SW_SHOWNORMAL);
        return true;
    }
    if (kLogsAllR.Contains(x, y) && !g_logList.empty()) {
        if (LogArmed(L"*")) LogsDelete(true, L"");
        else { g_logArm = L"*"; g_logArmT = GetTickCount(); SetStatus(K_WARN, T(L"Cliquez encore pour supprimer les %d journaux", L"Click again to delete all %d logs"), (int)g_logList.size()); }
        return true;
    }
    int part, i = LogRowAt(x, y, &part);
    if (i < 0) return kOptPanel.Contains(x, y);
    std::wstring name = g_logList[i].name, path = LogsDir() + name;
    if (part == 2) {
        if (LogArmed(name)) LogsDelete(false, name);
        else { g_logArm = name; g_logArmT = GetTickCount(); SetStatus(K_WARN, T(L"Cliquez encore sur la corbeille pour supprimer ce journal", L"Click the bin again to delete this log")); }
    } else if (part == 1) {
        std::wstring arg = L"/select,\"" + path + L"\"";
        ShellExecuteW(g_wnd, L"open", L"explorer.exe", arg.c_str(), NULL, SW_SHOWNORMAL);
    } else if ((INT_PTR)ShellExecuteW(g_wnd, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL) <= 32) {
        std::wstring arg = L"\"" + path + L"\"";   // (.log sans programme associe)
        ShellExecuteW(g_wnd, L"open", L"notepad.exe", arg.c_str(), NULL, SW_SHOWNORMAL);
    }
    return true;
}


// ---------------------------------------------------------------- onglet MODS (comme VCCoop, sans l'apercu 3D)
// Un mod = un sous-dossier de SACoop\mods (les fichiers poses a la racine forment "(fichiers isoles)"). Interrupteur :
// le dossier passe dans SACoop\mods-off (desactive : ni charge, ni envoye aux invites) et revient.
struct ModRow { std::wstring name; int files; uint64_t bytes; bool on, root; };
static std::vector<ModRow> g_modRows;
static int g_modHot = -1, g_modPart = 0;   // g_modPart : 1 = interrupteur
static const RectF kModsFolderR(826, 124, 110, 22), kModsR(452, 152, 488, 374);
static const float kModRowH = 50;

static void CountDir(const std::wstring &dir, int &files, uint64_t &bytes)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { CountDir(dir + fd.cFileName + L"\\", files, bytes); continue; }
        size_t n = wcslen(fd.cFileName);
        if (n > 4 && !_wcsicmp(fd.cFileName + n - 4, L".txt")) continue;
        files++;
        bytes += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void ModsTabScan()
{
    std::vector<ModRow> rows;
    for (int pass = 0; pass < 2; pass++) {
        std::wstring base = g_gameDir + (pass ? L"SACoop\\mods-off\\" : L"SACoop\\mods\\");
        ModRow root = { T(L"(fichiers isol\u00E9s)", L"(loose files)"), 0, 0, true, true };
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((base + L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.cFileName[0] == L'.') continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ModRow r = { fd.cFileName, 0, 0, pass == 0, false };
                CountDir(base + fd.cFileName + L"\\", r.files, r.bytes);
                rows.push_back(r);
            } else if (pass == 0) {
                size_t n = wcslen(fd.cFileName);
                if (n > 4 && !_wcsicmp(fd.cFileName + n - 4, L".txt")) continue;
                root.files++;
                root.bytes += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (root.files) rows.push_back(root);
    }
    std::sort(rows.begin(), rows.end(), [](const ModRow &a, const ModRow &b) { return a.root != b.root ? b.root : _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    g_modRows.swap(rows);
}
static float ModsMaxScroll() { return max(0.0f, g_modRows.size() * kModRowH - kModsR.Height); }
static RectF ModRowRect(int i) { return RectF(kModsR.X, kModsR.Y + i * kModRowH - g_scroll[TAB_MODS], kModsR.Width - 10, kModRowH - 6); }
static RectF ModToggleRect(const RectF &r) { return RectF(r.X + r.Width - 58, r.Y + 12, 44, 20); }

static void DrawMods(Graphics &g)
{
    DrawPanel(g);
    int on = 0;
    for (auto &m : g_modRows) on += m.on;
    Text(g, L"MODS", RectF(460, 122, 120, 26), 17, FontStyleBold, kInk, StringAlignmentNear);
    wchar_t cnt[64];
    swprintf_s(cnt, T(L"%d actif(s) / %d", L"%d active / %d"), on, (int)g_modRows.size());
    Text(g, cnt, RectF(540, 122, 160, 26), 12.5f, FontStyleBold, kGrey, StringAlignmentNear);
    Text(g, T(L"Ouvrir le dossier", L"Open folder"), kModsFolderR, 12, FontStyleUnderline, kInk, StringAlignmentFar);
    if (g_modRows.empty())
        Text(g, T(L"Aucun mod : posez-les dans SACoop\\mods (un dossier par mod).", L"No mods: put them in SACoop\\mods (one folder per mod)."), kModsR, 13, FontStyleRegular, kGrey);
    g.SetClip(kModsR);
    for (int i = 0; i < (int)g_modRows.size(); i++) {
        const ModRow &m = g_modRows[i];
        RectF r = ModRowRect(i);
        if (r.Y + r.Height < kModsR.Y || r.Y > kModsR.Y + kModsR.Height) continue;
        bool hot = i == g_modHot;
        GraphicsPath rp;
        RoundRect(rp, r, 10);
        SolidBrush rb(hot ? TH(cardSel) : TH(card));
        g.FillPath(&rb, &rp);
        Pen rpen(m.on ? kInk : TH(choiceBorder), m.on ? 1.4f : 1.2f);
        g.DrawPath(&rpen, &rp);
        Text(g, m.name, RectF(r.X + 16, r.Y + 4, r.Width - 100, 20), 12.5f, FontStyleBold, m.on ? kInk : kGrey, StringAlignmentNear);
        wchar_t info[96];
        swprintf_s(info, T(L"%d fichier(s) \u00B7 %.1f Mo%s", L"%d file(s) \u00B7 %.1f MB%s"), m.files, m.bytes / 1048576.0,
                   m.on ? L"" : T(L" \u00B7 d\u00E9sactiv\u00E9", L" \u00B7 off"));
        Text(g, info, RectF(r.X + 16, r.Y + 23, r.Width - 100, 18), 10.5f, FontStyleRegular, kGrey, StringAlignmentNear);
        if (m.root) continue;
        RectF t = ModToggleRect(r);   // interrupteur, comme les options
        GraphicsPath tp;
        RoundRect(tp, t, t.Height / 2);
        if (m.on) { SolidBrush tb(kInk); g.FillPath(&tb, &tp); }
        else { SolidBrush tb(TH(toggleOff)); g.FillPath(&tb, &tp); }
        SolidBrush knob(m.on ? TH(panel) : Color(255, 255, 255, 255));
        g.FillEllipse(&knob, m.on ? t.X + t.Width - 18 : t.X + 2, t.Y + 2, 16.0f, 16.0f);
    }
    g.ResetClip();
    Pen sep(WithA(kGrey, 0.4f), 1);
    g.DrawLine(&sep, kOptPanel.X + 18, 532.0f, kOptPanel.X + kOptPanel.Width - 18, 532.0f);
    FontFamily fam(L"Segoe UI");
    Font font(&fam, 12, FontStyleRegular, UnitPixel);
    StringFormat sf;
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush db(kGrey);
    g.DrawString(T(L"Les mods actifs remplacent ceux du jeu et sont envoy\u00E9s aux invit\u00E9s. D\u00E9sactiv\u00E9s : rang\u00E9s dans SACoop\\mods-off.",
                   L"Active mods replace the game's files and are sent to the guests. Disabled: moved to SACoop\\mods-off."),
                 -1, &font, RectF(kOptPanel.X + 20, 536, kOptPanel.Width - 40, 44), &sf, &db);
}

static int ModRowAt(float x, float y, int *part)
{
    *part = 0;
    if (!kModsR.Contains(x, y)) return -1;
    int i = (int)((y - kModsR.Y + g_scroll[TAB_MODS]) / kModRowH);
    if (i < 0 || i >= (int)g_modRows.size() || !ModRowRect(i).Contains(x, y)) return -1;
    RectF t = ModToggleRect(ModRowRect(i));
    t.Inflate(4, 4);
    *part = !g_modRows[i].root && t.Contains(x, y) ? 1 : 0;
    return i;
}

static bool ModsMouseDown(float x, float y)
{
    if (kModsFolderR.Contains(x, y)) {
        CreateDirectoryW((g_gameDir + L"SACoop\\mods").c_str(), NULL);
        ShellExecuteW(g_wnd, L"open", (g_gameDir + L"SACoop\\mods").c_str(), NULL, NULL, SW_SHOWNORMAL);
        return true;
    }
    int part, i = ModRowAt(x, y, &part);
    if (i < 0) return kOptPanel.Contains(x, y);
    const ModRow &m = g_modRows[i];
    if (part == 1) {
        std::wstring on = g_gameDir + L"SACoop\\mods\\" + m.name, off = g_gameDir + L"SACoop\\mods-off\\" + m.name;
        CreateDirectoryW((g_gameDir + (m.on ? L"SACoop\\mods-off" : L"SACoop\\mods")).c_str(), NULL);
        if (MoveFileExW((m.on ? on : off).c_str(), (m.on ? off : on).c_str(), 0))
            SetStatus(K_OK, m.on ? T(L"%s d\u00E9sactiv\u00E9", L"%s disabled") : T(L"%s activ\u00E9", L"%s enabled"), m.name.c_str());
        else SetStatus(K_ERR, T(L"Impossible de d\u00E9placer %s (jeu lanc\u00E9 ?)", L"Could not move %s (game running?)"), m.name.c_str());
        ModsTabScan();
    } else {
        std::wstring path = g_gameDir + (m.on ? L"SACoop\\mods\\" : L"SACoop\\mods-off\\") + (m.root ? L"" : m.name);
        ShellExecuteW(g_wnd, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    return true;
}

static void OnButton(int id)
{
    switch (id) {
    case B_HOST:
        if (g_lobby == LB_HOST) HostStart();
        else if (g_lobby == LB_GUEST) GuestToggleReady();
        else if (g_lobby == LB_NONE) LobbyHost();
        break;
    case B_JOIN:
        if (g_lobby != LB_NONE) { LobbyClose(); SetStatus(K_NORMAL, L"SACoop %s", g_localVer.c_str()); }
        else if (g_joinFallback) { g_joinFallback = false; Launch(2); }
        else LobbyJoin();
        break;
    case B_EXE: ChooseExe(); break;
    case B_CLOSE: g_state = ST_CLOSING; break;
    case B_MIN: ShowWindow(g_wnd, SW_MINIMIZE); break;
    case B_LOGS: g_tab = g_tab == TAB_LOGS ? -1 : TAB_LOGS; g_optHot = -1; if (g_tab == TAB_LOGS) LogsScan(); break;
    case B_THEME: g_dark = !g_dark; WritePrivateProfileStringW(L"Lanceur", L"Theme", g_dark ? L"sombre" : L"clair", g_iniLauncher.c_str()); break;
    case B_BUY: ShellExecuteW(g_wnd, L"open", kStoreUrl, NULL, NULL, SW_SHOWNORMAL); break;
    }
}

static void Tick()
{
    static DWORD last = GetTickCount();
    DWORD now = GetTickCount();
    float dt = min((now - last) / 1000.0f, 0.1f);
    last = now;
    g_time += dt;
    LobbyTick();
    LobbySoundsTick();
    if (!g_testSalon.empty()) TestSalonStep();
    for (int i = 0; i < B_COUNT; i++) {
        float want = (g_hot == i && g_btn[i].enabled) ? 1.0f : 0.0f;
        g_btn[i].hover += (want - g_btn[i].hover) * min(dt * 12, 1.0f);
    }
    if (g_state == ST_CLOSING) {
        g_alpha -= dt * 4;
        if (g_alpha <= 0) { DestroyWindow(g_wnd); return; }
    } else if (g_alpha < 1) g_alpha = min(g_alpha + dt * 5, 1.0f);

    if (g_state == ST_LAUNCH) {
        if (WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_proc); g_proc = NULL;
            g_state = ST_IDLE;
            SetStatus(K_ERR, T(L"Le jeu s'est ferm\u00E9 au d\u00E9marrage (voir sacoop.log)", L"The game closed on startup (see sacoop.log)"));
        } else {
            bool seen = false;
            EnumWindows(FindGameWindow, (LPARAM)&seen);
            if (seen && !g_winSeenT) g_winSeenT = now;
            if ((g_winSeenT && now - g_winSeenT > 1200) || now - g_launchT > 120000) g_state = ST_CLOSING;
        }
    }
    Present();
}

// ---------------------------------------------------------------- fenetre
static int HitButton(float x, float y)
{
    for (int i = 0; i < B_COUNT; i++)
        if (g_btn[i].visible && g_btn[i].r.Contains(x, y)) return i;
    return -1;
}
static int HitField(float x, float y)
{
    if (g_state != ST_IDLE) return -1;
    for (int i = 0; i < 2; i++) if (g_fields[i].r.Contains(x, y)) return i;
    return -1;
}

static void TypeChar(wchar_t ch)
{
    if (g_focus < 0) return;
    Field &f = g_fields[g_focus];
    if (f.text.size() >= f.maxLen) return;
    if (f.address) { if (!(iswalnum(ch) && ch < 128) && ch != L'.' && ch != L':' && ch != L'-' && ch != L'_') return; }
    else if (ch < 32 || ch > 126) return;   // police du jeu : ASCII
    f.text += ch;
}

static void WriteTestLog(HWND h)
{
    FILE *f = _wfopen(g_testLog.c_str(), L"w, ccs=UTF-8");
    RECT r;
    GetWindowRect(h, &r);
    if (f) { fwprintf(f, L"ulw=%d images=%d alpha=%.2f taille=%dx%d echelle=%.2f exe=%d local=%s pid=%lu fenetre_jeu=%lu ms\n%s\n", g_ulwOk, g_frames, g_alpha,
                      r.right - r.left, r.bottom - r.top, g_scale, (int)g_exeKind, g_localVer.c_str(), g_pid,
                      g_winSeenT ? g_winSeenT - g_launchT : 0, g_status.c_str()); fclose(f); }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_TIMER:
        if (wp == 3) { if (!g_busy) { KillTimer(h, 3); Launch(g_testLaunch); } return 0; }
        if (wp == 2) { WriteTestLog(h); DestroyWindow(h); return 0; }
        if (wp == 4) { KillTimer(h, 4); g_goWait = false; Launch(2, g_modsState == MS_READY ? L" -sacoop-mods" : L""); return 0; }
        Tick();
        return 0;
    case WM_MOUSEMOVE: {
        float x = (short)LOWORD(lp) / g_scale, y = (short)HIWORD(lp) / g_scale;
        g_hot = HitButton(x, y);
        g_tabHot = HitTab(x, y);
        HitOption(x, y, &g_optHot, &g_optPart);
        g_logRowHot = g_tab == TAB_LOGS ? LogRowAt(x, y, &g_logPart) : -1;
        g_modHot = g_tab == TAB_MODS ? ModRowAt(x, y, &g_modPart) : -1;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        SetCursor(LoadCursor(NULL, ((g_hot >= 0 && g_btn[g_hot].enabled) || g_tabHot >= 0 || g_optHot >= 0 || g_logRowHot >= 0 || g_modHot >= 0) ? IDC_HAND
                                   : HitField(x, y) >= 0 ? IDC_IBEAM : IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE: g_hot = -1; g_tabHot = -1; g_optHot = -1; return 0;
    case WM_MOUSEWHEEL:
        if (g_tab >= 0) {
            float step = -(short)HIWORD(wp) / 120.0f * kRowH * 1.5f;
            g_scroll[g_tab] = min(max(g_scroll[g_tab] + step, 0.0f), MaxScroll(g_tab));
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
            ScreenToClient(h, &pt);
            HitOption(pt.x / g_scale, pt.y / g_scale, &g_optHot, &g_optPart);
        }
        return 0;
    case WM_SETCURSOR: return TRUE;
    case WM_LBUTTONDOWN: {
        float x = (short)LOWORD(lp) / g_scale, y = (short)HIWORD(lp) / g_scale;
        int b = HitButton(x, y), f = HitField(x, y);
        if (b >= 0) { g_pressed = b; SetCapture(h); return 0; }
        if (f >= 0) { g_focus = f; g_time = 0; return 0; }
        g_focus = -1;
        int t = HitTab(x, y);
        if (t >= 0) { g_tab = g_tab == t ? -1 : t; g_optHot = -1; if (g_tab == TAB_NOTES) NotesMarkSeen(); if (g_tab == TAB_LOGS) LogsScan(); if (g_tab == TAB_MODS) ModsTabScan(); return 0; }   // un 2e clic referme
        if (g_tab == TAB_LOBBY && LobbyClick(x, y)) return 0;
        if (g_tab == TAB_LOGS && LogsMouseDown(x, y)) return 0;
        if (g_tab == TAB_MODS && ModsMouseDown(x, y)) return 0;
        int row, part;
        HitOption(x, y, &row, &part);
        if (row >= 0) { OptStep(row, part < 0 ? -1 : 1); return 0; }
        if (g_tab >= 0 && kOptPanel.Contains(x, y)) return 0;
        ReleaseCapture();
        SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);   // glisser la fenetre
        return 0;
    }
    case WM_LBUTTONUP: {
        int p = g_pressed;
        g_pressed = -1;
        ReleaseCapture();
        float x = (short)LOWORD(lp) / g_scale, y = (short)HIWORD(lp) / g_scale;
        if (p >= 0 && HitButton(x, y) == p && g_btn[p].enabled) OnButton(p);
        return 0;
    }
    case WM_CHAR:
        if (g_state != ST_IDLE) return 0;
        if (wp == 8) { if (g_focus >= 0 && !g_fields[g_focus].text.empty()) g_fields[g_focus].text.pop_back(); }
        else if (wp == 127) { if (g_focus >= 0) g_fields[g_focus].text.clear(); }   // Ctrl+Retour arriere
        else if (wp == 22 && g_focus >= 0 && OpenClipboard(h)) {   // Ctrl+V
            HANDLE d = GetClipboardData(CF_UNICODETEXT);
            const wchar_t *s = d ? (const wchar_t *)GlobalLock(d) : NULL;
            if (s) { for (; *s && *s != L'\r' && *s != L'\n'; s++) TypeChar(*s); GlobalUnlock(d); }
            CloseClipboard();
        } else if (wp == 9) { g_focus = g_focus == 0 ? 1 : 0; g_time = 0; }
        else if (wp == 13) { if (g_focus == 1 && g_lobby == LB_NONE) LobbyJoin(); else if (g_focus == 0) { g_focus = 1; g_time = 0; } }
        else if (wp == 27) g_focus = -1;
        else if (wp >= 32) TypeChar((wchar_t)wp);
        g_time = 0.2f;
        return 0;
    case WM_APP_GO:   // l'invite demarre un peu apres l'hote : deux jeux sur le meme PC ne peuvent pas demarrer ensemble
        LobbyClose();
        g_goWait = true;
        SetStatus(K_OK, T(L"L'h\u00F4te lance la partie\u2026", L"The host is starting the game\u2026"));
        SetTimer(h, 4, 3500, NULL);
        return 0;
    case WM_APP_LOBBYEND:
        if (g_lobby == LB_NONE) return 0;
        LobbyClose();
        if (wp == 1) {
            std::string why = lp ? (const char *)lp : "";
            if (!why.compare(0, 8, "version ")) SetStatus(K_ERR, T(L"Version diff\u00E9rente de l'h\u00F4te (%S)", L"Different version from the host (%S)"), why.c_str() + 8);
            else if (why == "full") SetStatus(K_ERR, T(L"Salon complet (4 joueurs)", L"Lobby is full (4 players)"));
            else if (why == "started") { SetStatus(K_WARN, T(L"Partie d\u00E9j\u00E0 lanc\u00E9e : \u00AB Rejoindre en jeu \u00BB", L"Session already started: \"Join in game\"")); g_joinFallback = true; }
            else SetStatus(K_ERR, T(L"Refus\u00E9 par l'h\u00F4te", L"Refused by the host"));
        } else if (wp == 2) {
            SetStatus(K_WARN, T(L"Pas de salon chez l'h\u00F4te : \u00AB Rejoindre en jeu \u00BB s'il joue d\u00E9j\u00E0", L"No lobby at the host: \"Join in game\" if they are already playing"));
            g_joinFallback = true;
        } else SetStatus(K_WARN, T(L"L'h\u00F4te a ferm\u00E9 le salon", L"The host closed the lobby"));
        return 0;
    case WM_APP_RELAUNCH: {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        std::wstring cmd = L"\"" + g_self + L"\"";
        std::vector<wchar_t> c(cmd.begin(), cmd.end());
        c.push_back(0);
        if (CreateProcessW(g_self.c_str(), c.data(), NULL, NULL, FALSE, 0, NULL, g_dir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            DestroyWindow(h);
        }
        return 0;
    }
    case WM_CLOSE: g_state = ST_CLOSING; return 0;
    case WM_DESTROY:
        if (g_testLaunch >= 0) WriteTestLog(h);   // test de lancement : fermeture apres la fenetre du jeu
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool EncoderClsid(const wchar_t *mime, CLSID *out)
{
    UINT n = 0, size = 0;
    GetImageEncodersSize(&n, &size);
    if (!size) return false;
    std::vector<BYTE> buf(size);
    ImageCodecInfo *info = (ImageCodecInfo *)buf.data();
    GetImageEncoders(n, size, info);
    for (UINT i = 0; i < n; i++) if (!wcscmp(info[i].MimeType, mime)) { *out = info[i].Clsid; return true; }
    return false;
}

static Bitmap *LoadPng(const wchar_t *file)
{
    for (const std::wstring &d : { g_dir, g_gameDir }) {
        if (d.empty()) continue;
        std::wstring p = d + L"SACoop\\interface\\" + file;
        if (!FileExists(p)) continue;
        // Copie en memoire : Bitmap::FromFile garde le fichier ouvert, et la mise a jour doit pouvoir le remplacer.
        Bitmap *b = Bitmap::FromFile(p.c_str());
        if (b && b->GetLastStatus() == Ok) {
            Bitmap *copy = new Bitmap(b->GetWidth(), b->GetHeight(), PixelFormat32bppPARGB);
            {
                Graphics g(copy);
                g.SetCompositingMode(CompositingModeSourceCopy);
                g.DrawImage(b, 0, 0, b->GetWidth(), b->GetHeight());
            }
            delete b;
            return copy;
        }
        delete b;
    }
    return NULL;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    InitializeCriticalSection(&g_cs);
    InitializeCriticalSection(&g_lcs);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_fr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    g_self = self;
    g_dir = DirOf(g_self);
    g_iniLauncher = g_dir + L"sacoop-launcher.ini";
    DeleteFileW((g_self + L".old").c_str());   // reste d'une mise a jour du lanceur
    // Langue : francais si Windows est en francais, anglais pour toute autre langue ; Langue=fr|en pour forcer.
    wchar_t lang[8] = L"";
    GetPrivateProfileStringW(L"Lanceur", L"Langue", L"", lang, 8, g_iniLauncher.c_str());
    if (!_wcsicmp(lang, L"fr")) g_fr = true;
    else if (!_wcsicmp(lang, L"en")) g_fr = false;
    {   // Theme : Theme=clair|sombre, sinon celui des applications de Windows
        wchar_t th[16] = L"";
        GetPrivateProfileStringW(L"Lanceur", L"Theme", L"", th, 16, g_iniLauncher.c_str());
        if (!_wcsicmp(th, L"sombre") || !_wcsicmp(th, L"dark")) g_dark = true;
        else if (!_wcsicmp(th, L"clair") || !_wcsicmp(th, L"light")) g_dark = false;
        else {
            DWORD v = 1, sz = sizeof(v);
            if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS) g_dark = v == 0;
        }
    }

    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3 && !_wcsicmp(argv[1], L"/check")) return (int)CheckExe(argv[2]);
    for (int i = 1; i + 1 < argc; i++) {
        if (!_wcsicmp(argv[i], L"/lang")) g_fr = !_wcsicmp(argv[i + 1], L"fr");
        if (!_wcsicmp(argv[i], L"/theme")) g_dark = !_wcsicmp(argv[i + 1], L"sombre");
        if (!_wcsicmp(argv[i], L"/echelle")) g_scale = (float)_wtof(argv[i + 1]);   // captures : rendu agrandi
        if (!_wcsicmp(argv[i], L"/testfenetre")) g_testLog = argv[i + 1];
        if (!_wcsicmp(argv[i], L"/testlancer") && i + 2 < argc) { g_testLaunch = _wtoi(argv[i + 1]); g_testLog = argv[i + 2]; }
        if (!_wcsicmp(argv[i], L"/testsalon") && i + 2 < argc) { g_testSalon = argv[i + 1]; g_testSalonLog = argv[i + 2]; }
    }

    GdiplusStartupInput gin;
    ULONG_PTR gtok;
    GdiplusStartup(&gtok, &gin, NULL);
    Layout();
    BuildOptions();
    LayoutTabs();

    wchar_t saved[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"Lanceur", L"Exe", L"", saved, MAX_PATH, g_iniLauncher.c_str());
    std::wstring start = (saved[0] && FileExists(saved)) ? saved : g_dir + L"gta_sa.exe";
    SetExe(start);
    g_bg = LoadPng(L"launcher.png");
    g_bgDark = LoadPng(L"launcher-sombre.png");
    if (g_exeKind == EXE_MISSING) SetStatus(K_ERR, T(L"Choisis ton gta_sa.exe (version 1.0 US)", L"Choose your gta_sa.exe (version 1.0 US)"));
    else if (g_exeKind != EXE_OK) SetStatus(K_ERR, T(L"Ce gta_sa.exe n'est pas la version 1.0 US", L"This gta_sa.exe is not version 1.0 US"));
    else SetStatus(K_NORMAL, L"SACoop %s", g_localVer.empty() ? L"" : g_localVer.c_str());

    // /maj <exe> <journal> : verification + mise a jour sans fenetre (tests) ; journal = etat final
    if (argc >= 4 && !_wcsicmp(argv[1], L"/maj")) {
        SetExe(argv[2]);
        if (g_exeKind == EXE_OK) { g_busy = true; UpdateThread(NULL); }
        FILE *f = _wfopen(argv[3], L"w, ccs=UTF-8");
        if (f) { fwprintf(f, L"exe=%d local=%s\n%s\n", (int)g_exeKind, g_localVer.c_str(), g_status.c_str()); fclose(f); }
        delete g_bg; delete g_bgDark;
        GdiplusShutdown(gtok);
        return 0;
    }

    // Capture d'un etat, sans fenetre (verification du rendu)
    if (argc >= 4 && !_wcsicmp(argv[1], L"/capture")) {
        std::wstring st = argv[3];
        g_alpha = 1;
        g_time = 0.3f;
        if (st == L"attente") { g_state = ST_LAUNCH; g_launchInfo = L"CJ h\u00E9berge la partie"; g_time = 1.3f; }
        else if (st == L"sansexe") { g_exeKind = EXE_OTHER; g_gameDir.clear(); g_localVer.clear(); SetStatus(K_ERR, T(L"Ce gta_sa.exe n'est pas la version 1.0 US", L"This gta_sa.exe is not version 1.0 US")); }
        else if (st == L"options" || st == L"coop") { g_tab = st == L"coop" ? TAB_COOP : TAB_VIDEO; g_optHot = TabRows(g_tab)[0]; g_optPart = 1; }
        else if (st == L"notes") { NotesOnlyThread(NULL); g_tab = TAB_NOTES; }
        else if (st == L"journaux") { g_tab = TAB_LOGS; LogsScan(); g_logRowHot = 1; g_logPart = 2; g_btn[B_LOGS].hover = 1; }
        else if (st == L"mods") { g_tab = TAB_MODS; ModsTabScan(); g_modHot = 0; }
        else if (st == L"rendu") { g_tab = TAB_RENDER; g_optHot = TabRows(TAB_RENDER)[0]; g_optPart = 1; }
        else if (st == L"salon" || st == L"salon-invite") {   // salon a 3 joueurs (faux), vu par l'hote ou par un invite
            bool host = st == L"salon";
            ReadSaves();
            g_lobby = host ? LB_HOST : LB_GUEST;
            g_peers = { { 0, "Joueur1", "CJ", true, 0 }, { 1, "Joueur2", "Grove 1", true, 38 }, { 2, "Joueur3", "Ballas 2", false, 71 } };
            g_myId = host ? 0 : 2;
            g_lobbyChoice = (int)g_saves.size();
            g_lobbyChoiceLabel = g_saves.empty() ? "" : g_saves.back().label;
            g_lobbyAddr = L"192.168.1.20";
            g_lobbyPort = 7800;
            g_myAddresses = L"192.168.1.20";
            g_hostMods = { { "voitures\\infernus.dff", 2400000, 1 }, { "voitures\\infernus.txd", 3100000, 2 }, { "handling.cfg", 900, 3 } };
            g_hostModsBytes = 5500900;
            g_hostModsReady = true;
            g_modsState = MS_DOWNLOAD; g_modsTotal = 5500900; g_modsDone = 3300000; g_modsCount = 3;
            g_tab = TAB_LOBBY;
            LayoutTabs();
            if (g_localVer.empty()) g_localVer = L"0.27.0-prealpha";
            if (host) SetStatus(K_OK, T(L"Salon ouvert \u00B7 port %d", L"Lobby open \u00B7 port %d"), 7800);
            else SetStatus(K_OK, T(L"Dans le salon de %s", L"In %s's lobby"), L"192.168.1.20");
        }
        else if (st == L"maj") { g_busy = true; g_progress = 0.42f; SetStatus(K_NORMAL, T(L"T\u00E9l\u00E9chargement de SACoop %s\u2026", L"Downloading SACoop %s\u2026"), L"0.1.1-prealpha"); g_focus = 0; g_time = 0.2f; }
        else { if (g_localVer.empty()) g_localVer = L"0.1.0-prealpha"; SetStatus(K_OK, T(L"SACoop %s \u00B7 \u00E0 jour", L"SACoop %s \u00B7 up to date"), g_localVer.c_str()); g_hot = B_HOST; g_btn[B_HOST].hover = 1; }
        int rc = 1;
        {
            Bitmap out((INT)(kImgW * g_scale), (INT)(kImgH * g_scale), PixelFormat32bppPARGB);
            RenderTo(out, g_scale);
            CLSID png;
            if (EncoderClsid(L"image/png", &png) && out.Save(argv[2], &png, NULL) == Ok) rc = 0;
        }   // (detruit avant GdiplusShutdown)
        delete g_bg; delete g_bgDark;
        GdiplusShutdown(gtok);
        return rc;
    }
    LocalFree(argv);

    // Taille : l'image a l'echelle de l'ecran (PPP), sans depasser 90 % de la zone de travail.
    HDC sdc = GetDC(NULL);
    g_scale = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, sdc);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    float fit = min((work.right - work.left) * 0.9f / kImgW, (work.bottom - work.top) * 0.9f / kImgH);
    g_scale = max(0.5f, min(g_scale, fit));
    g_winW = (int)(kImgW * g_scale);
    g_winH = (int)(kImgH * g_scale);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = L"SACoopLauncher";
    RegisterClassExW(&wc);
    int x = work.left + (work.right - work.left - g_winW) / 2, y = work.top + (work.bottom - work.top - g_winH) / 2;
    bool test = !g_testLog.empty() || !g_testSalonLog.empty();
    if (test) x = y = -5000;
    g_wnd = CreateWindowExW(WS_EX_LAYERED | (test ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : WS_EX_APPWINDOW), wc.lpszClassName, L"SACoop", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                            x, y, g_winW, g_winH, NULL, NULL, inst, NULL);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = g_winW;
    bi.bmiHeader.biHeight = -g_winH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    g_memDC = CreateCompatibleDC(NULL);
    g_dib = CreateDIBSection(g_memDC, &bi, DIB_RGB_COLORS, &g_bits, NULL, 0);
    SelectObject(g_memDC, g_dib);

    Present();
    ShowWindow(g_wnd, test ? SW_SHOWNOACTIVATE : SW_SHOW);
    SetTimer(g_wnd, 1, 16, NULL);
    if (!g_testLog.empty()) SetTimer(g_wnd, g_testLaunch >= 0 ? 3 : 2, g_testLaunch >= 0 ? 3000 : 5000, NULL);
    if (g_exeKind == EXE_OK && g_testSalonLog.empty()) StartUpdate();   // (test de salon : pas de mise a jour)
    else {
        if (g_exeKind != EXE_MISSING && !test) BadExeMessage();
        HANDLE nt = CreateThread(NULL, 0, NotesOnlyThread, NULL, 0, NULL);
        if (nt) CloseHandle(nt);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (g_proc) CloseHandle(g_proc);
    delete g_bg; delete g_bgDark;
    GdiplusShutdown(gtok);
    return 0;
}
