// Port of SCP - Containment Breach by Regalis and contributors (CC BY-SA 3.0).

#include "npc.h"
#include "events.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "hud.h"
#include "nav.h"
#include "wear.h"

#define DEG2RAD ((float)M_PI / 180.0f)
#define FRAMES(dt) ((dt) * 70.0f)
#define HIDE_DISTANCE 15.0f

static const char* s_modelNames[NPC_TYPES] = { "106", "049", "096", "guard", "classd", "mtf", "939", "966",
                                                "zombie", "surgeon", "066", "372", "5131", "1048a", "035", "860", "1499" };

static const float s_yawOffset[NPC_TYPES] = { 0, 0, 0, 180, 180, 180, 0, 0, 180, 0, 0, 0, 0, 0, 180, 0, 180 };

static const float s_speed[NPC_TYPES] = { 1.2f / 100, 1.5f / 100, 9.0f / 100, 2.0f / 100, 2.0f / 100,
                                          2.0f / 100, 4.0f / 100, 1.0f / 100, 0.8f / 100, 2.0f / 100,
                                          2.0f / 100, 4.0f / 100, 0, 1.5f / 100, 2.0f / 100, 8.0f / 100, 1.5f / 100 };

static SkinModel s_models[NPC_TYPES];
static bool s_modelTried[NPC_TYPES];
static Npc s_npcs[MAX_NPCS];
static NavGrid s_nav[MAX_TEMPLATES];
static const char* s_musicOverride;
static float s_106Reload;
static bool s_106Grab;
static bool s_mtfSpawned;

bool npc106TakeGrab(void)
{
	bool g = s_106Grab;
	s_106Grab = false;
	return g;
}
static char s_dbg096[32];
extern int g_losReason;

static float frand(float lo, float hi) { return lo + (hi - lo) * (rand() / (float)RAND_MAX); }
static float dist3(const float a[3], const float b[3])
{
	float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
	return sqrtf(dx * dx + dy * dy + dz * dz);
}
static float distXZ(const float a[3], const float b[3])
{
	float dx = a[0] - b[0], dz = a[2] - b[2];
	return sqrtf(dx * dx + dz * dz);
}

static float curve(float target, float old, float smooth, float dt)
{
	return old + (target - old) * fminf(1.0f, FRAMES(dt) / smooth);
}

void npcsLoad(void) { memset(s_npcs, 0, sizeof(s_npcs)); }

static SkinModel* modelFor(NpcType t)
{
	if (!s_modelTried[t]) {
		s_modelTried[t] = true;
		skinLoad(&s_models[t], s_modelNames[t]);
	}
	return s_models[t].ok ? &s_models[t] : NULL;
}

void npcsReset(void)
{
	for (int i = 0; i < MAX_NPCS; i++) npcRemove(i);
	s_musicOverride = NULL;
	s_106Reload = 0;
	s_mtfSpawned = false;
}

int npcSpawn(NpcType type, const float pos[3], float yaw)
{
	for (int i = 0; i < MAX_NPCS; i++) {
		Npc* n = &s_npcs[i];
		if (n->active) continue;
		memset(n, 0, sizeof(*n));
		n->active = true;
		n->type = type;
		memcpy(n->pos, pos, sizeof(n->pos));
		n->yaw = yaw;
		n->speed = s_speed[type];
		n->chan = n->chan2 = -1;
		n->pathRoom = -1;
		SkinModel* m = modelFor(type);
		if (m) skinInstanceInit(&n->skin, m);
		return i;
	}
	return -1;
}

Npc* npcGet(int i) { return i >= 0 && i < MAX_NPCS && s_npcs[i].active ? &s_npcs[i] : NULL; }

Npc* npcFind(NpcType type)
{
	for (int i = 0; i < MAX_NPCS; i++)
		if (s_npcs[i].active && s_npcs[i].type == type) return &s_npcs[i];
	return NULL;
}

void npcRemove(int i)
{
	Npc* n = &s_npcs[i];
	if (!n->active) return;
	sfxStop(n->chan);
	sfxStop(n->chan2);
	skinInstanceFree(&n->skin);
	memset(n, 0, sizeof(*n));
}

void npcAnimate(Npc* n, float start, float end, float speed, bool loop, float dt)
{

	n->prevFrame = n->frame;
	float t;
	if (speed > 0) {
		t = fmaxf(fminf(n->frame + speed * FRAMES(dt), end), start);
		if (loop && t >= end) t = start;
	} else {
		if (start < end) {
			float tmp = start;
			start = end;
			end = tmp;
		}
		if (loop) {
			t = n->frame + speed * FRAMES(dt);
			if (t < end) t = start;
			else if (t > start) t = end;
		} else {
			t = fmaxf(fminf(n->frame + speed * FRAMES(dt), start), end);
		}
	}
	n->frame = t;
}

