#include "room.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELL_SIZE 0.5f

static uint8_t* readFile(const char* path, size_t* size)
{
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* data = malloc(*size);
	if (data && fread(data, 1, *size, f) != *size) {
		free(data);
		data = NULL;
	}
	fclose(f);
	return data;
}

typedef struct {
	const uint8_t* p;
	const uint8_t* end;
} Reader;

static void rd(Reader* r, void* out, size_t n)
{
	if (r->p + n > r->end) {
		memset(out, 0, n);
		r->p = r->end;
		return;
	}
	memcpy(out, r->p, n);
	r->p += n;
}

static uint8_t  rdU8(Reader* r)  { uint8_t v;  rd(r, &v, 1); return v; }
static uint16_t rdU16(Reader* r) { uint16_t v; rd(r, &v, 2); return v; }
static int16_t  rdS16(Reader* r) { int16_t v;  rd(r, &v, 2); return v; }
static uint32_t rdU32(Reader* r) { uint32_t v; rd(r, &v, 4); return v; }

#define MAX_CACHED_TEX 512
typedef struct {
	char key[48];
	C3D_Tex tex;
	int refs;
} CachedTex;
static CachedTex s_cache[MAX_CACHED_TEX];

static bool loadTexture(C3D_Tex* tex, const char* key, bool lightmap)
{
	char path[128];
	snprintf(path, sizeof(path), "romfs:/tex/%s.t3d", key);
	size_t size;
	uint8_t* data = readFile(path, &size);
	if (!data) return false;
	bool ok = false;
	if (size >= 12 && memcmp(data, "T3D1", 4) == 0) {
		uint16_t w, h;
		memcpy(&w, data + 4, 2);
		memcpy(&h, data + 6, 2);
		GPU_TEXCOLOR fmt = data[8] == 1 ? GPU_RGBA4 : GPU_RGB565;
		if (C3D_TexInit(tex, w, h, fmt)) {
			size_t bytes = (size_t)w * h * 2;
			if (bytes > size - 12) bytes = size - 12;
			memcpy(tex->data, data + 12, bytes);
			C3D_TexFlush(tex);
			C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
			if (lightmap)
				C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
			else
				C3D_TexSetWrap(tex, GPU_REPEAT, GPU_REPEAT);
			ok = true;
		}
	}
	free(data);
	return ok;
}

C3D_Tex* roomAcquireTexture(const char* key, bool lightmap)
{
	int freeSlot = -1;
	for (int i = 0; i < MAX_CACHED_TEX; i++) {
		if (s_cache[i].refs > 0 && strcmp(s_cache[i].key, key) == 0) {
			s_cache[i].refs++;
			return &s_cache[i].tex;
		}
		if (s_cache[i].refs == 0 && freeSlot < 0) freeSlot = i;
	}
	if (freeSlot < 0 || !loadTexture(&s_cache[freeSlot].tex, key, lightmap)) return NULL;
	snprintf(s_cache[freeSlot].key, sizeof(s_cache[freeSlot].key), "%s", key);
	s_cache[freeSlot].refs = 1;
	return &s_cache[freeSlot].tex;
}

void roomReleaseTexture(C3D_Tex* tex)
{
	if (!tex) return;
	CachedTex* c = (CachedTex*)((char*)tex - offsetof(CachedTex, tex));
	if (--c->refs == 0) C3D_TexDelete(&c->tex);
}

