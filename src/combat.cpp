// Armes, tirs et degats entre joueurs.
//  - Arme en main : celle du joueur, donnee a son pantin (CPed::GiveWeapon 0x5E6080 puis SetCurrentWeapon 0x5E61F0,
//    modele de l'arme charge avant : CWeaponInfo 0x743C60, +0xC).
//  - Tirs : chaque tir du joueur local est compte (detour de CWeapon::Fire 0x742300) avec le point vise (rayon de la
//    camera). Chez les autres, chaque nouveau tir est rejoue par son pantin (CWeapon::Fire vers ce point) : flamme,
//    son, trace.
//  - Degats : c'est le tireur qui decide. Detour de CPedDamageResponseCalculator::ComputeDamageResponse (0x4B5AC0) :
//    un coup porte par un pantin (ou sa voiture) ne fait rien en local ; un coup du joueur local sur un pantin est
//    calcule par le jeu, envoye au joueur touche (MSG_DAMAGE), et le pantin garde sa vie.
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "combat.h"
#include "entities.h"
#include "anims.h"
#include <math.h>
#include <string.h>

using namespace game;

static uint8_t g_localShots;          // compteur des tirs du joueur local (MsgState.shots)
static uint32_t g_lastShotAt;         // when the local player last fired
uint32_t CombatLastShotAt() { return g_lastShotAt; }
static float g_localAim[3];           // point vise au dernier tir
static bool g_replaying;              // un pantin rejoue un tir : ne pas le compter comme le notre
static uint8_t g_lastShots[MAX_PLAYERS];
static bool g_shotsKnown[MAX_PLAYERS];

bool PassengerFireTarget(float *out);   // coop.cpp

typedef char(__fastcall *Fire_t)(int *weapon, void *, void *owner, float *origin, float *muzzle, void *targetEnt, float *target, void *driveBy);
static Fire_t o_Fire;

// Hote : tirs des PNJ (par case de la reserve des personnages), envoyes avec eux (entities.cpp) : leurs copies chez les
// invites tirent aussi (avant, la police de l'hote tirait sur un invite sans qu'il la voie tirer).
static uint8_t g_npcShots[512];
static float g_npcAim[512][3];
static int PedSlot(void *ped)
{
    Pool *p = *(Pool **)0xB74490;
    int i = (int)(((uint8_t *)ped - p->objects) / 0x7C4);
    return i >= 0 && i < p->size && i < 512 && p->objects + i * 0x7C4 == ped ? i : -1;
}
void CombatNpcShots(void *ped, uint8_t &shots, float *aim)
{
    int i = PedSlot(ped);
    if (i < 0) return;
    shots = g_npcShots[i];
    memcpy(aim, g_npcAim[i], 12);
}

// 0.54.75 : une balle tiree vers une voiture touche la carrosserie, pas le PNJ assis derriere la vitre (rapport du 18/10 : la visee
// du passager ne blessait aucun PNJ en voiture). Un tir de pistolet / fusil / mitraillette dont le point vise est a moins de
// 1,3 m d'un occupant (et a peu pres a sa hauteur) le blesse : meme coup qu'une balle (CWeapon::GenerateDamageEvent).
static int g_dbgShots, g_dbgOccHits;   // 0.54.79 : per-10 s summary of the drive-by shots replayed here (the per-shot logs are capped)
static void HitOccupantAt(void *shooter, int type, const float *pt)
{
    if (type < 22 || type > 38 || !pt) return;
    // 0.54.76 : test sur le RAYON de la balle (de l'arme vers le point vise, prolonge de 3 m derriere la carrosserie), pas sur la
    // distance au point vise : a 30 m le point vise tombe a 1-2 m de l'occupant et le coup n'etait pas compte (rapport du 18/10 :
    // "parfois ca touche, souvent non, la distance ne devrait pas compter").
    const float *sp = EntityPos(shooter);
    float o[3] = { sp[0], sp[1], sp[2] + 0.6f };
    float d[3] = { pt[0] - o[0], pt[1] - o[1], pt[2] - o[2] };
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 0.5f) return;
    for (int k = 0; k < 3; k++) d[k] /= len;
    Pool *pp = *(Pool **)0xB74490;
    void *myVeh = PedVehicle(shooter), *best = nullptr;
    float bd = 0.6f;
    for (int i = 0; i < pp->size; i++) {
        if (pp->flags[i] & 0x80) continue;
        void *p = pp->objects + i * 0x7C4;
        if (p == shooter || PuppetIndex(p) >= 0 || Field<float>(p, PED_HEALTH) <= 0.0f) continue;
        void *v = PedVehicle(p);
        if (!v || v == myVeh) continue;
        const float *q = EntityPos(p);
        for (int h = 0; h < 2; h++) {   // bassin et tete
            float c[3] = { q[0] - o[0], q[1] - o[1], q[2] + h * 0.55f - o[2] };
            float t = c[0] * d[0] + c[1] * d[1] + c[2] * d[2];
            if (t < 0.5f || t > len + 3.0f) continue;
            float e[3] = { c[0] - d[0] * t, c[1] - d[1] * t, c[2] - d[2] * t };
            float dist = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            if (dist < bd) { bd = dist; best = p; }
        }
    }
    if (!best) return;
    int dmg = 0;
    if (void *info = ((void *(__cdecl *)(int, int))0x743C60)(type, 1)) dmg = *(int16_t *)((uint8_t *)info + 0x20);
    if (dmg < 5 || dmg > 200) dmg = 25;
    static int said;
    if (said < 60) { said++; Log("tir sur un occupant de vehicule : arme %d, %d degats, personnage %p, ecart au rayon %.2f m, point vise a %.0f m", type, dmg, best, bd, len); }
    ApplyPedHit(best, shooter, type, dmg, 3);
    g_dbgOccHits++;
}

