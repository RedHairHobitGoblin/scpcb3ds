#include "intro.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "hud.h"
#include "items.h"
#include "npc.h"
#include "npc173.h"

#define RS (8.0f / 2048.0f)
#define DEG2RAD ((float)M_PI / 180.0f)

enum { I_OFF, I_WAKE, I_PAPER, I_DOOR, I_EXIT, I_ESCORT, I_CHAMBER, I_INSIDE, I_BLACKOUT, I_ESCAPE, I_DONE };

static struct {
	int stage;
	float t;
	float total;
	int ulgrin, guard2, guardFar, balcony, classD[2];
	int cellDoor, chamberDoor;
	float doorWait;
	int chanUlgrin, chanUlgrin2, chanGuard2, chanFar, chanPA, chanAmb;
	int strays;
	float strayTime;
	bool convoDone, see173, refused1, refused2;
	float nextRefuse;
} I;

bool introRunning(void) { return I.stage != I_OFF && I.stage != I_DONE; }

static void L(const World* w, float x, float y, float z, float out[3])
{
	float local[3] = { x * RS, y * RS, -z * RS };
	worldRoomToWorld(w, w->introRoom, local, out);
}

static int doorNear(const World* w, const float p[3])
{
	int best = -1;
	float bd = 0.6f;
	for (int i = 0; i < w->ndoors; i++) {
		float dx = w->doors[i].x - p[0], dz = w->doors[i].z - p[2];
		float d = sqrtf(dx * dx + dz * dz);
		if (d < bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

static int say(int npc, const char* snd, float range)
{
	Npc* n = npcGet(npc);
	if (!n) return sfxPlay(snd, 1.0f);
	float p[3] = { n->pos[0], n->pos[1] + 1.0f, n->pos[2] };
	return sfxPlay3D(snd, p, range, 1.0f);
}

static int sayRand(int npc, const char* fmt, int lo, int hi, float range)
{
	char s[96];
	snprintf(s, sizeof(s), fmt, lo + rand() % (hi - lo + 1));
	return say(npc, s, range);
}

static int spawnHuman(NpcType t, const float p[3], float yaw)
{
	int i = npcSpawn(t, p, yaw);
	Npc* n = npcGet(i);
	if (n) {
		n->scripted = true;
		n->frame = t == NPC_GUARD ? 77 : 210;
	}
	return i;
}

static void idleAnim(Npc* n, float dt)
{
	if (!n || n->dead) return;
	if (n->type == NPC_GUARD) npcAnimate(n, 77, 201, 0.2f, true, dt);
	else npcAnimate(n, 210, 235, 0.1f, true, dt);
}

static int doorAcross(const World* w, const float pos[3], float fx, float fz, float reach)
{
	for (int d = 0; d < w->ndoors; d++) {
		const Door* door = &w->doors[d];

		if (d == I.chamberDoor || door->openstate >= 180 || door->room != w->introRoom) continue;
		float c = cosf(door->angle * DEG2RAD), sn = sinf(door->angle * DEG2RAD);

		float ax = pos[0] - door->x, az = pos[2] - door->z;
		float bx = ax + fx * reach, bz = az + fz * reach;
		float az0 = ax * sn + az * c, az1 = bx * sn + bz * c;
		if ((az0 > 0) == (az1 > 0)) continue;
		float t = az0 / (az0 - az1);
		float hitx = (ax + (bx - ax) * t) * c - (az + (bz - az) * t) * sn;
		if (fabsf(hitx) < (door->type == DOOR_BIG ? 1.2f : 0.7f)) return d;
	}
	return -1;
}

static bool walk(World* w, int idx, const float target[3], float dt)
{
	Npc* n = npcGet(idx);
	if (!n || n->dead) return true;
	n->currSpeed = n->type == NPC_GUARD ? 0.015f : 0.014f;
	float before[3] = { n->pos[0], n->pos[1], n->pos[2] }, yaw = n->yaw;
	bool arrived = npcWalkTo(w, n, target, n->currSpeed * 70.0f, dt);
	float mx = n->pos[0] - before[0], mz = n->pos[2] - before[2], ml = sqrtf(mx * mx + mz * mz);
	int d = ml > 1e-5f ? doorAcross(w, before, mx / ml, mz / ml, 0.9f) : -1;
	if (d >= 0 && I.doorWait < 3.0f) {
		I.doorWait += dt;
		memcpy(n->pos, before, sizeof(before));
		n->yaw = yaw;
		Door* door = &w->doors[d];
		if (!door->open) {
			door->locked = false;
			worldToggleDoor(w, d);
		}
		idleAnim(n, dt);
		return false;
	}
	if (d < 0) I.doorWait = 0;
	if (n->type == NPC_GUARD) npcAnimate(n, 1, 38, n->currSpeed * 40, true, dt);
	else npcAnimate(n, 236, 260, n->currSpeed * 18, true, dt);
	if (arrived) idleAnim(n, dt);
	return arrived;
}

static float playerDist(const IntroPlayer* p, int idx)
{
	Npc* n = npcGet(idx);
	if (!n) return 1e9f;
	float dx = n->pos[0] - p->pos[0], dz = n->pos[2] - p->pos[2];
	return sqrtf(dx * dx + dz * dz);
}

static void localOf(const World* w, const float world[3], float out[3])
{
	float l[3];
	worldWorldToRoom(w, w->introRoom, world, l);
	out[0] = l[0] / RS;
	out[1] = l[1] / RS;
	out[2] = -l[2] / RS;
}

void introStart(World* w, IntroPlayer* p)
{
	memset(&I, 0, sizeof(I));
	I.chanUlgrin = I.chanUlgrin2 = I.chanGuard2 = I.chanFar = I.chanPA = I.chanAmb = -1;
	if (w->introRoom < 0) {
		I.stage = I_DONE;
		return;
	}
	I.stage = I_WAKE;
	npcsReset();

	L(w, -(3072 + 1024), 0, 192, p->pos);
	p->pos[1] = 0.3f;
	*p->yaw = 180;
	*p->pitch = -70;
	*p->fade = 1.0f;
	*p->frozen = true;

	float pos[3];
	L(w, -4096 + (rand() % 154 - 77), 0, 870, pos);
	I.ulgrin = spawnHuman(NPC_GUARD, pos, 0);
	L(w, -3840, 0, 768, pos);
	I.guard2 = spawnHuman(NPC_GUARD, pos, 0);
	L(w, -8288, 0, 1096, pos);
	I.guardFar = spawnHuman(NPC_GUARD, pos, 0);
	L(w, 40, 460, 1072, pos);
	I.balcony = spawnHuman(NPC_GUARD, pos, 90);
	I.classD[0] = I.classD[1] = -1;
	Npc* b = npcGet(I.balcony);
	if (b) b->pos[1] = pos[1];

	L(w, -4096, 0, 528, pos);
	I.cellDoor = doorNear(w, pos);
	L(w, 288, 0, 384, pos);
	I.chamberDoor = doorNear(w, pos);
	if (I.cellDoor >= 0) {
		w->doors[I.cellDoor].open = false;
		w->doors[I.cellDoor].openstate = 0;
	}
	if (I.chamberDoor >= 0) {
		w->doors[I.chamberDoor].open = false;
		w->doors[I.chamberDoor].openstate = 0;
		w->doors[I.chamberDoor].noButtons = true;
	}

	L(w, 1472, 0, 912, pos);
	npc173Place(pos, 180);
	npc173SetIdle(true);
	musicPlay("music/the dread", 0.5f);
}

static float yawTo3(const float from[3], const float to[3])
{
	return atan2f(-(to[0] - from[0]), -(to[2] - from[2])) / DEG2RAD;
}

static void finishIntoStartRoom(World* w, IntroPlayer* p)
{
	int start = -1;
	for (int i = 0; i < w->map.nrooms; i++)
		if (strcmp(w->tpl[w->map.rooms[i].tpl].info.name, "start") == 0) start = i;

	float l[3], wp[3], p173[3];
	worldWorldToRoom(w, w->introRoom, p->pos, l);
	npc173GetPos(p173);
	float l173[3];
	worldWorldToRoom(w, w->introRoom, p173, l173);
	float dead[3][3];
	int deadIdx[3] = { I.classD[0], I.classD[1], I.balcony };
	for (int k = 0; k < 3; k++) {
		Npc* n = npcGet(deadIdx[k]);
		if (n) worldWorldToRoom(w, w->introRoom, n->pos, dead[k]);
	}

	w->introActive = false;
	npcsReset();
	if (start < 0) {
		worldSpawnPoint(w, p->pos, p->yaw);
	} else {
		worldRoomToWorld(w, start, l, wp);
		memcpy(p->pos, wp, sizeof(wp));
		*p->yaw += w->map.rooms[start].angle;
		worldRoomToWorld(w, start, l173, wp);

		float c[3] = { wp[0], wp[1] + 0.5f, wp[2] };
		for (int k = 0; k < 3; k++) worldCollide(w, c);
		wp[0] = c[0];
		wp[2] = c[2];
		wp[1] = w->map.rooms[start].y;
		npc173Place(wp, yawTo3(wp, p->pos));

		for (int k = 0; k < 3; k++) {
			if (deadIdx[k] < 0) continue;
			worldRoomToWorld(w, start, dead[k], wp);
			int i = npcSpawn(k < 2 ? NPC_CLASSD : NPC_GUARD, wp, (float)(rand() % 360));
			Npc* n = npcGet(i);
			if (n) {
				n->scripted = true;
				n->dead = true;
				n->frame = k < 2 ? 19 : 286;
				if (k == 2) n->pos[1] = wp[1] - 460 * RS;
			}
		}
	}
	worldStream(w, p->pos);
	npcsSpawnFacility(w, p->pos);
	I.stage = I_ESCAPE;
	I.t = 0;
	*p->frozen = false;
	*p->fade = 0;
	*p->blinkTimer = 455;
}

void introFinish(World* w, IntroPlayer* p)
{
	if (introRunning()) finishIntoStartRoom(w, p);
}

const char* introUpdate(World* w, IntroPlayer* p, float dt)
{
	if (!introRunning()) return NULL;
	I.t += dt;
	I.total += dt;
	if (I.stage == I_ESCAPE) {
		if (I.t > 4.0f) {
			sfxPlay("room/intro/light2", 1);
			sfxPlay("scp/173/stonedrag", 0.8f);
			*p->blinkTimer = -10;
			npc173Spawn(w, p->pos);
			hudMessage("Hold Y to run.", 6);
			I.stage = I_DONE;
		}
		return NULL;
	}
	float bp[3];
	localOf(w, p->pos, bp);

	Npc* far = npcGet(I.guardFar);
	if (far) {
		idleAnim(far, dt);
		if (I.stage < I_CHAMBER) {
			char snd[48];
			if (I.chanFar < 0 || !sfxPlaying(I.chanFar)) {
				snprintf(snd, sizeof(snd), "room/intro/guard/music%d", 1 + rand() % 5);
				float fp[3] = { far->pos[0], far->pos[1] + 1, far->pos[2] };
				I.chanFar = sfxPlay3D(snd, fp, 6.0f, 0.5f);
			}
		}
	}
	idleAnim(npcGet(I.balcony), dt);

	if (!I.see173 && I.stage >= I_ESCORT) {
		float s[3];
		npc173GetPos(s);
		s[1] += 0.6f;
		float dx = s[0] - p->eye[0], dz = s[2] - p->eye[2];
		if (dx * dx + dz * dz < 64 && worldLineOfSight(w, p->eye, s)) {
			float yr = *p->yaw * DEG2RAD;
			if (sinf(yr) * dx - cosf(yr) * dz > 0) {
				sfxPlay("room/intro/see173", 1.0f);
				hudMessage("Press R to blink.", 6);
				I.see173 = true;
			}
		}
	}

	switch (I.stage) {
	case I_WAKE:
		*p->fade = fmaxf(0.0f, 1.0f - I.t / 3.0f);
		*p->pitch = -70.0f + 70.0f * fminf(1.0f, I.t / 4.0f);
		idleAnim(npcGet(I.ulgrin), dt);
		idleAnim(npcGet(I.guard2), dt);
		if (I.t > 4.5f) {
			*p->frozen = false;
			hudMessage("Pick up the paper on the desk.", 8);
			I.stage = I_PAPER;
			I.t = 0;
		}
		break;

	case I_PAPER:
		idleAnim(npcGet(I.ulgrin), dt);
		idleAnim(npcGet(I.guard2), dt);
		if (itemsHave("paper") || I.t > 25.0f) {
			if (itemsHave("paper")) hudMessage("Tap an item on the touch screen to hold it, then USE to read it.", 8);
			I.stage = I_DOOR;
			I.t = 0;
			I.chanUlgrin = say(I.ulgrin, "room/intro/guard/ulgrin/beforedooropen", 10);
		}
		break;

	case I_DOOR:
		idleAnim(npcGet(I.ulgrin), dt);
		idleAnim(npcGet(I.guard2), dt);
		if ((!sfxPlaying(I.chanUlgrin) && I.t > 2.0f) || I.t > 12.0f) {
			if (I.cellDoor >= 0) {
				w->doors[I.cellDoor].locked = false;
				worldToggleDoor(w, I.cellDoor);
			}
			I.chanUlgrin = say(I.ulgrin, "room/intro/guard/ulgrin/exitcell", 10);
			hudMessage("Step out of the cell.", 6);
			I.stage = I_EXIT;
			I.t = 0;
		}
		break;

	case I_EXIT:
		idleAnim(npcGet(I.ulgrin), dt);
		idleAnim(npcGet(I.guard2), dt);
		if (npcGet(I.ulgrin)) npcFace(npcGet(I.ulgrin), p->pos, 3, dt);
		if (bp[2] > 600) {
			I.chanUlgrin = sayRand(I.ulgrin, "room/intro/guard/ulgrin/escort%d", 1, 2, 10);
			hudMessage("Follow the guard.", 6);
			I.stage = I_ESCORT;
			I.t = 0;
		} else if (I.t > 15 && !I.refused1) {
			I.refused1 = true;
			I.chanUlgrin = sayRand(I.ulgrin, "room/intro/guard/ulgrin/exitcellrefuse%d", 1, 2, 10);
		} else if (I.t > 30 && !I.refused2) {
			I.refused2 = true;
			I.chanUlgrin = sayRand(I.ulgrin, "room/intro/guard/ulgrin/cellgas%d", 1, 2, 10);
		}
		break;

	case I_ESCORT: {
		float dest[3], behind[3];
		L(w, -320, 0, -704, dest);
		Npc* u = npcGet(I.ulgrin);
		float d = playerDist(p, I.ulgrin);
		bool arrived;
		if (d < 5.0f) {
			arrived = walk(w, I.ulgrin, dest, dt);
			I.strayTime = fmaxf(0, I.strayTime - dt);
		} else {

			idleAnim(u, dt);
			if (u) npcFace(u, p->pos, 3, dt);
			arrived = false;
			I.strayTime += dt;
			if (I.strayTime > 6 && I.strays == 0) {
				I.strays = 1;
				I.chanUlgrin = sayRand(I.ulgrin, "room/intro/guard/ulgrin/escortrefuse%d", 1, 2, 12);
			} else if (I.strayTime > 14 && I.strays == 1) {
				I.strays = 2;
				I.chanUlgrin = sayRand(I.ulgrin, "room/intro/guard/ulgrin/escortpissedoff%d", 1, 2, 12);
			} else if (I.strayTime > 22 && I.strays == 2) {
				sayRand(I.ulgrin, "room/intro/guard/ulgrin/escortkill%d", 1, 2, 12);
				sfxPlay("general/gunshot", 1.0f);
				I.stage = I_DONE;
				return "Subject D-9341 was shot by security for refusing to follow the escort.";
			}
		}

		if (u) {
			float yr = *p->yaw * DEG2RAD;
			behind[0] = p->pos[0] + sinf(yr) * 1.6f;
			behind[1] = p->pos[1];
			behind[2] = p->pos[2] - cosf(yr) * 1.6f;
			if (playerDist(p, I.guard2) > 2.2f) walk(w, I.guard2, behind, dt);
			else idleAnim(npcGet(I.guard2), dt);
		}
		if (!I.convoDone && I.t > 8 && !sfxPlaying(I.chanUlgrin)) {
			int c = 1 + rand() % 5;
			char a[64], b[64];
			snprintf(a, sizeof(a), "room/intro/guard/conversation%da", c);
			snprintf(b, sizeof(b), "room/intro/guard/conversation%db", c);
			I.chanUlgrin = say(I.ulgrin, a, 10);
			I.chanGuard2 = say(I.guard2, b, 10);
			I.convoDone = true;
		}
		if (arrived && d < 4.0f) {
			I.chanUlgrin = sayRand(I.ulgrin, "room/intro/guard/ulgrin/escortdone%d", 1, 5, 12);
			float a[3], b[3];
			L(w, -80, 0, 526, a);
			L(w, -128, 0, 320, b);
			I.classD[0] = spawnHuman(NPC_CLASSD, a, 90);
			I.classD[1] = spawnHuman(NPC_CLASSD, b, 90);
			I.stage = I_CHAMBER;
			I.t = 0;
		}
		break;
	}

	case I_CHAMBER: {
		float frames = I.t * 70.0f / 3.0f;
		idleAnim(npcGet(I.ulgrin), dt);
		idleAnim(npcGet(I.guard2), dt);
		sfxLoop3D(&I.chanAmb, "room/intro/173chamber", (float[3]){ 0, 1, 0 }, 1e9f, 0.4f);
		if (frames >= 130 && I.chanPA < 0) {
			I.chanPA = sfxPlay("room/intro/scientist/franklin/enterchamber", 1);
			hudMessage("Enter the containment chamber.", 8);
		}
		if (frames >= 120 && I.chamberDoor >= 0 && !w->doors[I.chamberDoor].open) worldToggleDoor(w, I.chamberDoor);
		for (int k = 0; k < 2; k++) {
			if (frames < 200 + k * 30) {
				idleAnim(npcGet(I.classD[k]), dt);
				continue;
			}
			float t[3];
			if (k == 0) L(w, 660, 0, 526, t);
			else L(w, 700, 0, 320, t);
			walk(w, I.classD[k], t, dt);
		}
		if (frames >= 450 && I.nextRefuse < 1) {
			I.nextRefuse = 1;
			I.chanPA = sfxPlay("room/intro/scientist/franklin/refuse1", 1);
			hudMessage("Enter the containment chamber.", 8);
		} else if (frames >= 650 && I.nextRefuse < 2) {
			I.nextRefuse = 2;
			I.chanPA = sfxPlay("room/intro/scientist/franklin/refuse2", 1);
			hudMessage("Last warning: enter the chamber.", 8);
		} else if (frames >= 850 && I.nextRefuse < 3) {
			I.nextRefuse = 3;
			I.chanPA = sfxPlay("room/intro/scientist/franklin/refuse3", 1);
			hudMessage("The guards have been ordered to open fire.", 6);
		} else if (frames > 1000) {
			sfxPlay("general/gunshot", 1.0f);
			I.stage = I_DONE;
			return "Subject D-9341 was shot by security for refusing to enter SCP-173's chamber.";
		}
		if (bp[0] > 288 + 120) {
			I.stage = I_INSIDE;
			I.t = 0;
		}
		break;
	}

	case I_INSIDE: {
		float f = I.t * 70.0f;
		idleAnim(npcGet(I.ulgrin), dt);
		idleAnim(npcGet(I.guard2), dt);
		for (int k = 0; k < 2; k++) idleAnim(npcGet(I.classD[k]), dt);
		sfxLoop3D(&I.chanAmb, "room/intro/173chamber", (float[3]){ 0, 1, 0 }, 1e9f, 0.4f);
#define AT(x) (f >= (x) && f - dt * 70.0f < (x))
		if (AT(300)) I.chanPA = sfxPlay("room/intro/scientist/franklin/approach173", 1);
		if (AT(440)) {
			sfxPlay("room/intro/bang1", 1);
			if (I.chamberDoor >= 0 && w->doors[I.chamberDoor].open) worldToggleDoor(w, I.chamberDoor);
			sayRand(I.balcony, "room/intro/guard/balcony/alert%d", 1, 2, 20);
		}
		if (AT(740)) I.chanPA = sfxPlay("room/intro/scientist/franklin/problem", 1);
		if (AT(1145)) {
			sfxPlay("room/intro/bang2", 1);
			say(I.classD[1], "room/intro/classd/dontlikethis", 8);
		}
		if (AT(1561)) {
			sfxPlay("room/intro/horror", 1);
			say(I.classD[0], "room/intro/classd/breen", 8);
			I.stage = I_BLACKOUT;
			I.t = 0;
		}
#undef AT
		break;
	}

	case I_BLACKOUT: {
		float f = I.t * 70.0f, pf = f - dt * 70.0f;
		sfxStop(I.chanAmb);
		I.chanAmb = -1;
		*p->blinkTimer = -10;
		Npc* d0 = npcGet(I.classD[0]);
		Npc* d1 = npcGet(I.classD[1]);
		if (pf < 60 && f >= 60 && d0) {
			float s[3] = { d0->pos[0], d0->pos[1], d0->pos[2] };
			npc173Place(s, 0);
			sayRand(I.classD[0], "scp/173/necksnap%d", 1, 3, 10);
			d0->dead = true;
			say(I.classD[1], "room/intro/classd/gasp", 8);
			sayRand(I.balcony, "room/intro/guard/balcony/wtf%d", 1, 2, 20);
		}
		if (pf < 105 && f >= 105 && d1) {
			float s[3] = { d1->pos[0], d1->pos[1], d1->pos[2] };
			npc173Place(s, 0);
			sfxPlay("scp/173/necksnap2", 1.0f);
			d1->dead = true;
		}
		if (pf < 100 && f >= 100) sfxPlay("room/intro/light2", 1);
		if (f > 130) *p->fade = 0.6f + 0.4f * (rand() % 2);
		if (f > 190) finishIntoStartRoom(w, p);
		break;
	}
	}
	return NULL;
}

void introDebugToChamber(World* w, IntroPlayer* p)
{
	if (I.stage > I_ESCORT) return;
	float dest[3];
	L(w, -320, 0, -704, dest);
	Npc* u = npcGet(I.ulgrin);
	if (u) {
		memcpy(u->pos, dest, sizeof(dest));
		npcSnapToFloor(w, u);
	}
	L(w, -320, 0, -400, p->pos);
	p->pos[1] = 0.3f;
	*p->frozen = false;
	*p->fade = 0;
	I.stage = I_ESCORT;
	I.t = 0;
	worldStream(w, p->pos);
}

void introDebugFollow(World* w, IntroPlayer* p, char* out, int size)
{
	Npc* u = npcGet(I.ulgrin);
	if (I.stage != I_ESCORT || !u) return;
	float yr = u->yaw * DEG2RAD;
	p->pos[0] = u->pos[0] + sinf(yr) * 2.0f;
	p->pos[2] = u->pos[2] - cosf(yr) * 2.0f;
	p->pos[1] = u->pos[1] + 0.3f;
	float l[3];
	localOf(w, u->pos, l);
	snprintf(out, size, "ulgrin %.0f,%.0f wait %.1f f%.0f", l[0], l[2], I.doorWait, u->frame);
}
