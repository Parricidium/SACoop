// Reseau SACoop : UDP, un hote et jusqu'a MAX_PLAYERS-1 invites. L'hote relaie l'etat de chacun a tous.
#pragma once
#include <stdint.h>

enum { MAX_PLAYERS = 4, NET_VERSION = 4, MAX_RELIABLE_PAYLOAD = 1200 };

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
};

#pragma pack(push, 1)
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
};
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

