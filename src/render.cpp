// Rendu moderne (facultatif) : occlusion ambiante (OcclusionAmbiante=1) et anticrenelage FXAA (Anticrenelage=1).
//  - Profondeur lisible : a la creation du peripherique (et apres chaque Reset), une texture de profondeur INTZ (format
//    de profondeur que les cartes savent lire comme une texture) de la taille de l'image remplace le tampon de
//    profondeur automatique du jeu : IDirect3DDevice9::SetDepthStencilSurface (vtable 39) est detournee, et le
//    tampon d'origine y est remplace par le notre. Pas d'INTZ (carte) ou image multi-echantillonnee : rien.
//  - Apres la scene 3D et ses effets, avant l'interface (appel de Render2dStuff 0x53E230 en 0x53EB12 dans Idle) :
//      1. l'image est recopiee (StretchRect) ;
//      2. occlusion : position de chaque pixel d'apres la profondeur (matrice de projection du jeu), normale deduite
//         des voisins, 12 echantillons dans l'hemisphere (rayon 0,7 m, tournes selon le pixel) ; coins, pieds des
//         murs, dessous des voitures assombris ; le ciel et le lointain (> 150 m) gardes ;
//      3. image x occlusion floutee (4x4, respecte les bords d'apres la profondeur) ;
//      4. FXAA (version 3.11 "qualite", simplifiee) sur le resultat, vers l'image.
//    Etat du peripherique sauve et remis (bloc d'etat), profondeur deliee pendant nos passes.
// Shaders HLSL compiles au lancement par le compilateur de Windows (d3dcompiler_47.dll), etat flottant (x87, SSE)
// garde autour : le compilateur le change, et le jeu calcule alors faux.
#include "util.h"
#include "sacoop.h"
#include "game.h"
#include "render.h"
#include <d3d9.h>
#include <d3dcompiler.h>
#include <float.h>
#include <xmmintrin.h>
#include <string.h>

typedef HRESULT(WINAPI *SetDS_t)(IDirect3DDevice9 *, IDirect3DSurface9 *);
static SetDS_t o_SetDS;
static IDirect3DDevice9 *g_dev;
static IDirect3DSurface9 *g_autoDepth;       // tampon de profondeur du jeu (reference gardee)
static IDirect3DTexture9 *g_intz;            // notre profondeur lisible
static IDirect3DSurface9 *g_intzSurf;
static IDirect3DTexture9 *g_scene, *g_ao, *g_tmp;
static IDirect3DPixelShader9 *g_psAO, *g_psComp, *g_psFxaa;
static IDirect3DStateBlock9 *g_state;
static UINT g_w, g_h;
static bool g_ok, g_compiled, g_logged;
static const D3DFORMAT FMT_INTZ = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z');

static HRESULT WINAPI h_SetDS(IDirect3DDevice9 *dev, IDirect3DSurface9 *s)
{
    if (s && s == g_autoDepth && g_intzSurf) s = g_intzSurf;
    return o_SetDS(dev, s);
}

// ---------------------------------------------------------------- shaders
static const char *kShaders = R"HLSL(
sampler2D sDepth : register(s0);
sampler2D sScene : register(s1);
sampler2D sAO    : register(s2);
float4 cProj  : register(c0);   // x = 1/P11, y = 1/P22, z = P33, w = P43
float4 cPix   : register(c1);   // x = 1/largeur, y = 1/hauteur, z = force, w = rayon (m)