static bool crossed(const Npc* n, float f) { return n->prevFrame < f && n->frame >= f; }

void npcFace(Npc* n, const float target[3], float rate, float dt)
{
	float want = atan2f(-(target[0] - n->pos[0]), -(target[2] - n->pos[2])) / DEG2RAD;
	float d = fmodf(want - n->yaw + 540.0f, 360.0f) - 180.0f;
	n->yaw += d * fminf(1.0f, rate * dt);
}

void npcSnapToFloor(const World* w, Npc* n)
{
	float probe[3] = { n->pos[0], n->pos[1] + 0.8f, n->pos[2] };
	float fy = worldRayDown(w, probe, 2.0f);
	if (isfinite(fy)) n->pos[1] = fy;
}

static int roomOf(const World* w, const float p[3])
{
	if (w->introActive) return w->introRoom;
	return worldRoomIndexAt(w, p);
}

static NavGrid* navFor(const World* w, int room)
{
	if (room < 0) return NULL;
	int t = w->map.rooms[room].tpl;
	if (!w->tpl[t].loaded) return NULL;
	if (!s_nav[t].built) navBuild(&s_nav[t], &w->tpl[t].room);
	return s_nav[t].built ? &s_nav[t] : NULL;
}

static int doorPolicy(NpcType t) { return t == NPC_106 ? 2 : (t == NPC_096 ? 1 : 0); }

static bool handleDoors(World* w, Npc* n, const float next[3])
{
	int policy = doorPolicy(n->type);
	if (policy == 2) return false;
	for (int i = 0; i < w->ndoors; i++) {
		Door* d = &w->doors[i];
		if (d->openstate >= 179.0f) continue;
		float dp[3] = { d->x, d->y, d->z };
		if (distXZ(dp, n->pos) > 1.6f || fabsf(d->y - n->pos[1]) > 1.5f) continue;

		float c = cosf(d->angle * DEG2RAD), sn = sinf(d->angle * DEG2RAD);
		float ax = n->pos[0] - d->x, az = n->pos[2] - d->z;
		float bx = next[0] - d->x, bz = next[2] - d->z;
		float alx = ax * c - az * sn, alz = ax * sn + az * c;
		float blx = bx * c - bz * sn, blz = bx * sn + bz * c;
		float halfWidth = d->type == DOOR_BIG ? 1.6f : 0.5f;
		if ((alz > 0) == (blz > 0) && fabsf(alz) > 0.35f) continue;
		float t = fabsf(alz - blz) > 1e-4f ? alz / (alz - blz) : 0.0f;
		float cx = alx + (blx - alx) * fminf(fmaxf(t, 0.0f), 1.0f);
		if (fabsf(cx) > halfWidth) continue;
		if (!d->open && (policy == 1 || worldDoorOpenable(d))) {
			worldToggleDoor(w, i);
			if (policy == 1) {
				d->openstate = fmaxf(d->openstate, 60.0f);
				sfxPlay3D("door/dooropenfast", dp, 10.0f, 1.0f);
			}
		}
		return d->openstate < 150.0f;
	}
	return false;
}

bool npcWalkTo(World* w, Npc* n, const float target[3], float speed, float dt)
{
	if (distXZ(n->pos, target) < 0.3f && fabsf(n->pos[1] - target[1]) < 1.0f) return true;

	float goal[3] = { target[0], target[1], target[2] };
	int room = roomOf(w, n->pos);
	if (!w->introActive) {
		int sx, sy, tx, ty, nx, ny;
		worldCell(n->pos, &sx, &sy);
		worldCell(target, &tx, &ty);
		if ((sx != tx || sy != ty) && worldNextCell(w, sx, sy, tx, ty, doorPolicy(n->type), &nx, &ny)) {
			goal[0] = (sx + nx) * 4.0f;
			goal[2] = -(sy + ny) * 4.0f;
			goal[1] = n->pos[1];
		}
	}

	float next[3] = { goal[0], goal[1], goal[2] };
	NavGrid* nav = navFor(w, room);
	if (nav) {
		n->repath -= dt;
		if (n->repath <= 0 || n->pathRoom != room || distXZ(goal, n->goal) > 0.75f || n->pathi >= n->npath) {
			float ls[3], lg[3];
			worldWorldToRoom(w, room, n->pos, ls);
			worldWorldToRoom(w, room, goal, lg);
			n->npath = navPath(nav, ls, lg, n->path, 24);
			n->pathi = 0;
			n->pathRoom = room;
			n->repath = 1.0f;
			memcpy(n->goal, goal, sizeof(goal));
			for (int i = 0; i < n->npath; i++) {
				float wp[3];
				worldRoomToWorld(w, room, n->path[i], wp);
				memcpy(n->path[i], wp, sizeof(wp));
			}
		}
		while (n->pathi < n->npath && distXZ(n->pos, n->path[n->pathi]) < 0.35f) n->pathi++;
		if (n->pathi < n->npath) memcpy(next, n->path[n->pathi], sizeof(next));
	}

	if (handleDoors(w, n, next)) return false;

	float dx = next[0] - n->pos[0], dz = next[2] - n->pos[2];
	float d = sqrtf(dx * dx + dz * dz);
	float step = speed * dt;
	if (d > 1e-3f) {
		if (step > d) step = d;
		n->pos[0] += dx / d * step;
		n->pos[2] += dz / d * step;
		npcFace(n, next, 8.0f, dt);
	}
	if (nav && n->pathi < n->npath) n->pos[1] += (next[1] - n->pos[1]) * fminf(1.0f, dt * 8.0f);
	else npcSnapToFloor(w, n);
	return distXZ(n->pos, target) < 0.3f;
}

