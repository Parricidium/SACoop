// Miroir des missions (la methode de VCCoop). Les missions ne tournent que chez l'hote (script.cpp) ; ce qu'elles
// montrent a l'ecran est rejoue chez les invites : l'hote capture une liste blanche de commandes de ses scripts de
// mission, avec les parametres evalues, et les envoie sur le flux fiable (ordonne, acquitte) ; l'invite les execute
// dans un script fantome a lui (meme gestionnaire du jeu, parametres reecrits en constantes).
//  - Traduction : personnage de l'hote (sa reference de pool = id MsgPed) -> sa copie ; vehicule (reference de pool
//    chez l'hote, MsgVehicle.ownerRef) -> sa copie ; marqueurs et spheres crees -> ceux de l'invite.
//  - Une commande dont l'entite n'existe pas encore chez l'invite attend en tete de file (3 s au plus).
//  - Debut de mission (START_MISSION de l'hote) : un invite a plus de 40 m est pose derriere l'hote.
//  - Progression : les variables globales du script (ScriptSpace [8, 43808) dans main.scm 1.0) qui ont change pendant
//    une mission sont envoyees a la fin (RL_GLOBALS), et chaque invite recoit l'etat complet a son arrivee en partie.
//    Seules les valeurs -1..255 circulent (drapeaux, compteurs) : pas les references d'entites propres a chaque jeu.
//  - Arrivee en cours de mission : l'hote garde le dernier texte de mission charge et les marqueurs/spheres actifs
//    (commandes deja capturees), et les renvoie a l'invite qui arrive, apres l'etat complet.
//  - Fin de mission (00D8 dans un script de mission) : l'invite retire ses marqueurs et spheres de mission et retablit
//    l'ecran (fondu, format cinema, controles, camera, textes, sons).
// Parametres SCM : 1 int32, 2 globale (ScriptSpace 0xA49960 + decalage), 3 locale (mission : 0xA48960, sinon script
// +0x3C), 4 int8, 5 int16, 6 float, 7/8 tableaux globaux/locaux, 9 texte 8, 0xA/0xB variable texte 8 globale/locale,
// 0xE texte de longueur variable, 0xF texte 16, 0x10/0x11 variable texte 16 globale/locale.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "peds.h"
#include "entities.h"
#include "vehicles.h"
#include "script.h"
#include "mirror.h"
#include "police.h"
#include "savesync.h"
#include "chat.h"
#include "events.h"
#include "population.h"
#include "hud.h"
#include "conditions.h"
#include "menu.h"
#include <string.h>
#include <math.h>

using namespace game;

enum : uint8_t { RL_MIRROR = 1, RL_MISSION_END = 2, RL_MISSION_START = 3, RL_GLOBALS = 4, RL_SESSION = 5, RL_BLIPS = 6 };

// Signature de chaque commande : i entier, f reel, s texte, P personnage, V vehicule, b/B marqueur (entree/sortie),
// q/Q sphere (entree/sortie), o/O objet (entree/sortie), k/K pickup, r/R feu de script. (A l'envoi, un P qui designe le joueur de l'hote devient H :
// son pantin chez l'invite.)
struct MirrorOp { uint16_t op; const char *sig; };
static const MirrorOp kOps[] = {
    { 0x00BA, "sii" },     // PRINT_BIG
    { 0x00BB, "sii" },     // PRINT
    { 0x00BC, "sii" },     // PRINT_NOW
    { 0x00BE, "" },        // CLEAR_PRINTS
    { 0x01E3, "siii" },    // PRINT_WITH_NUMBER_BIG
    { 0x01E4, "siii" },    // PRINT_WITH_NUMBER
    { 0x01E5, "siii" },    // PRINT_WITH_NUMBER_NOW
    { 0x03E5, "s" },       // PRINT_HELP
    { 0x03E6, "" },        // CLEAR_HELP
    { 0x054C, "s" },       // LOAD_MISSION_TEXT
    { 0x016A, "ii" },      // DO_FADE
    { 0x02A3, "i" },       // SWITCH_WIDESCREEN
    { 0x03DE, "f" },       // SET_PED_DENSITY_MULTIPLIER : la circulation suit celle des missions de l'hote
    { 0x01EB, "f" },       // SET_CAR_DENSITY_MULTIPLIER (en population partagee proche, population.cpp les retient)
    { 0x01B4, "ii" },      // SET_PLAYER_CONTROL
    { 0x0164, "b" },       // REMOVE_BLIP
    { 0x0165, "bi" },      // CHANGE_BLIP_COLOUR
    { 0x0168, "bi" },      // CHANGE_BLIP_SCALE
    { 0x018B, "bi" },      // CHANGE_BLIP_DISPLAY
    { 0x07E0, "bi" },      // SET_BLIP_AS_FRIENDLY
    { 0x0186, "VB" },      // ADD_BLIP_FOR_CAR
    { 0x0187, "PB" },      // ADD_BLIP_FOR_CHAR
    { 0x018A, "fffB" },    // ADD_BLIP_FOR_COORD
    { 0x02A7, "fffiB" },   // ADD_SPRITE_BLIP_FOR_CONTACT_POINT
    { 0x04CE, "fffiB" },   // ADD_SHORT_RANGE_SPRITE_BLIP_FOR_COORD
    { 0x03BC, "ffffQ" },   // ADD_SPHERE
    { 0x03BD, "q" },       // REMOVE_SPHERE
    { 0x03CF, "ii" },      // LOAD_MISSION_AUDIO
    { 0x03D1, "i" },       // PLAY_MISSION_AUDIO
    { 0x040D, "i" },       // CLEAR_MISSION_AUDIO
    { 0x015F, "ffffff" },  // SET_FIXED_CAMERA_POSITION
    { 0x0160, "fffi" },    // POINT_CAMERA_AT_POINT
    { 0x015A, "" },        // RESTORE_CAMERA
    // Cameras des mini-cinematiques (nombre de parametres lu dans les gestionnaires du jeu, run\saop.py) :
    { 0x0157, "iii" },     // POINT_CAMERA_AT_PLAYER : envoyee comme 0159 vers le pantin de l'hote
    { 0x0158, "Vii" },     // POINT_CAMERA_AT_CAR
    { 0x0159, "Pii" },     // POINT_CAMERA_AT_CHAR
    { 0x0679, "Vfffffffi" }, // ATTACH_CAMERA_TO_VEHICLE (decalage, rotation, inclinaison, transition)
    { 0x067A, "VfffVfi" }, // ATTACH_CAMERA_TO_VEHICLE_LOOK_AT_VEHICLE
    { 0x067B, "VfffPfi" }, // ATTACH_CAMERA_TO_VEHICLE_LOOK_AT_CHAR
    { 0x067C, "Pfffffffi" }, // ATTACH_CAMERA_TO_CHAR
    { 0x067D, "PfffVfi" }, // ATTACH_CAMERA_TO_CHAR_LOOK_AT_VEHICLE
    { 0x067E, "PfffPfi" }, // ATTACH_CAMERA_TO_CHAR_LOOK_AT_CHAR
    { 0x0936, "ffffffii" },// CAMERA_SET_VECTOR_MOVE : la camera glisse d'un point a un autre (duree ms, lissage)
    { 0x0920, "ffffffii" },// CAMERA_SET_VECTOR_TRACK : le point vise glisse
    { 0x0922, "ffii" },    // CAMERA_SET_LERP_FOV
    { 0x092F, "i" },       // CAMERA_PERSIST_TRACK
    { 0x0930, "i" },       // CAMERA_PERSIST_POS
    { 0x02EB, "" },        // RESTORE_CAMERA_JUMPCUT
    { 0x0373, "" },        // SET_CAMERA_BEHIND_PLAYER
    { 0x02E4, "s" },       // LOAD_CUTSCENE (CCutsceneMgr::LoadCutsceneData 0x4D5E80)
    { 0x02E7, "" },        // START_CUTSCENE (0x5B1460) : attend que la cinematique soit chargee
    { 0x02EA, "" },        // CLEAR_CUTSCENE (0x4D5ED0) : attend que celle de l'invite soit finie
    { 0x0055, "ifff" },    // SET_PLAYER_COORDINATES : l'invite est pose a cote (decale selon son numero)
    { 0x0109, "ii" },      // ADD_SCORE : l'argent gagne en mission, pour chacun
    { 0x00A1, "Pfff" },    // SET_CHAR_COORDINATES (joueur de l'hote : l'invite est pose a cote)
    { 0x0860, "Pi" },      // LINK_CHAR_WITH_INTERIOR (joueur de l'hote : l'invite aussi)
    { 0x04BB, "i" },       // SET_AREA_VISIBLE : seulement si l'invite vient d'etre pose avec l'hote, ou est pres de lui
    { 0x0107, "ifffO" },   // CREATE_OBJECT (modele negatif : table des objets utilises 0xA44B70, id a +24)
    { 0x029B, "ifffO" },   // CREATE_OBJECT_NO_OFFSET
    { 0x0108, "o" },       // DELETE_OBJECT
    { 0x01BC, "offf" },    // SET_OBJECT_COORDINATES
    { 0x0177, "of" },      // SET_OBJECT_HEADING
    { 0x0382, "oi" },      // SET_OBJECT_COLLISION
    { 0x0750, "oi" },      // SET_OBJECT_VISIBLE
    { 0x0188, "oB" },      // ADD_BLIP_FOR_OBJECT
    { 0x01B2, "Pii" },     // GIVE_WEAPON_TO_CHAR : au joueur de l'hote -> a l'invite lui-meme (bombe de peinture...)
    { 0x0555, "Pi" },      // REMOVE_WEAPON_FROM_CHAR (idem)
    { 0x048F, "P" },       // REMOVE_ALL_CHAR_WEAPONS (idem)
    { 0x01B9, "Pi" },      // SET_CURRENT_CHAR_WEAPON (idem)
    { 0x0394, "i" },       // PLAY_MISSION_PASSED_TUNE
    { 0x0213, "iifffK" },  // CREATE_PICKUP (objets de mission : ramasses par un invite, ils comptent chez l'hote, events.cpp)
    { 0x032B, "iiifffK" }, // CREATE_PICKUP_WITH_AMMO
    { 0x0215, "k" },       // REMOVE_PICKUP
    { 0x020C, "fffi" },    // ADD_EXPLOSION
    { 0x0565, "fffi" },    // ADD_EXPLOSION_NO_SOUND
    { 0x0948, "fffif" },   // ADD_EXPLOSION_VARIABLE_SHAKE
    { 0x02CF, "fffiiR" },  // START_SCRIPT_FIRE
    { 0x02D1, "r" },       // REMOVE_SCRIPT_FIRE
};

