#pragma once
#include <3ds.h>
#include <citro3d.h>
#include <stdbool.h>
#include <stdint.h>

#define SURF_ALPHA    1
#define SURF_LIGHTMAP 2

typedef struct {
	float pos[3];
	float uv0[2];
	float uv1[2];
	uint8_t color[4];
} RoomVertex;

typedef struct {
	uint8_t flags;
	int16_t diffuse, lightmap;
	uint32_t nverts, nidx;
	RoomVertex* verts;
	uint16_t* idx;
} RoomSurface;

typedef struct {
	int ntex;
	C3D_Tex** tex;

	int nsurf;
	RoomSurface* surf;

	int ntri;
	int nsightTri;
	float* tris;
	float minX, minZ;
	int gridW, gridH;
	uint32_t* cellStart;
	uint32_t* cellTris;

	bool hasStart;
	float start[4];

	float radius;
	float minY, maxY;
} Room;

bool roomLoad(Room* room, const char* path);
void roomFree(Room* room);
void roomDraw(Room* room, C3D_Tex* white, bool alphaPass);

C3D_Tex* roomAcquireTexture(const char* key, bool lightmap);
void roomReleaseTexture(C3D_Tex* tex);

void roomSetFogColor(u8 r, u8 g, u8 b);
