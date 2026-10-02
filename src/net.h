// Reseau SACoop : UDP, un hote et jusqu'a MAX_PLAYERS-1 invites. L'hote relaie l'etat de chacun a tous.
#pragma once
#include <stdint.h>

enum { MAX_PLAYERS = 4, NET_VERSION = 16, MAX_RELIABLE_PAYLOAD = 1200 };

enum MsgType : uint8_t {
    MSG_HELLO = 1,   // invite -> hote : je veux entrer (nom)
    MSG_WELCOME,     // hote -> invite : ton numero de joueur
    MSG_FULL,        // hote -> invite : partie pleine ou version differente
    MSG_STATE,       // etat d'un joueur (invite -> hote, puis hote -> tous)
    MSG_BYE,         // depart d'un joueur
    MSG_WORLD,       // hote -> invites : heure et meteo (1 fois par seconde)
    MSG_RELIABLE,    // enveloppe fiable et ordonnee : seq + charge utile (voir NetSendReliable)
    MSG_ACK,         // accuse de reception cumulatif d'un flux fiable
    MSG_PING,        // invite -> hote : heure d'envoi (mesure du ping)
    MSG_PONG,        // hote -> invite : la meme heure, renvoyee
    MSG_RESYNC,      // hote -> invite : ton flux fiable est perdu, reconnecte-toi (nouvelle session)
    MSG_CLOTHES,     // vetements d'un joueur (CPedClothesDesc), a chaque changement puis toutes les 2 s ; relaye par l'hote
    MSG_VEHICLE,     // etat d'un vehicule, par son proprietaire (vehicles.cpp) ; relaye par l'hote
    MSG_DAMAGE,      // coup porte par un joueur a un autre (combat.cpp) ; relaye par l'hote
    MSG_PED,         // hote -> invites : un personnage de mission (entities.cpp)
    MSG_PEDHIT,      // invite -> hote : coup porte a un personnage de mission
    MSG_MARKER,      // hote -> invites : commande LOCATE / IS_CHAR_IN_AREA a marqueur (conditions.cpp)
    MSG_BATCH,       // plusieurs messages d'une meme image : n (1 octet) puis, pour chacun, longueur (2 octets) et octets
};

#pragma pack(push, 1)
// Animation d'action en cours (anims.cpp) : groupe et numero du jeu, temps, poids (0-255).
struct NetAnim { int16_t group, id; float time; uint8_t blend; };
enum { ANIMS_MAX = 3 };
// Refus : reason 1 = partie pleine, 2 = version differente (hostVersion = la sienne).
struct MsgFull { uint8_t type, reason, hostVersion; };
struct MsgPing { uint8_t type; uint32_t time; };
// session : tire au sort par l'invite a chaque (re)connexion ; l'hote repart d'un flux fiable neuf quand il change.
struct MsgHello { uint8_t type, version; char name[24]; uint8_t rejoin; uint32_t session; };
struct MsgWelcome { uint8_t type, id; };
struct MsgBye { uint8_t type, id; };

