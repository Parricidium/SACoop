// Vue a la premiere personne a pied (F6, VuePremierePersonne=1), comme dans VCCoop.
//  - Apres CCam::Process (0x526FC0) de la camera active (TheCamera 0xB6F028 : m_nActiveCam +0x59, m_aCams +0x174,
//    0x238 octets chacune), si c'est la camera de suivi a pied (MODE_FOLLOWPED 4 : visee, bagarre, cinematiques
//    gardent celle du jeu), sa position (m_vecSource +0x19C) est posee dans les yeux de CJ : os de la tete
//    (CPed::GetBonePosition 0x5E4280, os 5) avance de 15 cm dans l'axe de la vue (le visage reste derriere la camera,
//    donc invisible). La direction (m_vecFront +0x190) reste celle du jeu : souris, sensibilite, inversion ; la
//    marche suit la vue. "Haut" (m_vecUp +0x1B4) recalcule. Plan proche a 5 cm (RwCameraSetNearClipPlane 0x7EE1D0
//    sur Scene.m_pRwCamera 0xC1703C).
//  - En vehicule, le jeu a deja ses vues interieures (touche V).
#include "util.h"
#include "sacoop.h"
#include "net.h"
#include "game.h"
#include "chat.h"
#include "camera.h"
#include <math.h>
#include <string.h>

using namespace game;

static bool g_fps;
bool FirstPersonActive() { return g_fps; }

static void Normalize(float *v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-5f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

typedef void(__fastcall *CamProcess_t)(void *cam, void *edx);
static CamProcess_t o_CamProcess;

static void __fastcall h_CamProcess(void *cam, void *edx)
{
    o_CamProcess(cam, edx);
    if (!g_fps) return;
    uint8_t *theCam = (uint8_t *)0xB6F028;
    if ((uint8_t *)cam != theCam + 0x174 + theCam[0x59] * 0x238) return;
    if (Field<int16_t>(cam, 0x0C) != 4 || *(uint8_t *)0xB5F851) return;   // suivi a pied seulement, pas en cinematique
    void *me = FindPlayerPed();
    if (!me || PedVehicle(me)) return;
    float head[3];
    ((void(__thiscall *)(void *, float *, int, bool))0x5E4280)(me, head, 5, false);
    float *src = (float *)((uint8_t *)cam + 0x19C), *front = (float *)((uint8_t *)cam + 0x190), *up = (float *)((uint8_t *)cam + 0x1B4);
    float f[3] = { front[0], front[1], front[2] };
    Normalize(f);
    float flat[3] = { f[0], f[1], 0 };
    Normalize(flat);
    // Les os datent de l'image precedente : avances d'une image de deplacement (sinon, en courant, on voyait la tete).
    const float *mv = MoveSpeed(me);
    float ts = *(float *)0xB7CB5C;   // CTimer::ms_fTimeStep
    for (int k = 0; k < 3; k++) src[k] = head[k] + mv[k] * ts + flat[k] * 0.15f;
    src[2] += 0.04f;
    float right[3] = { f[1], -f[0], 0 };
    Normalize(right);
    up[0] = right[1] * f[2] - right[2] * f[1];
    up[1] = right[2] * f[0] - right[0] * f[2];
    up[2] = right[0] * f[1] - right[1] * f[0];
    memcpy(front, f, 12);
    ((void *(__cdecl *)(void *, float))0x7EE1D0)(*(void **)0xC1703C, 0.05f);
    if (g_cfg.logScripts) {
        static uint32_t last;
        if (GetTickCount() - last > 3000) {
            last = GetTickCount();
            Log("camera : premiere personne, yeux %.2f %.2f %.2f, regard %.2f %.2f %.2f", src[0], src[1], src[2], f[0], f[1], f[2]);
        }
    }
}

bool CameraWindowMessage(UINT msg, WPARAM wp)
{
    if (msg != WM_KEYDOWN || wp != (WPARAM)g_cfg.fpsKey || !g_cfg.fpsView || ChatTyping()) return false;
    if (GameState() != 9 || *(uint8_t *)(0xBA6748 + 0x5C)) return false;   // en partie, hors menus
    g_fps = !g_fps;
    Log("camera : premiere personne %s", g_fps ? "oui" : "non");
    return true;
}

void CameraFrame()
{
    static bool tested;
    if (g_cfg.testFirstPerson && !tested && GameState() == 9) { tested = true; g_fps = true; Log("camera : premiere personne (test)"); }
    if (!g_cfg.fpsView) g_fps = false;
}

void InstallCamera()
{
    static const uint8_t pro[] = { 0xDB, 0x05, 0x34, 0xFE, 0xB6, 0x00 };   // fild dword ptr [0xB6FE34]
    o_CamProcess = (CamProcess_t)MakeDetour(0x526FC0, pro, sizeof(pro), (void *)h_CamProcess);
}
