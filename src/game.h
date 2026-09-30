// Structures et fonctions du jeu (1.0 US), verifiees dans l'exe (re\out).
#pragma once
#include <stdint.h>

struct CVector { float x, y, z; };

namespace game {
    // --- Etat general ---
    inline int GameState() { return *(int *)0xC8D4C0; }              // 9 = en partie

    // --- Entites : CPlaceable (m_placement +4 : position, +0x10 : cap ; m_matrix +0x14 : CMatrix, position +0x30) ---
    inline float *EntityPos(void *e)
    {
        uint8_t *p = (uint8_t *)e;
        uint8_t *m = *(uint8_t **)(p + 0x14);
        return m ? (float *)(m + 0x30) : (float *)(p + 4);
    }
    inline uint8_t &EntityArea(void *e) { return *((uint8_t *)e + 0x2F); }     // CEntity::m_nAreaCode (interieur)
    inline float *MoveSpeed(void *e) { return (float *)((uint8_t *)e + 0x44); } // CPhysical::m_vecMoveSpeed

    // --- Personnages (CPed) ---
    enum : int {
        PED_STATE = 0x530, PED_MOVESTATE = 0x534, PED_HEALTH = 0x540, PED_ARMOUR = 0x548,
        PED_ROTATION = 0x558, PED_AIMROT = 0x55C, PED_TYPE = 0x598, PED_INTEL = 0x47C, PED_CREATEDBY = 0x484,
        PED_WEAPONSLOT = 0x718, PED_WEAPONS = 0x5A0,   // 13 CWeapon de 0x1C ; type en +0
    };
    template <class T> inline T &Field(void *p, int off) { return *(T *)((uint8_t *)p + off); }
    // Deplacements (m_nMoveState) : 0 aucun, 1 immobile, 2/3 tourne, 4 marche, 5 trot, 6 course, 7 sprint.
    enum { MOVE_STILL = 1, MOVE_WALK = 4, MOVE_RUN = 6, MOVE_SPRINT = 7 };

    inline void *FindPlayerPed() { return ((void *(__cdecl *)(int))0x56E210)(-1); }

    // Vehicule : +0x460 conducteur, +0x464 passagers (8), +0x488 places passagers, +0x434/0x435 couleurs.
    enum : int { VEH_DRIVER = 0x460, VEH_PASSENGERS = 0x464, VEH_MAXPASS = 0x488, VEH_TYPE = 0x590 };
    // Personnage en vehicule : drapeau bInVehicle (+0x46C bit 0x100) et CPed::m_pVehicle (+0x58C).
    inline void *PedVehicle(void *ped)
    {
        return (Field<uint32_t>(ped, 0x46C) & 0x100) ? Field<void *>(ped, 0x58C) : nullptr;
    }

    // Pool des personnages (CPools::ms_pPedPool 0xB74490 : objets, octets d'etat, taille ; cases de 0x7C4) :
    // reference = (case << 8) | octet d'etat (bit 7 = case libre). Une reference sert a verifier qu'un personnage
    // qu'on a cree existe encore (le jeu peut le supprimer : fin de cinematique, nettoyage de zone).
    struct Pool { uint8_t *objects; uint8_t *flags; int size, top; };
    inline Pool *PedPool() { return *(Pool **)0xB74490; }
    inline int PedRef(void *ped)
    {
        Pool *p = PedPool();
        int i = (int)(((uint8_t *)ped - p->objects) / 0x7C4);
        return (i << 8) | p->flags[i];
    }
    inline void *PedFromRef(int ref)
    {
        Pool *p = PedPool();
        int i = ref >> 8;
        if (i < 0 || i >= p->size || (p->flags[i] & 0x80) || p->flags[i] != (ref & 0xFF)) return nullptr;
        return p->objects + i * 0x7C4;
    }

    // Creation d'un CCivilianPed : CPed::operator new (pool des personnages), puis le constructeur (type, modele).
    inline void *NewCivilianPed(int pedType, int model)
    {
        void *mem = ((void *(__cdecl *)(unsigned))0x5E4720)(0x79C);
        if (!mem) return nullptr;
        return ((void *(__thiscall *)(void *, int, int))0x5DDB70)(mem, pedType, model);
    }
    inline void SetCharCreatedBy(void *ped, char by) { ((void(__thiscall *)(void *, char))0x5E47E0)(ped, by); }   // 2 = mission
    inline void WorldAdd(void *e) { ((void(__cdecl *)(void *))0x563220)(e); }
    inline void WorldRemove(void *e) { ((void(__cdecl *)(void *))0x563280)(e); }
    inline void RemoveReferencesToDeletedObject(void *e) { ((void(__cdecl *)(void *))0x565510)(e); }
    inline void DeleteEntity(void *e) { ((void(__thiscall *)(void *, int))((*(void ***)e)[0]))(e, 1); }   // vtable[0] : destructeur

    // --- Chargement des modeles (CStreaming) ---
    inline void RequestModel(int id, int flags) { ((void(__cdecl *)(int, int))0x4087E0)(id, flags); }   // flags 2 : mission
    inline void LoadAllRequestedModels(bool priorityOnly) { ((void(__cdecl *)(bool))0x40EA10)(priorityOnly); }
    inline bool ModelLoaded(int id) { return *(uint8_t *)(0x8E4CC0 + id * 0x14 + 0x10) == 1; }   // ms_aInfoForModel[id].m_nLoadState

    // --- Taches (CPedIntelligence +4 : CTaskManager, taches principales [0..4], 3 = PRIMARY) ---
    inline void **PrimaryTasks(void *ped) { return (void **)(Field<uint8_t *>(ped, PED_INTEL) + 4); }
    inline void SetPrimaryTask(void *ped, void *task, int slot)
    {
        // CTaskManager::SetTask(CTask *, int slot, bool) : trois parametres (ret 0Ch).
        ((void(__thiscall *)(void *, void *, int, bool))0x681AF0)(PrimaryTasks(ped), task, slot, false);
    }
    enum : uintptr_t { VT_TaskSimpleGoToPoint = 0x86FD50 };
    // CTaskSimpleGoToPoint (0x24 octets) : move state +8, cible +0xC, rayon +0x18.
    inline void *NewGoToPoint(int moveState, const CVector &target, float radius)
    {
        void *mem = ((void *(__cdecl *)(unsigned))0x61A5A0)(0x24);   // CTask::operator new (pool des taches)
        if (!mem) return nullptr;
        return ((void *(__thiscall *)(void *, int, const CVector *, float, bool, bool))0x667CD0)(mem, moveState, &target, radius, false, false);
    }
}