static bool playerSees(const World* w, const PlayerView* pv, const float p[3])
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
	if (fabsf(x) > z * 1.15f + 0.4f || fabsf(y) > z * 0.7f + 0.4f) return false;

	for (int k = 0; k < 3; k++) {
		float q[3] = { p[0], p[1] - 0.35f * k, p[2] };
		if (worldLineOfSight(w, pv->eye, q)) return true;
	}
	return false;
}

static void centre(const Npc* n, float h, float out[3])
{
	out[0] = n->pos[0];
	out[1] = n->pos[1] + h;
	out[2] = n->pos[2];
}

static void playRand3D(const char* fmt, int lo, int hi, const float p[3], float range)
{
	char snd[48];
	snprintf(snd, sizeof(snd), fmt, lo + rand() % (hi - lo + 1));
	sfxPlay3D(snd, p, range, 1.0f);
}

enum { S106_WAIT, S106_RISE, S106_CHASE, S106_ATTACK };

static const char* update106(World* w, Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	float mid[3];
	centre(n, 0.8f, mid);
	switch (n->state) {
	case S106_WAIT:
		n->hidden = true;
		sfxStop(n->chan);
		n->chan = -1;
		if (eventsContained106()) break;
		n->timer -= FRAMES(dt);
		if (n->timer <= 0 && !w->introActive && worldRoomIndexAt(w, pv->pos) >= 0) {

			n->pos[0] = pv->pos[0];
			n->pos[2] = pv->pos[2];
			n->pos[1] = pv->pos[1];
			npcSnapToFloor(w, n);
			n->frame = 110;
			n->state = S106_RISE;
			n->hidden = false;
			sfxPlay("scp/106/decay0", 1.0f);
		}
		break;

	case S106_RISE:
		npcFace(n, pv->pos, 1.5f, dt);
		npcAnimate(n, 110, 259, 0.15f, false, dt);
		if (n->frame >= 258.9f) {
			n->state = S106_CHASE;
			n->state2 = 0;
		}
		break;

	case S106_CHASE: {
		if (rand() % 500 == 0) playRand3D("scp/106/corrosion%d", 1, 3, mid, 10.0f);
		bool visible = dist < 8.0f && worldLineOfSight(w, pv->eye, mid);
		if (visible) {
			if (playerSees(w, pv, mid) && n->lastSeen <= 0) {
				sfxPlay("horror/horror6", 1.0f);
				n->lastSeen = 60.0f;
			}
		} else {
			n->state2 -= FRAMES(dt);
		}
		n->lastSeen -= dt;

		if (dist > 0.8f) {
			n->currSpeed = curve(n->speed, n->currSpeed, 10.0f, dt);
			npcAnimate(n, 284, 333, n->currSpeed * 43.0f, true, dt);
			if (crossed(n, 286) || crossed(n, 311)) playRand3D("step/steppd%d", 1, 3, n->pos, 6.0f);
			npcWalkTo(w, n, pv->pos, FRAMES(n->currSpeed), dt);
		} else {
			n->state = S106_ATTACK;
			n->frame = 105;
		}

		if (n->state2 <= -frand(3000, 3500) && !playerSees(w, pv, mid) && dist > 5.0f) {
			n->state = S106_WAIT;
			n->timer = frand(22000, 27000);
		}

		s_106Reload -= FRAMES(dt);
		if (s_106Reload <= 0 && dist > 10.0f && n->state2 < -5 && !playerSees(w, pv, mid)) {
			float yr = pv->yaw * DEG2RAD;
			n->pos[0] = pv->pos[0] - sinf(yr) * 3.0f;
			n->pos[2] = pv->pos[2] + cosf(yr) * 3.0f;
			n->pos[1] = pv->pos[1];
			npcSnapToFloor(w, n);
			sfxPlay3D("scp/106/laugh", mid, 10.0f, 1.0f);
			playRand3D("scp/106/walldecay%d", 1, 3, mid, 10.0f);
			n->npath = 0;
			s_106Reload = 70 * 10.0f / 2.0f;
		}
		break;
	}

	case S106_ATTACK:
		npcFace(n, pv->pos, 4.0f, dt);
		npcAnimate(n, 105, 110, 0.15f, false, dt);
		if (n->frame >= 109.99f) {
			sfxPlay("ambient/general/damage2", 1.0f);
			sfxPlay("horror/horror5", 1.0f);
			sfxPlay("scp/106/laugh", 1.0f);
			n->state = S106_WAIT;
			n->timer = frand(22000, 27000);
			if (!w->isolated) {
				s_106Grab = true;
				return NULL;
			}
			return "Subject D-9341. Body partially decomposed by what is assumed to be SCP-106's \"corrosion\" effect. "
			       "Body disposed of via incineration.";
		}
		break;
	}
	if (n->state != S106_WAIT) sfxLoop3D(&n->chan, "scp/106/breathing", mid, 8.0f, 0.8f);
	return NULL;
}

