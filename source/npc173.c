#include "npc173.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"

#define DEG2RAD ((float)M_PI / 180.0f)
#define SPEED (38.0f / 100.0f * 70.0f)
#define KILL_DIST 0.65f
#define SEE_DIST 10.0f
#define HIDE_DISTANCE 15.0f
#define HEIGHT 1.16f

static Room s_mesh;
static bool s_meshOk;

static struct {
	bool active;
	float pos[3];
	float yaw;
	float lastSeenTime;
	float lastDist;
	float soundVol;
	int dragChan;
	bool seen;
	float dist;
	float waitDoor;
	float time;
	bool idle;
} s;

void npc173Load(void)
{
	s_meshOk = roomLoad(&s_mesh, "romfs:/models/npc173.scr");
	s.dragChan = -1;
}

static float cellX(int gx) { return gx * 8.0f; }
static float cellZ(int gy) { return -gy * 8.0f; }

void npc173Spawn(const World* w, const float playerPos[3])
{
	int pgx, pgy;
	worldCell(playerPos, &pgx, &pgy);
	int best = -1, bestScore = 1 << 30;
	for (int i = 0; i < w->map.nrooms; i++) {
		const MapRoomPlacement* r = &w->map.rooms[i];
		int d = abs(r->gx - pgx) + abs(r->gy - pgy);
		int score = abs(d - 4) * 10 + (r->zone != 1) * 100 + rand() % 5;
		if (score < bestScore) {
			bestScore = score;
			best = i;
		}
	}
	memset(&s, 0, sizeof(s));
	s.dragChan = -1;
	s.lastSeenTime = 1000;
	if (best < 0) return;
	s.active = true;
	s.pos[0] = cellX(w->map.rooms[best].gx);
	s.pos[2] = cellZ(w->map.rooms[best].gy);
	s.pos[1] = 0;
}

static int boundaryDoor(const World* w, int ax, int ay, int bx, int by)
{

	float x = (cellX(ax) + cellX(bx)) * 0.5f, z = (cellZ(ay) + cellZ(by)) * 0.5f;
	for (int i = 0; i < w->ndoors; i++)
		if (w->doors[i].room < 0 && fabsf(w->doors[i].x - x) < 0.5f && fabsf(w->doors[i].z - z) < 0.5f) return i;
	return -1;
}

static bool canOpen(const Door* d) { return !d->locked && d->keycard == 0 && d->code[0] == 0; }

static bool nextCell(const World* w, int sx, int sy, int tx, int ty, int* nx, int* ny)
{
	static int prev[MAP_SIZE + 1][MAP_SIZE + 1];
	static int queue[(MAP_SIZE + 1) * (MAP_SIZE + 1)];
	for (int x = 0; x <= MAP_SIZE; x++)
		for (int y = 0; y <= MAP_SIZE; y++) prev[x][y] = -1;
	if (sx < 0 || sy < 0 || sx > MAP_SIZE || sy > MAP_SIZE) return false;
	int head = 0, tail = 0;
	queue[tail++] = sx * 64 + sy;
	prev[sx][sy] = sx * 64 + sy;
	static const int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	while (head < tail) {
		int c = queue[head++], cx = c / 64, cy = c % 64;
		if (cx == tx && cy == ty) break;
		for (int k = 0; k < 4; k++) {
			int x = cx + dirs[k][0], y = cy + dirs[k][1];
			if (x < 0 || y < 0 || x > MAP_SIZE || y > MAP_SIZE || prev[x][y] >= 0 || !w->map.grid[x][y]) continue;
			int d = boundaryDoor(w, cx, cy, x, y);
			if (d >= 0 && !worldDoorPassable(w, d) && !canOpen(&w->doors[d])) continue;
			prev[x][y] = c;
			queue[tail++] = x * 64 + y;
		}
	}
	if (tx < 0 || ty < 0 || tx > MAP_SIZE || ty > MAP_SIZE || prev[tx][ty] < 0) return false;
	int c = tx * 64 + ty;
	while (prev[c / 64][c % 64] != sx * 64 + sy && c != sx * 64 + sy) c = prev[c / 64][c % 64];
	*nx = c / 64;
	*ny = c % 64;
	return true;
}

static bool inView(const PlayerView* pv, const float p[3])
{
	float yr = pv->yaw * DEG2RAD, pr = pv->pitch * DEG2RAD;
	float f[3] = { sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr) };
	float r[3] = { cosf(yr), 0, sinf(yr) };
	float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
	float d[3] = { p[0] - pv->eye[0], p[1] - pv->eye[1], p[2] - pv->eye[2] };
	float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
	if (z < 0.05f) return false;
	float x = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
	float y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
	const float margin = 0.45f;
	return fabsf(x) < z * 1.15f + margin && fabsf(y) < z * 0.70f + margin;
}

