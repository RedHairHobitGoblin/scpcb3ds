#pragma once
#include <citro2d.h>
#include <stdbool.h>

#include "world.h"

typedef enum { HUD_TITLE, HUD_GAME, HUD_DOCUMENT, HUD_KEYPAD, HUD_PAUSE, HUD_DEAD, HUD_ENDING } HudMode;

typedef enum {
	ACT_NONE,
	ACT_SELECT_SLOT,
	ACT_USE,
	ACT_DROP,
	ACT_CLOSE,
	ACT_KEYPAD_OK,
	ACT_RESUME,
	ACT_NEW_GAME,
	ACT_QUIT,
	ACT_MAIN_MENU,
	ACT_SAVE,
	ACT_LOAD,
	ACT_TAB_MAP,
	ACT_TAB_INV,
	ACT_LOOK_PAD,
	ACT_PAD_USE,
} HudActionKind;

typedef struct {
	HudActionKind kind;
	int arg;
} HudAction;

typedef struct {
	HudMode mode;
	bool mapTab;
	bool hasSave;
	bool lookPad;
	char message[192];
	float messageTime;
	int docTpl;
	char keypad[8];
	int keypadDoor;
	char deathMessage[320];
	float deathTime;
	char ending[4];
	float endingTime;
} Hud;

extern Hud g_hud;

#define PAD_X 8
#define PAD_Y 22
#define PAD_W 304
#define PAD_H 168
#define PAD_BLINK_X 242
#define PAD_BTN_Y 196
#define PAD_BTN_W 70
#define PAD_BTN_H 38

void hudInit(void);
void hudExit(void);
void hudMessage(const char* text, float seconds);
void hudShowDocument(int tpl);
void hudUpdate(float dt);

typedef struct {
	float blink;
	float stamina;
	float dark;
	float eyelid;
	bool crosshair;
	bool crouch;
	bool hasNav;
	float injuries;
	bool bleeding;
	int nvg;
	const World* world;
	const float* pos;
	float yaw;
	float fps;
	const char* debug;
} HudInfo;

void hudDrawTop(C3D_RenderTarget* top, const HudInfo* info);

void hudDrawTitleTop(C3D_RenderTarget* top);

void hudStartEnding(const char* code);
void hudDrawEndingTop(C3D_RenderTarget* top);
HudAction hudDrawBottom(C3D_RenderTarget* bottom, const HudInfo* info, bool touched, int tx, int ty);
