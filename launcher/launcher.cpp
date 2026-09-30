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
//  - Heberger / Rejoindre : lance le jeu (-sacoop hote | -sacoop invite <adresse>) puis reste affiche en ecran
//    d'attente jusqu'a ce que la fenetre du jeu apparaisse.
//
// Options de ligne de commande (tests) : /capture <png> <menu|attente|sansexe|maj|options|notes> ; /check <exe> (code
// de sortie : 0 = 1.0 US, 1 = absent, 2 = autre) ; /maj <exe> <journal> ; /testlancer <1|2> <journal>.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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

using namespace Gdiplus;

static const wchar_t *kReleasesApi = L"https://api.github.com/repos/Parricidium/SACoop/releases?per_page=40";
static const wchar_t *kStoreUrl = L"https://store.steampowered.com/app/12120/";   // (meme lien que le README)
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

// Markdown simple : titres, gras et code retires ; puces "- " -> "•".
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
enum { B_HOST, B_JOIN, B_EXE, B_BUY, B_THEME, B_CLOSE, B_MIN, B_COUNT };
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
    g_btn[B_BUY].r = RectF(250, 554, 130, 26);
    g_btn[B_CLOSE].r = RectF(938, 76, 28, 28);
    g_btn[B_MIN].r = RectF(904, 76, 28, 28);
    g_btn[B_THEME].r = RectF(62, 100, 26, 26);   // coin du panneau, a gauche du logo
}

static void UpdateButtons()
{
    bool menu = g_state == ST_IDLE, exeOk = g_exeKind == EXE_OK, busy = g_busy;
    for (int i = 0; i < B_COUNT; i++) g_btn[i].visible = true;
    g_btn[B_HOST].visible = g_btn[B_JOIN].visible = g_btn[B_EXE].visible = menu;
    g_btn[B_HOST].enabled = g_btn[B_JOIN].enabled = menu && exeOk && !busy && !g_localVer.empty();
    g_btn[B_EXE].enabled = menu && !busy;
    g_btn[B_CLOSE].enabled = g_btn[B_MIN].enabled = g_btn[B_BUY].enabled = g_btn[B_THEME].enabled = true;
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
enum { TAB_VIDEO, TAB_COOP, TAB_NOTES, TAB_COUNT };
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
    T2(TAB_VIDEO, "SansIntro", 1, L"Passer les logos", L"Skip logos", L"Pas de logos ni de vid\u00E9o d'ouverture au d\u00E9marrage.", L"No logos or intro video at startup.");
    T2(TAB_VIDEO, "SauvegardesLocales", 1, L"Sauvegardes \u00E0 part", L"Separate saves",
       L"R\u00E9glages et sauvegardes du jeu dans son dossier, s\u00E9par\u00E9s de vos sauvegardes solo.",
       L"Game settings and saves in its folder, apart from your solo saves.");
    // COOP
    C(TAB_COOP, "Tenue", 106, { 105, 106, 107, 102, 103, 104, 108, 109, 110, 114, 115, 116 }, L"Personnage vu par les autres", L"Character others see",
      { L"Grove Street 1", L"Grove Street 2", L"Grove Street 3", L"Ballas 1", L"Ballas 2", L"Ballas 3", L"Vagos 1", L"Vagos 2", L"Vagos 3", L"Aztecas 1", L"Aztecas 2", L"Aztecas 3" },
      { L"Grove Street 1", L"Grove Street 2", L"Grove Street 3", L"Ballas 1", L"Ballas 2", L"Ballas 3", L"Vagos 1", L"Vagos 2", L"Vagos 3", L"Aztecas 1", L"Aztecas 2", L"Aztecas 3" }, L"",
      L"Pr\u00E9-alpha : les autres joueurs vous voient sous ce personnage (CJ avec ses v\u00EAtements arrivera plus tard).",
      L"Pre-alpha: the other players see you as this character (CJ with his clothes comes later).");
}

static std::string GameIni() { return Narrow(g_gameDir + L"sacoop.ini"); }
static int OptGet(const Opt &o) { return GetPrivateProfileIntA("SACoop", o.key, o.def, GameIni().c_str()); }
static void OptSet(const Opt &o, int v) { char b[16]; wsprintfA(b, "%d", v); WritePrivateProfileStringA("SACoop", o.key, b, GameIni().c_str()); }

