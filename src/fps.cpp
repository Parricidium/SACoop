// Jeu a plus de 30 images/s (ImagesParSeconde 45 ou 60) : corrections des bogues de cadence du jeu d'origine, portees
// de Framerate Vigilante (Junior_Djjr, GTAmodding/FramerateVigilante, licence MIT, voir THIRD-PARTY.txt), sur les
// adresses du 1.0 US verifiees une a une dans notre exe. Le principe : la ou le jeu ajoute ou multiplie une valeur
// fixe a chaque image (pensee pour 30 images/s), on la met a l'echelle du pas de temps de l'image :
//     k = CTimer::ms_fTimeStep (0xB7CB5C) / (50 / 30)   (k = 1 a 30 images/s, 0,5 a 60)
// donc rien ne change a 30 images/s.
//  - nage (vitesse, plongee, remontee), flottaison du joueur ; Skimmer (hydravion) ; roues des vehicules sur rails ;
//    burnout ; voitures qui ralentissent trop vite sans gaz ; rotor des helicopteres ; pietons qui poussent les
//    voitures ; marche en visant au fusil ; portes, capots, echelle du camion de pompiers (CDoor : elan et
//    amortissement).
//  - Pas la sirene (appui court sur le klaxon) : ce code est deplace dans notre exe (0x6E0961 -> 0x403940).
//  - Cadence : un seul limiteur, celui de SACoop (window.cpp) : celui du jeu (RsGlobal.frameLimit 0xC1704C, et 14 ms
//    minimum entre deux images en 0x53E94A) se battait avec lui (25 images/s mesurees pour 30 demandees). Plafonds
//    automatiques comme Framerate Vigilante : 30 au billard et avec la copine (POOL2, GFSEX), 50 dans la mission
//    DRUGS1 en interieur (Big Smoke s'arrete de marcher).
#include "util.h"
#include "sacoop.h"
#include "game.h"
#include "fps.h"
#include <string.h>

static const float c06 = 0.6f, c1 = 1.0f, c008 = 0.08f, c003 = 0.03f;
static float g_k = 1.0f, g_tmp, g_buoy;

// k = ms_fTimeStep * 0,6 dans g_k (pile du FPU inchangee)
#define K_UPDATE __asm fld dword ptr ds:[0xB7CB5C] __asm fmul dword ptr [c06] __asm fstp dword ptr [g_k]

// Visee au fusil en marchant (0x61E0CA "fmul [0.07]") : 0,07 / k
static void __declspec(naked) AimWalk()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fmul dword ptr ds:[0x858CA8]
        fdiv dword ptr [g_k]
        ret
    }
}

// Nage (0x68A50E "fld st(1) ; fmul [eax] ; fld st(2)") : vitesse (3 flottants de la pile de l'appelant) / k
static void __declspec(naked) SwimSpeed()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr [esp + 20h]
        fdiv dword ptr [g_k]
        fstp dword ptr [esp + 20h]
        fld dword ptr [esp + 24h]
        fdiv dword ptr [g_k]
        fstp dword ptr [esp + 24h]
        fld dword ptr [esp + 1Ch]
        fdiv dword ptr [g_k]
        fstp dword ptr [esp + 1Ch]
        fld st(1)
        fmul dword ptr [eax]
        fld st(2)
        ret
    }
}

// Flottaison (0x6C27AE "fmul [ms_fTimeStep]", entite dans eax) : pour le joueur, (1 + k / 1,5) * k
static void __cdecl BuoyFactor(void *ent)
{
    float ts = *(float *)0xB7CB5C, k = ts * 0.6f;
    g_buoy = ent && ent == game::FindPlayerPed() ? (1.0f + k / 1.5f) * k : ts;
}
static void __declspec(naked) Buoyancy()
{
    __asm {
        pushad
        pushfd
        push eax
        call BuoyFactor
        add esp, 4
        popfd
        popad
        fmul dword ptr [g_buoy]
        ret
    }
}