static char __fastcall h_Fire(int *weapon, void *edx, void *owner, float *origin, float *muzzle, void *targetEnt, float *target, void *driveBy)
{
    // A puppet's own gun task fired (the fire command given in ReplayShots, for the animation and the flash): no bullet of
    // its own, the replayed shot (to the exact point its player hit) is the bullet.
    if (!g_replaying && owner && PuppetIndex(owner) >= 0) return 1;
    float passTarget[3];
    if (!g_replaying && owner && owner == FindPlayerPed() && PedVehicle(owner) && driveBy && PassengerFireTarget(passTarget)) {
        target = passTarget;   // passager qui vise : la balle va au point vise, pas ou la tache ou la camera la voulait
        targetEnt = nullptr;
    }
    char r = o_Fire(weapon, edx, owner, origin, muzzle, targetEnt, target, driveBy);
    if (r && !g_replaying && owner && owner == FindPlayerPed() && PedVehicle(owner) && driveBy && target) HitOccupantAt(owner, weapon[0], target);
    if (r && !g_replaying && owner && g_cfg.host && owner != FindPlayerPed() && (*((uint8_t *)owner + 0x36) & 7) == 3 && PuppetIndex(owner) < 0) {
        int i = PedSlot(owner);
        if (i >= 0) {
            g_npcShots[i]++;
            if (target) memcpy(g_npcAim[i], target, 12);
            else if (targetEnt) { memcpy(g_npcAim[i], EntityPos(targetEnt), 12); g_npcAim[i][2] += 0.5f; }
        }
    }
    if (r && !g_replaying && owner && owner == FindPlayerPed()) {
        if (PedVehicle(owner)) {   // 0.54.54 diagnostic: where the shots from a vehicle are aimed
            static int saidA;
            if (saidA < 12) { saidA++; const float *pp = EntityPos(owner); Log("tir du joueur en vehicule : cible %s %.0f %.0f %.0f, joueur en %.0f %.0f %.0f, origine %s", target ? "donnee" : "camera", target ? target[0] : 0.f, target ? target[1] : 0.f, target ? target[2] : 0.f, pp[0], pp[1], pp[2], driveBy ? "fenetre" : "aucune"); }
        }
        g_localShots++;
        g_lastShotAt = GetTickCount();
        if (target) memcpy(g_localAim, target, 12);
        else if (uint8_t *m = *(uint8_t **)(0xB6F028 + 0x14)) {   // TheCamera : position + 80 m dans l'axe de visee
            const float *pos = (const float *)(m + 0x30), *fwd = (const float *)(m + 0x10);
            for (int k = 0; k < 3; k++) g_localAim[k] = pos[k] + fwd[k] * 80.0f;
            // The point the shot really hits (CWorld::ProcessLineOfSight 0x56BA00, checked in gta_sa.exe 1.0 US: buildings,
            // vehicles, peds, objects; the player and his vehicle ignored through CWorld::pIgnoreEntity 0xB7CD68). The puppet
            // replays the shot from where it stands: aimed at a point 80 m away along OUR camera, its bullets passed beside a
            // small target, the switches of Robbing Uncle Sam took many shots from a guest (test of 15/10).
            uint8_t colPoint[0x2C] = {};
            void *hitEnt = nullptr;
            void *&ignore = *(void **)0xB7CD68;
            void *before = ignore;
            void *veh = PedVehicle(owner);
            ignore = veh ? veh : owner;
            bool hit = ((bool(__cdecl *)(const float *, const float *, void *, void **, bool, bool, bool, bool, bool, bool, bool, bool))0x56BA00)(
                pos, g_localAim, colPoint, &hitEnt, true, true, true, true, false, true, false, true);
            ignore = before;
            if (hit && hitEnt != owner) memcpy(g_localAim, colPoint, 12);
        }
    }
    return r;
}