static const wchar_t *TabName(int t)
{
    static const wchar_t *fr[] = { L"VID\u00C9O", L"COOP", L"NOUVEAUT\u00C9S" }, *en[] = { L"VIDEO", L"CO-OP", L"UPDATES" };
    return g_fr ? fr[t] : en[t];
}
static void LayoutTabs()
{
    float x = 440;
    Bitmap bm(1, 1);
    Graphics mg(&bm);
    for (int t = 0; t < TAB_COUNT; t++) {
        float w = 24 + MeasureW(mg, TabName(t), 11.5f, FontStyleBold);
        g_tabR[t] = RectF(x, 78, w, 26);
        x += w + 6;
    }
}
static std::vector<int> TabRows(int t) { std::vector<int> r; for (int i = 0; i < (int)g_opts.size(); i++) if (g_opts[i].tab == t) r.push_back(i); return r; }
static float NotesMaxScroll();
static float MaxScroll(int t) { return t == TAB_NOTES ? NotesMaxScroll() : max(0.0f, TabRows(t).size() * kRowH - kOptList.Height); }

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
        RectF r = g_tabR[t];
        GraphicsPath p;
        RoundRect(p, r, r.Height / 2);
        bool on = g_tab == t, hot = g_tabHot == t;
        if (on) { LinearGradientBrush lg(r, kGreen, kGold, LinearGradientModeHorizontal); g.FillPath(&lg, &p); }
        else { SolidBrush b(hot ? TH(tabHot) : TH(tab)); g.FillPath(&b, &p); }
        Text(g, TabName(t), r, 11.5f, FontStyleBold, on ? kOnAcc : Mix(kGrey, kInk, hot ? 1.0f : 0.0f));
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

static void DrawOptions(Graphics &g)
{
    if (g_tab < 0 || g_gameDir.empty()) return;
    if (g_tab == TAB_NOTES) { DrawNotes(g); return; }
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
    if (g_tab < 0 || g_tab == TAB_NOTES || !kOptList.Contains(x, y)) return;
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
    for (int t = 0; t < TAB_COUNT; t++) if (g_tabR[t].Contains(x, y)) return t;
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
        DrawButton(g, B_HOST, T(L"H\u00C9BERGER", L"HOST"), true);
        DrawButton(g, B_JOIN, T(L"REJOINDRE", L"JOIN"), false);
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
    {   // Acheter le jeu : pastille sombre avec un panier, vers Steam
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
        Text(g, L"Steam", RectF(b.r.X + 30, b.r.Y, b.r.Width - 36, b.r.Height), 12, FontStyleBold, Color(255, 255, 255, 255), StringAlignmentNear);
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

static void Launch(int mode)
{
    if (g_exeKind != EXE_OK || g_busy) return;
    std::wstring addr = Trim(g_fields[1].text);
    if (mode == 2 && addr.empty()) {
        SetStatus(K_ERR, T(L"Entre l'adresse de l'h\u00F4te", L"Enter the host address"));
        g_focus = 1;
        return;
    }
    SavePlayer();
    std::wstring args = mode == 1 ? L"-sacoop hote" : L"-sacoop invite " + addr;
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

static void OnButton(int id)
{
    switch (id) {
    case B_HOST: Launch(1); break;
    case B_JOIN: Launch(2); break;
    case B_EXE: ChooseExe(); break;
    case B_CLOSE: g_state = ST_CLOSING; break;
    case B_MIN: ShowWindow(g_wnd, SW_MINIMIZE); break;
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
        Tick();
        return 0;
    case WM_MOUSEMOVE: {
        float x = (short)LOWORD(lp) / g_scale, y = (short)HIWORD(lp) / g_scale;
        g_hot = HitButton(x, y);
        g_tabHot = HitTab(x, y);
        HitOption(x, y, &g_optHot, &g_optPart);
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        SetCursor(LoadCursor(NULL, ((g_hot >= 0 && g_btn[g_hot].enabled) || g_tabHot >= 0 || g_optHot >= 0) ? IDC_HAND
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
        if (t >= 0) { g_tab = g_tab == t ? -1 : t; g_optHot = -1; if (g_tab == TAB_NOTES) NotesMarkSeen(); return 0; }   // un 2e clic referme
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
        else if (wp == 13) { if (g_focus == 1) Launch(2); else if (g_focus == 0) { g_focus = 1; g_time = 0; } }
        else if (wp == 27) g_focus = -1;
        else if (wp >= 32) TypeChar((wchar_t)wp);
        g_time = 0.2f;
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
    bool test = !g_testLog.empty();
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
    if (test) SetTimer(g_wnd, g_testLaunch >= 0 ? 3 : 2, g_testLaunch >= 0 ? 3000 : 5000, NULL);
    if (g_exeKind == EXE_OK) StartUpdate();
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
