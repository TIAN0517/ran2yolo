#pragma once

#include <windows.h>

bool ScreenshotAssist_Init();
void ScreenshotAssist_Shutdown();

bool ScreenshotAssist_Find(const char* refName, int* outX, int* outY, int* outScore);
bool ScreenshotAssist_FindBest(const char* refName, int radius, int* outX, int* outY, int* outScore);
bool ScreenshotAndFind(HWND hWnd, const char* refName, int* outX, int* outY, int* outScore);

int ScreenshotToRelX(int screenX, int screenW);
int ScreenshotToRelY(int screenY, int screenH);
int GetScreenshotWH(int* outW, int* outH);