static bool buildGrid(Room* room)
{
	float minX = INFINITY, minZ = INFINITY, maxX = -INFINITY, maxZ = -INFINITY;
	for (int i = 0; i < room->ntri; i++) {
		const float* t = room->tris + i * 9;
		for (int v = 0; v < 3; v++) {
			minX = fminf(minX, t[v * 3]);     maxX = fmaxf(maxX, t[v * 3]);
			minZ = fminf(minZ, t[v * 3 + 2]); maxZ = fmaxf(maxZ, t[v * 3 + 2]);
		}
	}
	if (room->ntri == 0) { minX = minZ = 0; maxX = maxZ = 1; }
	room->minX = minX - 1.0f;
	room->minZ = minZ - 1.0f;
	room->gridW = (int)((maxX - room->minX + 1.0f) / CELL_SIZE) + 1;
	room->gridH = (int)((maxZ - room->minZ + 1.0f) / CELL_SIZE) + 1;
	int cells = room->gridW * room->gridH;

	room->cellStart = calloc(cells + 1, sizeof(uint32_t));
	if (!room->cellStart) return false;

	for (int pass = 0; pass < 2; pass++) {
		uint32_t* fill = NULL;
		if (pass == 1) {
			for (int c = 0; c < cells; c++) room->cellStart[c + 1] += room->cellStart[c];
			room->cellTris = malloc(sizeof(uint32_t) * (room->cellStart[cells] + 1));
			fill = calloc(cells, sizeof(uint32_t));
			if (!room->cellTris || !fill) {
				free(fill);
				return false;
			}
		}
		for (int i = 0; i < room->ntri; i++) {
			const float* t = room->tris + i * 9;
			float x0 = fminf(t[0], fminf(t[3], t[6])) - 0.25f, x1 = fmaxf(t[0], fmaxf(t[3], t[6])) + 0.25f;
			float z0 = fminf(t[2], fminf(t[5], t[8])) - 0.25f, z1 = fmaxf(t[2], fmaxf(t[5], t[8])) + 0.25f;
			int cx0 = (int)((x0 - room->minX) / CELL_SIZE), cx1 = (int)((x1 - room->minX) / CELL_SIZE);
			int cz0 = (int)((z0 - room->minZ) / CELL_SIZE), cz1 = (int)((z1 - room->minZ) / CELL_SIZE);
			if (cx0 < 0) cx0 = 0;
			if (cz0 < 0) cz0 = 0;
			if (cx1 >= room->gridW) cx1 = room->gridW - 1;
			if (cz1 >= room->gridH) cz1 = room->gridH - 1;
			for (int cz = cz0; cz <= cz1; cz++)
				for (int cx = cx0; cx <= cx1; cx++) {
					int c = cz * room->gridW + cx;
					if (pass == 0) room->cellStart[c + 1]++;
					else room->cellTris[room->cellStart[c] + fill[c]++] = i;
				}
		}
		free(fill);
	}
	return true;
}

bool roomLoad(Room* room, const char* path)
{
	memset(room, 0, sizeof(*room));
	size_t size;
	uint8_t* data = readFile(path, &size);
	if (!data) return false;
	Reader r = { data, data + size };
	char magic[4];
	rd(&r, magic, 4);
	if (memcmp(magic, "SCR1", 4) != 0) {
		free(data);
		return false;
	}

	room->ntex = rdU16(&r);
	room->tex = calloc(room->ntex + 1, sizeof(C3D_Tex*));
	char (*keys)[64] = calloc(room->ntex + 1, 64);
	if (!room->tex || !keys) goto oom;
	for (int i = 0; i < room->ntex; i++) {
		int n = rdU8(&r);
		if (n > 63) n = 63;
		rd(&r, keys[i], n);
	}

	room->nsurf = rdU16(&r);
	room->surf = calloc(room->nsurf + 1, sizeof(RoomSurface));
	bool* isLightmap = calloc(room->ntex + 1, sizeof(bool));
	if (!room->surf || !isLightmap) {
		free(isLightmap);
		goto oom;
	}
	for (int i = 0; i < room->nsurf; i++) {
		RoomSurface* s = &room->surf[i];
		s->flags = rdU8(&r);
		s->diffuse = rdS16(&r);
		s->lightmap = rdS16(&r);
		s->nverts = rdU32(&r);
		s->nidx = rdU32(&r);
		s->verts = linearAlloc(sizeof(RoomVertex) * s->nverts);
		s->idx = linearAlloc(sizeof(uint16_t) * (s->nidx + 1));
		if (!s->verts || !s->idx) {
			free(isLightmap);
			goto oom;
		}
		rd(&r, s->verts, sizeof(RoomVertex) * s->nverts);
		rd(&r, s->idx, sizeof(uint16_t) * s->nidx);
		if (s->nidx & 1) rdU16(&r);
		if (s->lightmap >= 0 && s->lightmap < room->ntex) isLightmap[s->lightmap] = true;
	}

	for (int i = 0; i < room->ntex; i++)
		room->tex[i] = roomAcquireTexture(keys[i], isLightmap[i]);
	free(isLightmap);
	free(keys);
	keys = NULL;

	room->ntri = rdU32(&r);
	room->tris = malloc(sizeof(float) * 9 * room->ntri + 4);
	if (!room->tris) goto oom;
	rd(&r, room->tris, sizeof(float) * 9 * room->ntri);

	room->hasStart = rdU8(&r) != 0;
	rd(&r, room->start, sizeof(room->start));
	room->nsightTri = room->ntri;
	if (r.p + 4 <= r.end) {
		uint32_t n = rdU32(&r);
		if ((int)n <= room->ntri) room->nsightTri = n;
	}

	free(data);
	data = NULL;
	if (!buildGrid(room)) goto oom;

	room->minY = INFINITY;
	room->maxY = -INFINITY;
	for (int i = 0; i < room->nsurf; i++) {
		RoomSurface* s = &room->surf[i];
		for (uint32_t v = 0; s->verts && v < s->nverts; v++) {
			const float* p = s->verts[v].pos;
			float r = sqrtf(p[0] * p[0] + p[2] * p[2]);
			if (r > room->radius) room->radius = r;
			if (p[1] < room->minY) room->minY = p[1];
			if (p[1] > room->maxY) room->maxY = p[1];
		}
	}
	return true;

oom:
	free(keys);
	free(data);
	roomFree(room);
	return false;
}