// Plongee (0x68A42B "fmul [-0.1]") : -0,1 * k
static void __declspec(naked) Dive()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fmul dword ptr ds:[0x858EF4]
        fmul dword ptr [g_k]
        ret
    }
}

// Remontee en nageant vite (0x68A4CA "fadd [0.01]") : (x + 0,01) / k
static void __declspec(naked) DiveSurface()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fadd dword ptr ds:[0x8708CC]
        fdiv dword ptr [g_k]
        ret
    }
}

// Skimmer (0x6D2771 "fmul [30.0]") : 30 * k
static void __declspec(naked) Skimmer()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fmul dword ptr ds:[0x871DDC]
        fmul dword ptr [g_k]
        ret
    }
}

// Roues sur rails (0x6B523F... "fadd [esi+828h..834h]") : x * k + roue
#define WHEEL_STUB(name, off) \
    static void __declspec(naked) name() { K_UPDATE __asm fmul dword ptr [g_k] __asm fadd dword ptr [esi + off] __asm ret }
WHEEL_STUB(Wheel1, 828h)
WHEEL_STUB(Wheel2, 82Ch)
WHEEL_STUB(Wheel3, 830h)
WHEEL_STUB(Wheel4, 834h)

// Burnout (0x6A4FE6 "fld [3000.0]") : 3000 * k
static void __declspec(naked) Burnout()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr ds:[0x859A94]
        fmul dword ptr [g_k]
        ret
    }
}

// Ralentissement sans gaz (5 sites "fld [0xC2B9CC]") : valeur * k
static void __declspec(naked) SlowDown()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr ds:[0xC2B9CC]
        fmul dword ptr [g_k]
        ret
    }
}

// Rotor d'helicoptere (0x6C4F29 "fadd [0.001]", 0x6C4F37 "fadd [0.003]") : + increment * k
static void __declspec(naked) RotorA()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr ds:[0x858CDC]
        fmul dword ptr [g_k]
        fstp dword ptr [g_tmp]
        fadd dword ptr [g_tmp]
        ret
    }
}
static void __declspec(naked) RotorB()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr ds:[0x859CD8]
        fmul dword ptr [g_k]
        fstp dword ptr [g_tmp]
        fadd dword ptr [g_tmp]
        ret
    }
}

// Pieton qui pousse une voiture (0x549652, 8 octets "mov edx,[esp+20h] ; mov eax,[esp+24h]") : force * k
static void __declspec(naked) PushCar()
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr [esp + 24h]
        fmul dword ptr [g_k]
        fstp dword ptr [esp + 24h]
        fld dword ptr [esp + 28h]
        fmul dword ptr [g_k]
        fstp dword ptr [esp + 28h]
        fld dword ptr [esp + 2Ch]
        fmul dword ptr [g_k]
        fstp dword ptr [esp + 2Ch]
        mov edx, dword ptr [esp + 24h]
        mov eax, dword ptr [esp + 28h]
        ret
    }
}

// Portes et pieces battantes (CDoor, 0x6F42xx-0x6F44xx) : elan * k, amortissement 1 - k * perte
static void __declspec(naked) SwingAdd()        // 0x6F4422 "fld [esi+14h] ; mov ecx, ebx" -> 0x6F4427
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr [esi + 14h]
        fmul dword ptr [g_k]
        mov ecx, ebx
        push 6F4427h
        ret
    }
}
static void __declspec(naked) SwingForceA()     // 0x6F42DB "fmul st(1) ; fadd [esi+14h]" -> 0x6F42E0
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fmul st, st(1)
        fmul dword ptr [g_k]
        fadd dword ptr [esi + 14h]
        push 6F42E0h
        ret
    }
}
static void __declspec(naked) SwingForceB()     // 0x6F437D "fadd [esi+14h] ; fstp [esi+14h]" -> 0x6F4383
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fmul dword ptr [g_k]
        fadd dword ptr [esi + 14h]
        fstp dword ptr [esi + 14h]
        push 6F4383h
        ret
    }
}
static void __declspec(naked) SwingDampA()      // 0x6F43A1 "fld [0.92]" -> 0x6F43A7
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr [g_k]
        fmul dword ptr [c008]
        fsubr dword ptr [c1]
        push 6F43A7h
        ret
    }
}
static void __declspec(naked) SwingDampB()      // 0x6F43CA "fld [0.97]" -> 0x6F43D0
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr [g_k]
        fmul dword ptr [c003]
        fsubr dword ptr [c1]
        push 6F43D0h
        ret
    }
}
static void __declspec(naked) SwingDampC()      // 0x6F43D2 "fld [0.97]" (autre branche) -> 0x6F43D8
{
    __asm {
        fld dword ptr ds:[0xB7CB5C]
        fmul dword ptr [c06]
        fstp dword ptr [g_k]
        fld dword ptr [g_k]
        fmul dword ptr [c003]
        fsubr dword ptr [c1]
        push 6F43D8h
        ret
    }
}

