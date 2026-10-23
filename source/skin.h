#pragma once
#include <citro3d.h>
#include <stdbool.h>
#include <stdint.h>

#include "room.h"

typedef struct {
	uint16_t frame;
	float v[4];
} SkinKey;

typedef struct {
	int16_t parent;
	float pos[3], rot[4], scale[3];
	uint16_t npos, nrot, nscale;
	SkinKey *kpos, *krot, *kscale;
	float invBind[12];
} SkinBone;

typedef struct {
	float pos[3];
	float uv[2];
	uint8_t bone[4], weight[4];
} SkinVert;

typedef struct {
	int nbones, nverts, nsurf;
	uint32_t lastFrame;
	SkinBone* bones;
	SkinVert* verts;
	int ntex;
	C3D_Tex** tex;
	uint16_t** idx;
	uint32_t* nidx;
	int16_t* surfTex;
	float scale;
	float floorY;
	bool ok;
} SkinModel;

typedef struct {
	const SkinModel* model;
	Room room;
	RoomVertex* bufs[2];
	RoomVertex* buf;
	float lastFrame;
	bool posed;
} SkinInstance;

bool skinLoad(SkinModel* m, const char* name);
void skinFree(SkinModel* m);

bool skinInstanceInit(SkinInstance* inst, const SkinModel* m);
void skinInstanceFree(SkinInstance* inst);

void skinPose(SkinInstance* inst, float frame);
void skinDraw(SkinInstance* inst, float x, float y, float z, float yawDeg, const C3D_Mtx* view, int uLoc,
              C3D_Tex* white);
