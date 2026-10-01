// Animations d'action recopiees d'un personnage a sa copie (pantin d'un joueur, copie d'un PNJ de l'hote) : sauts,
// accroupi, coups (tous styles), roulades, escalade, nage, chutes, recharge, lancer, gestes... Tout ce que le jeu joue
// sur lui hors de son groupe de deplacement (marche, course, sprint, repos : la copie les produit elle-meme en suivant
// sa position).
//  - Lecture : liste des associations du clump (plugin RpAnimBlendClumpData, decalage *(0xB5F878) ; maillon en +4 de
//    CAnimBlendAssociation : groupe +0xE, numero +0x2C, poids +0x18, vitesse de fondu +0x1C, temps +0x20, drapeaux
//    +0x2E). Groupe de deplacement du personnage : CPed +0x4D4 (54 pour CJ). Mesure 01/10 (autotest "anims") : sauts
//    0:116/118/122, accroupi 0:55/56, poings 33:214 + garde 33:223.
//  - Rejeu : RpAnimBlendClumpGetAssociation(clump, numero) 0x4D68B0, sinon CAnimManager::BlendAnimation(clump, groupe,
//    numero, fondu) 0x4D4610 si le bloc du groupe est charge (groupes *(0xB4EA34), 20 octets : bloc +0, animations +4 ;
//    bloc : nom +0, charge +0x10), sinon REQUEST_ANIMATION 04ED et on reessaie. Temps recale au-dela de 0,25 s ; une
//    animation qui n'est plus recue s'efface (fondu -4, drapeau 4 : supprimee une fois effacee).
//  - Visee : CTaskSimpleUseGun (type 1017) en tache secondaire 0 (voir coop.cpp).
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "mirror.h"
#include "anims.h"
#include <math.h>
#include <string.h>

using namespace game;

static uint8_t *FirstLink(void *clump) { return *(uint8_t **)*(void **)((uint8_t *)clump + *(int *)0xB5F878); }

int AnimsCollect(void *ped, NetAnim *out, int max)
{
    void *clump = Field<void *>(ped, 0x18);
    if (!clump) return 0;
    int motion = Field<int>(ped, 0x4D4), n = 0;
    NetAnim all[16];
    for (uint8_t *lk = FirstLink(clump); lk && n < 16; lk = *(uint8_t **)lk) {
        uint8_t *as = lk - 4;
        int group = *(int16_t *)(as + 0xE);
        float blend = *(float *)(as + 0x18);
        if (group == motion || group < 0 || blend < 0.2f) continue;
        all[n++] = { (int16_t)group, *(int16_t *)(as + 0x2C), *(float *)(as + 0x20), (uint8_t)(blend > 1 ? 255 : blend * 255) };
    }
    for (int i = 1; i < n; i++)   // les plus fortes d'abord
        for (int k = i; k > 0 && all[k].blend > all[k - 1].blend; k--) { NetAnim t = all[k]; all[k] = all[k - 1]; all[k - 1] = t; }
    if (n > max) n = max;
    memcpy(out, all, sizeof(NetAnim) * n);
    return n;
}

static bool GroupReady(int group)
{
    uint8_t *groups = *(uint8_t **)0xB4EA34;
    if (!groups || group < 0 || group > 200) return false;
    uint8_t *g = groups + group * 20, *block = *(uint8_t **)g;
    if (block && block[0x10] && *(void **)(g + 4)) return true;
    if (block) {   // bloc pas charge : demande (une fois toutes les 2 s)
        static uint32_t asked[256];
        uint32_t now = GetTickCount();
        if (now - asked[group & 255] > 2000) {
            asked[group & 255] = now;
            char name[17];
            memcpy(name, block, 16);
            name[16] = 0;
            int arg = (int)(uintptr_t)name;
            RunScriptCommandTyped(0x04ED, 1, "s", &arg);   // REQUEST_ANIMATION
        }
    }
    return false;
}