// 0.54.51 (host logs of 18/10: no shot of the guest's drive-by was ever counted on the host, while his bullets hurt): shots
// fired from a vehicle are also counted from the weapon's ammunition going down (CWeapon +0xC), whatever code fired them,
// when CWeapon::Fire did not count them itself.
static void AmmoShots()
{
    static int lastType = -1, lastAmmo, lastShots;
    void *me = FindPlayerPed();
    if (!me || !PedVehicle(me)) { lastType = -1; return; }
    int slot = Field<uint8_t>(me, PED_WEAPONSLOT);
    int *w = (int *)((uint8_t *)me + PED_WEAPONS + slot * 0x1C);
    int type = w[0], ammo = w[3];
    if (type == lastType && ammo < lastAmmo && lastAmmo - ammo <= 5 && type >= 22 && type <= 38 && g_localShots == (uint8_t)lastShots) {
        g_localShots += (uint8_t)(lastAmmo - ammo);
        g_lastShotAt = GetTickCount();
        if (uint8_t *m = *(uint8_t **)(0xB6F028 + 0x14)) {
            const float *pos = (const float *)(m + 0x30), *fwd = (const float *)(m + 0x10);
            for (int k = 0; k < 3; k++) g_localAim[k] = pos[k] + fwd[k] * 80.0f;
        }
        static int said;
        if (said < 10) { said++; Log("tir du joueur en vehicule compte par les munitions (arme %d, %d -> %d)", type, lastAmmo, ammo); }
    }
    lastType = type; lastAmmo = ammo; lastShots = g_localShots;
}

void CombatFillState(MsgState &s)
{
    AmmoShots();
    s.shots = g_localShots;
    memcpy(s.aim, g_localAim, 12);
}

// --- Arme en main du pantin ---
static int g_puppetWeapon[MAX_PLAYERS] = { -1, -1, -1, -1 };

// Met l'arme "type" en main (modele charge avant). current : derniere arme donnee (-1 : aucune).
void EnsurePedWeapon(void *ped, int type, int &current)
{
    if (type < 0 || type > 46) type = 0;
    if (current == type) return;
    uint8_t *info = WeaponInfo(type);
    if (!info) return;
    int m1 = *(int *)(info + 0xC), m2 = *(int *)(info + 0x10);
    const int models[2] = { m1, m2 };
    for (int m : models)
        if (m > 0 && !ModelLoaded(m)) { RequestModel(m, 2); LoadAllRequestedModels(false); }
    if ((m1 > 0 && !ModelLoaded(m1)) || (m2 > 0 && !ModelLoaded(m2))) return;   // on reessaie plus tard
    int slot = type ? GiveWeapon(ped, type, 9999) : 0;
    SetCurrentWeapon(ped, slot);
    current = type;
}
static void GivePuppetWeapon(int id, void *ped, int type) { EnsurePedWeapon(ped, type, g_puppetWeapon[id]); }

void CombatPuppetCreated(int id) { g_puppetWeapon[id] = -1; g_shotsKnown[id] = false; }

// Armes rejouees : a feu (22-38, roquettes comprises : le projectile part du pantin) et lancees (16 grenade, 17 gaz
// lacrymogene, 18 cocktail Molotov : le projectile vole et explose chez chacun).
static bool Replayable(int w) { return (w >= 22 && w <= 38) || (w >= 16 && w <= 18); }

