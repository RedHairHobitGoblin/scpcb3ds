#include "collide.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RADIUS_X 0.15f
#define RADIUS_Y 0.30f
#define SQUASH   (RADIUS_X / RADIUS_Y)

static uint32_t* s_stamp;
static int s_stampSize;
static uint32_t s_frame;

typedef struct { float x, y, z; } V3;

static V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 scale(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float sdiv(float a, float b) { return fabsf(b) > 1e-12f ? a / b : 0.0f; }

static V3 closestOnTri(V3 p, V3 a, V3 b, V3 c)
{
	V3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
	float d1 = dot(ab, ap), d2 = dot(ac, ap);
	if (d1 <= 0 && d2 <= 0) return a;
	V3 bp = sub(p, b);
	float d3 = dot(ab, bp), d4 = dot(ac, bp);
	if (d3 >= 0 && d4 <= d3) return b;
	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) return add(a, scale(ab, sdiv(d1, d1 - d3)));
	V3 cp = sub(p, c);
	float d5 = dot(ab, cp), d6 = dot(ac, cp);
	if (d6 >= 0 && d5 <= d6) return c;
	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) return add(a, scale(ac, sdiv(d2, d2 - d6)));
	float va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
		return add(b, scale(sub(c, b), sdiv(d4 - d3, (d4 - d3) + (d5 - d6))));
	float sum = va + vb + vc;
	if (fabsf(sum) < 1e-12f) return a;
	return add(a, add(scale(ab, vb / sum), scale(ac, vc / sum)));
}

bool collidePlayer(const Room* room, float pos[3])
{
	if (s_stampSize < room->ntri) {
		free(s_stamp);
		s_stamp = calloc(room->ntri, sizeof(uint32_t));
		s_stampSize = room->ntri;
		s_frame = 0;
	}

	bool grounded = false;
	V3 p = v3(pos[0], pos[1] * SQUASH, pos[2]);
	const float r = RADIUS_X;

	for (int iter = 0; iter < 4; iter++) {
		s_frame++;
		bool moved = false;
		int cx0 = (int)((p.x - r - room->minX) / 0.5f), cx1 = (int)((p.x + r - room->minX) / 0.5f);
		int cz0 = (int)((p.z - r - room->minZ) / 0.5f), cz1 = (int)((p.z + r - room->minZ) / 0.5f);
		if (cx0 < 0) cx0 = 0;
		if (cz0 < 0) cz0 = 0;
		if (cx1 >= room->gridW) cx1 = room->gridW - 1;
		if (cz1 >= room->gridH) cz1 = room->gridH - 1;

		for (int cz = cz0; cz <= cz1; cz++)
			for (int cx = cx0; cx <= cx1; cx++) {
				int c = cz * room->gridW + cx;
				for (uint32_t k = room->cellStart[c]; k < room->cellStart[c + 1]; k++) {
					uint32_t ti = room->cellTris[k];
					if (s_stamp[ti] == s_frame) continue;
					s_stamp[ti] = s_frame;
					const float* t = room->tris + ti * 9;
					V3 a = v3(t[0], t[1] * SQUASH, t[2]);
					V3 b = v3(t[3], t[4] * SQUASH, t[5]);
					V3 cc = v3(t[6], t[7] * SQUASH, t[8]);
					V3 q = closestOnTri(p, a, b, cc);
					V3 d = sub(p, q);
					float dist2 = dot(d, d);
					if (!(dist2 < r * r) || dist2 < 1e-12f) continue;
					float dist = sqrtf(dist2);
					V3 n = scale(d, 1.0f / dist);
					p = add(p, scale(n, r - dist));
					if (n.y > 0.6f) grounded = true;
					moved = true;
				}
			}
		if (!moved) break;
	}

	pos[0] = p.x;
	pos[1] = p.y / SQUASH;
	pos[2] = p.z;
	return grounded;
}