struct Site { uintptr_t addr; uint8_t len; uint8_t bytes[8]; void *stub; bool jump; };
static const Site kSites[] = {
    { 0x61E0CA, 6, { 0xD8, 0x0D, 0xA8, 0x8C, 0x85, 0x00 }, (void *)AimWalk, false },
    { 0x68A50E, 6, { 0xD9, 0xC1, 0xD8, 0x08, 0xD9, 0xC2 }, (void *)SwimSpeed, false },
    { 0x6C27AE, 6, { 0xD8, 0x0D, 0x5C, 0xCB, 0xB7, 0x00 }, (void *)Buoyancy, false },
    { 0x68A42B, 6, { 0xD8, 0x0D, 0xF4, 0x8E, 0x85, 0x00 }, (void *)Dive, false },
    { 0x68A4CA, 6, { 0xD8, 0x05, 0xCC, 0x08, 0x87, 0x00 }, (void *)DiveSurface, false },
    { 0x6D2771, 6, { 0xD8, 0x0D, 0xDC, 0x1D, 0x87, 0x00 }, (void *)Skimmer, false },
    { 0x6B523F, 6, { 0xD8, 0x86, 0x28, 0x08, 0x00, 0x00 }, (void *)Wheel1, false },
    { 0x6B524F, 6, { 0xD8, 0x86, 0x2C, 0x08, 0x00, 0x00 }, (void *)Wheel2, false },
    { 0x6B525D, 6, { 0xD8, 0x86, 0x30, 0x08, 0x00, 0x00 }, (void *)Wheel3, false },
    { 0x6B5269, 6, { 0xD8, 0x86, 0x34, 0x08, 0x00, 0x00 }, (void *)Wheel4, false },
    { 0x6A4FE6, 6, { 0xD9, 0x05, 0x94, 0x9A, 0x85, 0x00 }, (void *)Burnout, false },
    { 0x6D6E69, 6, { 0xD9, 0x05, 0xCC, 0xB9, 0xC2, 0x00 }, (void *)SlowDown, false },
    { 0x6D6EA8, 6, { 0xD9, 0x05, 0xCC, 0xB9, 0xC2, 0x00 }, (void *)SlowDown, false },
    { 0x6D767F, 6, { 0xD9, 0x05, 0xCC, 0xB9, 0xC2, 0x00 }, (void *)SlowDown, false },
    { 0x6D76AB, 6, { 0xD9, 0x05, 0xCC, 0xB9, 0xC2, 0x00 }, (void *)SlowDown, false },
    { 0x6D76CD, 6, { 0xD9, 0x05, 0xCC, 0xB9, 0xC2, 0x00 }, (void *)SlowDown, false },
    { 0x6C4F29, 6, { 0xD8, 0x05, 0xDC, 0x8C, 0x85, 0x00 }, (void *)RotorA, false },
    { 0x6C4F37, 6, { 0xD8, 0x05, 0xD8, 0x9C, 0x85, 0x00 }, (void *)RotorB, false },
    { 0x549652, 8, { 0x8B, 0x54, 0x24, 0x20, 0x8B, 0x44, 0x24, 0x24 }, (void *)PushCar, false },
    { 0x6F4422, 5, { 0xD9, 0x46, 0x14, 0x8B, 0xCB }, (void *)SwingAdd, true },
    { 0x6F42DB, 5, { 0xD8, 0xC9, 0xD8, 0x46, 0x14 }, (void *)SwingForceA, true },
    { 0x6F437D, 6, { 0xD8, 0x46, 0x14, 0xD9, 0x5E, 0x14 }, (void *)SwingForceB, true },
    { 0x6F43A1, 6, { 0xD9, 0x05, 0x14, 0x23, 0x87, 0x00 }, (void *)SwingDampA, true },
    { 0x6F43CA, 6, { 0xD9, 0x05, 0x18, 0x23, 0x87, 0x00 }, (void *)SwingDampB, true },
    { 0x6F43D2, 6, { 0xD9, 0x05, 0x0C, 0x23, 0x87, 0x00 }, (void *)SwingDampC, true },
};

