#pragma once
#include <citro3d.h>
#include <stdio.h>

#include "mapgen.h"
#include "room.h"

#define MAX_TEMPLATES 160
#define MAX_DOORS 1200
#define MAX_FILL 400

enum { DOOR_NORMAL = 0, DOOR_BIG = 1, DOOR_HEAVY = 2 };

typedef struct {
	char room[32];
	float x, y, z;
	int angle, open, big, keycard, locked, noButtons, roomDoor;
	char code[8];
} FillDoor;

typedef struct {
	RoomTemplateInfo info;
	Room room;
	bool loaded;
} RoomTemplate;

typedef struct {
	float x, y, z;
	int angle;
	int type;
	int keycard;
	bool locked;
	char code[8];
	bool open;
	bool noButtons;
	bool elevator;
	bool pressed;
	float openstate;
	int room;
	int roomDoor;
} Door;

#define MAX_EXTRAS 24
typedef struct {
	Room* model;
	float x, y, z, yaw;
	float sx, sy, sz;
	bool collide, visible;
} WorldExtra;

#define MAX_TUNNEL_CELLS 384
#define MAX_AREAS 4
typedef struct {
	Room* model;
	float x, y, z, yaw;
	bool collide;
} TunnelCell;

typedef struct {
	int room;
	float min[3], max[3];
	int first, count;
	bool hidden;
} OverlayArea;

typedef struct {
	int ntpl;
	RoomTemplate tpl[MAX_TEMPLATES];
	GeneratedMap map;
	int ndoors;
	Door doors[MAX_DOORS];
	int nfill;
	FillDoor fill[MAX_FILL];
	uint32_t seed;
	int introRoom;
	bool introActive;
	bool isolated;
	int nextras;
	WorldExtra extras[MAX_EXTRAS];
	int nareas;
	OverlayArea areas[MAX_AREAS];
	int ntunnel;
	TunnelCell tunnel[MAX_TUNNEL_CELLS];
	uint32_t fogColor;
	float fogNear, fogFar;
} World;

bool worldLoadTemplates(World* w);

void worldSave(const World* w, FILE* f);
bool worldRestoreDoors(World* w, FILE* f);
void worldGenerate(World* w, uint32_t seed, bool intro);
void worldSpawnPoint(const World* w, float pos[3], float* yaw);

void worldStream(World* w, const float pos[3]);
void worldUpdateDoors(World* w, float dt);

int worldUsableDoor(const World* w, const float pos[3], float yaw);

const char* worldUseDoor(World* w, int door, const char* heldTempname, bool* consumedSelection);

void worldToggleDoor(World* w, int door);
bool worldDoorPassable(const World* w, int door);

bool worldCollide(World* w, float pos[3]);

float worldRayDown(const World* w, const float pos[3], float maxDrop);

bool worldLineOfSight(const World* w, const float a[3], const float b[3]);

void worldDraw(World* w, const C3D_Mtx* view, int uLoc_modelView, const float cam[3], float yaw,
               C3D_Tex* white, float viewDist);

void worldDrawModel(Room* model, float x, float y, float z, float yawDeg, const C3D_Mtx* view, int uLoc,
                    C3D_Tex* white);

void worldDrawModelEx(Room* model, float x, float y, float z, float yawDeg, float pitchDeg, float scale,
                      const C3D_Mtx* view, int uLoc, C3D_Tex* white);

int worldAddOffGridRoom(World* w, const char* name, int gx, int gy, float y, int angle);

int worldAddRoomDoor(World* w, int room, const float local[3], int angle, int type);

int worldAddExtra(World* w, Room* model, float x, float y, float z, float yawDeg, bool collide);

int worldAreaAt(const World* w, const float pos[3]);
static inline bool worldInTunnels(const World* w, const float pos[3]) { return worldAreaAt(w, pos) >= 0; }

int worldBeginArea(World* w, int room);
void worldAddAreaPiece(World* w, int area, Room* model, float x, float y, float z, float yawDeg, bool collide);
void worldEndArea(World* w, int area, const float min[3], const float max[3]);

int worldAddDoorAt(World* w, const float pos[3], int angle, int type, int room);
void worldClearExtras(World* w);

void worldDoorViewpoint(const World* w, int door, float pos[3], float* yaw);

int worldBoundaryDoor(const World* w, int ax, int ay, int bx, int by);
bool worldNextCell(const World* w, int sx, int sy, int tx, int ty, int doorPolicy, int* nx, int* ny);
bool worldDoorOpenable(const Door* d);

int worldRoomDoor(const World* w, int room, int k);

void worldWorldToRoom(const World* w, int room, const float world[3], float out[3]);
void worldCell(const float pos[3], int* gx, int* gy);
int worldRoomIndexAt(const World* w, const float pos[3]);
const char* worldRoomAt(const World* w, const float pos[3]);
void worldRoomToWorld(const World* w, int room, const float local[3], float out[3]);
int worldRoomZone(const World* w, const float pos[3]);
int worldLoadedCount(const World* w);