// Etat d'un joueur, envoye ~30 fois par seconde.
struct MsgState {
    uint8_t type, id;
    uint8_t inGame;     // en partie (sinon au menu / en chargement)
    uint8_t area;       // interieur (CGame::currArea)
    uint32_t seq;
    float pos[3];
    float speed[3];
    float heading;      // orientation actuelle (radians)
    float health, armour;
    uint8_t moveState;  // CPed::m_nMoveState : 1 immobile, 4 marche, 6 course, 7 sprint
    uint8_t weapon;
    uint16_t skin;      // modele de son pantin chez les autres (reglage Tenue)
    uint32_t vehicleId; // vehicule reseau occupe (0 = a pied)
    uint8_t seat;       // 0 : au volant, 1..8 : passager
    uint8_t shots;      // compteur de tirs (chaque nouveau tir est rejoue par son pantin)
    float aim[3];       // point vise au dernier tir
    uint8_t carTask;    // 0, 1 : monte au volant, 2 : monte en passager (porte carDoor), 3 : descend
    uint8_t carDoor;
    uint32_t carTaskVeh;// vehicule reseau de cette montee / descente
    uint8_t fade;       // niveau de fondu de l'ecran (CCamera +0xBFC, 0 clair - 255 noir)
    uint8_t wanted;     // niveau de recherche de la police (0-6)
    uint8_t meleeSeq;   // compteur de coups au corps a corps (chaque nouveau coup est rejoue par le pantin)
    uint8_t meleeAnim;  // animation du dernier coup (coop.cpp, kMeleeAnims)
    uint8_t aiming;     // 1 : vise a pied (CTaskSimpleUseGun 1017) ; 2 : tire par la fenetre (CTaskSimpleGangDriveBy 1022) ; aim = point vise
    uint8_t animCount;  // animations d'action en cours (sauts, accroupi, coups, nage...), rejouees par le pantin
    NetAnim anims[ANIMS_MAX];
    uint8_t air;        // a pied : 1 en l'air (saut, chute), 2 accroche a un mur (escalade) ; le pantin suit sa hauteur
    char name[24];
    uint32_t time;      // GetTickCount de l'envoi (interpolation)
};
// CPedClothesDesc du joueur : 10 cles de modeles, 18 cles de textures, gras, muscle (0x78 octets).
struct MsgClothes { uint8_t type, id; uint32_t desc[30]; };
// Vehicule : identifiant = (joueur qui l'a enregistre << 24) | compteur ; envoye par son proprietaire.
struct MsgVehicle {
    uint8_t type, owner;
    uint32_t id;
    uint16_t model;
    uint8_t color1, color2;
    float pos[3], right[3], fwd[3], speed[3], turn[3];
    uint8_t driven;     // un joueur est dedans en ce moment
    float health;       // CVehicle +0x4C0 (1000 neuf, < 250 en feu)
    uint8_t flags;      // VF_*
    uint8_t wheels[4], doors[6];   // CDamageManager (voitures seulement)
    uint32_t lights, panels;
    int ownerRef;       // reference de pool du vehicule chez son proprietaire (handle de ses scripts)
    float steer, gas, brake;   // commandes du conducteur (CVehicle +0x494, +0x49C, +0x4A0), rejouees par la copie
    uint8_t handbrake;  // frein a main (+0x428, bit 0x20)
};
enum { VF_SIREN = 1, VF_WRECKED = 2, VF_DAMAGE = 4 /* champs de degats valides */, VF_MISSION = 8 /* vehicule de mission de l'hote */, VF_SCRIPT = 16 /* cree par un script de mission (CreatedBy 2) */ };
// Coup porte par le joueur "from" au joueur "to" : touche decidee chez le tireur, degats appliques par le jeu du joueur
// touche (regles du joueur, gilet, reaction, mort).
// pedId : coup d'un personnage de mission de l'hote (son id MsgPed), 0 : du joueur "from".
struct MsgDamage { uint8_t type, from, to, weapon, bodyPart, pad[3]; float damage; uint32_t pedId; };
// Personnage de mission de l'hote (id = sa reference de pool chez l'hote), ~15 fois par seconde.
struct MsgPed {
    uint8_t type, flags;        // PF_*
    uint16_t model;
    uint32_t id;
    float pos[3], speed[3], heading, health;
    uint8_t moveState, weapon, seat, area;
    uint32_t vehicleId;         // vehicule reseau (0 : a pied)
    char special[8];            // modele special (290-299) : son nom (CStreaming::RequestSpecialModel)
    uint8_t shots;              // compteur de tirs (chaque nouveau tir est rejoue par la copie)
    float aim[3];               // point vise au dernier tir (PF_AIMING : point vise maintenant)
    uint8_t animCount;          // animations d'action en cours (anims.cpp)
    NetAnim anims[2];
};
enum { PF_DEAD = 1, PF_AMBIENT = 2 /* passant ordinaire (population partagee) */, PF_DRIVEBY = 4 /* tire par la fenetre d'un vehicule */, PF_AIMING = 8 /* vise (CTaskSimpleUseGun) */ };
struct MsgPedHit { uint8_t type, from, weapon, bodyPart; uint32_t id; float damage; };
// Commande de zone a marqueur de l'hote (valeurs evaluees ; vals[0] = son joueur, remplace par celui de l'invite).
struct MsgMarker { uint8_t type, n; uint16_t op; int vals[8]; };
struct MsgWorld {
    uint8_t type;
    uint8_t hours, minutes;
    short oldWeather, newWeather, forcedWeather;
    float weatherBlend;   // CWeather::InterpolationValue (passage de l'ancienne a la nouvelle meteo)
};
#pragma pack(pop)

struct NetPlayer {
    bool connected;
    MsgState state;         // dernier etat recu
    uint32_t lastSeen;      // GetTickCount de la derniere reception (signes de vie compris)
    uint32_t lastStateAt;   // GetTickCount du dernier MSG_STATE
    uint32_t lastSeq;       // numero du dernier etat applique (les etats arrives dans le desordre sont ignores)
};

extern NetPlayer g_players[MAX_PLAYERS];
extern int g_localId;   // 0 = hote ; -1 = invite pas encore accepte

bool NetStart();        // selon g_cfg (hote ou invite)
void NetStop();
void NetPoll();         // lit tous les paquets en attente
bool NetRunning();
void NetSendState(const MsgState &s);
void NetSendBye();
void NetKeepAlive();    // signe de vie envoye depuis un autre fil (watchdog.cpp), meme si le jeu ne presente plus d'image
void NetFlush();   // envoie les messages regroupes de l'image (fin de CoopFrame)
void NetSendToGuests(const void *data, int len);   // hote seulement
void NetSendToAll(const void *data, int len);
void NetSendReliable(const void *data, int len);   // hote : a tous les invites ; invite : a l'hote
void NetSendReliableTo(int peer, const void *data, int len);
extern void (*g_onWorld)(const MsgWorld &w);       // invite : appele a la reception d'un MsgWorld
extern void (*g_onState)(const MsgState &s);       // chaque etat de joueur recu
extern void (*g_onReliable)(int from, const uint8_t *data, int len);
extern void (*g_onJoin)(int peer);                 // hote : un invite vient d'entrer (ou revient)
extern uint16_t g_myPing;
extern void (*g_onClothes)(const MsgClothes &c);   // vetements d'un autre joueur
extern void (*g_onVehicle)(const MsgVehicle &v);   // etat d'un vehicule d'un autre joueur
extern void (*g_onDamage)(const MsgDamage &d);     // coup porte par un joueur a un autre
extern void (*g_onPed)(const MsgPed &p);           // invite : personnage de mission de l'hote
extern void (*g_onPedHit)(const MsgPedHit &h);     // hote : coup d'un invite sur un personnage de mission
extern void (*g_onMarker)(const MsgMarker &m);     // invite : marqueur de zone de l'hote