static bool visibleFrom(const World* w, const float eye[3], const float p[3])
{
	float top[3] = { p[0], p[1] + HEIGHT * 0.85f, p[2] };
	float mid[3] = { p[0], p[1] + HEIGHT * 0.5f, p[2] };
	return worldLineOfSight(w, eye, top) || worldLineOfSight(w, eye, mid);
}

static const char* deathMessage(const char* room)
{
	if (!strcmp(room, "lockroom") || !strcmp(room, "room2closets") || !strcmp(room, "coffin"))
		return "Subject D-9341. Cause of death: Fatal cervical fracture. The surveillance tapes confirm that the subject was killed by SCP-173.";
	if (!strcmp(room, "room2doors"))
		return "\"If I'm not mistaken, one of the main purposes of these rooms was to stop SCP-173 from moving further in the event of a containment breach. So, who's brilliant idea was it to put A GODDAMN MAN-SIZED VENTILATION DUCT in there?\"";
	return "Subject D-9341. Cause of death: Fatal cervical fracture. Assumed to be attacked by SCP-173.";
}

const char* npc173Update(World* w, const PlayerView* pv, float dt)
{
	if (!s.active || pv->dead || s.idle) {
		if (s.dragChan >= 0) sfxStop(s.dragChan);
		s.dragChan = -1;

		float c[3] = { s.pos[0], s.pos[1] + HEIGHT * 0.5f, s.pos[2] };
		if (s.active && !pv->dead && (pv->eyesClosed || !inView(pv, c)))
			s.yaw = atan2f(-(pv->pos[0] - s.pos[0]), -(pv->pos[2] - s.pos[2])) / DEG2RAD;
		return NULL;
	}
	s.time += dt;
	s.lastSeenTime += dt;
	float dx = pv->pos[0] - s.pos[0], dz = pv->pos[2] - s.pos[2];
	float dist = sqrtf(dx * dx + dz * dz + (pv->pos[1] - s.pos[1]) * (pv->pos[1] - s.pos[1]));
	s.dist = dist;

	float centre[3] = { s.pos[0], s.pos[1] + HEIGHT * 0.5f, s.pos[2] };
	bool losPlayer = dist < SEE_DIST && visibleFrom(w, pv->eye, s.pos);
	bool watched = !pv->eyesClosed && losPlayer && inView(pv, centre);
	s.seen = watched;
	float oldPos[2] = { s.pos[0], s.pos[2] };

	if (watched) {
		if (dist < 3.5f && s.lastSeenTime > 60.0f) {
			char snd[32];
			snprintf(snd, sizeof(snd), "horror/horror%d", 3 + rand() % 2);
			sfxPlay(snd, 1.0f);
			s.lastSeenTime = 0;
		}
		if (dist < 1.5f && rand() % 700 == 0) {
			char snd[32];
			snprintf(snd, sizeof(snd), "scp/173/rattle%d", 1 + rand() % 3);
			sfxPlay3D(snd, centre, 10.0f, 1.0f);
		}
		if (dist < 1.5f && s.lastDist > 2.0f) {
			static const int stings[5] = { 1, 2, 9, 10, 14 };
			char snd[32];
			snprintf(snd, sizeof(snd), "horror/horror%d", stings[rand() % 5]);
			sfxPlay(snd, 1.0f);
		}
		s.lastDist = dist;
		s.soundVol = fmaxf(0.0f, s.soundVol - dt * 3.5f);
	} else if (dist > HIDE_DISTANCE * 0.8f) {

		if (rand() % 70 == 0) {
			int pgx, pgy;
			worldCell(pv->pos, &pgx, &pgy);
			for (int tries = 0; tries < 12; tries++) {
				int i = rand() % (w->map.nrooms ? w->map.nrooms : 1);
				const MapRoomPlacement* r = &w->map.rooms[i];
				int d = abs(r->gx - pgx) > abs(r->gy - pgy) ? abs(r->gx - pgx) : abs(r->gy - pgy);
				if (d != 2) continue;
				float p[3] = { cellX(r->gx), 0, cellZ(r->gy) };
				if (visibleFrom(w, pv->eye, p) && inView(pv, p)) continue;
				memcpy(s.pos, p, sizeof(p));
				break;
			}
		}
	} else {

		int sgx, sgy, pgx, pgy;
		worldCell(s.pos, &sgx, &sgy);
		worldCell(pv->pos, &pgx, &pgy);
		float target[2] = { pv->pos[0], pv->pos[2] };
		if (sgx != pgx || sgy != pgy) {
			int nx, ny;
			if (nextCell(w, sgx, sgy, pgx, pgy, &nx, &ny)) {
				int d = boundaryDoor(w, sgx, sgy, nx, ny);
				target[0] = (cellX(sgx) + cellX(nx)) * 0.5f;
				target[1] = (cellZ(sgy) + cellZ(ny)) * 0.5f;

				float cx = cellX(sgx), cz = cellZ(sgy);
				bool alongX = nx != sgx;
				float off = alongX ? fabsf(s.pos[2] - cz) : fabsf(s.pos[0] - cx);
				if (off > 0.3f) {
					target[0] = alongX ? s.pos[0] : cx;
					target[1] = alongX ? cz : s.pos[2];
				}
				if (d >= 0 && !worldDoorPassable(w, d)) {
					float ddx = w->doors[d].x - s.pos[0], ddz = w->doors[d].z - s.pos[2];
					if (ddx * ddx + ddz * ddz < 1.2f * 1.2f) {
						if (!w->doors[d].open && canOpen(&w->doors[d])) {
							worldToggleDoor(w, d);
							float dp[3] = { w->doors[d].x, 1.0f, w->doors[d].z };
							sfxPlay3D("door/dooropen173", dp, 10.0f, 1.0f);
						}
						target[0] = s.pos[0];
						target[1] = s.pos[2];
					}
				}
			} else {
				target[0] = s.pos[0];
				target[1] = s.pos[2];
			}
		}
		float tx = target[0] - s.pos[0], tz = target[1] - s.pos[2];
		float tl = sqrtf(tx * tx + tz * tz);
		float step = SPEED * dt;
		if (tl > 1e-3f) {
			if (step > tl) step = tl;
			s.pos[0] += tx / tl * step;
			s.pos[2] += tz / tl * step;

			float c[3] = { s.pos[0], s.pos[1] + 0.5f, s.pos[2] };
			worldCollide(w, c);
			s.pos[0] = c[0];
			s.pos[2] = c[2];
		}

		if (losPlayer && dist < KILL_DIST) {
			char snd[32];
			snprintf(snd, sizeof(snd), "scp/173/necksnap%d", 1 + rand() % 3);
			sfxPlay(snd, 1.0f);
			if (s.dragChan >= 0) sfxStop(s.dragChan);
			s.dragChan = -1;
			return deathMessage(worldRoomAt(w, pv->pos));
		}
	}

	float probe[3] = { s.pos[0], s.pos[1] + 0.35f, s.pos[2] };
	float fy = worldRayDown(w, probe, 3.0f);
	if (isfinite(fy)) s.pos[1] = fy;

	if (!watched) s.yaw = atan2f(-(pv->pos[0] - s.pos[0]), -(pv->pos[2] - s.pos[2])) / DEG2RAD;

	float moved = sqrtf((s.pos[0] - oldPos[0]) * (s.pos[0] - oldPos[0]) + (s.pos[2] - oldPos[1]) * (s.pos[2] - oldPos[1]));
	float target = fminf(1.0f, moved / fmaxf(dt, 1e-3f) * 2.5f / 70.0f);
	s.soundVol += (target - s.soundVol) * fminf(1.0f, dt * 20.0f);
	if (dist < HIDE_DISTANCE && s.soundVol > 0.02f) sfxLoop3D(&s.dragChan, "scp/173/stonedrag", centre, 10.0f, s.soundVol);
	else if (s.dragChan >= 0) {
		sfxStop(s.dragChan);
		s.dragChan = -1;
	}
	return NULL;
}

