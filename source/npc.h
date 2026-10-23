#pragma once
#include <citro3d.h>
#include <stdbool.h>
#include <stdio.h>

#include "npc173.h"
#include "skin.h"
#include "world.h"

typedef enum {
	NPC_106, NPC_049, NPC_096, NPC_GUARD, NPC_CLASSD,
	NPC_MTF, NPC_939, NPC_966, NPC_ZOMBIE, NPC_SURGEON, NPC_066, NPC_372, NPC_5131, NPC_1048A, NPC_035, NPC_860, NPC_1499,
	NPC_TYPES
} NpcType;

#define MAX_NPCS 24

typedef struct {
	bool active;
	NpcType type;
	float pos[3];
	float yaw;
	float frame;
	float speed;
	float currSpeed;
	int state;
	float state2, state3, timer, lastSeen;
	float prevFrame;
	int chan, chan2;
	bool hidden;
	bool scripted;
	bool dead;
	bool placed;

	float goal[3];
	float path[24][3];
	int npath, pathi, pathRoom;
	float repath;
	SkinInstance skin;
} Npc;

typedef struct {
	char text[320];
} NpcDeath;

void npcsLoad(void);
void npcsSave(FILE* f);
bool npcsRestore(FILE* f);
void npcsReset(void);
int npcSpawn(NpcType type, const float pos[3], float yaw);
Npc* npcGet(int i);
Npc* npcFind(NpcType type);
void npcRemove(int i);

void npcsSpawnFacility(const World* w, const float playerPos[3]);

void npcsSpawn5131(const float playerPos[3]);

void npcsCheckMTF(const World* w, const float playerPos[3]);

const char* npcsUpdate(World* w, const PlayerView* pv, float dt);

const char* npcsMusicOverride(void);

bool npc106TakeGrab(void);
void npcsDraw(const C3D_Mtx* view, int uLoc, C3D_Tex* white, const float cam[3], float viewDist);

void npcAnimate(Npc* n, float start, float end, float speed, bool loop, float dt);

bool npcWalkTo(World* w, Npc* n, const float target[3], float speed, float dt);
void npcFace(Npc* n, const float target[3], float rate, float dt);
void npcSnapToFloor(const World* w, Npc* n);

void npcDebugInfo(const float pos[3], char* out, int size);
int npcDebugNext(const World* w, float pos[3], float* yaw, bool first);