float ViewZ(float2 uv) { float d = tex2Dlod(sDepth, float4(uv, 0, 0)).r; return cProj.w / (d - cProj.z); }
float3 ViewPos(float2 uv)
{
    float z = ViewZ(uv);
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    return float3(ndc.x * cProj.x * z, ndc.y * cProj.y * z, z);
}
float2 ToUV(float3 p)
{
    float2 ndc = float2(p.x / (cProj.x * p.z), p.y / (cProj.y * p.z));
    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

float4 PsAO(float2 uv : TEXCOORD0, float2 vpos : VPOS) : COLOR0
{
    float d = tex2Dlod(sDepth, float4(uv, 0, 0)).r;
    if (d >= 0.99999) return 1;
    float3 p = ViewPos(uv);
    if (p.z > 150) return 1;
    float3 px = ViewPos(uv + float2(cPix.x, 0)), py = ViewPos(uv + float2(0, cPix.y));
    float3 n = normalize(cross(px - p, py - p));
    if (n.z > 0) n = -n;   // vers la camera
    // rotation par pixel (motif 4x4)
    float a = frac(sin(dot(floor(vpos), float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float ca = cos(a), sa = sin(a);
    float3 t = normalize(abs(n.y) < 0.99 ? cross(n, float3(0, 1, 0)) : cross(n, float3(1, 0, 0)));
    float3 b = cross(n, t);
    float occ = 0;
    const int N = 12;
    [unroll] for (int i = 0; i < N; i++) {
        float fi = (i + 0.5) / N;
        float ang = fi * 6.2831853 * 2.618 ;
        float r = sqrt(fi);
        float2 dxy = float2(cos(ang), sin(ang));
        dxy = float2(dxy.x * ca - dxy.y * sa, dxy.x * sa + dxy.y * ca) * r;
        float h = sqrt(saturate(1 - r * r));
        float3 dir = t * dxy.x + b * dxy.y + n * h;
        float3 s = p + dir * cPix.w * (0.35 + 0.65 * fi);
        float2 suv = ToUV(s);
        if (any(suv < 0) || any(suv > 1)) continue;
        float sz = ViewZ(suv);
        float diff = s.z - sz;   // > 0 : la surface vue est devant l'echantillon
        occ += (diff > 0.03 ? 1 : 0) * saturate(1 - (diff - 0.03) / (cPix.w * 2.5));
    }
    float ao = 1 - occ / N * min(cPix.z, 1);
    ao = lerp(ao, 1, saturate((p.z - 90) / 60));   // fondu au loin
    return ao;
}

float4 PsComp(float2 uv : TEXCOORD0) : COLOR0
{
    float z0 = ViewZ(uv);
    float sum = 0, wsum = 0;
    [unroll] for (int y = -1; y <= 2; y++)
    [unroll] for (int x = -1; x <= 2; x++) {
        float2 o = float2(x - 0.5, y - 0.5) * cPix.xy;
        float z = ViewZ(uv + o);
        float w = saturate(1 - abs(z - z0) / (0.05 * z0 + 0.1));
        sum += tex2Dlod(sAO, float4(uv + o, 0, 0)).r * w;
        wsum += w;
    }
    float ao = wsum > 0 ? sum / wsum : 1;
    float4 c = tex2Dlod(sScene, float4(uv, 0, 0));
    if (cPix.z > 1.5) return float4(ao, ao, ao, 1);   // TestOcclusion : l'occlusion seule
    return float4(c.rgb * ao, 1);
}

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float4 PsFxaa(float2 uv : TEXCOORD0) : COLOR0
{
    float2 px = cPix.xy;
    float3 cM = tex2Dlod(sScene, float4(uv, 0, 0)).rgb;
    float lM = Luma(cM);
    float lN = Luma(tex2Dlod(sScene, float4(uv + float2(0, -px.y), 0, 0)).rgb);
    float lS = Luma(tex2Dlod(sScene, float4(uv + float2(0, px.y), 0, 0)).rgb);
    float lW = Luma(tex2Dlod(sScene, float4(uv + float2(-px.x, 0), 0, 0)).rgb);
    float lE = Luma(tex2Dlod(sScene, float4(uv + float2(px.x, 0), 0, 0)).rgb);
    float lMin = min(lM, min(min(lN, lS), min(lW, lE)));
    float lMax = max(lM, max(max(lN, lS), max(lW, lE)));
    float range = lMax - lMin;
    if (range < max(0.0312, lMax * 0.125)) return float4(cM, 1);
    float lNW = Luma(tex2Dlod(sScene, float4(uv + float2(-px.x, -px.y), 0, 0)).rgb);
    float lNE = Luma(tex2Dlod(sScene, float4(uv + float2(px.x, -px.y), 0, 0)).rgb);
    float lSW = Luma(tex2Dlod(sScene, float4(uv + float2(-px.x, px.y), 0, 0)).rgb);
    float lSE = Luma(tex2Dlod(sScene, float4(uv + float2(px.x, px.y), 0, 0)).rgb);
    float edgeH = abs(lNW + lNE - 2 * lN) + 2 * abs(lW + lE - 2 * lM) + abs(lSW + lSE - 2 * lS);
    float edgeV = abs(lNW + lSW - 2 * lW) + 2 * abs(lN + lS - 2 * lM) + abs(lNE + lSE - 2 * lE);
    bool horz = edgeH >= edgeV;
    float l1 = horz ? lN : lW, l2 = horz ? lS : lE;
    float g1 = abs(l1 - lM), g2 = abs(l2 - lM);
    float stp = horz ? px.y : px.x;
    float lLocal;
    if (g1 >= g2) { stp = -stp; lLocal = (l1 + lM) * 0.5; } else lLocal = (l2 + lM) * 0.5;
    float gScaled = max(g1, g2) * 0.25;
    float2 e = uv;
    if (horz) e.y += stp * 0.5; else e.x += stp * 0.5;
    float2 dir = horz ? float2(px.x, 0) : float2(0, px.y);
    float2 u1 = e - dir, u2 = e + dir;
    float d1 = Luma(tex2Dlod(sScene, float4(u1, 0, 0)).rgb) - lLocal, d2 = Luma(tex2Dlod(sScene, float4(u2, 0, 0)).rgb) - lLocal;
    bool r1 = abs(d1) >= gScaled, r2 = abs(d2) >= gScaled;
    static const float Q[10] = { 1, 1, 1, 1, 1.5, 2, 2, 2, 4, 8 };
    [unroll] for (int i = 0; i < 10; i++) {
        if (!r1) { u1 -= dir * Q[i]; d1 = Luma(tex2Dlod(sScene, float4(u1, 0, 0)).rgb) - lLocal; r1 = abs(d1) >= gScaled; }
        if (!r2) { u2 += dir * Q[i]; d2 = Luma(tex2Dlod(sScene, float4(u2, 0, 0)).rgb) - lLocal; r2 = abs(d2) >= gScaled; }
    }
    float dist1 = horz ? uv.x - u1.x : uv.y - u1.y, dist2 = horz ? u2.x - uv.x : u2.y - uv.y;
    bool near1 = dist1 < dist2;
    float dmin = min(dist1, dist2), len = dist1 + dist2;
    float off = -dmin / len + 0.5;
    bool smaller = lM < lLocal;
    bool ok = ((near1 ? d1 : d2) < 0) != smaller;
    off = ok ? off : 0;
    // sous-pixel
    float avg = (2 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) / 12;
    float sub = saturate(abs(avg - lM) / range);
    sub = (-2 * sub + 3) * sub * sub;
    off = max(off, sub * sub * 0.75);
    float2 fuv = uv;
    if (horz) fuv.y += off * stp; else fuv.x += off * stp;
    return float4(tex2Dlod(sScene, float4(fuv, 0, 0)).rgb, 1);
}
)HLSL";

typedef HRESULT(WINAPI *D3DCompile_t)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

static IDirect3DPixelShader9 *Compile(D3DCompile_t compile, const char *entry)
{
    ID3DBlob *code = nullptr, *err = nullptr;
    HRESULT hr = compile(kShaders, strlen(kShaders), "sacoop", nullptr, nullptr, entry, "ps_3_0", 1 << 15 /* O3 */, 0, &code, &err);
    IDirect3DPixelShader9 *ps = nullptr;
    if (FAILED(hr) || !code) Log("rendu : shader %s refuse : %s", entry, err ? (const char *)err->GetBufferPointer() : "?");
    else g_dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &ps);
    if (code) code->Release();
    if (err) err->Release();
    return ps;
}

static void CompileShaders()
{
    if (g_compiled) return;
    g_compiled = true;
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    D3DCompile_t compile = m ? (D3DCompile_t)GetProcAddress(m, "D3DCompile") : nullptr;
    if (!compile) { Log("rendu : d3dcompiler_47.dll introuvable, rendu moderne coupe"); return; }
    unsigned int fpu = _controlfp(0, 0);
    unsigned int mxcsr = _mm_getcsr();
    g_psAO = Compile(compile, "PsAO");
    g_psComp = Compile(compile, "PsComp");
    g_psFxaa = Compile(compile, "PsFxaa");
    _controlfp(fpu, 0xFFFFFFFF);
    _mm_setcsr(mxcsr);
    Log("rendu : shaders %s", g_psAO && g_psComp && g_psFxaa ? "prets" : "incomplets");
}

// ---------------------------------------------------------------- ressources
static void Release()
{
    if (g_dev && o_SetDS && g_autoDepth) o_SetDS(g_dev, g_autoDepth);   // on rend le sien au jeu (Reset)
    IUnknown **all[] = { (IUnknown **)&g_intzSurf, (IUnknown **)&g_intz, (IUnknown **)&g_scene, (IUnknown **)&g_ao, (IUnknown **)&g_tmp,
                         (IUnknown **)&g_state, (IUnknown **)&g_autoDepth };
    for (IUnknown **p : all) if (*p) { (*p)->Release(); *p = nullptr; }
    g_ok = false;
}

static void Create(IDirect3DDevice9 *dev, const D3DPRESENT_PARAMETERS *pp)
{
    g_dev = dev;
    if (!g_cfg.ao && !g_cfg.fxaa) return;
    if (!o_SetDS) {
        void **vt = *(void ***)dev;
        o_SetDS = (SetDS_t)PatchPointer(&vt[39], (void *)h_SetDS);
    }
    CompileShaders();
    if (!g_psComp || !g_psFxaa) return;
    D3DSURFACE_DESC bd;
    IDirect3DSurface9 *bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    bb->GetDesc(&bd);
    bb->Release();
    g_w = bd.Width; g_h = bd.Height;
    if (bd.MultiSampleType != D3DMULTISAMPLE_NONE) { Log("rendu : image multi-echantillonnee (anticrenelage du jeu), rendu moderne coupe"); return; }
    IDirect3D9 *d3d = nullptr;
    dev->GetDirect3D(&d3d);
    D3DDISPLAYMODE dm;
    dev->GetDisplayMode(0, &dm);
    bool intzOk = d3d && SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dm.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, FMT_INTZ));
    if (d3d) d3d->Release();
    if (!intzOk) { Log("rendu : profondeur INTZ non geree par la carte, rendu moderne coupe"); return; }
    dev->GetDepthStencilSurface(&g_autoDepth);
    bool ok = g_autoDepth &&
              SUCCEEDED(dev->CreateTexture(g_w, g_h, 1, D3DUSAGE_DEPTHSTENCIL, FMT_INTZ, D3DPOOL_DEFAULT, &g_intz, nullptr)) &&
              SUCCEEDED(g_intz->GetSurfaceLevel(0, &g_intzSurf)) &&
              SUCCEEDED(dev->CreateTexture(g_w, g_h, 1, D3DUSAGE_RENDERTARGET, bd.Format, D3DPOOL_DEFAULT, &g_scene, nullptr)) &&
              SUCCEEDED(dev->CreateTexture(g_w, g_h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_ao, nullptr)) &&
              SUCCEEDED(dev->CreateTexture(g_w, g_h, 1, D3DUSAGE_RENDERTARGET, bd.Format, D3DPOOL_DEFAULT, &g_tmp, nullptr)) &&
              SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &g_state));
    if (!ok) { Log("rendu : ressources refusees, rendu moderne coupe"); Release(); return; }
    o_SetDS(dev, g_intzSurf);
    g_ok = true;
    Log("rendu : profondeur lisible %ux%u, occlusion %d, FXAA %d", g_w, g_h, (int)g_cfg.ao, (int)g_cfg.fxaa);
}

