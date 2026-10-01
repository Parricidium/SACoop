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
#include <math.h>
#include <string.h>

using namespace game;

static uint8_t g_localShots;          // compteur des tirs du joueur local (MsgState.shots)
static float g_localAim[3];           // point vise au dernier tir
static bool g_replaying;              // un pantin rejoue un tir : ne pas le compter comme le notre
static uint8_t g_lastShots[MAX_PLAYERS];
static bool g_shotsKnown[MAX_PLAYERS];

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

static char __fastcall h_Fire(int *weapon, void *edx, void *owner, float *origin, float *muzzle, void *targetEnt, float *target, void *driveBy)
{
    char r = o_Fire(weapon, edx, owner, origin, muzzle, targetEnt, target, driveBy);
    if (r && !g_replaying && owner && g_cfg.host && owner != FindPlayerPed() && (*((uint8_t *)owner + 0x36) & 7) == 3 && PuppetIndex(owner) < 0) {
        int i = PedSlot(owner);
        if (i >= 0) {
            g_npcShots[i]++;
            if (target) memcpy(g_npcAim[i], target, 12);
            else if (targetEnt) { memcpy(g_npcAim[i], EntityPos(targetEnt), 12); g_npcAim[i][2] += 0.5f; }
        }
    }
    if (r && !g_replaying && owner && owner == FindPlayerPed()) {
        g_localShots++;
        if (target) memcpy(g_localAim, target, 12);
        else if (uint8_t *m = *(uint8_t **)(0xB6F028 + 0x14)) {   // TheCamera : position + 80 m dans l'axe de visee
            const float *pos = (const float *)(m + 0x30), *fwd = (const float *)(m + 0x10);
            for (int k = 0; k < 3; k++) g_localAim[k] = pos[k] + fwd[k] * 80.0f;
        }
    }
    return r;
}

void CombatFillState(MsgState &s)
{
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

// Rejoue les tirs recus : l'arme du pantin tire vers le point vise (au plus 3 par image).
static void ReplayShots(void *ped, int weaponId, int n, const float *aim);
void CombatUpdatePuppet(int id, void *ped, const MsgState &s)
{
    GivePuppetWeapon(id, ped, s.weapon);
    if (!g_shotsKnown[id]) { g_shotsKnown[id] = true; g_lastShots[id] = s.shots; return; }
    int n = (uint8_t)(s.shots - g_lastShots[id]);
    g_lastShots[id] = s.shots;
    if (n <= 0 || n > 20 || s.weapon < 22 || s.weapon > 38) return;   // armes a feu seulement
    if (n > 3) n = 3;
    ReplayShots(ped, s.weapon, n, s.aim);
}

// Invite : copie d'un PNJ de l'hote, ses nouveaux tirs rejoues comme ceux d'un pantin.
void CombatReplayCopyShots(void *ped, int weaponId, uint8_t shots, const float *aim, uint8_t &last, bool &known)
{
    if (!known) { known = true; last = shots; return; }
    int n = (uint8_t)(shots - last);
    last = shots;
    if (n <= 0 || n > 20 || weaponId < 22 || weaponId > 38 || PedVehicle(ped)) return;
    ReplayShots(ped, weaponId, n > 3 ? 3 : n, aim);
}

static void ReplayShots(void *ped, int weaponId, int n, const float *aimIn)
{
    int slot = Field<uint8_t>(ped, PED_WEAPONSLOT);
    int *weapon = (int *)((uint8_t *)ped + PED_WEAPONS + slot * 0x1C);
    if (weapon[0] != weaponId) return;
    float aim[3] = { aimIn[0], aimIn[1], aimIn[2] };
    // Le pantin se tourne vers sa cible (a pied).
    if (!PedVehicle(ped)) {
        const float *p = EntityPos(ped);
        float h = atan2f(-(aim[0] - p[0]), aim[1] - p[1]);
        Field<float>(ped, PED_ROTATION) = h;
        Field<float>(ped, PED_AIMROT) = h;
    }
    g_replaying = true;
    for (int k = 0; k < n; k++) {
        weapon[1] = 0;        // pret
        weapon[2] = 50;       // balles dans le chargeur
        weapon[3] = 9999;
        o_Fire(weapon, nullptr, ped, nullptr, nullptr, nullptr, aim, nullptr);
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
