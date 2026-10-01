// Tchat (chat.cpp).
#pragma once
#include <windows.h>
#include <stdint.h>

bool ChatTyping();
bool ChatWindowMessage(UINT msg, WPARAM wp);                 // window.cpp : vrai = message pour le tchat
bool ChatReliable(int from, const uint8_t *data, int len);   // mirror.cpp : message fiable de tchat ?
void ChatForEachLine(void (*draw)(int player, const char *name, const char *text, float alpha), const char **input);
void ChatTest(const char *text);
