#include "pd.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "hud.h"
#include "npc.h"

#define RS (8.0f / 2048.0f)
#define DEG2RAD ((float)M_PI / 180.0f)
#define FRAMES(dt) ((dt) * 70.0f)

#define PD_X (-400.0f)
#define PD_Z (400.0f)

enum { PD_START = 0, PD_THRONE = 1 };

static Room s_start, s_hall, s_throne, s_pillar, s_tower, s_terrain;
static bool s_loaded;

static struct {
	bool active;
	float t;
	int stage;
	int trick;
	float eyesTime;
	bool inTrench;
	float planeX;
	int pillar[2];
	int chanRumble, chanVoices;
	const char* music;
} S;

bool pdActive(void) { return S.active; }

void pdReset(World* w)
{
	S.active = false;
	w->isolated = false;
	worldClearExtras(w);
}
const char* pdMusic(void) { return S.active ? S.music : NULL; }

void pdLoad(void)
{
	if (s_loaded) return;
	s_loaded = roomLoad(&s_start, "romfs:/rooms/pocketdimension.scr");
	roomLoad(&s_hall, "romfs:/models/pd_hall.scr");
	roomLoad(&s_throne, "romfs:/models/pd_throne.scr");
	roomLoad(&s_pillar, "romfs:/models/pd_pillar.scr");
	roomLoad(&s_tower, "romfs:/models/pd_tower.scr");
	roomLoad(&s_terrain, "romfs:/models/pd_terrain.scr");
}

static void P(float bx, float by, float bz, float out[3])
{
	out[0] = PD_X + bx;
	out[1] = by;
	out[2] = PD_Z - bz;
}

static void throne(float dx, float dy, float dz, float out[3]) { P(dx, dy, 32.0f + dz, out); }

static void teleport(PdPlayer* p, const float to[3])
{
	memcpy(p->pos, to, sizeof(float) * 3);
	*p->blinkTimer = -10;
}

static float distXZ(const float* a, const float* b)
{
	float dx = a[0] - b[0], dz = a[2] - b[2];
	return sqrtf(dx * dx + dz * dz);
}

static Npc* oldMan(void)
{
	for (int i = 0; i < MAX_NPCS; i++) {
		Npc* n = npcGet(i);
		if (n && n->type == NPC_106) return n;
	}
	return NULL;
}

static void build(World* w)
{
	worldClearExtras(w);
	float p[3];
	P(0, 0, 0, p);
	worldAddExtra(w, &s_start, p[0], p[1], p[2], 0, true);
	for (int i = 1; i <= 8; i++) {
		float angle = (i - 1) * (360.0f / 8.0f);
		P(cosf(angle * DEG2RAD) * 512 * RS, 0, sinf(angle * DEG2RAD) * 512 * RS, p);
		worldAddExtra(w, &s_hall, p[0], p[1], p[2], angle - 90, true);
	}
	throne(0, 0, 0, p);
	worldAddExtra(w, &s_throne, p[0], p[1], p[2], 0, true);
	S.pillar[0] = worldAddExtra(w, &s_pillar, p[0], p[1], p[2], 0, false);
	S.pillar[1] = worldAddExtra(w, &s_pillar, p[0], p[1], p[2], 0, false);
	if (S.pillar[1] >= 0) {
		w->extras[S.pillar[1]].sx = 1.5f;
		w->extras[S.pillar[1]].sy = 2.0f;
		w->extras[S.pillar[1]].sz = 1.5f;
	}
	P(0, 0, 64.0f, p);
	worldAddExtra(w, &s_tower, p[0], p[1], p[2], 0, true);
	P(0, 2944 * RS, 32.0f, p);
	worldAddExtra(w, &s_terrain, p[0], p[1], p[2], 0, true);
}

static void towerPivot(int i, float out[3])
{
	static const float pv[5][3] = {
		{ 0, 200, 0 }, { 390, 200, 272 }, { 838, 200, -551 }, { -139, 200, 1201 }, { -1238, -1664, 381 },
	};
	const float* v = pv[i - 12];
	P(v[0] * RS, v[1] * RS, 64.0f + v[2] * RS, out);
}

