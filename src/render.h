// Rendu moderne : occlusion ambiante et FXAA (render.cpp).
#pragma once
#include <d3d9.h>

void InstallRender();
void RenderDeviceCreated(IDirect3DDevice9 *dev, const D3DPRESENT_PARAMETERS *pp);
void RenderBeforeReset();
void RenderAfterReset(IDirect3DDevice9 *dev, const D3DPRESENT_PARAMETERS *pp);