static const char* update096(World* w, Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	float head[3];
	centre(n, 0.9f, head);
	switch (n->state) {
	case 0: {
		if (dist < 8.0f) sfxLoop3D(&n->chan, "music/096", head, 8.0f, 1.0f);
		else if (n->chan >= 0) {
			sfxStop(n->chan);
			n->chan = -1;
		}
		npcAnimate(n, 936, 1263, 0.1f, true, dt);

		float fx = -sinf(n->yaw * DEG2RAD), fz = -cosf(n->yaw * DEG2RAD);
		float tx = pv->pos[0] - n->pos[0], tz = pv->pos[2] - n->pos[2];
		bool front = fx * tx + fz * tz > 0, sees = dist < 8.0f && playerSees(w, pv, head);
		snprintf(s_dbg096, sizeof(s_dbg096), " fr%d se%d r%d", front, sees, g_losReason);
		if (dist < 8.0f && front && !pv->eyesClosed && sees) {
			sfxPlay("scp/096/triggered", 1.0f);
			sfxStop(n->chan);
			n->chan = -1;
			n->frame = 194;
			n->state = 1;
			n->state3 = 0;
		}
		break;
	}
	case 1:
		if (n->frame < 312) {
			npcAnimate(n, 193, 311, 0.3f, false, dt);
			if (n->frame > 310.9f) {
				n->state = 2;
				n->frame = 737;
			}
		} else {
			npcAnimate(n, 677, 736, 0.3f, false, dt);
			if (n->frame > 735.9f) {
				n->state = 2;
				n->frame = 737;
			}
		}
		sfxLoop3D(&n->chan, "music/096angered", head, 10.0f, 1.0f);
		break;
	case 2:
		npcAnimate(n, 737, 822, 0.3f, false, dt);
		if (n->frame >= 822) {
			n->state = 3;
			n->state2 = 0;
		}
		sfxLoop3D(&n->chan, "music/096angered", head, 10.0f, 1.0f);
		break;
	case 3:
		n->state2 += FRAMES(dt);
		if (n->state2 > 70 * 18) {
			npcAnimate(n, 823, 847, n->speed * 8, false, dt);
			if (n->frame > 846.9f) {
				n->state = 4;
				sfxStop(n->chan);
				n->chan = -1;
			}
		} else {
			npcAnimate(n, 737, 822, 0.3f, true, dt);
			sfxLoop3D(&n->chan, "music/096angered", head, 10.0f, 1.0f);
		}
		break;
	case 4:
		s_musicOverride = "music/096chase";
		sfxLoop3D(&n->chan, "scp/096/scream", head, 7.5f, 1.0f);
		if (dist < 2.8f || n->frame < 150) {
			if (n->frame > 193) n->frame = 2;
			npcAnimate(n, 2, 193, 0.7f, true, dt);
			if (dist > 1.0f) {
				n->currSpeed = curve(n->speed * 2.0f, n->currSpeed, 15.0f, dt);
				npcFace(n, pv->pos, 10.0f, dt);
				float d = FRAMES(n->currSpeed) * dt;
				float dx = pv->pos[0] - n->pos[0], dz = pv->pos[2] - n->pos[2], l = sqrtf(dx * dx + dz * dz);
				if (l > 1e-3f) {
					n->pos[0] += dx / l * fminf(d, l);
					n->pos[2] += dz / l * fminf(d, l);
				}
				npcSnapToFloor(w, n);
			} else {
				sfxPlay("ambient/general/damage5", 1.0f);
				return "A large amount of blood found in [DATA REDACTED]. DNA indentified as Subject D-9341. Most likely "
				       "[DATA REDACTED] by SCP-096.";
			}
		} else {
			if (n->frame > 847) n->currSpeed = curve(n->speed, n->currSpeed, 20.0f, dt);
			if (n->frame < 906) npcAnimate(n, 737, 906, n->speed * 8, false, dt);
			else npcAnimate(n, 907, 935, n->currSpeed * 8, true, dt);
			npcWalkTo(w, n, pv->pos, FRAMES(n->currSpeed), dt);
		}
		break;

	case 5: {
		if (dist >= 16.0f) break;
		sfxLoop3D(&n->chan, "music/096", head, 14.0f, 1.0f);
		if (n->frame >= 422) {
			n->state2 += FRAMES(dt);
			if (n->state2 > 1000) {
				if (n->state2 > 1600) n->state2 = (float)(rand() % 500);
				n->currSpeed = curve(n->speed * 0.1f, n->currSpeed, 5.0f, dt);
				if (n->frame < 1382) npcAnimate(n, 1369, 1382, n->currSpeed * 45, false, dt);
				else npcAnimate(n, 1383, 1456, n->currSpeed * 45, true, dt);
				n->state3 -= dt;
				bool seen = worldLineOfSight(w, pv->eye, head);
				if (n->state3 <= 0) {
					float a = n->yaw * DEG2RAD, ahead[3] = { head[0] - sinf(a) * 1.5f, head[1], head[2] - cosf(a) * 1.5f };
					if (!seen && !worldLineOfSight(w, head, ahead)) n->timer = n->yaw + frand(80, 110);
					n->state3 = 3.0f;
				}
				if (seen) {
					npcFace(n, pv->pos, 130.0f / 70.0f, dt);
					if (dist < 1.5f) n->state2 = 0;
				} else {
					float d = n->timer - n->yaw;
					while (d > 180) d -= 360;
					while (d < -180) d += 360;
					n->yaw += d * fminf(1.0f, FRAMES(dt) / 50.0f);
				}
			} else if (n->frame > 472) {
				n->currSpeed = curve(n->speed * 0.05f, n->currSpeed, 8.0f, dt);
				npcAnimate(n, 1383, 1469, n->currSpeed * 45, false, dt);
				if (n->frame >= 1468.9f) n->frame = 423;
			} else {
				n->currSpeed = curve(0, n->currSpeed, 4.0f, dt);
				npcAnimate(n, 423, 471, 0.2f, true, dt);
			}
			float a = n->yaw * DEG2RAD, step = FRAMES(n->currSpeed) * dt;
			float c[3] = { n->pos[0] - sinf(a) * step, n->pos[1] + 0.5f, n->pos[2] - cosf(a) * step };
			worldCollide(w, c);
			n->pos[0] = c[0];
			n->pos[2] = c[2];
			npcSnapToFloor(w, n);
		} else {
			npcAnimate(n, 312, 422, 0.3f, false, dt);
		}

		float fx = -sinf(n->yaw * DEG2RAD), fz = -cosf(n->yaw * DEG2RAD);
		float tx = pv->pos[0] - n->pos[0], tz = pv->pos[2] - n->pos[2], tl = sqrtf(tx * tx + tz * tz);
		bool front = tl > 1e-3f && (fx * tx + fz * tz) / tl > cosf(55 * DEG2RAD);
		if (front && !pv->eyesClosed && playerSees(w, pv, head)) {
			sfxPlay("scp/096/triggered", 1.0f);
			sfxStop(n->chan);
			n->chan = -1;
			n->frame = 677;
			n->state = 1;
			n->state3 = 0;
		}
		break;
	}
	}
	return NULL;
}