void pdEnter(World* w, PdPlayer* p)
{
	pdLoad();
	memset(&S, 0, sizeof(S));
	S.active = true;
	S.chanRumble = S.chanVoices = -1;
	S.t = 0.1f;
	S.music = "music/pd";
	w->isolated = true;
	build(w);

	sfxStopAll();
	sfxPlay("room/pocketdimension/enter", 1.0f);
	sfxPlay("scp/106/laugh", 1.0f);
	float start[3];
	P(0, 0.8f, 0, start);
	teleport(p, start);
	*p->injuries += 0.5f;
	hudMessage("", 0);

	Npc* n = oldMan();
	if (n) {
		n->scripted = true;
		n->hidden = false;
		n->state = 1;
		n->frame = 55;
	}
}

static void leave(World* w, PdPlayer* p, const char* roomName)
{

	int target = -1;
	for (int i = 0; i < w->map.nrooms; i++) {
		const MapRoomPlacement* r = &w->map.rooms[i];
		if (r->offGrid) continue;
		if (strcmp(w->tpl[r->tpl].info.name, roomName) == 0) target = i;
	}
	if (target < 0)
		for (int tries = 0; tries < 50 && target < 0; tries++) {
			int i = rand() % w->map.nrooms;
			if (!w->map.rooms[i].offGrid) target = i;
		}
	S.active = false;
	w->isolated = false;
	w->fogColor = 0;
	worldClearExtras(w);
	sfxStopAll();
	sfxPlay("room/pocketdimension/exit", 1.0f);
	float to[3] = { 0, 0.4f, 0 };
	if (target >= 0) {
		to[0] = w->map.rooms[target].gx * 8.0f;
		to[2] = -w->map.rooms[target].gy * 8.0f;
	}
	teleport(p, to);
	worldStream(w, p->pos);

	float probe[3] = { to[0], 0.9f, to[2] };
	float fy = worldRayDown(w, probe, 3.0f);
	if (isfinite(fy)) p->pos[1] = fy + 0.35f;
	*p->fade = 0;

	Npc* n = oldMan();
	if (n) {
		n->scripted = false;
		n->hidden = true;
		n->state = 0;
		n->timer = 10000;
	}
}

