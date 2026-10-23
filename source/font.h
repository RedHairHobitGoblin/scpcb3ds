#pragma once
#include <citro2d.h>
#include <stdbool.h>

#define FONT_ADVANCE 6
#define FONT_LINE 9

bool fontInit(void);
void fontExit(void);

float fontDraw(const char* s, float x, float y, float scale, u32 color, float wrap);
float fontWidth(const char* s, float scale);