static const char* update049(World* w, Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	float head[3];
	centre(n, 1.1f, head);
	switch (n->state) {
	case 1:
		if (dist < HIDE_DISTANCE) {
			npcAnimate(n, 37, 269, 0.7f, false, dt);
			if (n->frame > 268.9f) n->state = 2;
		}
		break;
	case 2: {
		if (dist > HIDE_DISTANCE * 2) {
			npcAnimate(n, 269, 345, 0.2f, true, dt);
			break;
		}
		bool sees = dist < 10.0f && worldLineOfSight(w, head, pv->eye);
		if (sees) {
			if (n->state3 == 0 && !sfxPlaying(n->chan2)) {
				char snd[32];
				snprintf(snd, sizeof(snd), "scp/049/spotted%d", 1 + rand() % 7);
				n->chan2 = sfxPlay3D(snd, head, 10.0f, 1.0f);
				n->state3 = 1;
			}
			if (dist < 0.5f && g_wear.hazmat) {
				g_wear.hazmat = 0;
				n->chan2 = sfxPlay3D("scp/049/takeoffhazmat", head, 10.0f, 1.0f);
			} else if (dist < 0.5f && g_wear.ring714) {
				if (!sfxPlaying(n->chan2)) n->chan2 = sfxPlay3D("scp/049/714equipped", head, 10.0f, 1.0f);
			} else if (dist < 0.5f) {
				char snd[32];
				snprintf(snd, sizeof(snd), "scp/049/kidnap%d", 1 + rand() % 2);
				sfxPlay(snd, 1.0f);
				sfxPlay("horror/horror13", 1.0f);
				n->state = 3;
				return "An active instance of SCP-049-2 was discovered in [REDACTED]. Terminated by Nine-Tailed Fox.";
			}
		} else if (n->state3 != 0 && rand() % 600 == 0 && !sfxPlaying(n->chan2)) {
			char snd[32];
			snprintf(snd, sizeof(snd), "scp/049/searching%d", 1 + rand() % 6);
			n->chan2 = sfxPlay3D(snd, head, 10.0f, 1.0f);
		}
		n->currSpeed = curve(n->speed, n->currSpeed, 20.0f, dt);
		if (dist < 3.0f) npcAnimate(n, fmaxf(fminf(n->frame, 428), 387), 463, n->currSpeed * 38, true, dt);
		else npcAnimate(n, fmaxf(fminf(n->frame, 358), 346), 393, n->currSpeed * 38, true, dt);
		if (crossed(n, 360) || crossed(n, 380)) playRand3D("scp/049/step%d", 1, 3, n->pos, 8.0f);
		npcWalkTo(w, n, pv->pos, FRAMES(n->currSpeed), dt);
		break;
	}
	case 3:
		npcAnimate(n, 537, 660, 0.7f, false, dt);
		break;
	}
	return NULL;
}

