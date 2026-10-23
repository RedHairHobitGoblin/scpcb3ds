#include "skin.h"

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void matTRS(float m[12], const float p[3], const float q[4], const float s[3])
{
	float w = q[0], x = q[1], y = q[2], z = q[3];
	float r[9] = {
		1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
		2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
		2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y),
	};
	for (int i = 0; i < 3; i++) {
		m[i * 4 + 0] = r[i * 3 + 0] * s[0];
		m[i * 4 + 1] = r[i * 3 + 1] * s[1];
		m[i * 4 + 2] = r[i * 3 + 2] * s[2];
		m[i * 4 + 3] = p[i];
	}
}

static void matMul(float out[12], const float a[12], const float b[12])
{
	float t[12];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++)
			t[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] + a[i * 4 + 2] * b[2 * 4 + j];
		t[i * 4 + 3] = a[i * 4 + 0] * b[3] + a[i * 4 + 1] * b[7] + a[i * 4 + 2] * b[11] + a[i * 4 + 3];
	}
	memcpy(out, t, sizeof(t));
}

static void matInverse(float out[12], const float m[12])
{
	float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
	float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
	float det = a * A + b * B + c * C;
	float inv = fabsf(det) > 1e-20f ? 1.0f / det : 0.0f;
	float r[9] = {
		A * inv, -(b * i - c * h) * inv, (b * f - c * e) * inv,
		B * inv, (a * i - c * g) * inv, -(a * f - c * d) * inv,
		C * inv, -(a * h - b * g) * inv, (a * e - b * d) * inv,
	};
	for (int row = 0; row < 3; row++) {
		out[row * 4 + 0] = r[row * 3 + 0];
		out[row * 4 + 1] = r[row * 3 + 1];
		out[row * 4 + 2] = r[row * 3 + 2];
		out[row * 4 + 3] = -(r[row * 3 + 0] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]);
	}
}

static void matApply(const float m[12], const float v[3], float out[3])
{
	out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
	out[1] = m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7];
	out[2] = m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11];
}

typedef struct {
	const uint8_t* p;
	const uint8_t* end;
	bool bad;
} Rd;

static void rd(Rd* r, void* out, size_t n)
{
	if (r->p + n > r->end) {
		memset(out, 0, n);
		r->bad = true;
		r->p = r->end;
		return;
	}
	memcpy(out, r->p, n);
	r->p += n;
}

static SkinKey* readKeys(Rd* r, int n, int comps)
{
	if (!n) return NULL;
	SkinKey* k = calloc(n, sizeof(SkinKey));
	for (int i = 0; i < n; i++) {
		rd(r, &k[i].frame, 2);
		rd(r, k[i].v, 4 * comps);
	}
	return k;
}

static void bindLocal(const SkinBone* b, float m[12]) { matTRS(m, b->pos, b->rot, b->scale); }