// Commandes supplementaires (MiroirEtendu=1), portees de la liste de VCCoop. Signatures verifiees dans la base de
// commandes de Sanny Builder pour San Andreas (nombre et type des parametres) ; les memes lettres que ci-dessus.
static const MirrorOp kOpsExtra[] = {
    // Textes
    { 0x03D5, "s" },         // CLEAR_THIS_PRINT
    { 0x03D6, "s" },         // CLEAR_THIS_BIG_PRINT
    { 0x03EB, "" },          // CLEAR_SMALL_PRINTS
    { 0x0217, "sii" },       // PRINT_BIG_Q
    { 0x036D, "siiii" },     // PRINT_WITH_2_NUMBERS_BIG
    { 0x02FD, "siiii" },     // PRINT_WITH_2_NUMBERS_NOW
    { 0x02FF, "siiiii" },    // PRINT_WITH_3_NUMBERS
    { 0x0302, "siiiiii" },   // PRINT_WITH_4_NUMBERS
    { 0x0303, "siiiiii" },   // PRINT_WITH_4_NUMBERS_NOW
    { 0x0308, "siiiiiiii" }, // PRINT_WITH_6_NUMBERS
    { 0x0384, "ssii" },      // PRINT_STRING_IN_STRING_NOW
    { 0x0512, "s" },         // PRINT_HELP_FOREVER
    { 0x0513, "si" },        // PRINT_HELP_FOREVER_WITH_NUMBER
    { 0x0912, "iii" },       // SET_MESSAGE_FORMATTING
    { 0x09C1, "i" },         // ADD_NEXT_MESSAGE_TO_PREVIOUS_BRIEFS
    { 0x09EE, "i" },         // FORCE_BIG_MESSAGE_AND_COUNTER
    { 0x0989, "i" },         // SET_HELP_MESSAGE_BOX_SIZE
    // Ecran et cameras
    { 0x0169, "iii" },       // SET_FADING_COLOUR
    { 0x0003, "i" },         // SHAKE_CAM
    { 0x03C8, "" },          // SET_CAMERA_IN_FRONT_OF_PLAYER
    { 0x0460, "fi" },        // SET_INTERPOLATION_PARAMETERS
    // Marqueurs
    { 0x02A8, "fffiB" },     // ADD_SPRITE_BLIP_FOR_COORD
    { 0x0570, "fffiB" },     // ADD_SHORT_RANGE_SPRITE_BLIP_FOR_CONTACT_POINT
    { 0x03DC, "kB" },        // ADD_BLIP_FOR_PICKUP
    { 0x0161, "ViiB" },      // ADD_BLIP_FOR_CAR_OLD
    { 0x0167, "fffiiB" },    // ADD_BLIP_FOR_COORD_OLD
    { 0x07BF, "bi" },        // SET_BLIP_ALWAYS_DISPLAY_ON_ZOOMED_RADAR
    { 0x08DC, "bfff" },      // SET_BLIP_ENTRY_EXIT
    { 0x08FB, "bi" },        // SET_COORD_BLIP_APPEARANCE
    { 0x09A6, "i" },         // SHOW_BLIPS_ON_ALL_LEVELS
    { 0x09AC, "i" },         // HIDE_ALL_FRONTEND_BLIPS
    // Sons de mission
    { 0x018C, "fffi" },      // ADD_ONE_OFF_SOUND
    { 0x03D7, "ifff" },      // SET_MISSION_AUDIO_POSITION
    { 0x0949, "iP" },        // ATTACH_MISSION_AUDIO_TO_CHAR
    { 0x097C, "io" },        // ATTACH_MISSION_AUDIO_TO_OBJECT
    { 0x097A, "fffi" },      // REPORT_MISSION_AUDIO_EVENT_AT_POSITION
    { 0x097B, "oi" },        // REPORT_MISSION_AUDIO_EVENT_AT_OBJECT
    { 0x09F1, "Pi" },        // REPORT_MISSION_AUDIO_EVENT_AT_CHAR
    { 0x09F7, "Vi" },        // REPORT_MISSION_AUDIO_EVENT_AT_CAR
    { 0x0917, "si" },        // SWITCH_AUDIO_ZONE
    // Cinematiques
    { 0x03CB, "fff" },       // LOAD_SCENE: the scenery around the host's scene (cutscene place, interior) is loaded on the guest too
    { 0x04E4, "ff" },        // REQUEST_COLLISION
    { 0x0244, "fff" },       // SET_CUTSCENE_OFFSET
    { 0x08F0, "ss" },        // APPEND_TO_NEXT_CUTSCENE
    { 0x070C, "V" },         // EXPLODE_CAR_IN_CUTSCENE
    { 0x08CB, "Viii" },      // EXPLODE_CAR_IN_CUTSCENE_SHAKE_AND_BITS
    // Monde : routes, trafic, entrees, garages
    { 0x01E7, "ffffff" },    // SWITCH_ROADS_ON
    { 0x01E8, "ffffff" },    // SWITCH_ROADS_OFF
    { 0x022A, "ffffff" },    // SWITCH_PED_ROADS_ON
    { 0x022B, "ffffff" },    // SWITCH_PED_ROADS_OFF
    { 0x091D, "ffffff" },    // SWITCH_ROADS_BACK_TO_ORIGINAL
    { 0x091E, "ffffff" },    // SWITCH_PED_ROADS_BACK_TO_ORIGINAL
    { 0x06D0, "i" },         // SWITCH_EMERGENCY_SERVICES
    { 0x06D7, "i" },         // SWITCH_RANDOM_TRAINS
    { 0x0923, "i" },         // SWITCH_AMBIENT_PLANES
    { 0x096A, "i" },         // SWITCH_POLICE_HELIS
    { 0x072C, "i" },         // SWITCH_COPS_ON_BIKES
    { 0x09B7, "si" },        // SET_ZONE_NO_COPS
    { 0x07FB, "si" },        // SWITCH_ENTRY_EXIT
    { 0x0363, "ffffii" },    // SET_VISIBILITY_OF_CLOSEST_OBJECT_OF_TYPE
    { 0x0360, "s" },         // OPEN_GARAGE
    { 0x0361, "s" },         // CLOSE_GARAGE
    { 0x0299, "s" },         // ACTIVATE_GARAGE
    { 0x02B9, "s" },         // DEACTIVATE_GARAGE
    { 0x02FA, "si" },        // CHANGE_GARAGE_TYPE
    { 0x021B, "sV" },        // SET_TARGET_CAR_FOR_MISSION_GARAGE
    { 0x093A, "si" },        // SET_GARAGE_RESPRAY_FREE
    // Joueur, police
    { 0x01F0, "i" },         // SET_MAX_WANTED_LEVEL
    { 0x01F7, "ii" },        // SET_POLICE_IGNORE_PLAYER
    { 0x03BF, "ii" },        // SET_EVERYONE_IGNORE_PLAYER
    { 0x03C7, "f" },         // SET_WANTED_MULTIPLIER
    { 0x06AF, "ii" },        // DISABLE_PLAYER_SPRINT
    { 0x08DD, "i" },         // SWITCH_DEATH_PENALTIES
    { 0x08DE, "i" },         // SWITCH_ARREST_PENALTIES
    // Vehicules et personnages de mission (leurs copies)
    { 0x020A, "Vi" },        // LOCK_CAR_DOORS
    { 0x0568, "Pi" },        // SET_CHAR_NEVER_TARGETTED
    // Objets et ramassages de mission
    { 0x0453, "offf" },      // SET_OBJECT_ROTATION
    { 0x0392, "oi" },        // SET_OBJECT_DYNAMIC
    { 0x035C, "oVfff" },     // PLACE_OBJECT_RELATIVE_TO_CAR
    { 0x034E, "offffffi" },  // SLIDE_OBJECT
    { 0x01C7, "o" },         // DONT_REMOVE_OBJECT
    { 0x0566, "oi" },        // SET_OBJECT_AREA_VISIBLE
    { 0x02E1, "fffiiK" },    // CREATE_MONEY_PICKUP
};

static bool IsPersistBlipOp(int op) { return op == 0x04CE || op == 0x02A7; }   // icones de contact : gardees apres la mission


// Cinematiques : CCutsceneMgr::ms_cutsceneLoadStatus 0xB5F84C (2 = chargee), ms_running 0xB5F851,
// HasCutsceneFinished 0x5B0570.
static bool CutsceneLoaded() { return *(uint8_t *)0xB5F84C == 2; }
static bool CutsceneRunning() { return *(uint8_t *)0xB5F851 != 0; }
static bool CutsceneFinished() { return ((bool(__cdecl *)())0x5B0570)(); }
static const MirrorOp *FindOp(int op)
{
    for (auto &o : kOps) if (o.op == op) return &o;
    if (g_cfg.mirrorExtra) for (auto &o : kOpsExtra) if (o.op == op) return &o;
    return nullptr;
}

// --- Hote : lecture des parametres ---
static int *LocalVar(void *script, int idx)
{
    if (*((uint8_t *)script + 0xDC)) return (int *)0xA48960 + idx;
    return (int *)((uint8_t *)script + 0x3C) + idx;
}
static uint8_t *GlobalVar(int offset) { return (uint8_t *)0xA49960 + offset; }

struct Param { char kind; int value; char text[16]; int *out; };