void RenderDeviceCreated(IDirect3DDevice9 *dev, const D3DPRESENT_PARAMETERS *pp) { Create(dev, pp); }
void RenderBeforeReset() { Release(); }
void RenderAfterReset(IDirect3DDevice9 *dev, const D3DPRESENT_PARAMETERS *pp) { Create(dev, pp); }

// ---------------------------------------------------------------- passes
struct QuadVtx { float x, y, z, rhw, u, v; };
static void Quad(UINT w, UINT h)
{
    float fw = (float)w - 0.5f, fh = (float)h - 0.5f;
    QuadVtx q[4] = { { -0.5f, -0.5f, 0, 1, 0, 0 }, { fw, -0.5f, 0, 1, 1, 0 }, { -0.5f, fh, 0, 1, 0, 1 }, { fw, fh, 0, 1, 1, 1 } };
    g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(QuadVtx));
}
static void Sampler(int s, IDirect3DBaseTexture9 *t, bool linear)
{
    g_dev->SetTexture(s, t);
    g_dev->SetSamplerState(s, D3DSAMP_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    g_dev->SetSamplerState(s, D3DSAMP_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    g_dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    g_dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    g_dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    g_dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, 0);
}

static void PostProcess()
{
    if (!g_ok || !g_dev || game::GameState() != 9 || *(uint8_t *)(0xBA6748 + 0x5C)) return;   // en partie, hors menus
    if (!g_cfg.ao && !g_cfg.fxaa) return;
    D3DMATRIX proj;
    g_dev->GetTransform(D3DTS_PROJECTION, &proj);
    if (proj._34 == 0.0f || proj._11 == 0.0f || proj._22 == 0.0f) return;   // pas une perspective
    IDirect3DSurface9 *bb = nullptr, *rt0 = nullptr, *ds = nullptr;
    if (FAILED(g_dev->GetRenderTarget(0, &rt0)) || !rt0) return;
    g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    if (!bb) { rt0->Release(); return; }
    if (bb != rt0) { bb->Release(); rt0->Release(); return; }   // la scene n'est pas dans l'image (photo, etc.)
    g_dev->GetDepthStencilSurface(&ds);
    g_state->Capture();

    IDirect3DSurface9 *sceneS = nullptr, *aoS = nullptr, *tmpS = nullptr;
    g_scene->GetSurfaceLevel(0, &sceneS);
    g_ao->GetSurfaceLevel(0, &aoS);
    g_tmp->GetSurfaceLevel(0, &tmpS);
    g_dev->StretchRect(bb, nullptr, sceneS, nullptr, D3DTEXF_NONE);

    o_SetDS(g_dev, nullptr);   // profondeur lue, pas ecrite
    g_dev->SetVertexShader(nullptr);
    g_dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    g_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    static const bool testAO = GetPrivateProfileIntA("SACoop", "TestOcclusion", 0, IniPath()) != 0;
    float c[8] = { 1.0f / proj._11, 1.0f / proj._22, proj._33, proj._43, 1.0f / g_w, 1.0f / g_h, testAO ? 2.0f : 1.0f, 0.7f };
    g_dev->SetPixelShaderConstantF(0, c, 2);
    Sampler(0, g_intz, false);
    Sampler(1, g_scene, false);

    IDirect3DTexture9 *src = g_scene;
    if (g_cfg.ao && g_psAO) {
        g_dev->SetRenderTarget(0, aoS);
        g_dev->SetPixelShader(g_psAO);
        Quad(g_w, g_h);
        g_dev->SetRenderTarget(0, g_cfg.fxaa ? tmpS : bb);
        Sampler(2, g_ao, false);
        g_dev->SetPixelShader(g_psComp);
        Quad(g_w, g_h);
        src = g_tmp;
    }
    if (g_cfg.fxaa) {
        g_dev->SetRenderTarget(0, bb);
        Sampler(1, src, true);
        g_dev->SetPixelShader(g_psFxaa);
        Quad(g_w, g_h);
    }
    for (int s = 0; s < 3; s++) g_dev->SetTexture(s, nullptr);
    g_state->Apply();
    g_dev->SetRenderTarget(0, rt0);
    o_SetDS(g_dev, ds);
    if (sceneS) sceneS->Release();
    if (aoS) aoS->Release();
    if (tmpS) tmpS->Release();
    if (ds) ds->Release();
    bb->Release();
    rt0->Release();
    if (!g_logged) { g_logged = true; Log("rendu : premiere image traitee (projection %.3f %.3f %.4f %.4f)", proj._11, proj._22, proj._33, proj._43); }
}

static void __cdecl h_Render2dStuff()
{
    PostProcess();
    ((void(__cdecl *)())0x53E230)();
}

void InstallRender()
{
    if (!g_cfg.ao && !g_cfg.fxaa) return;
    const uint8_t *p = (const uint8_t *)0x53EB12;
    if (p[0] == 0xE8 && 0x53EB17 + *(const int32_t *)(p + 1) == 0x53E230) PatchCall(0x53EB12, (void *)h_Render2dStuff);
    else Log("rendu : appel de Render2dStuff inattendu, rendu moderne coupe");
}