#include "npc_more.inc"

static void updateHuman(World* w, Npc* n, float dt)
{
	if (n->dead) {
		n->frame = n->type == NPC_CLASSD ? 19 : 286;
		return;
	}
	if (n->scripted) return;
	(void)w;
	if (n->type == NPC_GUARD) npcAnimate(n, 77, 201, 0.2f, true, dt);
	else npcAnimate(n, 210, 235, 0.1f, true, dt);
}

void npcsSpawnFacility(const World* w, const float playerPos[3])
{
	int pgx, pgy;
	worldCell(playerPos, &pgx, &pgy);

	int i = npcSpawn(NPC_106, (float[3]){ 0, -30, 0 }, 0);
	if (npcGet(i)) {
		npcGet(i)->state = S106_WAIT;
		npcGet(i)->timer = frand(22000, 27000);
		npcGet(i)->hidden = true;
	}

	int r049 = -1, r096 = -1, best096 = -1;
	for (int r = 0; r < w->map.nrooms; r++) {
		const MapRoomPlacement* mr = &w->map.rooms[r];
		if (mr->offGrid) continue;
		if (strcmp(w->tpl[mr->tpl].info.name, "room049") == 0) r049 = r;
		int d = abs(mr->gx - pgx) + abs(mr->gy - pgy);
		if (mr->zone == 2 && w->tpl[mr->tpl].info.shape == ROOM2 && d > best096 && r != r049) {
			best096 = d;
			r096 = r;
		}
	}

	bool chamber = r049 >= 0;
	if (r049 < 0) {
		for (int r = 0; r < w->map.nrooms && r049 < 0; r++)
			if (w->map.rooms[r].zone == 2 && r != r096 && !w->map.rooms[r].offGrid) r049 = r;
	}
	if (r049 >= 0 && !chamber) {
		float p[3] = { w->map.rooms[r049].gx * 8.0f, 0, -w->map.rooms[r049].gy * 8.0f };
		int k = npcSpawn(NPC_049, p, 0);
		if (npcGet(k)) {
			npcGet(k)->state = 1;
			npcGet(k)->frame = 37;
		}
	}

	for (int r = 0; r < w->map.nrooms; r++) {
		const char* name = w->tpl[w->map.rooms[r].tpl].info.name;
		if (strcmp(name, "room3storage") == 0)
			for (int k = 0; k < 3; k++) {
				static const float spots[3][3] = { { 8.3125f, -21.68f, -4.4375f }, { 14.9375f, -21.68f, -4.5625f },
				                                   { 2.3125f, -21.68f, 24.8125f } };
				float wp[3];
				worldRoomToWorld(w, r, spots[k], wp);
				npcSpawn(NPC_939, wp, (float)(rand() % 360));
			}
		if (strcmp(name, "room966") == 0)
			for (int k = 0; k < 2; k++) {
				float l[3] = { k ? 0.0f : 0.25f, 0, k ? 2.0f : -2.5f }, wp[3];
				worldRoomToWorld(w, r, l, wp);
				Npc* m = npcGet(npcSpawn(NPC_966, wp, 0));
				if (m) m->state = 0;
			}
	}

	bool pj = false;
	for (int r = 0; r < w->map.nrooms; r++)
		if (strcmp(w->tpl[w->map.rooms[r].tpl].info.name, "roompj") == 0) pj = true;
	Npc* moth = pj ? NULL : npcGet(npcSpawn(NPC_372, playerPos, 0));
	if (moth) {
		moth->hidden = true;
		moth->timer = 70 * 60;
	}

	bool servers = false;
	for (int r = 0; r < w->map.nrooms; r++)
		if (strcmp(w->tpl[w->map.rooms[r].tpl].info.name, "room2servers") == 0) servers = true;
	if (r096 >= 0 && !servers) {
		float p[3] = { w->map.rooms[r096].gx * 8.0f, 0, -w->map.rooms[r096].gy * 8.0f };
		int k = npcSpawn(NPC_096, p, (float)(rand() % 360));
		if (npcGet(k)) npcGet(k)->frame = 936;
	}
}