// Lit un parametre a ip (avance ip) : valeur entiere/reelle ou texte, et l'adresse de la variable si c'en est une.
static bool ReadParam(void *script, uint8_t *&ip, Param &p)
{
    uint8_t t = *ip++;
    p.out = nullptr;
    memset(p.text, 0, sizeof(p.text));
    switch (t) {
    case 1: case 6: p.value = *(int *)ip; ip += 4; return true;
    case 4: p.value = *(int8_t *)ip; ip += 1; return true;
    case 5: p.value = *(int16_t *)ip; ip += 2; return true;
    case 2: p.out = (int *)GlobalVar(*(uint16_t *)ip); p.value = *p.out; ip += 2; return true;
    case 3: p.out = LocalVar(script, *(uint16_t *)ip); p.value = *p.out; ip += 2; return true;
    case 7: case 8: {
        uint16_t base = *(uint16_t *)ip, iv = *(uint16_t *)(ip + 2);
        int16_t flags = *(int16_t *)(ip + 4);
        ip += 6;
        int index = flags < 0 ? *(int *)GlobalVar(iv) : *LocalVar(script, iv);
        p.out = t == 7 ? (int *)GlobalVar(base + index * 4) : LocalVar(script, base + index);
        p.value = *p.out;
        return true;
    }
    case 9: memcpy(p.text, ip, 8); ip += 8; return true;
    case 0xA: memcpy(p.text, GlobalVar(*(uint16_t *)ip), 8); ip += 2; return true;
    case 0xB: memcpy(p.text, LocalVar(script, *(uint16_t *)ip), 8); ip += 2; return true;
    case 0xE: { int n = *ip++; memcpy(p.text, ip, n < 15 ? n : 15); ip += n; return true; }
    case 0xF: memcpy(p.text, ip, 15); ip += 16; return true;
    case 0x10: memcpy(p.text, GlobalVar(*(uint16_t *)ip), 15); ip += 2; return true;
    case 0x11: memcpy(p.text, LocalVar(script, *(uint16_t *)ip), 15); ip += 2; return true;
    }
    return false;   // type inconnu : la commande n'est pas capturee
}

struct Capture { const MirrorOp *op; int n; Param p[10]; };
static Capture g_cap;
static bool g_capturing;

// Hote : commandes encore actives (texte de mission, marqueurs, spheres), rejouees pour un invite qui arrive.
struct Active { int handle; int len; bool persist, after; uint8_t data[256]; };   // after : apres les creations
static Active g_active[160];
static void KeepActive(int op, const uint8_t *buf, int len, int handle)
{
    if (op == 0x0164 || op == 0x03BD || op == 0x0108 || op == 0x0215) {   // retrait
        for (auto &a : g_active) if (a.len && a.handle == handle) a.len = 0;
        if (op == 0x0108) for (auto &a : g_active) if (a.len && a.after && (a.handle & 0xFFFFFF) == (handle & 0xFFFFFF)) a.len = 0;
        return;
    }
    if (op == 0x054C) handle = -1;   // un seul texte de mission
    for (auto &a : g_active) if (a.len && a.handle == handle) a.len = 0;
    for (auto &a : g_active)
        if (!a.len) { a.handle = handle; a.len = len; a.persist = IsPersistBlipOp(op); a.after = op == 0x01BC || op == 0x0177 || op == 0x0382 || op == 0x0750; memcpy(a.data, buf, len); return; }
}
static void SendActive(int peer)
{
    for (auto &a : g_active) if (a.len && a.handle == -1) NetSendReliableTo(peer, a.data, a.len);   // texte d'abord
    for (auto &a : g_active) if (a.len && a.handle != -1 && !a.after) NetSendReliableTo(peer, a.data, a.len);
    for (auto &a : g_active) if (a.len && a.handle != -1 && a.after) NetSendReliableTo(peer, a.data, a.len);   // etat des objets
}

// --- Variables globales ---
enum { GLOBALS_BEGIN = 8, GLOBALS_END = 43808, GLOBALS_N = (GLOBALS_END - GLOBALS_BEGIN) / 4 };
static int g_snap[GLOBALS_N];
static bool g_snapValid;
static bool g_fullSent[MAX_PLAYERS];
static int *Globals() { return (int *)(0xA49960 + GLOBALS_BEGIN); }
static bool Shareable(int v) { return v >= -1 && v <= 255; }

// Envoie les globales (toutes, ou celles qui different de l'instantane) ; peer < 0 : a tous les invites.
static int SendGlobals(int peer, bool all)
{
    uint8_t buf[1 + 2 + 190 * 6];
    int n = 0, total = 0;
    const int *g = Globals();
    auto flush = [&]() {
        if (!n) return;
        buf[0] = RL_GLOBALS;
        *(uint16_t *)(buf + 1) = (uint16_t)n;
        if (peer < 0) NetSendReliable(buf, 3 + n * 6); else NetSendReliableTo(peer, buf, 3 + n * 6);
        n = 0;
    };
    for (int i = 0; i < GLOBALS_N; i++) {
        if (!Shareable(g[i]) || (!all && g[i] == g_snap[i])) continue;
        *(uint16_t *)(buf + 3 + n * 6) = (uint16_t)i;
        memcpy(buf + 3 + n * 6 + 2, &g[i], 4);
        n++; total++;
        if (n == 190) flush();
    }
    flush();
    return total;
}

// --- Marqueurs du radar ---
// CRadar::ms_RadarTrace 0xBA86F0 : 175 entrees de 0x28 octets. +0x00 couleur, +0x04 entite, +0x08 position, +0x14 compteur
// (poignee = indice | compteur << 16), +0x1C taille, +0x20 entree/sortie, +0x24 icone (int8), +0x25 drapeaux (0x01 vif,
// 0x02 en service, 0x04 courte portee), +0x26 affichage (bits 0-1), type (bits 2-5), apparence (bits 6-7).
// Les marqueurs que l'histoire pose depuis le script principal (mission suivante, icones de contact) ne passaient pas
// par le miroir de commandes, limite aux scripts de mission : l'invite gardait ceux de la sauvegarde et rien d'autre.
// L'hote envoie donc l'etat de ses marqueurs de coordonnees (types 4 et 5) ; l'invite les reproduit.
enum { TRACES = 175, TRACE_SIZE = 0x28, BLIP_WIRE = 21, BLIPS_PER_MSG = 11 };
struct Blip { float x, y, z; uint32_t colour; uint16_t size; int8_t sprite; uint8_t flags, kind; };
static uint8_t *Trace(int i) { return (uint8_t *)0xBA86F0 + i * TRACE_SIZE; }
static int BlipType(uint8_t kind) { return (kind >> 2) & 15; }

// Marqueur partage : en service, de type coordonnees (4) ou point de contact (5), pas une entree/sortie de batiment (creee
// par le jeu), pas la destination posee par le joueur sur la carte (icone 41).
static bool SharedTrace(const uint8_t *t)
{
    int type = BlipType(t[0x26]);   // (une case libre est de type 0 : ClearActualBlip)
    if (type != 4 && type != 5) return false;
    if (*(const uint32_t *)(t + 0x20)) return false;
    if ((int8_t)t[0x24] == 41) return false;
    return true;
}
static void ReadBlip(const uint8_t *t, Blip &b)
{
    memcpy(&b.x, t + 0x08, 4); memcpy(&b.y, t + 0x0C, 4); memcpy(&b.z, t + 0x10, 4);
    memcpy(&b.colour, t, 4);
    memcpy(&b.size, t + 0x1C, 2);
    b.sprite = (int8_t)t[0x24];
    b.flags = t[0x25];
    b.kind = t[0x26];
}
static void PackBlip(const Blip &b, uint8_t *w)
{
    memcpy(w, &b.x, 4); memcpy(w + 4, &b.y, 4); memcpy(w + 8, &b.z, 4);
    memcpy(w + 12, &b.colour, 4); memcpy(w + 16, &b.size, 2);
    w[18] = (uint8_t)b.sprite; w[19] = b.flags; w[20] = b.kind;
}
static void UnpackBlip(const uint8_t *w, Blip &b)
{
    memcpy(&b.x, w, 4); memcpy(&b.y, w + 4, 4); memcpy(&b.z, w + 8, 4);
    memcpy(&b.colour, w + 12, 4); memcpy(&b.size, w + 16, 2);
    b.sprite = (int8_t)w[18]; b.flags = w[19]; b.kind = w[20];
}
// Meme marqueur (type, icone, position a 0,5 pres) : l'etat (couleur, taille, drapeaux) peut differer.
static bool SameSpot(const Blip &a, const Blip &b)
{
    return BlipType(a.kind) == BlipType(b.kind) && a.sprite == b.sprite &&
           fabsf(a.x - b.x) < 0.5f && fabsf(a.y - b.y) < 0.5f && fabsf(a.z - b.z) < 1.5f;
}
static bool SameState(const Blip &a, const Blip &b)
{
    return a.colour == b.colour && a.size == b.size && a.flags == b.flags && ((a.kind ^ b.kind) & 0xC3) == 0;
}

// Hote : tous les 500 ms, l'etat des marqueurs partages ; renvoye a chaque invite en partie quand il change, a son arrivee, et
// toutes les 5 s (rien ne se perd, l'invite rapproche de toute facon).
static void HostBlipsFrame()
{
    if (!g_cfg.radarSync) return;
    static uint32_t scanAt, sentAt[MAX_PLAYERS], sentHash[MAX_PLAYERS];
    static bool sent[MAX_PLAYERS];
    static Blip list[TRACES];
    static int count, logged;
    static uint32_t hash = 1;
    uint32_t now = GetTickCount();
    if (now - scanAt >= 500) {
        scanAt = now;
        int n = 0;
        uint32_t h = 2166136261u;
        for (int i = 0; i < TRACES; i++) {
            const uint8_t *t = Trace(i);
            if (!SharedTrace(t)) continue;
            ReadBlip(t, list[n]);
            uint8_t w[BLIP_WIRE];
            PackBlip(list[n], w);
            for (int k = 0; k < BLIP_WIRE; k++) h = (h ^ w[k]) * 16777619u;
            n++;
        }
        if (n != count || h != hash) {
            if (logged++ < 40) Log("radar : l'hote a %d marqueurs partages (etait %d)", n, count);
            count = n; hash = h;
        }
    }
    for (int p = 1; p < MAX_PLAYERS; p++) {
        if (!g_players[p].connected || !g_players[p].state.inGame) { sent[p] = false; continue; }
        if (sent[p] && sentHash[p] == hash && now - sentAt[p] < 5000) continue;
        sent[p] = true; sentHash[p] = hash; sentAt[p] = now;
        int done = 0;
        do {
            int k = count - done < BLIPS_PER_MSG ? count - done : BLIPS_PER_MSG;
            uint8_t m[3 + BLIPS_PER_MSG * BLIP_WIRE];
            m[0] = RL_BLIPS;
            m[1] = (uint8_t)((done == 0 ? 1 : 0) | (done + k >= count ? 2 : 0));
            m[2] = (uint8_t)k;
            for (int i = 0; i < k; i++) PackBlip(list[done + i], m + 3 + i * BLIP_WIRE);
            NetSendReliableTo(p, m, 3 + k * BLIP_WIRE);
            done += k;
        } while (done < count);
    }
}

