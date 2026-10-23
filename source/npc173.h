#pragma once
#include <citro3d.h>
#include <stdbool.h>
#include <stdio.h>

#include "world.h"

typedef struct {
	const float* pos;
	const float* eye;
	float yaw, pitch;
	bool eyesClosed;
	bool dead;
} PlayerView;

void npc173Load(void);
void npc173Save(FILE* f);
void npc173Restore(FILE* f);
void npc173Spawn(const World* w, const float playerPos[3]);

const char* npc173Update(World* w, const PlayerView* pv, float dt);
void npc173Draw(const C3D_Mtx* view, int uLoc, C3D_Tex* white);
bool npc173Seen(void);

void npc173Place(const float pos[3], float yaw);
void npc173SetIdle(bool idle);
void npc173GetPos(float out[3]);
bool npc173IsIdle(void);
float npc173Distance(void);