static const char* pillarRoom(World* w, PdPlayer* p, float dt)
{
	(void)dt;
	float th[3];
	throne(0, 0, 0, th);

	float t = S.t;
	for (int k = 0; k < 2; k++) {
		WorldExtra* e = &w->extras[S.pillar[k]];
		float bx = k == 0 ? 3384 * RS + cosf(t * 0.8f * DEG2RAD) * 5 : sinf(t * 1.6f * DEG2RAD) * 4;
		float bz = k == 0 ? sinf(t * 1.6f * DEG2RAD) * 4 : 3384 * RS + cosf(t * 0.8f * DEG2RAD) * 5;
		e->x = th[0] + bx;
		e->z = th[2] - bz;
		e->y = 0;
		e->yaw = t * 2;
	}

	if (S.trick == 1 || S.trick == 2) {
		if (p->pos[1] < 5.0f) S.trick = 0;
		return NULL;
	}

	if (p->pos[1] > 6.0f) {
		S.music = "music/pdtrench";
		w->fogColor = 0x26372F;
		if (!S.inTrench) {
			S.inTrench = true;
			S.planeX = p->pos[0] + 4000 * RS;
			sfxPlay("room/pocketdimension/trenchplane", 1.0f);
		}
		S.planeX -= FRAMES(dt) * RS * 4.0f;
		if (S.planeX < th[0] - 4000 * RS) S.planeX = p->pos[0] + 4000 * RS;

		static const float safeSpots[3][2] = { { -1452, -37 }, { -121, 188 }, { 1223, -196 } };
		bool safe = false;
		for (int i = 0; i < 3; i++) {
			float s[3];
			throne(safeSpots[i][0] * RS, 0, safeSpots[i][1] * RS, s);
			if (distXZ(p->pos, s) < 200 * RS) safe = true;
		}
		float dist = fabsf(p->pos[0] - S.planeX);
		if (!safe && dist < 8.0f) {
			*p->injuries += (8.0f - dist) * FRAMES(dt) * 0.0003f;
			sfxLoop3D(&S.chanRumble, "room/pocketdimension/rumble", (float[3]){ S.planeX, 12, p->pos[2] }, 8.0f, 1.0f);
		}
		if (p->pos[1] < 8.5f) {
			S.inTrench = false;
			w->fogColor = 0;
			S.music = "music/pd";
			float to[3];
			throne(-400 * RS, -304 * RS, 0, to);
			teleport(p, to);
		}
		return NULL;
	}
	S.music = "music/pd";

	for (int k = 0; k < 2; k++) {
		WorldExtra* e = &w->extras[S.pillar[k]];
		float ep[3] = { e->x, 0, e->z };
		float dist = distXZ(p->pos, ep);
		if (dist < 6.0f) sfxLoop3D(&S.chanRumble, "room/pocketdimension/rumble", ep, 6.0f, 1.0f);
		if (dist < 100 * RS) {
			sfxPlay("room/pocketdimension/impact", 1.0f);
			return "In addition to the decomposed appearance typical of SCP-106's victims, the body exhibits injuries that "
			       "have not been observed before: massive skull fracture, three broken ribs, fractured shoulder and multiple "
			       "heavy lacerations.";
		}
	}

	float eyes[3];
	throne(0, 1376 * RS, -2848 * RS, eyes);
	float d = sqrtf((p->eye[0] - eyes[0]) * (p->eye[0] - eyes[0]) + (p->eye[1] - eyes[1]) * (p->eye[1] - eyes[1]) +
	                (p->eye[2] - eyes[2]) * (p->eye[2] - eyes[2]));
	if (d < 2000 * RS) {
		*p->injuries += FRAMES(dt) / 4000.0f;
		S.eyesTime += FRAMES(dt) / 1000.0f;
		if (S.eyesTime > 1.0f && S.eyesTime < 1000.0f) {
			sfxPlay("room/pocketdimension/kneel", 1.0f);
			sfxPlay("room/pocketdimension/screech", 1.0f);
			S.eyesTime = 1000.0f;
		}
		float want = atan2f(eyes[0] - p->eye[0], -(eyes[2] - p->eye[2])) / DEG2RAD;
		float dy = fmodf(want - *p->yaw + 540.0f, 360.0f) - 180.0f;
		*p->yaw += dy * fminf(1.0f, dt * 2.0f);
		if (*p->crouch) {
			float to[3];
			throne(-1344 * RS, 2944 * RS, -1184 * RS, to);
			teleport(p, to);
			*p->crouch = false;
			sfxPlay("room/pocketdimension/explosion", 1.0f);
			S.eyesTime = 0;
		}
	} else if (p->pos[1] < -180 * RS) {
		float ex[3];
		throne(1024 * RS, 0, 0, ex);
		float tdist = distXZ(p->pos, ex);
		if (tdist < 640 * RS) *p->fade = fminf(0.8f, (640 * RS - tdist) * 1.5f);
		if (tdist < 130 * RS) {
			leave(w, p, "room2shaft");
			return NULL;
		}
	}

	if (p->pos[1] < -1600 * RS) {
		if (distXZ(p->pos, th) > 4750 * RS) {
			leave(w, p, "room106");
			return NULL;
		}
		sfxPlay("horror/horror8", 1.0f);
		return "In addition to the decomposed appearance typical of the victims of SCP-106, the subject seems to have suffered "
		       "multiple heavy fractures to both of his legs.";
	}
	return NULL;
}