// (On ne touche qu'aux associations creees ici et que personne n'a adoptees : une tache du jeu qui reprend une animation
// deja jouee (RpAnimBlendClumpGetAssociation) y pose son rappel (CAnimBlendAssociation +0x30 type 1 fin / 2 suppression,
// +0x34 fonction, +0x38 donnee : SetFinishCallback 0x4CEBE0, SetDeleteCallback 0x4CEBC0) et garde son adresse. L'effacer
// avec le drapeau "supprimer une fois effacee" laissait la tache avec une adresse morte : plantage 0x4D1750 au test du
// 01/10, sur les gestes de discussion des copies de PNJ (leur tache CTaskSimpleChat reprenait le geste recopie).)
static bool InClump(void *clump, void *assoc)
{
    for (uint8_t *lk = FirstLink(clump); lk; lk = *(uint8_t **)lk) if (lk - 4 == assoc) return true;
    return false;
}
static bool Adopted(void *assoc) { return *(int *)((uint8_t *)assoc + 0x30) != 0; }
static bool Partial(void *assoc) { return (*(uint16_t *)((uint8_t *)assoc + 0x2E) & 0x10) != 0; }   // par-dessus l'animation de base

// L'animation des os (0x4D1680, appelee pour chaque os) suppose au moins une animation "corps entier" (non partielle) sur
// le personnage : sans elle elle lit une liste vide et plante (0x4D1750). Rejouer une animation corps entier efface
// celle de base (BlendAnimation) ; quand la notre s'efface a son tour, on redonne le repos de son groupe de deplacement
// (CPed +0x4D4, animation 3) et c'est le moteur qui fait la transition. Filet : si plus aucune animation corps entier
// ne reste, le repos est remis tout de suite.
static void *BlendIdle(void *ped, void *clump, float delta)
{
    return ((void *(__cdecl *)(void *, int, int, float))0x4D4610)(clump, Field<int>(ped, 0x4D4), 3, delta);
}
static void EnsureBase(void *ped, void *clump)
{
    for (uint8_t *lk = FirstLink(clump); lk; lk = *(uint8_t **)lk) {
        uint8_t *as = lk - 4;
        if (!Partial(as) && (*(float *)(as + 0x18) > 0.01f || *(float *)(as + 0x1C) > 0)) return;
    }
    BlendIdle(ped, clump, 1000.0f);
    static int said;
    if (said < 10) { said++; Log("animations : plus d'animation de base sur %p, repos remis", ped); }
}

void AnimsApply(void *ped, const NetAnim *in, int n, AnimMirror &m)
{
    void *clump = Field<void *>(ped, 0x18);
    if (!clump) return;
    // en cours d'effacement : disparues, ou adoptees entre-temps (on retire alors notre demande de suppression)
    int nf = 0;
    for (int k = 0; k < m.nFading; k++) {
        void *as = m.fading[k];
        if (!InClump(clump, as)) continue;
        if (Adopted(as)) { *(uint16_t *)((uint8_t *)as + 0x2E) &= ~4; continue; }
        m.fading[nf++] = as;
    }
    m.nFading = nf;
    for (int k = 0; k < m.count; k++)
        if (m.assoc[k] && (!InClump(clump, m.assoc[k]) || Adopted(m.assoc[k]))) m.assoc[k] = nullptr;   // supprimee ou adoptee : plus a nous
    AnimMirror next = {};
    next.nFading = m.nFading;
    memcpy(next.fading, m.fading, sizeof(m.fading));
    for (int i = 0; i < n && i < ANIMS_MAX; i++) {
        const NetAnim &a = in[i];
        uint8_t *as = nullptr;
        for (int k = 0; k < m.count; k++) if (m.ids[k] == a.id && m.assoc[k]) as = (uint8_t *)m.assoc[k];
        if (!as) {
            if (((void *(__cdecl *)(void *, int))0x4D68B0)(clump, a.id)) continue;   // deja jouee par le jeu lui-meme
            if (!GroupReady(a.group)) continue;
            as = (uint8_t *)((void *(__cdecl *)(void *, int, int, float))0x4D4610)(clump, a.group, a.id, 8.0f);
            if (!as) continue;
            static int said;
            if (said < 20) { said++; Log("animations : %d:%d rejouee sur %p", a.group, a.id, ped); }
        }
        float len = *(float *)(*(uint8_t **)(as + 0x14) + 0x10);   // CAnimBlendHierarchy : duree +0x10
        float t = a.time;
        if (len > 0 && t > len) t = len;
        if (fabsf(*(float *)(as + 0x20) - t) > 0.25f) *(float *)(as + 0x20) = t;
        *(uint16_t *)(as + 0x2E) |= 1;    // en cours
        if (*(float *)(as + 0x18) < a.blend / 255.0f) *(float *)(as + 0x1C) = 8.0f;
        next.ids[next.count] = a.id;
        next.assoc[next.count++] = as;
    }
    for (int k = 0; k < m.count; k++) {   // plus recues : effacees (supprimees par le jeu une fois a zero), surveillees
        if (!m.assoc[k]) continue;
        bool still = false;
        for (int i = 0; i < next.count; i++) still |= next.assoc[i] == m.assoc[k];
        if (still) continue;
        uint8_t *as = (uint8_t *)m.assoc[k];
        if (!Partial(as)) { BlendIdle(ped, clump, 4.0f); continue; }   // corps entier : le moteur passe au repos
        *(float *)(as + 0x1C) = -4.0f;
        *(uint16_t *)(as + 0x2E) |= 4;
        if (next.nFading < 8) next.fading[next.nFading++] = as;
    }
    m = next;
    EnsureBase(ped, clump);
}

