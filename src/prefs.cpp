// Reglages du jeu imposes par le lanceur :
//  - LangueJeu (-1 = celle de Windows, 0 anglais, 1 francais, 2 allemand, 3 italien, 4 espagnol) : le jeu choisit son
//    fichier de textes (text\*.gxt) d'apres FrontEndMenuManager +0x84 (0xBA67CC) dans CText::Load (0x6A01A0, table
//    AMERICAN / FRENCH / GERMAN / ITALIAN / SPANISH.GXT) ; on y pose la langue voulue a chaque chargement des textes.
//  - SourisInverseeY (0 par defaut) : 0xBA6745 (MousePointerStateHelper, lY multiplie par -1 en 0x53F43E quand il vaut
//    1). Mesure (TestSouris, 01/10) : a 1, souris poussee vers l'avant -> regard vers le haut (normal) ; a 0, vers le
//    bas (inverse). Le jeu le met a 1 avant de lire gta_sa.set (0x57C933) mais 0 avec « reglages par defaut » des
//    commandes (0x573C00) et dans les gta_sa.set qui en gardent la trace : souris inversee chez beaucoup de joueurs.
//    Pose au menu principal puis a l'arrivee en partie ; changeable ensuite dans les options du jeu (le reglage du
//    lanceur revient au lancement suivant).
#include "util.h"
#include "sacoop.h"
#include "game.h"
#include "prefs.h"

static int GameLanguage()
{
    if (g_cfg.gameLang >= 0 && g_cfg.gameLang <= 4) return g_cfg.gameLang;
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_FRENCH: return 1;
    case LANG_GERMAN: return 2;
    case LANG_ITALIAN: return 3;
    case LANG_SPANISH: return 4;
    default: return 0;
    }
}

typedef void(__fastcall *TextLoad_t)(void *self, void *edx, int keepMissionPack);
static TextLoad_t o_TextLoad;
static void __fastcall h_TextLoad(void *self, void *edx, int keepMissionPack)
{
    static int logged = -1;
    int lang = GameLanguage();
    *(uint8_t *)0xBA67CC = (uint8_t)lang;
    static const char *const names[] = { "anglais", "francais", "allemand", "italien", "espagnol" };
    if (logged != lang) { logged = lang; Log("langue du jeu : %s", names[lang]); }
    o_TextLoad(self, edx, keepMissionPack);
}

void InstallPrefs()
{
    static const uint8_t pro[] = { 0x83, 0xEC, 0x28, 0x8B, 0x44, 0x24, 0x2C };   // sub esp, 28h ; mov eax, [esp+2Ch]
    o_TextLoad = (TextLoad_t)MakeDetour(0x6A01A0, pro, sizeof(pro), (void *)h_TextLoad);
    if (!o_TextLoad) Log("langue du jeu : CText::Load inattendu, langue d'origine");
}

void PrefsFrame()
{
    static bool atMenu, inGame;
    int st = game::GameState();
    if ((st == 7 && !atMenu) || (st == 9 && !inGame)) {
        if (st == 7) atMenu = true; else inGame = true;
        *(uint8_t *)0xBA6745 = g_cfg.invertMouseY ? 0 : 1;   // (1 = normal, voir plus haut)
        Log("souris : axe vertical %s", g_cfg.invertMouseY ? "inverse" : "normal");
    }
}
