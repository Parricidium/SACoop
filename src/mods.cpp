// Mods partages (ModsPartages=1). Dossier SACoop\mods\ (sous-dossiers libres, leur nom ne sert qu'a s'y retrouver) :
//  - nom.dff / nom.txd / nom.col / nom.ifp : remplacent le fichier "nom" de gta3.img (voiture, arme, personnage...) ;
//  - handling.cfg et carcols.dat : leurs lignes remplacent celles du jeu qui commencent par le meme nom (vehicule,
//    dans la meme section pour carcols.dat) ; les autres lignes du jeu restent.
// Au demarrage, les modeles sont rassembles dans SACoop\cache\sacmods.img (archive VER2, refaite seulement si la
// liste, la taille ou la date d'un fichier a change). CStreaming::LoadCdDirectory() (0x5B82C0) est detournee : notre
// archive y est ajoutee (CStreaming::AddImageToList 0x407610) et lue AVANT les autres (LoadCdDirectory(nom, numero)
// 0x5B6170) : le jeu garde la premiere archive qui declare un fichier (CStreamingInfo::GetCdPosnAndSize 0x4075A0),
// la notre gagne donc sur gta3.img. Memoire de streaming relevee a MemoireStreaming Mo (256 par defaut ; le jeu met
// 50 Mo en 0x8A5A80 dans CStreaming::Init2) : les modeles HD sont plus lourds.
// CFileMgr::OpenFile (0x538900, un simple saut vers fopen 0x8232D8) est redirige : HANDLING.CFG et CARCOLS.DAT sont
// lus dans SACoop\cache\ (fusionnes).
// Invite passe par le salon du lanceur (-sacoop-mods) : seuls les fichiers de SACoop\cache\mods-liste.txt (ceux de
// l'hote, que le lanceur vient de recevoir) sont pris, les autres mods locaux sont ignores : memes modeles chez tous.
#include "util.h"
#include "sacoop.h"
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

struct ModFile { std::string rel, path; uint32_t size; uint64_t time; };
static std::vector<ModFile> g_files;
static bool g_imgReady, g_handling, g_carcols;