const char* npcsUpdate(World* w, const PlayerView* pv, float dt)
{
	s_musicOverride = NULL;
	const char* death = NULL;
	for (int i = 0; i < MAX_NPCS; i++) {
		Npc* n = &s_npcs[i];
		if (!n->active) continue;
		const char* d = NULL;
		if (!n->placed && !n->scripted && (n->type == NPC_049 || n->type == NPC_096)) {
			int room = roomOf(w, n->pos);
			NavGrid* nav = navFor(w, room);
			if (nav) {
				float l[3], out[3];
				worldWorldToRoom(w, room, n->pos, l);
				if (navNearest(nav, l, out)) worldRoomToWorld(w, room, out, n->pos);
				n->placed = true;
			}
		}
		if (pv->dead && !n->scripted) {
			if (n->type == NPC_049 && n->state == 3) npcAnimate(n, 537, 660, 0.7f, false, dt);
			continue;
		}
		switch (n->type) {
		case NPC_106: d = n->scripted ? NULL : update106(w, n, pv, dt); break;
		case NPC_096: d = n->scripted ? NULL : update096(w, n, pv, dt); break;
		case NPC_049: d = n->scripted ? NULL : update049(w, n, pv, dt); break;
		case NPC_939: d = n->scripted ? NULL : update939(w, n, pv, dt); break;
		case NPC_966: d = n->scripted ? NULL : update966(w, n, pv, dt); break;
		case NPC_ZOMBIE:
		case NPC_SURGEON: d = n->scripted ? NULL : updateZombie(w, n, pv, dt); break;
		case NPC_MTF: d = n->scripted ? NULL : updateMTF(w, n, pv, dt); break;
		case NPC_066: d = update066(w, n, pv, dt); break;
		case NPC_372: d = update372(w, n, pv, dt); break;
		case NPC_5131: d = update5131(w, n, pv, dt); break;
		case NPC_1048A: d = n->scripted ? NULL : update1048a(w, n, pv, dt); break;
		case NPC_860:
		case NPC_1499: break;
		default: updateHuman(w, n, dt); break;
		}
		if (d && !death) death = d;
	}
	return death;
}

const char* npcsMusicOverride(void) { return s_musicOverride; }

void npcsDraw(const C3D_Mtx* view, int uLoc, C3D_Tex* white, const float cam[3], float viewDist)
{
	for (int i = 0; i < MAX_NPCS; i++) {
		Npc* n = &s_npcs[i];
		if (!n->active || n->hidden || !n->skin.model) continue;
		if (distXZ(n->pos, cam) > viewDist + 1.5f || fabsf(n->pos[1] - cam[1]) > 5.0f) continue;
		skinPose(&n->skin, n->frame);
		skinDraw(&n->skin, n->pos[0], n->pos[1], n->pos[2], n->yaw + s_yawOffset[n->type], view, uLoc, white);
	}
}

int npcDebugNext(const World* w, float pos[3], float* yaw, bool first)
{

	static const NpcType order[] = { NPC_939, NPC_966, NPC_066, NPC_106, NPC_096, NPC_049, NPC_MTF, NPC_GUARD, NPC_CLASSD };
	const int norder = (int)(sizeof(order) / sizeof(order[0]));
	static int step = -1;
	if (first) step = -1;
	for (int k = 0; k < norder; k++) {
		step = (step + 1) % norder;
		for (int i = 0; i < MAX_NPCS; i++) {
			Npc* n = &s_npcs[i];
			if (!n->active || n->dead || n->type != order[step]) continue;
			if (n->type == NPC_106 && n->state == S106_WAIT) {
				n->timer = 0;
				return NPC_106;
			}

			float head[3] = { n->pos[0], n->pos[1] + 1.0f, n->pos[2] };
			bool found = false;
			for (int dir = 0; dir < 8 && !found; dir++) {
				float f = (n->yaw + dir * 45.0f * (dir % 2 ? 1 : -1) * ((dir + 1) / 2) / (dir ? (float)((dir + 1) / 2) : 1)) * DEG2RAD;
				float c[3] = { n->pos[0] - sinf(f) * 2.5f, n->pos[1] + 0.35f, n->pos[2] - cosf(f) * 2.5f };
				float probe[3] = { c[0], c[1] + 0.5f, c[2] };
				float fy = worldRayDown(w, probe, 2.0f);
				float eye[3] = { c[0], c[1] + 0.6f, c[2] };
				if (isfinite(fy) && worldLineOfSight(w, eye, head)) {
					memcpy(pos, c, sizeof(c));
					found = true;
				}
			}
			if (!found) {
				float f = n->yaw * DEG2RAD;
				pos[0] = n->pos[0] - sinf(f) * 2.5f;
				pos[2] = n->pos[2] - cosf(f) * 2.5f;
				pos[1] = n->pos[1] + 0.35f;
			}
			*yaw = atan2f(n->pos[0] - pos[0], -(n->pos[2] - pos[2])) / DEG2RAD;
			return n->type;
		}
	}
	return -1;
}