// Visee : la tache CTaskSimpleUseGun (ordre AIM) en tache secondaire 0 tant que "want", point vise a jour a chaque image
// (ctor 0x61DE60, point +0x20, SetTaskSecondary 0x681B60, ControlGun 0x61E040 : sans ordre a chaque image elle retombe).
void AimMirror(void *ped, bool want, const float *aim)
{
    void **tm = PrimaryTasks(ped);
    void *sec = tm[5];
    bool has = sec && ((int(__thiscall *)(void *))(*(void ***)sec)[4])(sec) == 1017;
    if (want && !has) {
        if (void *mem = ((void *(__cdecl *)(unsigned))0x61A5A0)(0x3C)) {
            void *task = ((void *(__thiscall *)(void *, void *, float, float, float, int, int, int))0x61DE60)(mem, nullptr, aim[0], aim[1], aim[2], 1, 1, 1);
            ((void(__thiscall *)(void *, void *, int))0x681B60)(tm, task, 0);
            static int said;
            if (said < 20) { said++; Log("visee rejouee sur %p (vers %.0f %.0f %.0f)", ped, aim[0], aim[1], aim[2]); }
        }
    } else if (want && has) {
        memcpy((uint8_t *)sec + 0x20, aim, 12);
        ((bool(__thiscall *)(void *, void *, void *, int))0x61E040)(sec, ped, nullptr, 1);
    } else if (!want && has) {
        ((void(__thiscall *)(void *, void *, int))0x681B60)(tm, nullptr, 0);
    }
    if (want) {
        const float *pp = EntityPos(ped);
        float h = atan2f(-(aim[0] - pp[0]), aim[1] - pp[1]);
        Field<float>(ped, PED_ROTATION) = h;
        Field<float>(ped, PED_AIMROT) = h;
    }
}

// Point vise par un personnage qui vise (tache secondaire 0 = CTaskSimpleUseGun) : sa cible (+0x1C) ou son point (+0x20).
bool AimOf(void *ped, float *aim)
{
    void *sec = PrimaryTasks(ped)[5];
    if (!sec || ((int(__thiscall *)(void *))(*(void ***)sec)[4])(sec) != 1017) return false;
    if (void *target = *(void **)((uint8_t *)sec + 0x1C)) {
        const float *p = EntityPos(target);
        aim[0] = p[0]; aim[1] = p[1]; aim[2] = p[2] + 0.4f;
    } else memcpy(aim, (uint8_t *)sec + 0x20, 12);
    return true;
}