void roomFree(Room* room)
{
	for (int i = 0; i < room->ntex; i++)
		roomReleaseTexture(room->tex[i]);
	for (int i = 0; i < room->nsurf; i++) {
		linearFree(room->surf[i].verts);
		linearFree(room->surf[i].idx);
	}
	free(room->tex);
	free(room->surf);
	free(room->tris);
	free(room->cellStart);
	free(room->cellTris);
	memset(room, 0, sizeof(*room));
}

static u32 s_fogColor = 0xFF000000;

void roomSetFogColor(u8 r, u8 g, u8 b) { s_fogColor = 0xFF000000u | ((u32)b << 16) | ((u32)g << 8) | r; }

static C3D_Tex* surfTex(Room* room, int i, C3D_Tex* fallback)
{
	if (i >= 0 && i < room->ntex && room->tex[i]) return room->tex[i];
	return fallback;
}

void roomDraw(Room* room, C3D_Tex* white, bool alphaPass)
{
	for (int i = 0; i < room->nsurf; i++) {
		RoomSurface* s = &room->surf[i];
		if (!s->verts || !s->idx || s->nidx == 0) continue;
		if (((s->flags & SURF_ALPHA) != 0) != alphaPass) continue;

		C3D_TexBind(0, surfTex(room, s->diffuse, white));
		C3D_TexEnv* env[4];
		for (int k = 0; k < 4; k++) {
			env[k] = C3D_GetTexEnv(k);
			C3D_TexEnvInit(env[k]);
		}

		int last;
		if (s->flags & SURF_LIGHTMAP) {

			C3D_TexBind(1, surfTex(room, s->lightmap, white));
			C3D_TexEnvSrc(env[0], C3D_RGB, GPU_TEXTURE1, GPU_CONSTANT, 0);
			C3D_TexEnvFunc(env[0], C3D_RGB, GPU_ADD);
			C3D_TexEnvColor(env[0], 0xFF141414);
			C3D_TexEnvSrc(env[0], C3D_Alpha, GPU_TEXTURE0, 0, 0);
			C3D_TexEnvFunc(env[0], C3D_Alpha, GPU_REPLACE);

			C3D_TexEnvSrc(env[1], C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE0, 0);
			C3D_TexEnvFunc(env[1], C3D_RGB, GPU_MODULATE);
			C3D_TexEnvScale(env[1], C3D_RGB, GPU_TEVSCALE_2);

			C3D_TexEnvSrc(env[2], C3D_RGB, GPU_PREVIOUS, GPU_PRIMARY_COLOR, 0);
			C3D_TexEnvFunc(env[2], C3D_RGB, GPU_MODULATE);
			last = 3;
		} else {

			C3D_TexEnvSrc(env[0], C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
			C3D_TexEnvFunc(env[0], C3D_RGB, GPU_MODULATE);
			C3D_TexEnvScale(env[0], C3D_RGB, (s->flags & SURF_ALPHA) ? GPU_TEVSCALE_1 : GPU_TEVSCALE_2);
			C3D_TexEnvSrc(env[0], C3D_Alpha, GPU_TEXTURE0, 0, 0);
			C3D_TexEnvFunc(env[0], C3D_Alpha, GPU_REPLACE);
			last = 1;
		}

		C3D_TexEnvSrc(env[last], C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
		C3D_TexEnvOpRgb(env[last], GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
		C3D_TexEnvFunc(env[last], C3D_RGB, GPU_INTERPOLATE);
		C3D_TexEnvColor(env[last], s_fogColor);

		C3D_BufInfo* bi = C3D_GetBufInfo();
		BufInfo_Init(bi);
		BufInfo_Add(bi, s->verts, sizeof(RoomVertex), 4, 0x3210);
		C3D_DrawElements(GPU_TRIANGLES, s->nidx, C3D_UNSIGNED_SHORT, s->idx);
	}
}