bool skinLoad(SkinModel* m, const char* name)
{
	memset(m, 0, sizeof(*m));
	m->scale = 1.0f;

	FILE* f = fopen("romfs:/npcs/npcs.txt", "r");
	if (f) {
		char line[96], key[32];
		float sc, lo;
		while (fgets(line, sizeof(line), f))
			if (sscanf(line, "%31s %f %f", key, &sc, &lo) == 3 && strcmp(key, name) == 0) {
				m->scale = sc;
				m->floorY = lo;
			}
		fclose(f);
	}

	char path[96];
	snprintf(path, sizeof(path), "romfs:/npcs/%s.skn", name);
	f = fopen(path, "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* data = malloc(size);
	if (!data || fread(data, 1, size, f) != size) {
		free(data);
		fclose(f);
		return false;
	}
	fclose(f);

	Rd r = { data, data + size, false };
	char magic[4];
	rd(&r, magic, 4);
	uint16_t nb, ns;
	uint32_t nv;
	rd(&r, &nb, 2);
	rd(&r, &ns, 2);
	rd(&r, &nv, 4);
	rd(&r, &m->lastFrame, 4);
	if (memcmp(magic, "SKN1", 4) != 0) {
		free(data);
		return false;
	}
	m->nbones = nb;
	m->nverts = nv;
	m->nsurf = ns;
	m->bones = calloc(nb, sizeof(SkinBone));
	for (int i = 0; i < nb; i++) {
		SkinBone* b = &m->bones[i];
		rd(&r, &b->parent, 2);
		rd(&r, b->pos, 12);
		rd(&r, b->rot, 16);
		rd(&r, b->scale, 12);
		rd(&r, &b->npos, 2);
		rd(&r, &b->nrot, 2);
		rd(&r, &b->nscale, 2);
		b->kpos = readKeys(&r, b->npos, 3);
		b->krot = readKeys(&r, b->nrot, 4);
		b->kscale = readKeys(&r, b->nscale, 3);

		for (int k = 1; k < 4; k++) b->rot[k] = -b->rot[k];
		for (int j = 0; j < b->nrot; j++)
			for (int k = 1; k < 4; k++) b->krot[j].v[k] = -b->krot[j].v[k];
	}

	float (*world)[12] = malloc(sizeof(float[12]) * (nb ? nb : 1));
	for (int i = 0; i < nb; i++) {
		float loc[12];
		bindLocal(&m->bones[i], loc);
		int p = m->bones[i].parent;
		if (p >= 0 && p < i) matMul(world[i], world[p], loc);
		else memcpy(world[i], loc, sizeof(loc));
		matInverse(m->bones[i].invBind, world[i]);
	}

	m->verts = malloc(sizeof(SkinVert) * nv);
	for (uint32_t i = 0; i < nv; i++) {
		SkinVert* v = &m->verts[i];
		rd(&r, v->pos, 12);
		rd(&r, v->uv, 8);
		rd(&r, v->bone, 4);
		rd(&r, v->weight, 4);
		for (int k = 0; k < 4; k++)
			if (v->bone[k] >= nb) v->weight[k] = 0;
	}

	int meshNode = 0;
	for (uint32_t i = 0; i < nv; i++) {
		float p[3];
		matApply(world[meshNode], m->verts[i].pos, p);
		memcpy(m->verts[i].pos, p, sizeof(p));
	}
	free(world);

	m->tex = calloc(ns, sizeof(C3D_Tex*));
	m->idx = calloc(ns, sizeof(uint16_t*));
	m->nidx = calloc(ns, sizeof(uint32_t));
	m->surfTex = calloc(ns, sizeof(int16_t));
	m->ntex = ns;
	for (int s = 0; s < ns; s++) {
		uint8_t kl;
		rd(&r, &kl, 1);
		char key[64] = { 0 };
		rd(&r, key, kl < 63 ? kl : 63);
		if (kl >= 63) r.p += kl - 63;
		m->tex[s] = key[0] ? roomAcquireTexture(key, false) : NULL;
		m->surfTex[s] = s;
		rd(&r, &m->nidx[s], 4);
		m->idx[s] = linearAlloc(sizeof(uint16_t) * (m->nidx[s] + 1));
		if (m->idx[s]) rd(&r, m->idx[s], 2 * m->nidx[s]);
		if (m->idx[s]) GSPGPU_FlushDataCache(m->idx[s], 2 * m->nidx[s]);
	}
	free(data);
	m->ok = !r.bad;
	return m->ok;
}

void skinFree(SkinModel* m)
{
	for (int i = 0; i < m->nbones; i++) {
		free(m->bones[i].kpos);
		free(m->bones[i].krot);
		free(m->bones[i].kscale);
	}
	for (int s = 0; s < m->nsurf; s++) {
		roomReleaseTexture(m->tex[s]);
		linearFree(m->idx[s]);
	}
	free(m->bones);
	free(m->verts);
	free(m->tex);
	free(m->idx);
	free(m->nidx);
	free(m->surfTex);
	memset(m, 0, sizeof(*m));
}

bool skinInstanceInit(SkinInstance* inst, const SkinModel* m)
{
	memset(inst, 0, sizeof(*inst));
	if (!m->ok) return false;
	inst->model = m;
	inst->bufs[0] = linearAlloc(sizeof(RoomVertex) * m->nverts);
	inst->bufs[1] = linearAlloc(sizeof(RoomVertex) * m->nverts);
	if (!inst->bufs[0] || !inst->bufs[1]) {
		if (inst->bufs[0]) linearFree(inst->bufs[0]);
		if (inst->bufs[1]) linearFree(inst->bufs[1]);
		inst->bufs[0] = inst->bufs[1] = NULL;
		return false;
	}
	inst->buf = inst->bufs[0];
	Room* r = &inst->room;
	r->ntex = m->ntex;
	r->tex = m->tex;
	r->nsurf = m->nsurf;
	r->surf = calloc(m->nsurf, sizeof(RoomSurface));
	for (int s = 0; s < m->nsurf; s++) {
		RoomSurface* rs = &r->surf[s];
		rs->flags = 0;
		rs->diffuse = m->surfTex[s];
		rs->lightmap = -1;
		rs->nverts = m->nverts;
		rs->nidx = m->nidx[s];
		rs->verts = inst->buf;
		rs->idx = m->idx[s];
	}
	inst->lastFrame = -1;
	return true;
}

void skinInstanceFree(SkinInstance* inst)
{
	if (inst->bufs[0]) linearFree(inst->bufs[0]);
	if (inst->bufs[1]) linearFree(inst->bufs[1]);
	free(inst->room.surf);
	memset(inst, 0, sizeof(*inst));
}

static void sample(const SkinKey* k, int n, float frame, int comps, const float* bind, float* out, bool quat)
{
	if (n == 0) {
		memcpy(out, bind, sizeof(float) * comps);
		return;
	}
	if (frame <= k[0].frame) {
		memcpy(out, k[0].v, sizeof(float) * comps);
		return;
	}
	if (frame >= k[n - 1].frame) {
		memcpy(out, k[n - 1].v, sizeof(float) * comps);
		return;
	}
	int lo = 0, hi = n - 1;
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;
		if (k[mid].frame <= frame) lo = mid;
		else hi = mid;
	}

	int span = k[hi].frame - k[lo].frame;
	float t = span > 0 ? (frame - k[lo].frame) / (float)span : 1.0f;
	const float* a = k[lo].v;
	const float* b = k[hi].v;
	if (quat) {

		float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
		float sb = d < 0 ? -1.0f : 1.0f, len = 0;
		for (int i = 0; i < 4; i++) {
			out[i] = a[i] + (sb * b[i] - a[i]) * t;
			len += out[i] * out[i];
		}
		len = len > 0 ? 1.0f / sqrtf(len) : 1.0f;
		for (int i = 0; i < 4; i++) out[i] *= len;
	} else {
		for (int i = 0; i < comps; i++) out[i] = a[i] + (b[i] - a[i]) * t;
	}
}