const char* pdUpdate(World* w, PdPlayer* p, float dt)
{
	if (!S.active) return NULL;
	float frames = FRAMES(dt);
	S.t += frames;
	*p->injuries += frames * 0.00005f;
	if (*p->fade > 0) *p->fade = fmaxf(0, *p->fade - dt);

	float centre[3];
	P(0, 0, 0, centre);
	Npc* n = oldMan();

	if (S.stage == PD_START) {
		if (n && n->scripted) {
			float angle = fmodf(S.t / 10.0f, 360.0f);
			float r = 6.0f - sinf(S.t / 10.0f * DEG2RAD);
			n->pos[0] = centre[0] - sinf(angle * DEG2RAD) * r;
			n->pos[2] = centre[2] - cosf(angle * DEG2RAD) * r;
			n->pos[1] = 0.2f + 0.35f + sinf((S.t / 14.0f + 160) * DEG2RAD) * 0.4f - 0.55f;
			n->yaw = angle + 90;
			npcAnimate(n, 55, 104, 0.5f, true, dt);
			if (S.t > 65 * 70 && rand() % 800 == 0) {
				sfxPlay("horror/horror8", 1.0f);
				n->scripted = false;
				n->state = 2;
			}
		}
		if (distXZ(p->pos, centre) > 1700 * RS) {
			*p->blinkTimer = -10;
			int c = 1 + rand() % 25;
			float to[3];
			if (c <= 4) {
				float dx = centre[0] - p->pos[0], dz = centre[2] - p->pos[2];
				to[0] = p->pos[0] + dx * 1.9f;
				to[1] = p->pos[1];
				to[2] = p->pos[2] + dz * 1.9f;
				sfxPlay("scp/106/laugh", 1.0f);
				teleport(p, to);
			} else if (c <= 10) {
				throne(0, 0.5f, 0, to);
				S.stage = PD_THRONE;
				sfxPlay("scp/106/laugh", 1.0f);
				teleport(p, to);
			} else if (c <= 12) {
				P(0, 0.5f, 0, to);
				teleport(p, to);
			} else if (c <= 15) {
				throne(-400 * RS, -304 * RS, 0, to);
				S.stage = PD_THRONE;
				teleport(p, to);
			} else if (c <= 19) {
				leave(w, p, "tunnel");
				return NULL;
			} else if (c <= 22) {
				towerPivot(12, to);
				to[1] = 0.6f;
				S.stage = 15;
				teleport(p, to);
			} else {
				throne(0, 2288 * RS, 0, to);
				S.stage = PD_THRONE;
				S.trick = 1;
				sfxPlay("scp/106/laugh", 1.0f);
				teleport(p, to);
			}
		}
	} else if (S.stage == PD_THRONE) {
		const char* d = pillarRoom(w, p, dt);
		if (d || !S.active) return d;
	} else {
		if (n && S.stage > 12) {
			float pv[3];
			towerPivot(S.stage, pv);
			n->scripted = true;
			n->hidden = false;
			n->pos[0] = pv[0];
			n->pos[1] = 0.27f - 0.15f;
			n->pos[2] = pv[2];
			npcFace(n, p->pos, 20.0f, dt);
			if (rand() % 750 == 0) {
				*p->blinkTimer = -10;
				S.stage--;
				sfxPlay("horror/horror8", 1.0f);
			}
			if (S.stage == 12) {
				n->scripted = false;
				n->state = 2;
			}
		}
		if (p->pos[1] < -1600 * RS) {
			float p16[3];
			towerPivot(16, p16);
			if (distXZ(p->pos, p16) < 144 * RS) {
				float to[3];
				P(0, 0.5f, 0, to);
				teleport(p, to);
				S.stage = PD_START;
			} else {
				sfxPlay("horror/horror8", 1.0f);
				return "In addition to the decomposed appearance typical of the victims of SCP-106, the subject seems to have "
				       "suffered multiple heavy fractures to both of his legs.";
			}
		}
	}

	if (n && !n->hidden && distXZ(n->pos, p->pos) < 0.3f) {
		n->scripted = false;
		n->state = 3;
		n->frame = 105;
	}
	if (S.t > 0 && !sfxPlaying(S.chanVoices) && rand() % 900 == 0)
		S.chanVoices = sfxPlay("room/pocketdimension/prisonvoices", 0.4f);
	return NULL;
}