static std::string Lower(std::string s) { for (char &c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string BaseName(const std::string &rel) { size_t p = rel.find_last_of("\\/"); return p == std::string::npos ? rel : rel.substr(p + 1); }
static std::string Ext(const std::string &name) { size_t p = name.rfind('.'); return p == std::string::npos ? "" : Lower(name.substr(p)); }
static std::string Dir(const char *sub) { return std::string(GameDir()) + sub; }

static void Scan(const std::string &dir, const std::string &rel)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        std::string r = rel + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { Scan(dir + fd.cFileName + "\\", r + "\\"); continue; }
        g_files.push_back({ r, dir + fd.cFileName, fd.nFileSizeLow, ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// Invite du salon : seulement la liste de l'hote.
static void FilterBySessionList()
{
    if (!strstr(GetCommandLineA(), "-sacoop-mods")) return;
    FILE *f = fopen(Dir("SACoop\\cache\\mods-liste.txt").c_str(), "r");
    std::vector<std::string> keep;
    char line[600];
    while (f && fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n) keep.push_back(Lower(line));
    }
    if (f) fclose(f);
    size_t before = g_files.size();
    g_files.erase(std::remove_if(g_files.begin(), g_files.end(), [&](const ModFile &m) {
        return std::find(keep.begin(), keep.end(), Lower(m.rel)) == keep.end(); }), g_files.end());
    Log("mods : liste de l'hote, %d fichiers sur %d", (int)g_files.size(), (int)before);
}

static bool IsModel(const std::string &ext) { return ext == ".dff" || ext == ".txd" || ext == ".col" || ext == ".ifp"; }

// Archive VER2 : en-tete "VER2" + nombre, puis 32 octets par fichier (position et taille en secteurs de 2048 octets,
// nom sur 24 caracteres), puis les fichiers alignes sur 2048.
static bool BuildImg()
{
    std::vector<const ModFile *> models;
    std::vector<std::string> names;
    for (auto &m : g_files) {
        std::string b = BaseName(m.rel);
        if (!IsModel(Ext(b)) || b.size() > 23) continue;
        if (std::find(names.begin(), names.end(), Lower(b)) != names.end()) continue;   // meme nom deux fois : le premier
        names.push_back(Lower(b));
        models.push_back(&m);
    }
    if (models.empty()) return false;
    std::string sig;
    for (auto *m : models) { char b[64]; sprintf(b, "|%u|%llu", m->size, (unsigned long long)m->time); sig += Lower(m->rel) + b + "\n"; }
    std::string img = Dir("SACoop\\cache\\sacmods.img"), sigPath = Dir("SACoop\\cache\\sacmods.sig");
    CreateDirectoryA(Dir("SACoop\\cache").c_str(), NULL);
    {
        FILE *f = fopen(sigPath.c_str(), "rb");
        std::string old;
        char buf[4096];
        size_t n;
        while (f && (n = fread(buf, 1, sizeof(buf), f)) > 0) old.append(buf, n);
        if (f) fclose(f);
        if (old == sig && GetFileAttributesA(img.c_str()) != INVALID_FILE_ATTRIBUTES) { Log("mods : sacmods.img a jour (%d modeles)", (int)models.size()); return true; }
    }
    FILE *out = fopen(img.c_str(), "wb");
    if (!out) { Log("mods : impossible d'ecrire %s", img.c_str()); return false; }
    uint32_t count = (uint32_t)models.size();
    uint32_t headerSectors = (8 + count * 32 + 2047) / 2048;
    std::vector<uint8_t> header(headerSectors * 2048, 0);
    memcpy(header.data(), "VER2", 4);
    memcpy(header.data() + 4, &count, 4);
    fwrite(header.data(), 1, header.size(), out);
    uint32_t sector = headerSectors;
    std::vector<uint8_t> data;
    for (uint32_t i = 0; i < count; i++) {
        FILE *in = fopen(models[i]->path.c_str(), "rb");
        data.assign(models[i]->size, 0);
        if (in) { fread(data.data(), 1, data.size(), in); fclose(in); }
        uint32_t secs = (uint32_t)((data.size() + 2047) / 2048);
        data.resize(secs * 2048, 0);
        fwrite(data.data(), 1, data.size(), out);
        uint8_t *e = header.data() + 8 + i * 32;
        uint16_t s16 = (uint16_t)secs;
        memcpy(e, &sector, 4);
        memcpy(e + 4, &s16, 2);
        std::string b = BaseName(models[i]->rel);
        memcpy(e + 8, b.c_str(), b.size());
        sector += secs;
    }
    fseek(out, 0, SEEK_SET);
    fwrite(header.data(), 1, header.size(), out);
    fclose(out);
    FILE *f = fopen(sigPath.c_str(), "wb");
    if (f) { fwrite(sig.data(), 1, sig.size(), f); fclose(f); }
    Log("mods : sacmods.img fabrique (%d modeles, %u Mo)", (int)count, sector / 512);
    return true;
}

// Fusion de handling.cfg / carcols.dat : une ligne de mod remplace la ligne du jeu de meme cle (premier mot ; pour
// carcols.dat, dans la meme section "col"/"car"/"car4"...), les lignes nouvelles sont ajoutees a la fin (handling)
// ou en fin de section (carcols).
static bool ReadLines(const std::string &path, std::vector<std::string> &lines)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string all;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    fclose(f);
    size_t p = 0;
    while (p < all.size()) {
        size_t e = all.find('\n', p);
        if (e == std::string::npos) e = all.size();
        std::string l = all.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        p = e + 1;
    }
    return true;
}
static std::string Key(const std::string &l)
{
    size_t a = l.find_first_not_of(" \t");
    if (a == std::string::npos || l[a] == ';' || l[a] == '#') return "";
    size_t b = l.find_first_of(" \t,", a);
    return Lower(l.substr(a, b == std::string::npos ? std::string::npos : b - a));
}
static bool MergeData(const char *gameFile, const char *modName, const char *outName, bool sections)
{
    std::vector<std::string> mods;
    for (auto &m : g_files) if (Lower(BaseName(m.rel)) == modName) ReadLines(m.path, mods);
    if (mods.empty()) return false;
    std::vector<std::string> game;
    if (!ReadLines(Dir(gameFile), game)) return false;
    // cle complete : section + premier mot (carcols) ; handling : le prefixe de ligne (! $ % ^) fait partie du mot
    auto keyed = [&](const std::vector<std::string> &ls, std::vector<std::string> &keys) {
        std::string section;
        for (auto &l : ls) {
            std::string k = Key(l);
            if (sections && (k == "col" || k == "car" || k == "car4" || k == "end" || k == "car3")) { section = k == "end" ? "" : k; keys.push_back(""); continue; }
            keys.push_back(k.empty() ? "" : section + "|" + k);
        }
    };
    std::vector<std::string> gk, mk;
    keyed(game, gk);
    keyed(mods, mk);
    std::vector<bool> used(mods.size(), false);
    int replaced = 0, added = 0;
    for (size_t i = 0; i < game.size(); i++) {
        if (gk[i].empty()) continue;
        for (size_t j = 0; j < mods.size(); j++)
            if (!used[j] && mk[j] == gk[i]) { game[i] = mods[j]; used[j] = true; replaced++; break; }
    }
    for (size_t j = 0; j < mods.size(); j++) {
        if (used[j] || mk[j].empty()) continue;
        std::string sec = mk[j].substr(0, mk[j].find('|'));
        size_t at = game.size();
        if (sections && !sec.empty()) {   // fin de la section : son "end"
            std::string cur;
            for (size_t i = 0; i < game.size(); i++) {
                std::string k = Key(game[i]);
                if (k == "col" || k == "car" || k == "car4" || k == "car3") cur = k;
                else if (k == "end" && cur == sec) { at = i; break; }
            }
        } else {   // handling : avant la ligne "; the end" s'il y en a une, sinon a la fin
            for (size_t i = 0; i < game.size(); i++) if (Lower(game[i]).find("the end") != std::string::npos) { at = i; break; }
        }
        game.insert(game.begin() + at, mods[j]);
        gk.insert(gk.begin() + at, mk[j]);
        added++;
    }
    FILE *f = fopen(Dir(outName).c_str(), "wb");
    if (!f) return false;
    for (auto &l : game) { fputs(l.c_str(), f); fputs("\r\n", f); }
    fclose(f);
    Log("mods : %s fusionne (%d lignes remplacees, %d ajoutees)", modName, replaced, added);
    return true;
}

// --- Detours ---
typedef void(__cdecl *LoadDirs_t)();
static LoadDirs_t o_LoadDirs;
static void __cdecl h_LoadDirs()
{
    if (g_imgReady) {
        static const char *name = "SACOOP\\CACHE\\SACMODS.IMG";
        int idx = ((int(__cdecl *)(const char *, bool))0x407610)(name, true);   // CStreaming::AddImageToList
        if (idx >= 0 && idx < 8) {
            ((void(__cdecl *)(const char *, int))0x5B6170)(name, idx);   // lue la premiere : elle gagne
            Log("mods : sacmods.img ajoutee (archive %d)", idx);
        } else Log("mods : sacmods.img refusee (archive %d)", idx);
    }
    o_LoadDirs();
}

typedef FILE *(__cdecl *Fopen_t)(const char *, const char *);
static FILE *__cdecl h_OpenFile(const char *name, const char *mode)
{
    Fopen_t fo = (Fopen_t)0x8232D8;
    if (name && (g_handling || g_carcols)) {
        const char *b = name + strlen(name);
        while (b > name && b[-1] != '\\' && b[-1] != '/') b--;
        if (g_handling && !_stricmp(b, "handling.cfg")) return fo(Dir("SACoop\\cache\\handling.cfg").c_str(), mode);
        if (g_carcols && !_stricmp(b, "carcols.dat")) return fo(Dir("SACoop\\cache\\carcols.dat").c_str(), mode);
    }
    return fo(name, mode);
}

static int g_streamMb;
void ModsFrame()
{
    // Memoire de streaming : posee par CStreaming::Init2 apres le chargement des archives, relevee ensuite.
    if (g_streamMb && *(int *)0x8A5A80 < g_streamMb * 1024 * 1024) {
        *(int *)0x8A5A80 = g_streamMb * 1024 * 1024;
        Log("mods : memoire de streaming %d Mo", g_streamMb);
    }
}

void InstallMods()
{
    if (!GetPrivateProfileIntA("SACoop", "ModsPartages", 1, IniPath())) return;
    Scan(Dir("SACoop\\mods\\"), "");
    FilterBySessionList();
    if (g_files.empty()) return;
    g_imgReady = BuildImg();
    CreateDirectoryA(Dir("SACoop\\cache").c_str(), NULL);
    g_handling = MergeData("data\\handling.cfg", "handling.cfg", "SACoop\\cache\\handling.cfg", false);
    g_carcols = MergeData("data\\carcols.dat", "carcols.dat", "SACoop\\cache\\carcols.dat", true);
    if (g_imgReady) {
        static const uint8_t pro[] = { 0x83, 0xC8, 0xFF, 0xA3, 0x90, 0x4C, 0x8E, 0x00 };   // or eax,-1 ; mov [0x8E4C90],eax
        o_LoadDirs = (LoadDirs_t)MakeDetour(0x5B82C0, pro, sizeof(pro), (void *)h_LoadDirs);
        g_streamMb = GetPrivateProfileIntA("SACoop", "MemoireStreaming", 256, IniPath());
        if (g_streamMb < 50 || g_streamMb > 1024) g_streamMb = 256;
    }
    if (g_handling || g_carcols) {
        const uint8_t *p = (const uint8_t *)0x538900;
        if (p[0] == 0xE9 && 0x538905 + *(const int32_t *)(p + 1) == 0x8232D8) PatchJump(0x538900, (void *)h_OpenFile);
        else { Log("mods : CFileMgr::OpenFile inattendu, donnees du jeu gardees"); g_handling = g_carcols = false; }
    }
    Log("mods : %d fichiers dans SACoop\\mods", (int)g_files.size());
}