// Hote, chaque image : etat complet pour chaque invite arrive en partie.
static int g_hostSlot = -1;          // partie de l'hote : emplacement charge, -1 nouvelle partie
static bool g_sessionSent[MAX_PLAYERS];

static int g_seenSlot = -1;          // hote au menu : emplacement qu'il charge

static void HostGlobalsFrame()
{
    // La partie que l'hote a lancee depuis le menu (chargement ou nouvelle) est annoncee a chaque invite au menu.
    int &seenSlot = g_seenSlot;
    static bool wasInGame;
    bool inGame = GameState() == 9 && FindPlayerPed();
    if (inGame && !wasInGame) { g_hostSlot = seenSlot; seenSlot = -1; for (auto &b : g_sessionSent) b = false; }
    if (!inGame && wasInGame) seenSlot = -1;
    wasInGame = inGame;
    if (!inGame) return;
    HostBlipsFrame();
    for (int i = 1; i < MAX_PLAYERS; i++) {
        if (!g_players[i].connected) { g_sessionSent[i] = false; continue; }
        if (g_sessionSent[i] || g_players[i].state.inGame) continue;
        g_sessionSent[i] = true;
        uint8_t m[2] = { RL_SESSION, (uint8_t)(int8_t)g_hostSlot };
        NetSendReliableTo(i, m, 2);
        Log("miroir : partie annoncee au joueur %d (%s)", i, g_hostSlot >= 0 ? "sauvegarde" : "nouvelle partie");
    }
    for (int i = 1; i < MAX_PLAYERS; i++) {
        if (!g_players[i].connected) { g_fullSent[i] = false; continue; }
        if (g_fullSent[i] || !g_players[i].state.inGame) continue;
        g_fullSent[i] = true;
        Log("miroir : etat complet de l'histoire envoye au joueur %d (%d variables)", i, SendGlobals(i, true));
        SendActive(i);
    }
}

// Hote : une mission de l'histoire demarre (script.cpp) : position de l'hote pour le regroupement des invites.
void MirrorMissionStart()
{
    memcpy(g_snap, Globals(), sizeof(g_snap));
    g_snapValid = true;
    void *me = FindPlayerPed();
    if (!g_cfg.host || !NetRunning() || !me) return;
    uint8_t buf[1 + 16];
    buf[0] = RL_MISSION_START;
    memcpy(buf + 1, EntityPos(me), 12);
    float h = Field<float>(me, PED_ROTATION);
    memcpy(buf + 13, &h, 4);
    NetSendReliable(buf, sizeof(buf));
}

void MirrorBefore(void *script, int op)
{
    g_capturing = false;
    if (!g_cfg.host || !NetRunning() || !ScriptIsMission(script)) return;
    if (op == 0x00D8) {   // MISSION_HAS_FINISHED
        if (g_snapValid) Log("miroir : %d variables de l'histoire changees envoyees", SendGlobals(-1, false));
        for (auto &a : g_active) if (!a.persist) a.len = 0;
        g_snapValid = false;
        uint8_t m = RL_MISSION_END;
        NetSendReliable(&m, 1);
        Log("miroir : fin de mission envoyee");
        return;
    }
    const MirrorOp *mo = FindOp(op);
    if (!mo) return;
    uint8_t *ip = *(uint8_t **)((uint8_t *)script + 0x14);
    g_cap.op = mo;
    g_cap.n = (int)strlen(mo->sig);
    for (int k = 0; k < g_cap.n; k++) {
        g_cap.p[k].kind = mo->sig[k];
        if (!ReadParam(script, ip, g_cap.p[k])) return;
        if (g_cap.p[k].kind == 'P' && FindPlayerPed() && g_cap.p[k].value == PedRef(FindPlayerPed())) g_cap.p[k].kind = 'H';
    }
    g_capturing = true;
}

void MirrorAfter(void *script, int op)
{
    if (!g_capturing) return;
    g_capturing = false;
    uint8_t buf[256];
    int len = 0;
    buf[len++] = RL_MIRROR;
    if (op == 0x0157) { op = 0x0159; g_cap.p[0].kind = 'H'; }   // camera sur le joueur de l'hote : sur son pantin
    *(uint16_t *)(buf + len) = (uint16_t)op; len += 2;
    buf[len++] = (uint8_t)g_cap.n;
    for (int k = 0; k < g_cap.n; k++) {
        Param &p = g_cap.p[k];
        buf[len++] = (uint8_t)p.kind;
        if (p.kind == 's') {
            int n = (int)strnlen(p.text, 15);
            buf[len++] = (uint8_t)n;
            memcpy(buf + len, p.text, n); len += n;
        } else {
            int v = strchr("BQOKR", p.kind) && p.out ? *p.out : p.value;   // sortie : le handle de l'hote
            memcpy(buf + len, &v, 4); len += 4;
        }
    }
    NetSendReliable(buf, len);
    (void)script;
    // Commandes a garder pour un invite qui arrive : texte de mission, creation/retrait de marqueur ou sphere.
    // (objets : les deplacements apres la creation ne sont pas gardes ; cle distincte de celle des marqueurs)
    int handle = 0;
    // (MiroirEtendu : la cle est celle de la sortie s'il y en a une -- ADD_BLIP_FOR_PICKUP "kB" porte sur un ramassage mais
    // cree un marqueur --, et une commande qui ne fait que modifier une entite sans la creer, la retirer ni fixer son etat
    // n'est pas gardee : elle ecraserait la creation, et l'invite qui arrive ne verrait jamais l'entite.)
    bool extra = g_cfg.mirrorExtra, hasOut = false;
    for (int k = 0; k < g_cap.n; k++) if (strchr("BQOKR", g_cap.p[k].kind)) hasOut = true;
    for (int pass = 0; pass < (extra ? 2 : 1); pass++)
        for (int k = 0; k < g_cap.n && !handle; k++) {
            char kd = g_cap.p[k].kind;
            if (!strchr("BQbqOoKk", kd)) continue;
            bool isOut = kd == 'B' || kd == 'Q' || kd == 'O' || kd == 'K';
            if (extra && pass == 0 && hasOut && !isOut) continue;   // 1re passe : seulement les sorties
            handle = isOut && g_cap.p[k].out ? *g_cap.p[k].out : g_cap.p[k].value;
            if (kd == 'O' || kd == 'o') handle ^= 0x40000000;
            if (kd == 'K' || kd == 'k') handle ^= 0x60000000;
        }
    if (extra && handle && !hasOut && op != 0x0164 && op != 0x03BD && op != 0x0108 && op != 0x0215 &&
        op != 0x01BC && op != 0x0177 && op != 0x0382 && op != 0x0750)
        handle = 0;   // simple modification d'une entite : pas gardee
    if (op == 0x0188) handle = 0;   // marqueur d'objet : rejoue avec l'objet... non garde (ordre de recreation)
    // Etat d'un objet de mission (position, cap, collision, visibilite) : le dernier de chaque sorte est garde aussi,
    // renvoye apres les creations a un invite qui arrive en cours de mission (cle : objet + sorte, bit 0x10000000).
    if (handle && (op == 0x01BC || op == 0x0177 || op == 0x0382 || op == 0x0750)) {
        int kind = op == 0x01BC ? 0 : op == 0x0177 ? 1 : op == 0x0382 ? 2 : 3;
        handle = (handle & 0x00FFFFFF) | 0x10000000 | (kind << 24);
    }
    if (op == 0x054C || handle) KeepActive(op, buf, len, handle);
}

// --- Invite : rejeu ---
enum { MAX_MAP = 128 };
struct HandleMap { int host, guest; bool persist; };   // persist : icone radar (04CE, 02A7), gardee apres la mission
static HandleMap g_blips[MAX_MAP], g_spheres[MAX_MAP], g_objects[MAX_MAP], g_pickups[MAX_MAP], g_fires[MAX_MAP];
static int MapGet(HandleMap *m, int host)
{
    for (int i = 0; i < MAX_MAP; i++) if (m[i].host == host && host) return m[i].guest;
    return 0;
}
static void MapSet(HandleMap *m, int host, int guest, bool persist = false)
{
    for (int i = 0; i < MAX_MAP; i++) if (m[i].host == host) { m[i].guest = guest; m[i].persist = persist; return; }
    for (int i = 0; i < MAX_MAP; i++) if (!m[i].host) { m[i].host = host; m[i].guest = guest; m[i].persist = persist; return; }
}
static void MapDel(HandleMap *m, int host)
{
    for (int i = 0; i < MAX_MAP; i++) if (m[i].host == host) m[i].host = m[i].guest = 0;
}

enum { QUEUE = 1024 };
struct Pending { uint8_t data[256]; int len; uint32_t since; };
static Pending g_queue[QUEUE];
static int g_qHead, g_qTail;

static void OnReliable(int from, const uint8_t *data, int len)
{
    (void)from;
    if (SaveSyncReliable(data, len)) return;   // sauvegarde partagee (savesync.cpp) : traitee tout de suite
    if (ChatReliable(from, data, len)) return;  // tchat (chat.cpp)
    if (PoliceReliable(data, len)) return;      // ejection par un policier de l'hote (police.cpp)
    if (EventsReliable(from, data, len)) return;   // pickups, tags (events.cpp)
    if (len >= 2 && data[0] == RL_SESSION) {   // partie de l'hote : l'invite encore au menu la suit
        if (g_cfg.host) return;
        int slot = (int8_t)data[1];
        char path[MAX_PATH];
        wsprintfA(path, "%s%i.b", (const char *)0xC16F18, slot + 1);
        if (slot >= 0 && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) slot = -1;   // pas cette sauvegarde
        if (RequestGameStart(slot)) Log("miroir : partie de l'hote suivie (%s)", slot >= 0 ? path : "nouvelle partie");
        return;
    }
    if (g_cfg.host || len < 1 || len > 256) return;
    if ((g_qTail + 1) % QUEUE == g_qHead) { Log("miroir : file pleine, commande perdue"); return; }
    Pending &p = g_queue[g_qTail];
    memcpy(p.data, data, len);
    p.len = len;
    p.since = 0;
    g_qTail = (g_qTail + 1) % QUEUE;
}