void npc173Draw(const C3D_Mtx* view, int uLoc, C3D_Tex* white)
{
	if (!s.active || !s_meshOk) return;

	worldDrawModel(&s_mesh, s.pos[0], s.pos[1], s.pos[2], s.yaw + 180, view, uLoc, white);
}

bool npc173Seen(void) { return s.seen; }
float npc173Distance(void) { return s.active ? s.dist : 1e9f; }

void npc173Place(const float pos[3], float yaw)
{
	s.active = true;
	memcpy(s.pos, pos, sizeof(s.pos));
	s.yaw = yaw;
}

void npc173SetIdle(bool idle) { s.idle = idle; }

void npc173GetPos(float out[3]) { memcpy(out, s.pos, sizeof(s.pos)); }

bool npc173IsIdle(void) { return s.idle; }

void npc173Save(FILE* f)
{
	fwrite(&s.active, sizeof(s.active), 1, f);
	fwrite(s.pos, sizeof(s.pos), 1, f);
	fwrite(&s.yaw, sizeof(s.yaw), 1, f);
	fwrite(&s.idle, sizeof(s.idle), 1, f);
}

void npc173Restore(FILE* f)
{
	fread(&s.active, sizeof(s.active), 1, f);
	fread(s.pos, sizeof(s.pos), 1, f);
	fread(&s.yaw, sizeof(s.yaw), 1, f);
	fread(&s.idle, sizeof(s.idle), 1, f);
	s.dragChan = -1;
}