static bool cellRange(const Room* room, float x0, float z0, float x1, float z1, int* cx0, int* cz0, int* cx1, int* cz1)
{
	*cx0 = (int)((fminf(x0, x1) - room->minX) / 0.5f);
	*cx1 = (int)((fmaxf(x0, x1) - room->minX) / 0.5f);
	*cz0 = (int)((fminf(z0, z1) - room->minZ) / 0.5f);
	*cz1 = (int)((fmaxf(z0, z1) - room->minZ) / 0.5f);
	if (*cx0 < 0) *cx0 = 0;
	if (*cz0 < 0) *cz0 = 0;
	if (*cx1 >= room->gridW) *cx1 = room->gridW - 1;
	if (*cz1 >= room->gridH) *cz1 = room->gridH - 1;
	return *cx0 <= *cx1 && *cz0 <= *cz1;
}

static float rayTri(V3 o, V3 d, V3 a, V3 b, V3 c)
{
	V3 e1 = sub(b, a), e2 = sub(c, a);
	V3 p = v3(d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x);
	float det = dot(e1, p);
	if (fabsf(det) < 1e-9f) return -1;
	float inv = 1.0f / det;
	V3 s = sub(o, a);
	float u = dot(s, p) * inv;
	if (u < 0 || u > 1) return -1;
	V3 q = v3(s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x);
	float v = dot(d, q) * inv;
	if (v < 0 || u + v > 1) return -1;
	float t = dot(e2, q) * inv;
	return t >= 0 ? t : -1;
}

float roomRayDown(const Room* room, const float p[3], float maxDrop)
{
	int cx = (int)((p[0] - room->minX) / 0.5f), cz = (int)((p[2] - room->minZ) / 0.5f);
	if (cx < 0 || cz < 0 || cx >= room->gridW || cz >= room->gridH) return -INFINITY;
	V3 o = v3(p[0], p[1], p[2]), d = v3(0, -1, 0);
	float best = -INFINITY;
	int c = cz * room->gridW + cx;
	for (uint32_t k = room->cellStart[c]; k < room->cellStart[c + 1]; k++) {
		const float* t = room->tris + room->cellTris[k] * 9;
		float hit = rayTri(o, d, v3(t[0], t[1], t[2]), v3(t[3], t[4], t[5]), v3(t[6], t[7], t[8]));
		if (hit >= 0 && hit <= maxDrop && p[1] - hit > best) best = p[1] - hit;
	}
	return best;
}

bool roomSegmentBlocked(const Room* room, const float a[3], const float b[3])
{
	V3 o = v3(a[0], a[1], a[2]);
	V3 d = sub(v3(b[0], b[1], b[2]), o);
	float len = sqrtf(dot(d, d));
	if (len < 1e-4f) return false;
	int cx0, cz0, cx1, cz1;
	if (!cellRange(room, a[0], a[2], b[0], b[2], &cx0, &cz0, &cx1, &cz1)) return false;
	if (s_stampSize < room->ntri) {
		free(s_stamp);
		s_stamp = calloc(room->ntri, sizeof(uint32_t));
		s_stampSize = room->ntri;
		s_frame = 0;
	}
	s_frame++;

	int steps = (int)(len / 0.25f) + 1;
	for (int i = 0; i <= steps; i++) {
		float f = (float)i / steps;
		int cx = (int)((a[0] + (b[0] - a[0]) * f - room->minX) / 0.5f);
		int cz = (int)((a[2] + (b[2] - a[2]) * f - room->minZ) / 0.5f);
		if (cx < 0 || cz < 0 || cx >= room->gridW || cz >= room->gridH) continue;
		int c = cz * room->gridW + cx;
		for (uint32_t k = room->cellStart[c]; k < room->cellStart[c + 1]; k++) {
			uint32_t ti = room->cellTris[k];
			if (s_stamp[ti] == s_frame || (int)ti >= room->nsightTri) continue;
			s_stamp[ti] = s_frame;
			const float* t = room->tris + ti * 9;
			float hit = rayTri(o, d, v3(t[0], t[1], t[2]), v3(t[3], t[4], t[5]), v3(t[6], t[7], t[8]));
			if (hit >= 0 && hit <= 1.0f) return true;
		}
	}
	return false;
}