alignas(16) static uint8_t g_script[0x200];   // CRunningScript fantome de l'invite
static uint8_t g_code[300];

static void ExecCommand(int op)
{
    *(uint8_t **)(g_script + 0x10) = g_code;
    *(uint8_t **)(g_script + 0x14) = g_code + 2;
    ScriptOriginalHandler(op / 100)(g_script, op);
}

// --- Invite : marqueurs du radar de l'hote ---
// Les messages RL_BLIPS passent par la meme file que les commandes rejouees (ordre garde : un marqueur de mission cree
// par le miroir est deja la quand l'etat des marqueurs arrive, il est reconnu et pas double). Pour chaque marqueur
// de l'hote : le meme chez nous (type, icone, position) est reconnu, sinon il est cree. Un marqueur de chez nous
// qui n'est plus chez l'hote est retire s'il y etait au dernier etat recu, ou, au premier etat de la partie, s'il n'y
// est pas (la sauvegarde de l'invite date d'avant). Les marqueurs que seul l'invite a (activites locales) ne sont
// pas touches. L'etat (couleur, taille, drapeaux) n'est recopie que s'il vient de changer chez l'hote.
static Blip g_got[TRACES + BLIPS_PER_MSG], g_prevBlips[TRACES];
static int g_gotN, g_prevN;
static bool g_havePrev;
static bool g_hostMission;   // the host is in a story mission (RL_MISSION_START .. RL_MISSION_END)
static bool g_partial;       // a list from the host is still being received (several messages)

static int CreateBlip(const Blip &b)
{
    int type = BlipType(b.kind);
    bool shortRange = (b.flags & 0x04) != 0;
    int op = type == 5 ? (shortRange ? 0x0570 : 0x02A7) : (shortRange ? 0x04CE : 0x018A);
    int c = 2;
    *(uint16_t *)g_code = (uint16_t)op;
    const float xyz[3] = { b.x, b.y, b.z };
    for (float f : xyz) { g_code[c++] = 6; memcpy(g_code + c, &f, 4); c += 4; }
    if (op != 0x018A) { int sp = b.sprite; g_code[c++] = 1; memcpy(g_code + c, &sp, 4); c += 4; }
    g_code[c++] = 3; *(uint16_t *)(g_code + c) = 0; c += 2;   // sortie : variable locale 0
    *(int *)(g_script + 0x3C) = -1;
    ExecCommand(op);
    int handle = *(int *)(g_script + 0x3C);
    int idx = handle & 0xFFFF;
    if (handle == -1 || idx >= TRACES || BlipType(Trace(idx)[0x26]) == 0) return -1;
    return idx;
}
static void ApplyBlipState(int idx, const Blip &b)
{
    uint8_t *t = Trace(idx);
    memcpy(t, &b.colour, 4);
    memcpy(t + 0x1C, &b.size, 2);
    t[0x24] = (uint8_t)b.sprite;
    t[0x25] = (uint8_t)((b.flags & ~0x02) | (t[0x25] & 0x02));   // drapeaux de l'hote, sauf "en service" (bit 1) qui reste le notre
    t[0x26] = (uint8_t)((t[0x26] & 0x3C) | (b.kind & 0xC3));   // le type reste celui de la creation
}
static void RemoveBlipAt(int idx)
{
    int handle = idx | (*(uint16_t *)(Trace(idx) + 0x14) << 16);
    *(uint16_t *)g_code = 0x0164;
    g_code[2] = 1; memcpy(g_code + 3, &handle, 4);
    ExecCommand(0x0164);
}

static void BlipsReconcile()
{
    bool used[TRACES] = {};
    int made = 0, updated = 0, removed = 0, failed = 0;
    for (int h = 0; h < g_gotN; h++) {
        const Blip &b = g_got[h];
        int found = -1;
        for (int i = 0; i < TRACES && found < 0; i++) {
            if (used[i] || !SharedTrace(Trace(i))) continue;
            Blip g;
            ReadBlip(Trace(i), g);
            if (SameSpot(g, b)) found = i;
        }
        bool apply = false;
        if (found < 0) {
            found = CreateBlip(b);
            if (found < 0) { failed++; continue; }
            made++;
            apply = true;
        } else {
            apply = !g_havePrev;
            if (!apply) {
                bool known = false;
                for (int p = 0; p < g_prevN && !known; p++) if (SameSpot(g_prevBlips[p], b)) { known = true; apply = !SameState(g_prevBlips[p], b); }
                if (!known) apply = true;
            }
            if (apply) updated++;
        }
        used[found] = true;
        if (apply) ApplyBlipState(found, b);
    }
    for (int i = 0; i < TRACES; i++) {
        if (used[i] || !SharedTrace(Trace(i))) continue;
        Blip g;
        ReadBlip(Trace(i), g);
        // During a host mission the host's list is the whole truth: the guest's own mission-start letters and contact
        // icons (from his own save and script) are not on the host's map any more, so they must not be on the guest's
        // either (test of 14/10: the joining player still had all the other mission letters).
        bool hostHadIt = !g_havePrev || g_hostMission;
        for (int p = 0; p < g_prevN && !hostHadIt; p++) if (SameSpot(g, g_prevBlips[p])) hostHadIt = true;
        if (!hostHadIt) continue;
        RemoveBlipAt(i);
        removed++;
    }
    static int logged;
    if ((made || removed || failed || !g_havePrev) && logged++ < 60)
        Log("radar : %d marqueurs de l'hote, %d crees, %d retires, %d mis a jour%s%s", g_gotN, made, removed, updated,
            failed ? ", creation impossible (table pleine)" : "", g_havePrev ? "" : " (premier etat de la partie)");
    memcpy(g_prevBlips, g_got, sizeof(Blip) * g_gotN);
    g_prevN = g_gotN;
    g_havePrev = true;
}

static void BlipsReplay(const uint8_t *d, int len)
{
    if (!g_cfg.radarSync || len < 3) return;
    int n = d[2];
    if (len < 3 + n * BLIP_WIRE) return;
    if (d[1] & 1) g_gotN = 0;
    for (int i = 0; i < n && g_gotN < TRACES; i++) UnpackBlip(d + 3 + i * BLIP_WIRE, g_got[g_gotN++]);
    g_partial = !(d[1] & 2);
    if (d[1] & 2) BlipsReconcile();
}

// The engine hides every contact-point blip (and its 3D marker) while the player is "on a mission", i.e. while the script
// variable ONMISSION, ScriptSpace[CTheScripts::OnAMissionFlag], is 1 (CTheScripts::IsPlayerOnAMission, 0x464D50). The host's
// main script sets it, so the host's radar is clean; the guest never ran the mission start, so all the contact icons stayed
// on his radar although the same blips were in the host's list too (hidden there) - test of 14/10. While the host is in a
// mission the guest's flag is forced to 1 as well, and put back when it ends.
static bool g_omForced;
static int g_omSaved;
static uint32_t *OnMissionVar()
{
    int off = *(int *)0xA476AC;
    if (off <= 0 || off >= 0x40000 || (off & 3)) return nullptr;
    return (uint32_t *)((uint8_t *)0xA49960 + off);
}
static void MissionFlagRestore()
{
    if (!g_omForced) return;
    g_omForced = false;
    if (uint32_t *v = OnMissionVar()) {
        if (*v == 1) *v = (uint32_t)g_omSaved;
        Log("miroir : drapeau MISSION de l'invite remis a %d", g_omSaved);
    }
}
static void MissionFlagFrame()
{
    if (!g_hostMission) { MissionFlagRestore(); return; }
    uint32_t *v = OnMissionVar();
    if (!v) { static bool said; if (!said) { said = true; Log("miroir : drapeau MISSION introuvable (decalage %d)", *(int *)0xA476AC); } return; }
    if (*v == 1) return;
    if (!g_omForced) { g_omForced = true; g_omSaved = (int)*v; Log("miroir : mission de l'hote, drapeau MISSION de l'invite mis a 1 (etait %d, variable a l'offset %d): icones de contact masquees", g_omSaved, *(int *)0xA476AC); }
    *v = 1;
}

// The host's mission starts or ends: from now on (or no more) the guest's own coordinate / contact blips that the host
// does not have are removed. At the start the last list received is reconciled again right away (the host's main
// script has just removed its mission letters; waiting for the next list could take up to 5 s).
static void BlipsMissionState(bool active)
{
    g_hostMission = active;
    if (!active || !g_cfg.radarSync || !g_havePrev || g_partial || g_prevN <= 0) return;
    memcpy(g_got, g_prevBlips, sizeof(Blip) * g_prevN);
    g_gotN = g_prevN;
    BlipsReconcile();
}

// Guest, every 100 ms during a host mission: the guest looks like the host. Only the host's blips stay (its objective
// blips and what the mission put on the map); a mission-start letter or contact icon that the guest's own main script
// makes again (OG, BS...) is removed on the spot, instead of waiting for the next list from the host. Blips that the
// mission mirror created are left alone: they are in the host's list within half a second.
static bool MirrorOwnsBlip(int idx)
{
    for (auto &b : g_blips) if (b.host && (b.guest & 0xFFFF) == idx) return true;
    return false;
}
static void BlipsMissionSweep()
{
    if (!g_hostMission || !g_cfg.radarSync || !g_havePrev || g_partial) return;
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (now - last < 100) return;
    last = now;
    int removed = 0;
    for (int i = 0; i < TRACES; i++) {
        if (!SharedTrace(Trace(i)) || MirrorOwnsBlip(i)) continue;
        Blip g;
        ReadBlip(Trace(i), g);
        bool hostHas = false;
        for (int p = 0; p < g_prevN && !hostHas; p++) if (SameSpot(g, g_prevBlips[p])) hostHas = true;
        if (hostHas) continue;
        RemoveBlipAt(i);
        removed++;
    }
    static int logged;
    if (removed && logged++ < 20) Log("radar : mission de l'hote, %d marqueur(s) de l'invite retire(s) (le script de l'invite les refait)", removed);
}

