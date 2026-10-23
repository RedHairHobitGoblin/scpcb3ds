#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MAP_SIZE 18

enum { ROOM1 = 1, ROOM2, ROOM2C, ROOM3, ROOM4 };

typedef struct {
	char key[32];
	char name[32];
	int shape, commonness, zones[5], large;
} RoomTemplateInfo;

typedef struct {
	int tpl;
	int gx, gy;
	int zone;
	int angle;
	bool offGrid;
	float y;
} MapRoomPlacement;

typedef struct {
	float x, z;
	int angle;
	int type;
	bool open;
	int room;
} MapDoorPlacement;

#define MAX_MAP_ROOMS 400
#define MAX_MAP_DOORS 600

typedef struct {
	int nrooms;
	MapRoomPlacement rooms[MAX_MAP_ROOMS];
	int ndoors;
	MapDoorPlacement doors[MAX_MAP_DOORS];
	int grid[MAP_SIZE + 1][MAP_SIZE + 1];
} GeneratedMap;

void rngSeed(uint32_t seed);
int rngRand(int lo, int hi);
float rngRnd(float lo, float hi);

void generateMap(GeneratedMap* map, const RoomTemplateInfo* tpl, int ntpl, uint32_t seed);