// Rejoue les tirs recus : l'arme du pantin tire vers le point vise (au plus 3 par image).
static void ReplayShots(void *ped, int weaponId, int n, const float *aim);
void CombatUpdatePuppet(int id, void *ped, const MsgState &s)
{
    GivePuppetWeapon(id, ped, s.weapon);
    { static uint32_t lastDbg; uint32_t t = GetTickCount();
      if (t - lastDbg > 10000) { lastDbg = t; if (g_dbgShots) { Log("tirs en voiture rejoues (10 s) : %d, dont %d sur un occupant ; arme %d, chargeur %d", g_dbgShots, g_dbgOccHits, (int)s.weapon, ((int *)((uint8_t *)ped + PED_WEAPONS + Field<uint8_t>(ped, PED_WEAPONSLOT) * 0x1C))[2]); g_dbgShots = g_dbgOccHits = 0; } } }
    if (!g_shotsKnown[id]) { g_shotsKnown[id] = true; g_lastShots[id] = s.shots; return; }
    int n = (uint8_t)(s.shots - g_lastShots[id]);
    g_lastShots[id] = s.shots;
    if (n > 0 && PedVehicle(ped)) {   // 0.54.50 diagnostic: shots received for a puppet in a vehicle
        static int saidV;
        int slot = Field<uint8_t>(ped, PED_WEAPONSLOT);
        if (saidV < 20) { saidV++; Log("tirs recus d'un pantin en vehicule : %d, arme %d, en main %d (emplacement %d)", n, (int)s.weapon, ((int *)((uint8_t *)ped + PED_WEAPONS + slot * 0x1C))[0], slot); }
    }
    if (n <= 0 || n > 20 || !Replayable(s.weapon)) return;   // armes a feu et armes lancees
    if (n > 3) n = 3;
    ReplayShots(ped, s.weapon, n, s.aim);
}

// Invite : copie d'un PNJ de l'hote, ses nouveaux tirs rejoues comme ceux d'un pantin.
void CombatReplayCopyShots(void *ped, int weaponId, uint8_t shots, const float *aim, uint8_t &last, bool &known)
{
    if (!known) { known = true; last = shots; return; }
    int n = (uint8_t)(shots - last);
    last = shots;
    if (n <= 0 || n > 20 || !Replayable(weaponId)) return;
    ReplayShots(ped, weaponId, n > 3 ? 3 : n, aim);
}

static void ReplayShots(void *ped, int weaponId, int n, const float *aimIn)
{
    int slot = Field<uint8_t>(ped, PED_WEAPONSLOT);
    int *weapon = (int *)((uint8_t *)ped + PED_WEAPONS + slot * 0x1C);
    if (weapon[0] != weaponId) { static int saidW; if (saidW < 10) { saidW++; Log("tir rejoue ignore : arme en main %d, voulue %d", weapon[0], weaponId); } return; }
    float aim[3] = { aimIn[0], aimIn[1], aimIn[2] };
    // Le pantin se tourne vers sa cible (a pied).
    if (!PedVehicle(ped)) {
        const float *p = EntityPos(ped);
        float h = atan2f(-(aim[0] - p[0]), aim[1] - p[1]);
        Field<float>(ped, PED_ROTATION) = h;
        Field<float>(ped, PED_AIMROT) = h;
    }
    if (weaponId >= 16 && weaponId <= 18) { static int said; if (said < 10) { said++; Log("lancer rejoue (arme %d) par %p", weaponId, ped); } }
    // The firing itself, as the other players see it (test of 15/10: "they see them aiming but no shot, no animation"): the
    // puppet's gun task (CTaskSimpleUseGun, secondary slot, given by AimMirror) gets the FIRE command for this frame
    // (CTaskSimpleUseGun::ControlGun 0x61E040, command 2; AimMirror puts AIM back next frame): the game plays the shot,
    // the recoil and the muzzle flash. Its own bullet is dropped in h_Fire.
    if (!PedVehicle(ped) && weaponId >= 22 && weaponId <= 34 && PuppetIndex(ped) >= 0) AimFire(ped);
    // From a vehicle (drive-by): CWeapon::Fire's last argument is the drive-by origin (a CVector*, as
    // CTaskSimpleGangDriveBy's own shot at 0x628328 passes it). Without it the bullet started from the seated body's
    // weapon point, inside the car, and hit the car itself: 2 bullets of a clip got through (16/10). The shot leaves from
    // the window on the side of the target, at shoulder height.
    float from[3], *fromP = nullptr;
    if (void *veh = PedVehicle(ped)) {
        if (weaponId >= 22 && weaponId <= 34 && PuppetIndex(ped) >= 0) AimFireCar(ped, aim);
        const float *p = EntityPos(ped);
        float dx = aim[0] - p[0], dy = aim[1] - p[1], d = sqrtf(dx * dx + dy * dy);
        if (d > 0.01f) { dx /= d; dy /= d; }
        int vtype = *(int *)((uint8_t *)veh + VEH_TYPE);
        float out = (vtype == 9 || vtype == 10 || vtype == 2) ? 0.4f : 1.1f;   // (bikes and quads: no body around)
        from[0] = p[0] + dx * out; from[1] = p[1] + dy * out; from[2] = p[2] + 0.55f;
        fromP = from;
    }
    if (PedVehicle(ped)) { static int saidR; if (saidR < 12) { saidR++; const float *pp = EntityPos(ped); Log("tir rejoue depuis un vehicule : cible %.0f %.0f %.0f, pantin en %.0f %.0f %.0f", aim[0], aim[1], aim[2], pp[0], pp[1], pp[2]); } }
    g_replaying = true;
    for (int k = 0; k < n; k++) {
        weapon[1] = 0;        // pret
        weapon[2] = 50;       // balles dans le chargeur
        weapon[3] = 9999;
        o_Fire(weapon, nullptr, ped, fromP, fromP, nullptr, aim, fromP);
        if (fromP) g_dbgShots++;
        if (g_cfg.host && fromP && PuppetIndex(ped) >= 0) HitOccupantAt(ped, weaponId, aim);
    }
    g_replaying = false;
}