void skinPose(SkinInstance* inst, float frame)
{
	const SkinModel* m = inst->model;
	if (!m || !inst->buf) return;
	if (inst->posed && fabsf(frame - inst->lastFrame) < 0.01f) return;
	inst->posed = true;
	inst->lastFrame = frame;

	inst->buf = inst->buf == inst->bufs[0] ? inst->bufs[1] : inst->bufs[0];
	for (int s = 0; s < inst->room.nsurf; s++) inst->room.surf[s].verts = inst->buf;

	static float world[256][12], skinMat[256][12];
	int nb = m->nbones < 256 ? m->nbones : 256;
	for (int i = 0; i < nb; i++) {
		const SkinBone* b = &m->bones[i];
		float p[3], q[4], s[3], loc[12];
		sample(b->kpos, b->npos, frame, 3, b->pos, p, false);
		sample(b->krot, b->nrot, frame, 4, b->rot, q, true);
		sample(b->kscale, b->nscale, frame, 3, b->scale, s, false);
		matTRS(loc, p, q, s);
		if (b->parent >= 0 && b->parent < i) matMul(world[i], world[b->parent], loc);
		else memcpy(world[i], loc, sizeof(loc));
		matMul(skinMat[i], world[i], b->invBind);
	}

	const float sc = m->scale, yo = -m->floorY * sc;
	for (int i = 0; i < m->nverts; i++) {
		const SkinVert* v = &m->verts[i];
		float acc[3] = { 0, 0, 0 };
		for (int k = 0; k < 4; k++) {
			if (!v->weight[k]) continue;
			float p[3], w = v->weight[k] * (1.0f / 255.0f);
			matApply(skinMat[v->bone[k]], v->pos, p);
			acc[0] += p[0] * w;
			acc[1] += p[1] * w;
			acc[2] += p[2] * w;
		}
		RoomVertex* o = &inst->buf[i];
		o->pos[0] = acc[0] * sc;
		o->pos[1] = acc[1] * sc + yo;
		o->pos[2] = -acc[2] * sc;
		o->uv0[0] = v->uv[0];
		o->uv0[1] = v->uv[1];
		o->uv1[0] = o->uv1[1] = 0;
		o->color[0] = o->color[1] = o->color[2] = 150;
		o->color[3] = 255;
	}
	GSPGPU_FlushDataCache(inst->buf, sizeof(RoomVertex) * m->nverts);
}

void skinDraw(SkinInstance* inst, float x, float y, float z, float yawDeg, const C3D_Mtx* view, int uLoc,
              C3D_Tex* white)
{
	if (!inst->buf || !inst->posed) return;
	C3D_Mtx mv;
	Mtx_Copy(&mv, view);
	Mtx_Translate(&mv, x, y, z, true);
	Mtx_RotateY(&mv, yawDeg * (float)M_PI / 180.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc, &mv);
	roomDraw(&inst->room, white, false);
}
