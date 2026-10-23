#include "nav.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "collide.h"

#define PROBE_TOP 1.0f
#define PROBE_DROP 4.0f
#define STEP_HEIGHT 0.35f

bool navBuild(NavGrid* g, const Room* room)
{
	memset(g, 0, sizeof(*g));
	g->minX = room->minX;
	g->minZ = room->minZ;
	g->w = (int)(room->gridW * 0.5f / NAV_CELL);
	g->h = (int)(room->gridH * 0.5f / NAV_CELL);
	if (g->w <= 0 || g->h <= 0) return false;
	int n = g->w * g->h;
	g->floorY = malloc(sizeof(float) * n);
	g->walk = calloc(n, 1);
	if (!g->floorY || !g->walk) {
		navFree(g);
		return false;
	}
	for (int z = 0; z < g->h; z++)
		for (int x = 0; x < g->w; x++) {
			int i = z * g->w + x;
			float p[3] = { g->minX + (x + 0.5f) * NAV_CELL, PROBE_TOP, g->minZ + (z + 0.5f) * NAV_CELL };
			float fy = roomRayDown(room, p, PROBE_DROP);
			g->floorY[i] = isfinite(fy) ? fy : NAN;
			if (!isfinite(fy)) continue;

			float c[3] = { p[0], fy + 0.32f, p[2] };
			collidePlayer(room, c);
			float dx = c[0] - p[0], dz = c[2] - p[2];
			g->walk[i] = dx * dx + dz * dz < 0.02f * 0.02f;
		}
	g->built = true;
	return true;
}

void navFree(NavGrid* g)
{
	free(g->floorY);
	free(g->walk);
	memset(g, 0, sizeof(*g));
}

static int cellOf(const NavGrid* g, const float p[3])
{
	int x = (int)((p[0] - g->minX) / NAV_CELL), z = (int)((p[2] - g->minZ) / NAV_CELL);
	if (x < 0 || z < 0 || x >= g->w || z >= g->h) return -1;
	return z * g->w + x;
}

static int nearestWalkable(const NavGrid* g, const float p[3])
{
	int c = cellOf(g, p);
	int cx = c >= 0 ? c % g->w : (int)((p[0] - g->minX) / NAV_CELL);
	int cz = c >= 0 ? c / g->w : (int)((p[2] - g->minZ) / NAV_CELL);
	int best = -1;
	float bestD = 1e9f;
	for (int r = 0; r <= 8 && best < 0; r++)
		for (int dz = -r; dz <= r; dz++)
			for (int dx = -r; dx <= r; dx++) {
				if (abs(dx) != r && abs(dz) != r) continue;
				int x = cx + dx, z = cz + dz;
				if (x < 0 || z < 0 || x >= g->w || z >= g->h || !g->walk[z * g->w + x]) continue;
				float fy = g->floorY[z * g->w + x];
				float d = dx * dx + dz * dz + fabsf(fy - p[1]) * 16.0f;
				if (d < bestD) {
					bestD = d;
					best = z * g->w + x;
				}
			}
	return best;
}

bool navNearest(const NavGrid* g, const float p[3], float out[3])
{
	if (!g->built) return false;
	int c = nearestWalkable(g, p);
	if (c < 0) return false;
	out[0] = g->minX + (c % g->w + 0.5f) * NAV_CELL;
	out[1] = g->floorY[c];
	out[2] = g->minZ + (c / g->w + 0.5f) * NAV_CELL;
	return true;
}

int navPath(const NavGrid* g, const float from[3], const float to[3], float (*out)[3], int max)
{
	if (!g->built) return 0;
	int s = nearestWalkable(g, from), t = nearestWalkable(g, to);
	if (s < 0 || t < 0) return 0;
	int n = g->w * g->h;
	static int* prev;
	static int* queue;
	static int cap;
	if (cap < n) {
		free(prev);
		free(queue);
		prev = malloc(sizeof(int) * n);
		queue = malloc(sizeof(int) * n);
		cap = n;
	}
	for (int i = 0; i < n; i++) prev[i] = -1;
	int head = 0, tail = 0;
	queue[tail++] = s;
	prev[s] = s;
	static const int dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
	while (head < tail && prev[t] < 0) {
		int c = queue[head++], cx = c % g->w, cz = c / g->w;
		for (int k = 0; k < 8; k++) {
			int x = cx + dirs[k][0], z = cz + dirs[k][1];
			if (x < 0 || z < 0 || x >= g->w || z >= g->h) continue;
			int i = z * g->w + x;
			if (prev[i] >= 0 || !g->walk[i]) continue;
			if (fabsf(g->floorY[i] - g->floorY[c]) > STEP_HEIGHT) continue;

			if (k >= 4 && (!g->walk[cz * g->w + x] || !g->walk[z * g->w + cx])) continue;
			prev[i] = c;
			queue[tail++] = i;
		}
	}
	if (prev[t] < 0) return 0;

	static int chain[4096];
	int len = 0;
	for (int c = t; c != s && len < 4096; c = prev[c]) chain[len++] = c;
	int count = 0;
	for (int k = len - 1; k >= 0 && count < max; k--) {
		if (k != 0 && (len - 1 - k) % 4 != 3) continue;
		int c = chain[k];
		out[count][0] = g->minX + (c % g->w + 0.5f) * NAV_CELL;
		out[count][1] = g->floorY[c];
		out[count][2] = g->minZ + (c / g->w + 0.5f) * NAV_CELL;
		count++;
	}
	return count;
}