static void RestoreScreen();
static uint32_t g_teleportedAt;
static bool IsWeaponOp(int op) { return op == 0x01B2 || op == 0x0555 || op == 0x048F || op == 0x01B9; }
static uint32_t g_mirrorCamAt;   // invite : derniere commande de camera rejouee (garde-fou CameraWatch)

// Mouvements de camera en cours (CAMERA_SET_VECTOR_MOVE/TRACK 0936/0920, gardes par CAMERA_PERSIST_* 092F/0930) :
// effaces comme le fait le jeu (0x50D2D0 : actifs +0xC8C/+0xCEC, persistants +0xCEE/+0xCEF de TheCamera) au retour de
// la camera derriere le joueur. Apres la cinematique des velos (02/10), la camera de l'invite restait en l'air.
static void CameraVectorReset()
{
    uint8_t *cam = (uint8_t *)0xB6F028;
    cam[0xC8C] = cam[0xCEC] = cam[0xCEE] = cam[0xCEF] = 0;
}   // invite : derniere teleportation avec l'hote (SET_AREA_VISIBLE a suivre)

// Text commands (PRINT*, PRINT_HELP*): the host's mission text table must be loaded on the guest too, or the key gives
// nothing and the popup never shows (Home Invasion, 14/10: the guest saw none of the help boxes although PRINT_HELP_FOREVER
// was replayed). The last LOAD_MISSION_TEXT (054C) is remembered; if a key comes back empty the table is loaded again.
// The first lookups are logged (key and the start of the text) to see what the guest really has.
static char g_missionText[16];
static bool IsTextOp(int op) { return op == 0x03E5 || op == 0x0512 || op == 0x0513 || (op >= 0x00BA && op <= 0x00BC) || (op >= 0x01E3 && op <= 0x01E5); }
static void TextCheck(int op, const uint8_t *d, int len)
{
    if (len < 7 || d[4] != 's') return;
    int sl = d[5];
    if (sl <= 0 || sl > 15 || 6 + sl > len) return;
    char key[16] = {};
    memcpy(key, d + 6, sl);
    auto lookup = [&]() { return (const uint8_t *)GxtLookup(key); };   // CText::Get gives 8-bit characters
    const uint8_t *t = lookup();
    static uint32_t lastReload;
    bool missing = !t || !t[0];
    if (missing && g_missionText[0] && GetTickCount() - lastReload > 2000) {
        lastReload = GetTickCount();
        int arg = (int)(uintptr_t)g_missionText;
        RunScriptCommandTyped(0x054C, 1, "s", &arg);
        t = lookup();
        Log("texte : cle \"%s\" vide chez l'invite, table de mission \"%s\" rechargee -> %s", key, g_missionText, t && t[0] ? "trouvee" : "toujours vide");
    }
    static int said;
    if (said < 40) {
        said++;
        char s[48] = {};
        if (t) for (int i = 0; i < 40 && t[i]; i++) s[i] = t[i] >= 32 && t[i] < 127 ? (char)t[i] : '?';
        Log("texte : %04X cle \"%s\" -> %s", op, key, !t ? "(pas de texte)" : !t[0] ? "(vide)" : s);
    }
}

// Rejoue une commande ; faux si une entite manque encore (on reessaiera).
static bool Replay(const uint8_t *d, int len)
{
    if (d[0] == RL_MISSION_END) {
        BlipsMissionState(false);
        MissionFlagRestore();
        VehiclesMissionEnded();
        PopulationHostDensity(false, 1.0f);   // densites normales apres la mission de l'hote
        PopulationHostDensity(true, 1.0f);
        for (auto &b : g_blips) if (b.host && !b.persist) {
            *(uint16_t *)g_code = 0x0164;
            g_code[2] = 1; memcpy(g_code + 3, &b.guest, 4);
            ExecCommand(0x0164);
            b.host = b.guest = 0;
        }
        for (auto &o : g_objects) if (o.host) {   // objets de mission (chez l'hote, le nettoyage de mission les retire)
            *(uint16_t *)g_code = 0x0108;
            g_code[2] = 1; memcpy(g_code + 3, &o.guest, 4);
            ExecCommand(0x0108);
            o.host = o.guest = 0;
        }
        for (auto &s : g_spheres) if (s.host) {
            *(uint16_t *)g_code = 0x03BD;
            g_code[2] = 1; memcpy(g_code + 3, &s.guest, 4);
            ExecCommand(0x03BD);
            s.host = s.guest = 0;
        }
        for (auto &k : g_pickups) if (k.host) {   // pickups de mission restants
            *(uint16_t *)g_code = 0x0215;
            g_code[2] = 1; memcpy(g_code + 3, &k.guest, 4);
            ExecCommand(0x0215);
            k.host = k.guest = 0;
        }
        for (auto &f : g_fires) if (f.host) {
            *(uint16_t *)g_code = 0x02D1;
            g_code[2] = 1; memcpy(g_code + 3, &f.guest, 4);
            ExecCommand(0x02D1);
            f.host = f.guest = 0;
        }
        RestoreScreen();
        Log("miroir : fin de mission de l'hote, ecran retabli");
        return true;
    }
    if (d[0] == RL_BLIPS) { BlipsReplay(d, len); return true; }
    if (d[0] == RL_GLOBALS && len >= 3) {
        int n = *(const uint16_t *)(d + 1);
        int *g = Globals();
        for (int k = 0; k < n && 3 + k * 6 + 6 <= len; k++) {
            int idx = *(const uint16_t *)(d + 3 + k * 6);
            if (idx < GLOBALS_N) memcpy(&g[idx], d + 3 + k * 6 + 2, 4);
        }
        return true;
    }
    if (d[0] == RL_MISSION_START && len >= 17) {
        BlipsMissionState(true);
        void *me = FindPlayerPed();
        float host[3], h;
        memcpy(host, d + 1, 12);
        memcpy(&h, d + 13, 4);
        const float *p = EntityPos(me);
        float dx = p[0] - host[0], dy = p[1] - host[1], dz = p[2] - host[2];
        if (dx * dx + dy * dy + dz * dz > 40.0f * 40.0f && !PedVehicle(me)) {
            // derriere l'hote (avant = (-sin h, cos h)), en ligne selon le numero du joueur
            float back = 2.0f, side = 1.2f * (g_localId - 2);
            float pos[3] = { host[0] + sinf(h) * back + cosf(h) * side, host[1] - cosf(h) * back + sinf(h) * side, host[2] };
            // (into the host's interior too, with the scenery loaded: a mission that starts indoors)
            int hostArea = g_players[0].state.inGame ? g_players[0].state.area : 0;
            TeleportPed(me, pos, h, hostArea, "mission start");
            Log("miroir : mission de l'hote, regroupe derriere lui");
        }
        return true;
    }
    if (d[0] != RL_MIRROR || len < 4) return true;
    int op = *(const uint16_t *)(d + 1), n = d[3];
    if (IsWeaponOp(op) && len > 4 && d[4] != 'H') return true;   // armes d'un personnage de mission : sa copie suit le reseau
    if (op == 0x02E7 && !CutsceneLoaded()) return false;                           // chargement en cours
    if (op == 0x02EA && CutsceneRunning() && !CutsceneFinished()) {
        // L'hote a fini (ou passe) sa cinematique : on passe celle de l'invite, comme un appui sur Croix
        // (PCTempJoyState de la manette 0, +0x20), une image sur deux.
        static uint32_t frame;
        static const bool noSkip = GetPrivateProfileIntA("SACoop", "TestSansPasser", 0, IniPath()) != 0;   // test : passer impossible
        if (!noSkip) ((int16_t *)(0xB73458 + 0xA8))[0x20 / 2] = (++frame & 2) ? 255 : 0;
        return false;
    }
    if (op == 0x02EA) ((int16_t *)(0xB73458 + 0xA8))[0x20 / 2] = 0;
    if ((op == 0x0107 || op == 0x029B || op == 0x0213 || op == 0x032B) && len >= 9 && d[4] == 'i') {   // modele de l'objet charge avant
        int model;
        memcpy(&model, d + 5, 4);
        if (model < 0) model = *(int *)(0xA44B88 + -model * 28);
        if (model <= 0 || model >= 20000) return true;
        if (!ModelLoaded(model)) { RequestModel(model, 2); LoadAllRequestedModels(false); }
        if (!ModelLoaded(model)) return false;
    }
    int pos = 4, out = 0;
    int c = 0;
    *(uint16_t *)g_code = (uint16_t)op;
    c = 2;
    int outHost[4] = {}, outIdx[4] = {};
    char outKind[4] = {};
    for (int k = 0; k < n; k++) {
        if (pos >= len) return true;
        char kind = (char)d[pos++];
        if (kind == 's') {
            int sl = d[pos++];
            g_code[c++] = 0xE; g_code[c++] = (uint8_t)sl;
            memcpy(g_code + c, d + pos, sl); c += sl; pos += sl;
            continue;
        }
        int v;
        memcpy(&v, d + pos, 4); pos += 4;
        switch (kind) {
        case 'P': {
            void *ped = MissionCopyById((uint32_t)v);
            if (!ped) return false;
            v = PedRef(ped);
            break;
        }
        case 'H': {
            // Joueur de l'hote : pour une teleportation ou un interieur, c'est l'invite lui-meme qui suit ;
            // sinon (marqueur, camera...), son pantin chez l'invite.
            void *ped = op == 0x00A1 || op == 0x0860 || IsWeaponOp(op) ? FindPlayerPed() : PuppetOf(0);
            if (!ped) return false;
            v = PedRef(ped);
            break;
        }
        case 'V': {
            void *veh = NetVehicleByOwnerRef(0, v);
            if (!veh) return false;
            v = VehicleRef(veh);
            break;
        }
        case 'b': v = MapGet(g_blips, v); if (!v) return true; break;      // marqueur inconnu : rien a faire
        case 'q': v = MapGet(g_spheres, v); if (!v) return true; break;
        case 'o': v = MapGet(g_objects, v); if (!v) return true; break;
        case 'k': v = MapGet(g_pickups, v); if (!v) return true; break;
        case 'r': v = MapGet(g_fires, v); if (!v) return true; break;
        case 'B': case 'Q': case 'O': case 'K': case 'R':
            outKind[out] = kind; outHost[out] = v; outIdx[out] = out;
            g_code[c++] = 3; *(uint16_t *)(g_code + c) = (uint16_t)out; c += 2;
            out++;
            continue;
        }
        g_code[c++] = kind == 'f' ? 6 : 1;
        memcpy(g_code + c, &v, 4); c += 4;
    }
    // SET_PLAYER_COORDINATES / SET_CHAR_COORDINATES du joueur de l'hote : a cote de lui, decale selon le numero.
    bool selfTeleport = op == 0x0055 || (op == 0x00A1 && d[4] == 'H');
    if (selfTeleport && c >= 2 + 20) {
        float x;
        memcpy(&x, g_code + 2 + 5 + 1, 4);
        x += 1.5f * g_localId;
        memcpy(g_code + 2 + 5 + 1, &x, 4);
        if (PedVehicle(FindPlayerPed())) return true;   // en voiture : on ne l'arrache pas
        g_teleportedAt = GetTickCount();
    }
    if (op == 0x0860 && d[4] != 'H') return true;   // interieur d'un autre personnage : sa copie suit le reseau
    if (op == 0x01B2) {   // modeles de l'arme charges avant (sinon arme sans modele)
        int type; memcpy(&type, g_code + 2 + 5 + 1, 4);
        if (uint8_t *info = type > 0 && type < 47 ? WeaponInfo(type) : nullptr) {
            const int models[2] = { *(int *)(info + 0xC), *(int *)(info + 0x10) };
            for (int m : models) if (m > 0 && !ModelLoaded(m)) { RequestModel(m, 2); LoadAllRequestedModels(false); }
        }
    }
    // Densites des passants / de la circulation reglees par la mission : seulement pres de l'hote (population.cpp) ;
    // appliquees partout, un invite loin de l'hote n'avait plus personne autour de lui (test du 04/10).
    if (op == 0x03DE || op == 0x01EB) {
        float f; memcpy(&f, g_code + 2 + 1, 4);
        PopulationHostDensity(op == 0x01EB, f);
        return true;
    }
    if (op == 0x04BB) {   // zone visible : seulement avec l'hote, sinon la ville de l'invite se dechargerait
        void *host = PuppetOf(0), *me = FindPlayerPed();
        bool closeBy = false;
        if (host && me) {
            const float *a = EntityPos(host), *b = EntityPos(me);
            float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
            closeBy = dx * dx + dy * dy + dz * dz < 40.0f * 40.0f;
        }
        if (!closeBy && GetTickCount() - g_teleportedAt > 3000) return true;
    }
    if (op == 0x054C && len > 6 && d[4] == 's' && d[5] > 0 && d[5] < 16) { memset(g_missionText, 0, sizeof(g_missionText)); memcpy(g_missionText, d + 6, d[5]); }
    if (IsTextOp(op)) TextCheck(op, d, len);
    if (op == 0x015A || op == 0x02EB || op == 0x0373) CameraVectorReset();
    if (op == 0x015F || op == 0x0160 || (op >= 0x0157 && op <= 0x0159) || (op >= 0x0679 && op <= 0x067E) || op == 0x0936 || op == 0x0920) g_mirrorCamAt = GetTickCount();
    ExecCommand(op);
    static int logged;
    static uint8_t perOp[0x1000];   // (the first 20 of each command: CLEAR_HELP every frame used up the whole budget)
    if (logged < 700 && perOp[op & 0xFFF] < 20) { perOp[op & 0xFFF]++; logged++; Log("miroir : %04X rejouee", op); }
    for (int k = 0; k < out; k++) {
        int guest = *(int *)(g_script + 0x3C + outIdx[k] * 4);
        HandleMap *m = outKind[k] == 'B' ? g_blips : outKind[k] == 'O' ? g_objects : outKind[k] == 'K' ? g_pickups : outKind[k] == 'R' ? g_fires : g_spheres;
        MapSet(m, outHost[k], guest, IsPersistBlipOp(op));
    }
    // The guest was just put somewhere else by the host's script (next to the host, or into the interior it entered): the
    // scenery and collisions around the new spot are loaded on the spot (LOAD_SCENE 03CB), as TeleportPed does. Without it
    // a guest brought into an interior saw only the sky and fell (Home Invasion, 14/10).
    if (selfTeleport || (op == 0x0860 && d[4] == 'H')) {
        if (void *me = FindPlayerPed()) {
            int sc[3];
            memcpy(sc, EntityPos(me), 12);
            RunScriptCommandTyped(0x03CB, 3, "fff", sc);
            NoteLocalRelocated();
            Log("miroir : decor charge autour de l'invite apres %04X (%.1f %.1f %.1f, interieur %d)", op, *(float *)&sc[0], *(float *)&sc[1], *(float *)&sc[2], (int)EntityArea(me));
        }
    }
    if (op == 0x0164) MapDel(g_blips, *(const int *)(d + 5));
    if (op == 0x03BD) MapDel(g_spheres, *(const int *)(d + 5));
    if (op == 0x0108) MapDel(g_objects, *(const int *)(d + 5));
    if (op == 0x0215) MapDel(g_pickups, *(const int *)(d + 5));
    if (op == 0x02D1) MapDel(g_fires, *(const int *)(d + 5));
    return true;
}

