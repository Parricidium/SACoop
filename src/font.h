// CFont du jeu (1.0 US) : textes du HUD coop (hud.cpp, panel.cpp).
#pragma once
#include <stdint.h>

namespace font {
    inline void SetScale(float x, float y) { ((void(__cdecl *)(float, float))0x719380)(x, y); }
    inline void SetColor(uint32_t rgba) { ((void(__cdecl *)(uint32_t))0x719430)(rgba); }   // CRGBA par valeur : r en octet bas
    inline void SetFontStyle(int s) { ((void(__cdecl *)(char))0x719490)((char)s); }       // 0 gothique, 1 sous-titres, 2 menu, 3 pricedown
    inline void SetProportional(bool b) { ((void(__cdecl *)(bool))0x7195B0)(b); }
    inline void SetBackground(bool b, bool box) { ((void(__cdecl *)(bool, bool))0x7195C0)(b, box); }
    inline void SetDropShadow(int n) { ((void(__cdecl *)(char))0x719570)((char)n); }
    inline void SetEdge(int n) { ((void(__cdecl *)(char))0x719590)((char)n); }
    inline void SetDropColor(uint32_t rgba) { ((void(__cdecl *)(uint32_t))0x719510)(rgba); }
    inline void SetOrientation(int o) { ((void(__cdecl *)(char))0x719610)((char)o); }      // 0 centre, 1 gauche, 2 droite
    inline void SetCentreSize(float w) { ((void(__cdecl *)(float))0x7194E0)(w); }
    inline void Print(float x, float y, const char *s) { ((void(__cdecl *)(float, float, const char *))0x71A700)(x, y, s); }
    inline void DrawFonts() { ((void(__cdecl *)())0x71A210)(); }
}