void InstallFps()
{
    if (g_cfg.maxFps <= 30) return;   // a 30 images/s le jeu d'origine est juste : rien a corriger
    if (!GetPrivateProfileIntA("SACoop", "CorrectionsCadence", 1, IniPath())) { Log("cadence : corrections coupees (CorrectionsCadence=0)"); return; }
    int ok = 0, bad = 0;
    for (const Site &s : kSites) {
        if (memcmp((void *)s.addr, s.bytes, s.len)) { bad++; Log("cadence : code inattendu en %08X, correction sautee", (unsigned)s.addr); continue; }
        if (s.jump) PatchJump(s.addr, s.stub, s.len);
        else PatchCall(s.addr, s.stub, s.len);
        ok++;
    }
    // 14 ms minimum entre deux images (Idle 0x53E94A "cmp eax, 0Eh") : retire, notre limiteur cadence
    static const uint8_t wait14[] = { 0x83, 0xF8, 0x0E };
    if (!memcmp((void *)0x53E94A, wait14, 3)) { uint8_t z = 0; Patch(0x53E94C, &z, 1); }
    Log("cadence : %d images/s, %d corrections de Framerate Vigilante posees (%d sautees)", g_cfg.maxFps, ok, bad);
}

// Plafond du moment (window.cpp, limiteur) : ImagesParSeconde, ou moins dans les cas connus.
static int g_cap;
int FpsCurrentCap() { return g_cap > 0 && g_cap < g_cfg.maxFps ? g_cap : g_cfg.maxFps; }

void FpsFrame()
{
    if (g_cfg.maxFps <= 0) return;
    // le limiteur du jeu ne doit jamais retenir une image avant le notre (1000 / 1000 = 1 ms)
    *(int *)0xC1704C = 1000;
    if (g_cfg.maxFps <= 30) { g_cap = 0; return; }
    static uint32_t last;
    uint32_t now = GetTickCount();
    if (now - last < 500) return;
    last = now;
    int cap = 0;
    // scripts actifs (CTheScripts::pActiveScripts 0xA8B42C ; CRunningScript : suivant +0, nom +8)
    int guard = 0;
    for (uint8_t *s = *(uint8_t **)0xA8B42C; s && guard < 200; s = *(uint8_t **)s, guard++) {
        const char *name = (const char *)(s + 8);
        if (!_strnicmp(name, "POOL2", 8) || !_strnicmp(name, "GFSEX", 8)) cap = 30;
        else if (!_strnicmp(name, "DRUGS1", 8) && *(int *)0xB72914 != 0 && (cap == 0 || cap > 50)) cap = 50;   // CGame::currArea
    }
    if (cap != g_cap) Log("cadence : plafond %d images/s (%s)", cap ? cap : g_cfg.maxFps, cap == 30 ? "billard / copine" : cap == 50 ? "DRUGS1 en interieur" : "normal");
    g_cap = cap;
}