void npcDebugInfo(const float pos[3], char* out, int size)
{
	static const char* names[NPC_TYPES] = { "106", "049", "096", "grd", "cD", "mtf", "939", "966",
	                                         "zmb", "008", "066", "372", "513", "1048", "035", "860", "1499" };
	int best = -1;
	float bd = 1e9f;
	for (int i = 0; i < MAX_NPCS; i++) {
		Npc* n = &s_npcs[i];
		if (!n->active || n->dead) continue;
		float d = dist3(n->pos, pos);
		if (d < bd) {
			bd = d;
			best = i;
		}
	}
	if (best < 0) {
		snprintf(out, size, "no npc");
		return;
	}
	Npc* n = &s_npcs[best];
	snprintf(out, size, "%s s%d f%.0f d%.1f%s%s", names[n->type], n->state, n->frame, bd, n->hidden ? " hid" : "",
	         n->type == NPC_096 ? s_dbg096 : "");
}

typedef struct {
	int type;
	float pos[3], yaw, frame, speed, currSpeed;
	int state;
	float state2, state3, timer, lastSeen;
	bool hidden, scripted, dead;
} NpcSave;

void npcsSave(FILE* f)
{
	int n = 0;
	for (int i = 0; i < MAX_NPCS; i++) n += s_npcs[i].active;
	fwrite(&n, sizeof(n), 1, f);
	for (int i = 0; i < MAX_NPCS; i++) {
		const Npc* p = &s_npcs[i];
		if (!p->active) continue;
		NpcSave v = { p->type, { p->pos[0], p->pos[1], p->pos[2] }, p->yaw, p->frame, p->speed, p->currSpeed, p->state,
		              p->state2, p->state3, p->timer, p->lastSeen, p->hidden, p->scripted, p->dead };
		fwrite(&v, sizeof(v), 1, f);
	}
	fwrite(&s_106Reload, sizeof(s_106Reload), 1, f);
}

bool npcsRestore(FILE* f)
{
	npcsReset();
	int n;
	if (fread(&n, sizeof(n), 1, f) != 1 || n < 0 || n > MAX_NPCS) return false;
	for (int k = 0; k < n; k++) {
		NpcSave v;
		if (fread(&v, sizeof(v), 1, f) != 1) return false;
		Npc* p = npcGet(npcSpawn((NpcType)v.type, v.pos, v.yaw));
		if (!p) continue;
		p->frame = v.frame;
		p->speed = v.speed;
		p->currSpeed = v.currSpeed;
		p->state = v.state;
		p->state2 = v.state2;
		p->state3 = v.state3;
		p->timer = v.timer;
		p->lastSeen = v.lastSeen;
		p->hidden = v.hidden;
		p->scripted = v.scripted;
		p->dead = v.dead;
		p->placed = true;
	}
	fread(&s_106Reload, sizeof(s_106Reload), 1, f);
	return true;
}

void npcsSpawn5131(const float playerPos[3])
{
	if (npcFind(NPC_5131)) return;
	Npc* n = npcGet(npcSpawn(NPC_5131, playerPos, 0));
	if (n) {
		n->hidden = true;
		n->timer = 70 * 10;
	}
}

void npcsCheckMTF(const World* w, const float playerPos[3])
{
	if (s_mtfSpawned || w->introActive || w->isolated) return;
	if (worldRoomZone(w, playerPos) < 3) return;

	int r = -1;
	for (int i = 0; i < w->map.nrooms && r < 0; i++)
		if (strcmp(w->tpl[w->map.rooms[i].tpl].info.name, "gateaentrance") == 0) r = i;
	if (r < 0) return;
	s_mtfSpawned = true;
	sfxPlay("character/mtf/announc", 1.0f);
	for (int k = 0; k < 3; k++) {
		float p[3] = { w->map.rooms[r].gx * 8.0f + k * 0.5f, 0, -w->map.rooms[r].gy * 8.0f };
		Npc* n = npcGet(npcSpawn(NPC_MTF, p, 0));
		if (n) {
			n->state = 0;
			n->timer = 70 * (10 + k * 7);
		}
	}
}