// Ecran de l'invite apres une mission de l'hote : fondu d'entree, format normal, controles, camera, textes, sons.
static void RestoreScreen()
{
    CameraVectorReset();
    static const struct { uint16_t op; int args[2]; int n; } cmds[] = {
        // (pas de CLEAR_PRINTS 00BE : il effacait "MISSION ACCOMPLIE / RESPECT +" et "MISSION ECHOUEE", affiches
        // juste avant la fin de mission, test du 04/10)
        { 0x016A, { 500, 1 }, 2 }, { 0x02A3, { 0 }, 1 }, { 0x01B4, { 0, 1 }, 2 }, { 0x02EB, {}, 0 },
        { 0x03E6, {}, 0 }, { 0x040D, { 1 }, 1 }, { 0x040D, { 2 }, 1 },
    };
    for (auto &cmd : cmds) {
        int c = 2;
        *(uint16_t *)g_code = cmd.op;
        for (int k = 0; k < cmd.n; k++) { g_code[c++] = 1; memcpy(g_code + c, &cmd.args[k], 4); c += 4; }
        ExecCommand(cmd.op);
    }
}

// Ecrit le resultat d'une commande dans sa variable de sortie (parametre suivant du script, qui avance).
bool ScriptStoreResult(void *script, int value)
{
    uint8_t *&ip = *(uint8_t **)((uint8_t *)script + 0x14);
    Param p;
    if (!ReadParam(script, ip, p) || !p.out) return false;
    *p.out = value;
    return true;
}

// Valeurs des n premiers parametres d'une commande (ip juste apres l'opcode), sans avancer le script.
bool ScriptReadValues(void *script, uint8_t *ip, int n, int *vals)
{
    Param p;
    for (int k = 0; k < n; k++) {
        if (!ReadParam(script, ip, p)) return false;
        vals[k] = p.value;
    }
    return true;
}

// Execute une commande dans le script fantome ; types[k] : 'i' entier, 'f' reel.
void RunScriptCommandTyped(int op, int nargs, const char *types, const int *args)
{
    memcpy(g_script + 8, "sacoop", 7);
    int c = 2;
    *(uint16_t *)g_code = (uint16_t)op;
    for (int k = 0; k < nargs; k++) {
        if (types[k] == 's') {   // chaine (args[k] = son adresse) : type 0x0E, longueur puis caracteres
            const char *str = (const char *)(uintptr_t)args[k];
            int len = lstrlenA(str);
            if (len > 31) len = 31;
            g_code[c++] = 0x0E;
            g_code[c++] = (uint8_t)len;
            memcpy(g_code + c, str, len);
            c += len;
            continue;
        }
        if (types[k] == 'o') {   // sortie : variable locale args[k] du script fantome (ScriptGhostVar)
            g_code[c++] = 3; *(uint16_t *)(g_code + c) = (uint16_t)args[k]; c += 2;
            continue;
        }
        g_code[c++] = types[k] == 'f' ? 6 : 1; memcpy(g_code + c, &args[k], 4); c += 4;
    }
    ExecCommand(op);
}
int ScriptGhostVar(int i) { return *(int *)(g_script + 0x3C + i * 4); }

// Execute une commande a parametres entiers dans le script fantome (autotests, retablissement de l'ecran).
void RunScriptCommand(int op, int nargs, const int *args)
{
    memcpy(g_script + 8, "sacoop", 7);
    int c = 2;
    *(uint16_t *)g_code = (uint16_t)op;
    for (int k = 0; k < nargs; k++) { g_code[c++] = 1; memcpy(g_code + c, &args[k], 4); c += 4; }
    ExecCommand(op);
}

// Autotest "objet" (hote, hors mission) : un baril (1225) cree, deplace puis supprime par le script fantome marque
// "mission", comme le ferait une mission : les commandes passent par la capture du miroir.
void MirrorTestObject(int step, const float *pos)
{
    static int handle;
    memcpy(g_script + 8, "sacoop", 7);
    g_script[0xDC] = 1;
    int c = 2, op = step == 0 ? 0x0107 : step == 1 ? 0x01BC : 0x0108;
    auto i32 = [&](uint8_t t, const void *v) { g_code[c++] = t; memcpy(g_code + c, v, 4); c += 4; };
    *(uint16_t *)g_code = (uint16_t)op;
    if (step == 0) {
        int model = 1225;
        if (!ModelLoaded(model)) { RequestModel(model, 2); LoadAllRequestedModels(false); }
        i32(1, &model); i32(6, &pos[0]); i32(6, &pos[1]); i32(6, &pos[2]);
        g_code[c++] = 3; *(uint16_t *)(g_code + c) = 0; c += 2;   // sortie : locale de mission 0
    } else {
        i32(1, &handle);
        if (step == 1) { i32(6, &pos[0]); i32(6, &pos[1]); i32(6, &pos[2]); }
    }
    *(uint8_t **)(g_script + 0x10) = g_code;
    *(uint8_t **)(g_script + 0x14) = g_code + 2;
    MirrorBefore(g_script, op);
    ExecCommand(op);
    MirrorAfter(g_script, op);
    if (step == 0) handle = *(int *)0xA48960;
    g_script[0xDC] = 0;
    Log("autotest : objet %d (etape %d)", handle, step);
}

