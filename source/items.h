#pragma once
#include <citro3d.h>
#include <stdbool.h>
#include <stdio.h>

#include "room.h"
#include "world.h"

#define MAX_ITEM_TEMPLATES 160
#define MAX_WORLD_ITEMS 400
#define INV_SLOTS 10

typedef struct {
	char name[48];
	char tempname[20];
	char model[48];
	char inv[32];
	char doc[32];
	char display[48];
	Room mesh;
	bool meshTried, meshOk;
	C3D_Tex icon;
	bool iconTried, iconOk;
} ItemTemplate;

typedef struct {
	int tpl;
	float pos[3];
	float yaw;
	bool active, settled;
	char note[24];
} WorldItem;

typedef struct {
	int ntpl;
	ItemTemplate tpl[MAX_ITEM_TEMPLATES];
	int nitems;
	WorldItem items[MAX_WORLD_ITEMS];
	int inv[INV_SLOTS];
	char invNote[INV_SLOTS][24];
	int selected;
} Items;

extern Items g_items;

void itemsLoadTemplates(void);
void itemsSave(FILE* f);
bool itemsRestore(FILE* f);
void itemsSpawn(const World* w);
void itemsUpdate(const World* w, float dt);
void itemsDraw(const C3D_Mtx* view, int uLoc, const float cam[3], C3D_Tex* white, float viewDist);

int itemsPickable(const float eye[3], float yaw, float pitch);

bool itemsPickUp(int item);
void itemsDrop(int slot, const float pos[3], float yaw);
const char* itemsHeldTempname(void);
int itemsFindTemplate(const char* name, const char* tempname);
bool itemsHave(const char* tempnamePrefix);

C3D_Tex* itemsIcon(int tpl);

void items914(int item, const char* setting, const float out[3]);

int itemsSpawnAt(const char* name, const char* tempname, const float pos[3]);

const char* itemsSlotName(int slot);

void itemsDebugNearest(const float eye[3], float yaw, float pitch, char* out, int size);
