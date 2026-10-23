#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "room.h"

#define NAV_CELL 0.25f

typedef struct {
	float minX, minZ;
	int w, h;
	float* floorY;
	uint8_t* walk;
	bool built;
} NavGrid;

bool navBuild(NavGrid* g, const Room* room);
void navFree(NavGrid* g);

int navPath(const NavGrid* g, const float from[3], const float to[3], float (*out)[3], int max);

bool navNearest(const NavGrid* g, const float p[3], float out[3]);