void CombatTestShot(void *ped, const float *aim)
{
    int slot = Field<uint8_t>(ped, PED_WEAPONSLOT);
    int *weapon = (int *)((uint8_t *)ped + PED_WEAPONS + slot * 0x1C);
    float target[3] = { aim[0], aim[1], aim[2] };
    weapon[1] = 0;
    if (weapon[2] < 1) weapon[2] = 1;
    h_Fire(weapon, nullptr, ped, nullptr, nullptr, nullptr, target, nullptr);
}

// --- Degats ---
typedef void(__fastcall *Damage_t)(int *calc, void *, void *victim, float *resp, bool speak);
static Damage_t o_Damage;

static bool g_killing;   // coup voulu (CombatKillPuppet, coup recu d'un joueur) : le jeu l'applique tel quel

static void __fastcall h_Damage(int *calc, void *edx, void *victim, float *resp, bool speak)
{
    if (g_killing) { o_Damage(calc, edx, victim, resp, speak); return; }
    void *damager = (void *)calc[0];
    // Coup d'un pantin ou d'une copie de personnage de mission (ou de la voiture qu'il conduit) : decide chez son
    // joueur / chez l'hote, rien en local.
    void *damagerPed = damager && (*((uint8_t *)damager + 0x36) & 7) == 2 ? Field<void *>(damager, VEH_DRIVER) : damager;
    bool fromPuppet = damagerPed && (PuppetIndex(damagerPed) >= 0 || IsMissionCopy(damagerPed));
    if (fromPuppet && !*((uint8_t *)resp + 10)) {
        resp[0] = resp[1] = 0;
        *((uint8_t *)resp + 8) = 0;
        *((uint8_t *)resp + 9) = 0;
        *((uint8_t *)resp + 10) = 1;   // calcule : le jeu ne recommencera pas
        return;
    }
    int victimPlayer = PuppetIndex(victim);
    if (victimPlayer < 0 && IsMissionCopy(victim)) {   // copie d'un personnage de mission : l'hote decide
        void *me = FindPlayerPed();
        bool mine = damager && (damager == me || ((*((uint8_t *)damager + 0x36) & 7) == 2 && Field<void *>(damager, VEH_DRIVER) == me));
        float damage = *(float *)&calc[1];
        if (!*((uint8_t *)resp + 10) && mine && damage > 0) SendMissionPedHit(victim, calc[3], calc[2], damage);
        resp[0] = resp[1] = 0;
        *((uint8_t *)resp + 8) = 0;
        *((uint8_t *)resp + 9) = 0;
        *((uint8_t *)resp + 10) = 1;
        return;
    }
    if (victimPlayer < 0) { o_Damage(calc, edx, victim, resp, speak); return; }
    // Pantin touche : rien en local (son joueur fait foi) ; un coup du joueur local est envoye a son joueur.
    bool fresh = !*((uint8_t *)resp + 10);
    void *me = FindPlayerPed();
    bool mine = damager && (damager == me || ((*((uint8_t *)damager + 0x36) & 7) == 2 && Field<void *>(damager, VEH_DRIVER) == me));
    // Two players in the same vehicle (a passenger's drive-by): never a hit between them. Only our own hits: the shots of
    // the host's people on a guest riding with him must still reach that guest (review of 15/10: dropped since 0.54.18,
    // a guest in the host's car could not be hurt).
    if (mine && me && PedVehicle(me) && PedVehicle(me) == PedVehicle(victim)) fresh = false;
    float damage = *(float *)&calc[1];
    if (fresh && mine && damage > 0) {
        MsgDamage d = { MSG_DAMAGE, (uint8_t)(g_localId < 0 ? 0 : g_localId), (uint8_t)victimPlayer, (uint8_t)calc[3], (uint8_t)calc[2], {}, damage, 0 };
        NetSendToAll(&d, sizeof(d));
        Log("coup sur le joueur %d : %.1f (arme %d, partie %d)", victimPlayer, damage, calc[3], calc[2]);
    }
    // Hote : un personnage de mission (ou sa voiture) touche le pantin d'un invite : le coup est pour ce joueur.
    uint32_t pedId = fresh && damage > 0 && g_cfg.host && !mine ? MissionPedId(damagerPed) : 0;
    if (pedId) {
        MsgDamage d = { MSG_DAMAGE, 0, (uint8_t)victimPlayer, (uint8_t)calc[3], (uint8_t)calc[2], {}, damage, pedId };
        NetSendToAll(&d, sizeof(d));
        Log("coup du personnage %08X (type %d) sur le joueur %d : %.0f (arme %d)", pedId, Field<int>(damagerPed, 0x598), victimPlayer, damage, calc[3]);
    }
    resp[0] = resp[1] = 0;
    *((uint8_t *)resp + 8) = 0;
    *((uint8_t *)resp + 9) = 0;
    *((uint8_t *)resp + 10) = 1;
}