// Chaque tour de boucle, menu compris : reception du flux fiable (l'invite au menu doit entendre RL_SESSION), et
// hote au menu : emplacement en cours de chargement.
void MirrorMenuFrame()
{
    g_onReliable = OnReliable;
    if (g_cfg.host && GameState() == 7) { int s = MenuLoadingSlot(); if (s >= 0) g_seenSlot = s; }
}

// Invite arrive apres les fondus de l'hote (sa propre INTRO est bloquee) : reste noir alors que l'hote voit clair.
// Si l'ecran est noir sans fondu en cours (CCamera +0xBFC alpha, +0x51 m_bFading) depuis 2 s, joueur vivant, hors
// cinematique, et que l'hote est clair : fondu d'entree (DO_FADE 500 ms).
static void UnstickFade()
{
    static uint32_t stuckSince;
    uint8_t *cam = (uint8_t *)0xB6F028;
    float alpha = *(float *)(cam + 0xBFC);
    // Pendant une cinematique (celle de l'hote, rejouee) : le fondu de l'hote, tel quel (sinon l'invite deja noir au
    // lancement pouvait voir toute la cinematique en noir, son fondu d'entree rejoue ne prenant pas).
    if (CutsceneRunning() && g_players[0].connected && g_players[0].state.inGame) {
        cam[0x51] = 0;
        *(float *)(cam + 0xBFC) = g_players[0].state.fade;
        stuckSince = 0;
        return;
    }
    int st = Field<int>(FindPlayerPed(), PED_STATE);
    const NetPlayer &host = g_players[0];
    bool stuck = alpha > 200.0f && !cam[0x51] && st != 54 && st != 55 && !CutsceneRunning() &&
                 host.connected && host.state.inGame && host.state.fade < 20;
    if (!stuck) { stuckSince = 0; return; }
    if (!stuckSince) stuckSince = GetTickCount();
    if (GetTickCount() - stuckSince < 2000) return;
    stuckSince = 0;
    int args[2] = { 500, 1 };
    RunScriptCommand(0x016A, 2, args);
    Log("miroir : ecran noir alors que l'hote voit clair, fondu d'entree");
}

// Fin de cinematique forcee (CCutsceneMgr, comme CLEAR_CUTSCENE 0x4D5ED0), commandes et camera rendues au joueur.
// Avant, un CLEAR_CUTSCENE qui ne passait pas a temps etait abandonne : la cinematique restait "en cours" et le jeu
// coupait les commandes de l'invite pour de bon (1er test reel, GG : il voyait l'hote bouger sans pouvoir bouger).
static void ForceCutsceneEnd(const char *why)
{
    ((int16_t *)(0xB73458 + 0xA8))[0x20 / 2] = 0;
    if (CutsceneRunning()) ((void(__cdecl *)())0x4D5ED0)();
    int ctl[2] = { 0, 1 };
    RunScriptCommand(0x01B4, 2, ctl);    // SET_PLAYER_CONTROL joueur 0, oui
    RunScriptCommand(0x02EB, 0, nullptr);   // RESTORE_CAMERA_JUMPCUT
    Log("miroir : fin de cinematique forcee (%s), commandes rendues", why);
}

// F10 "skip cutscene": the game's own skip (CCutsceneMgr::SkipCutscene 0x5B1700, what the skip button does) first; if the
// player presses again within 8 s (it did not end), or no cutscene is running (stuck controls / camera after one), the
// end is forced (ForceCutsceneEnd) and the controls and camera are given back.
bool CutsceneActive() { return CutsceneRunning(); }
void SkipCutsceneNow()
{
    static uint32_t softAt;
    uint32_t now = GetTickCount();
    bool running = CutsceneRunning();
    if (running && (!softAt || now - softAt > 8000)) {
        softAt = now;
        ((void(__cdecl *)())0x5B1700)();
        Log("cutscene: skipped from the F10 menu");
        HudToast(PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH ? "Cinematique passee" : "Cutscene skipped", 2500);
        return;
    }
    softAt = 0;
    ForceCutsceneEnd(running ? "F10 menu, second press" : "F10 menu, none running");
    HudToast(PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH ? "Commandes et camera rendues" : running ? "Cutscene ended by force" : "Controls and camera restored", 2500);
}

// Garde-fou : la camera de l'invite est tenue (fixe, mouvement de mission) alors que l'hote a retrouve la sienne depuis
// 2,5 s, hors cinematique, et une camera de mission a ete rejouee dans les 3 dernieres minutes (pas celle d'un magasin
// de l'invite) : elle lui est rendue (mouvements effaces, RESTORE_CAMERA_JUMPCUT 02EB, camera dans le dos 0373).
static void CameraWatch()
{
    static uint32_t since;
    uint32_t now = GetTickCount();
    const NetPlayer &h = g_players[0];
    bool stuck = *(uint8_t *)(0xB6F028 + 0x2B) == 0 && h.connected && h.state.inGame && !h.state.camScripted
                 && now - h.lastStateAt < 1000 && !CutsceneRunning() && g_mirrorCamAt && now - g_mirrorCamAt < 180000;
    if (!stuck) { since = 0; return; }
    if (!since) { since = now; return; }
    if (now - since < 2500) return;
    since = 0;
    CameraVectorReset();
    static const uint16_t ops[2] = { 0x02EB, 0x0373 };
    for (uint16_t op : ops) { *(uint16_t *)g_code = op; ExecCommand(op); }
    Log("miroir : camera de l'invite rendue (l'hote n'est plus en camera de mission)");
}

// Autotest "camcoince" (invite) : une camera fixe comme celle d'une mission, que l'hote n'a pas : CameraWatch doit la rendre.
void MirrorTestStuckCamera(const float *pos)
{
    int a[6]; float f[6] = { pos[0], pos[1], pos[2] + 25.0f, 0, 0, 0 }; memcpy(a, f, 24);
    RunScriptCommandTyped(0x015F, 6, "ffffff", a);
    int b[4]; float g[3] = { pos[0], pos[1], pos[2] }; memcpy(b, g, 12); b[3] = 2;
    RunScriptCommandTyped(0x0160, 4, "fffi", b);
    g_mirrorCamAt = GetTickCount();
    Log("autotest : camera fixe en l'air (comme une mission)");
}

// Guest: the host's mission goes on in another zone than the guest's (an interior, or outside after the host came out) for
// more than 4 s: the guest is put back next to the host, in his zone, with the scenery loaded (TeleportPed). The mirrored
// SET_AREA_VISIBLE / SET_CHAR_COORDINATES of the host's script do it at first; this catches what they miss (a guest that
// fell and was put back outside, died and came back from the hospital, was held up...). Without it the guest saw only the sky
// in the host's interior cutscene and the mission characters fell below him (Home Invasion, 14/10).
static void AreaRegroupFrame(uint32_t now)
{
    static uint32_t since, lastFix;
    static int streak;   // regroups in a row without the zones ever matching in between: gives up after 3
    void *me = FindPlayerPed(), *host = PuppetOf(0);
    const NetPlayer &h = g_players[0];
    if (!g_hostMission || !h.connected || !h.state.inGame || !me || !host || PedVehicle(me) || CutsceneActive() ||
        Field<float>(me, PED_HEALTH) <= 0.0f) { since = 0; return; }
    int hostArea = h.state.area, mine = EntityArea(me), cur = *(int *)0xB72914;
    if (mine == hostArea && cur == hostArea) { since = 0; streak = 0; return; }
    if (!since) since = now;
    if (now - since < 4000 || now - lastFix < 12000 || streak >= 3) return;
    streak++;
    lastFix = now;
    since = 0;
    const float *b = EntityPos(host), *a = EntityPos(me);
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2], hd = Field<float>(host, PED_ROTATION);
    bool together = dx * dx + dy * dy + dz * dz < 15.0f * 15.0f;
    float pos[3] = { b[0] + sinf(hd) * 1.0f, b[1] - cosf(hd) * 1.0f, b[2] + 0.3f };
    Log("miroir : l'invite n'est pas dans la zone de l'hote (%d/%d contre %d), %s", mine, cur, hostArea, together ? "zone corrigee sur place" : "remis a cote de lui");
    TeleportPed(me, together ? a : pos, Field<float>(me, PED_ROTATION), hostArea, "zone de l'hote");
}

void MirrorFrame()
{
    g_onReliable = OnReliable;
    if (g_cfg.host) { if (NetRunning()) HostGlobalsFrame(); return; }
    if (GameState() != 9 || !FindPlayerPed()) { g_havePrev = false; g_omForced = false; return; }
    UnstickFade();
    CameraWatch();
    memcpy(g_script + 8, "sacoop", 7);
    uint32_t now = GetTickCount();
    for (int budget = 0; budget < 64 && g_qHead != g_qTail; budget++) {
        Pending &p = g_queue[g_qHead];
        if (!p.since) p.since = now;
        if (!Replay(p.data, p.len)) {
            int op = p.len > 2 ? *(const uint16_t *)(p.data + 1) : 0;
            uint32_t patience = op == 0x02E7 ? 20000 : op == 0x02EA ? 10000 : 3000;
            if (now - p.since < patience) break;   // entite ou cinematique pas encore prete : on attend (dans l'ordre)
            if (op == 0x02EA) ForceCutsceneEnd("la cinematique n'a pas pu etre passee");   // jamais abandonnee
            else Log("miroir : commande %04X abandonnee (entite absente)", op);
        }
        g_qHead = (g_qHead + 1) % QUEUE;
    }
    BlipsMissionSweep();
    MissionFlagFrame();
    AreaRegroupFrame(now);
}