// Coup comme celui d'une balle : CWeapon::GenerateDamageEvent (0x73A530 : victime, auteur, arme, degats, partie du
// corps, direction) calcule la reponse (CPedDamageResponseCalculator) et poste l'evenement (reaction, chute, mort).
void ApplyPedHit(void *victim, void *damager, int weapon, int damage, int bodyPart)
{
    if (weapon < 0 || weapon > 54) weapon = 0;
    if (bodyPart < 3 || bodyPart > 9) bodyPart = 3;
    g_killing = true;
    ((char(__cdecl *)(void *, void *, int, int, int, int))0x73A530)(victim, damager, weapon, damage, bodyPart, 0);
    g_killing = false;
}

// Le joueur du pantin est mort chez lui : coup fatal sur le pantin, le jeu joue la chute.
void CombatKillPuppet(void *ped, int weapon) { ApplyPedHit(ped, nullptr, weapon, 1000, 3); }

void *PuppetOf(int id);

static void OnDamage(const MsgDamage &d)
{
    if (d.to != g_localId || (!d.pedId && !g_cfg.friendlyFire)) return;
    void *me = FindPlayerPed();
    if (!me || GameState() != 9 || Field<float>(me, PED_HEALTH) <= 0.0f) return;
    int damage = (int)(d.damage + 0.5f);
    if (damage < 1 || damage > 1000) return;
    if (!d.pedId) {   // (a player sitting in our vehicle: his drive-by never hurts us)
        void *pup = PuppetOf(d.from);
        if (pup && PedVehicle(me) && PedVehicle(pup) == PedVehicle(me)) { Log("coup du joueur %d ignore : il est dans notre vehicule", d.from); return; }
    }
    float before = Field<float>(me, PED_HEALTH) + Field<float>(me, PED_ARMOUR);
    void *damager = d.pedId ? MissionCopyById(d.pedId) : PuppetOf(d.from);   // auteur : pantin ou copie (mort creditee)
    ApplyPedHit(me, damager, d.weapon, damage, d.bodyPart);
    Log("touche par le joueur %d : %d (arme %d, partie %d) -> vie %.0f, gilet %.0f (-%.0f)", d.from, damage, d.weapon, d.bodyPart,
        Field<float>(me, PED_HEALTH), Field<float>(me, PED_ARMOUR), before - Field<float>(me, PED_HEALTH) - Field<float>(me, PED_ARMOUR));
}

void InstallCombat()
{
    static const uint8_t fire[] = { 0x83, 0xEC, 0x3C, 0x53, 0x56 };
    o_Fire = (Fire_t)MakeDetour(0x742300, fire, sizeof(fire), (void *)h_Fire);
    static const uint8_t dmg[] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xFF };
    o_Damage = (Damage_t)MakeDetour(0x4B5AC0, dmg, sizeof(dmg), (void *)h_Damage);
    g_onDamage = OnDamage;
}
