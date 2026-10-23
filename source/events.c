// Port of SCP - Containment Breach by Regalis and contributors (CC BY-SA 3.0).

#include "events.h"

#include <3ds.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "hud.h"
#include "items.h"
#include "npc.h"
#include "npc173.h"
#include "scp294.h"
#include "wear.h"

#define DEG2RAD ((float)M_PI / 180.0f)
#define FRAMES(dt) ((dt) * 70.0f)
#define MAX_EVENTS 128
#define MAX_OBJECTS 400

typedef struct Event Event;
typedef const char* (*EventFn)(World* w, Event* e, EvPlayer* p, float dt);

struct Event {
	int def;
	int room;
	bool removed;
	float state, state2, state3;
	float state4;
	float state5;
	int pressed;
	int chan, chan2;
	int npc[4];
};

typedef struct {
	const char* name;
	EventFn fn;
} EventDef;

static Event s_events[MAX_EVENTS];
static int s_nevents;

static struct {
	char room[32];
	int idx;
	float pos[3];
} s_obj[MAX_OBJECTS];
static int s_nobj;

static float frand(float lo, float hi) { return lo + (hi - lo) * (rand() / (float)RAND_MAX); }
static int irand(int lo, int hi) { return lo + rand() % (hi - lo + 1); }

static float distXZ(const float* a, const float* b)
{
	float dx = a[0] - b[0], dz = a[2] - b[2];
	return sqrtf(dx * dx + dz * dz);
}

static float dist3(const float* a, const float* b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void roomCentre(const World* w, int room, float out[3])
{
	out[0] = w->map.rooms[room].gx * 8.0f;
	out[1] = 0;
	out[2] = -w->map.rooms[room].gy * 8.0f;
}

static const char* roomName(const World* w, int room) { return w->tpl[w->map.rooms[room].tpl].info.name; }

bool eventsRoomObject(const World* w, int room, int k, float out[3])
{
	const char* name = roomName(w, room);
	for (int i = 0; i < s_nobj; i++)
		if (s_obj[i].idx == k && strcmp(s_obj[i].room, name) == 0) {
			worldRoomToWorld(w, room, s_obj[i].pos, out);
			return true;
		}
	return false;
}

static float roomDist(const World* w, const Event* e, const EvPlayer* p)
{
	float c[3];
	roomCentre(w, e->room, c);
	return distXZ(p->pos, c);
}

static bool inRoom(const World* w, const Event* e, const EvPlayer* p) { return worldRoomIndexAt(w, p->pos) == e->room; }

static bool inView(const EvPlayer* p, const float target[3])
{
	float yr = *p->yaw * DEG2RAD, pr = *p->pitch * DEG2RAD;
	float f[3] = { sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr) };
	float d[3] = { target[0] - p->eye[0], target[1] - p->eye[1], target[2] - p->eye[2] };
	float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (l < 1e-3f) return true;
	return (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / l > 0.55f;
}

static bool scp173InView(const EvPlayer* p)
{
	float s[3];
	npc173GetPos(s);
	s[1] += 0.6f;
	return inView(p, s);
}

static void playAt(const char* snd, const float pos[3], float range)
{
	float p[3] = { pos[0], pos[1] + 0.5f, pos[2] };
	sfxPlay3D(snd, p, range, 1.0f);
}

static const char* evLockroom173(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	float d = roomDist(w, e, p);
	if (d < 6.0f && d > 0) {
		if (npc173IsIdle()) {
			e->removed = true;
			return NULL;
		}
		float s[3];
		npc173GetPos(s);
		if (!scp173InView(p) || dist3(s, p->pos) > 15.0f) {
			float c[3];
			roomCentre(w, e->room, c);
			float a = (225 - 90 + w->map.rooms[e->room].angle) * DEG2RAD;
			float to[3] = { c[0] + cosf(a) * 2, 0, c[2] - sinf(a) * 2 };
			npc173Place(to, 0);
			e->removed = true;
		}
	}
	return NULL;
}

static const char* evRoom2doors173(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (!inRoom(w, e, p) || npc173IsIdle() || scp173InView(p)) return NULL;
	float o[3];
	if (eventsRoomObject(w, e->room, 0, o)) {
		o[1] = 0;
		npc173Place(o, 0);
	}
	e->removed = true;
	return NULL;
}

static const char* evRoom2trick(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (!inRoom(w, e, p)) return NULL;
	float c[3];
	roomCentre(w, e->room, c);
	if (distXZ(c, p->pos) >= 2.0f) return NULL;
	float s[3];
	npc173GetPos(s);
	Npc* old = npcFind(NPC_106);
	if (dist3(s, p->pos) < 6.0f || (old && !old->hidden && dist3(old->pos, p->pos) < 6.0f)) {
		e->removed = true;
		return NULL;
	}
	p->pos[0] = c[0] + (c[0] - p->pos[0]);
	p->pos[2] = c[2] + (c[2] - p->pos[2]);
	p->pos[1] += 0.05f;
	*p->yaw += 180.0f;
	*p->blinkTimer = -10;
	sfxPlay("horror/horror11", 1.0f);
	e->removed = true;
	return NULL;
}

static const char* evTestroom173(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p) || npc173IsIdle()) return NULL;
	float o0[3], o1[3];
	if (!eventsRoomObject(w, e->room, 0, o0) || !eventsRoomObject(w, e->room, 1, o1)) {
		e->removed = true;
		return NULL;
	}
	if (e->state == 0) {
		int d0 = worldRoomDoor(w, e->room, 0);
		if (d0 < 0 || w->doors[d0].open) {
			o0[1] = 0;
			npc173Place(o0, 0);
			e->state = 1;
		}
		return NULL;
	}
	e->state += FRAMES(dt);
	float d = distXZ(p->pos, o1);
	if (d < 1.0f) {
		e->state = fmaxf(e->state, 70 * 12);
	} else if (d > 1.4f && e->state > 70 * 12 && *p->blinkTimer <= -10) {
		float s[3];
		npc173GetPos(s);
		if (distXZ(s, o0) <= 5.0f) {
			playAt("general/glassbreak", s, 10.0f);
			o1[1] = 0;
			npc173Place(o1, 0);
		}
		e->removed = true;
	}
	return NULL;
}

static const char* evTunnel106(World* w, Event* e, EvPlayer* p, float dt)
{
	Npc* old = npcFind(NPC_106);
	if (!old) {
		e->removed = true;
		return NULL;
	}
	float d = roomDist(w, e, p);
	if (e->state == 0) {
		if (d < 5.0f && d > 0 && old->hidden) e->state = 1;
	} else if (e->state == 1) {
		if (d < 3.0f || rand() % 7000 == 0) {
			e->state = 2;
			e->state2 = 0;
			sfxPlay("horror/horror10", 1.0f);
		} else if (d > 8.0f) {
			e->removed = true;
		}
	} else {
		e->state2 += FRAMES(dt);
		float c[3];
		roomCentre(w, e->room, c);
		if (e->state2 <= 180) {
			old->scripted = true;
			old->hidden = false;
			old->pos[0] = c[0];
			old->pos[2] = c[2];
			old->pos[1] = p->pos[1] + 1.0f - fminf(sinf(e->state2 * DEG2RAD) * 1.5f, 1.1f) - 0.15f;
			npcFace(old, p->pos, 10.0f, dt);
			npcAnimate(old, 55, 104, 0.1f, true, dt);
		} else {
			old->scripted = false;
			old->state = 2;
			old->pos[1] = 0;
			npcSnapToFloor(w, old);
			e->removed = true;
		}
	}
	return NULL;
}

static const char* evCheckpoint(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (inRoom(w, e, p) && e->state2 == 0) {
		float local[3];
		worldWorldToRoom(w, e->room, p->pos, local);
		if (-local[2] < 0) {
			sfxPlay(w->map.rooms[e->room].zone <= 1 ? "ambient/tozone2" : "ambient/tozone3", 1.0f);
			e->state2 = 1;
		}
	}
	int d0 = worldRoomDoor(w, e->room, 0), d1 = worldRoomDoor(w, e->room, 1);
	if (d0 >= 0) {
		bool open = w->doors[d0].open;
		if (open != (e->state != 0)) {
			float a[3] = { w->doors[d0].x, w->doors[d0].y, w->doors[d0].z };
			playAt("door/doorcheckpoint", a, 10.0f);
			if (d1 >= 0) {
				float b[3] = { w->doors[d1].x, w->doors[d1].y, w->doors[d1].z };
				playAt("door/doorcheckpoint", b, 10.0f);
			}
		}
		e->state = open;
	}
	return NULL;
}

static const char* evRoar682(World* w, Event* e, EvPlayer* p, float dt)
{
	if (e->state == 0) {
		if (inRoom(w, e, p)) e->state = 70.0f * irand(300, 1000);
		return NULL;
	}
	float before = e->state;
	e->state -= FRAMES(dt);
	if (before >= 17 * 70 && e->state < 17 * 70) sfxPlay("scp/682/roar", 1.0f);
	if (e->state < 70) e->removed = true;
	return NULL;
}

static const char* evButtghost(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (!inRoom(w, e, p)) return NULL;
	float o[3];
	if (!eventsRoomObject(w, e->room, 0, o)) return NULL;
	if (distXZ(p->pos, o) < 1.8f) {
		if (e->state == 0) {
			sfxPlay3D("scp/joke/789j", o, 10.0f, 1.0f);
			e->state = 1;
			e->removed = true;
		}
	}
	return NULL;
}

static const char* evToiletguard(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	float d = roomDist(w, e, p);
	if (e->state == 0) {
		if (d < 8.0f && d > 0) e->state = 1;
		return NULL;
	}
	if (e->state == 1) {
		float o[3];
		if (!eventsRoomObject(w, e->room, 1, o)) {
			e->removed = true;
			return NULL;
		}
		float c[3];
		roomCentre(w, e->room, c);
		float yaw = atan2f(-(c[0] - o[0]), -(c[2] - o[2])) / DEG2RAD - 20;
		e->npc[0] = npcSpawn(NPC_GUARD, o, yaw);
		Npc* g = npcGet(e->npc[0]);
		if (g) {
			g->scripted = true;
			g->dead = true;
			g->frame = 287;
			npcSnapToFloor(w, g);
		}
		e->state = 2;
		return NULL;
	}
	Npc* g = npcGet(e->npc[0]);
	if (!g) {
		e->removed = true;
		return NULL;
	}
	float gp[3] = { g->pos[0], g->pos[1] + 0.8f, g->pos[2] };
	if (d < 15.0f && d >= 4.0f && e->state2 == 0) {
		sfxLoop3D(&e->chan, "character/guard/suicideguard1", gp, 15.0f, 1.0f);
	} else if (d < 4.0f && p->noise > 1.0f && e->state2 == 0) {
		sfxStop(e->chan);
		e->chan = -1;
		e->chan2 = sfxPlay3D("character/guard/suicideguard2", gp, 15.0f, 1.0f);
		e->state2 = 1;
	} else if (e->state2 == 1 && !sfxPlaying(e->chan2)) {
		e->removed = true;
	}
	return NULL;
}

static const char* evRoom2fan(World* w, Event* e, EvPlayer* p, float dt)
{
	float o[3];
	if (!eventsRoomObject(w, e->room, 0, o)) roomCentre(w, e->room, o);
	if (inRoom(w, e, p) && e->state3 > 0.01f) sfxLoop3D(&e->chan, "ambient/room ambience/fan", o, 5.0f, e->state3 / 4.0f);
	else if (e->chan >= 0) {
		sfxStop(e->chan);
		e->chan = -1;
	}
	e->state3 += (e->state2 * 5 - e->state3) * fminf(1.0f, FRAMES(dt) / 150.0f);
	if (roomDist(w, e, p) < 16.0f) {
		if (e->state < 0) {
			e->state = irand(15, 30) * 70.0f;
			float was = e->state2;
			e->state2 = (float)irand(0, 1);
			if (!inRoom(w, e, p)) e->state3 = e->state2 * 5;
			else if (was == 0 && e->state2 == 1) sfxPlay3D("ambient/room ambience/fanon", o, 8.0f, 1.0f);
			else if (was == 1 && e->state2 == 0) sfxPlay3D("ambient/room ambience/fanoff", o, 8.0f, 1.0f);
		} else {
			e->state -= FRAMES(dt);
		}
	}
	return NULL;
}

static const char* evRoom2offices3(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p)) return NULL;
	e->state += FRAMES(dt);
	int d0 = worldRoomDoor(w, e->room, 0);
	if (d0 < 0) {
		e->removed = true;
		return NULL;
	}
	if (e->state > 700) {
		float dp[3] = { w->doors[d0].x, w->doors[d0].y + 1.0f, w->doors[d0].z };
		if (distXZ(dp, p->pos) > 0.5f && !inView(p, dp)) {
			if (w->doors[d0].open) worldToggleDoor(w, d0);
			e->removed = true;
		}
	}
	return NULL;
}

#include "events_lcz.inc"

static bool s_contained106;
bool eventsContained106(void) { return s_contained106; }

#include "events_gates.inc"
#include "events_914.inc"

#define MAX_LAZY 64
static struct {
	Room* room;
	const char* path;
	float idle;
} s_lazy[MAX_LAZY];
static int s_nlazy;

static Room* useModel(Room* r, const char* path)
{
	for (int i = 0; i < s_nlazy; i++)
		if (s_lazy[i].room == r) {
			if (!s_lazy[i].path) {
				s_lazy[i].path = path;
				roomLoad(r, path);
			}
			s_lazy[i].idle = 0;
			return r;
		}
	if (s_nlazy < MAX_LAZY) {
		s_lazy[s_nlazy].room = r;
		s_lazy[s_nlazy].path = path;
		s_lazy[s_nlazy].idle = 0;
		s_nlazy++;
		roomLoad(r, path);
	}
	return r;
}

static void sweepModels(float dt)
{
	for (int i = 0; i < s_nlazy; i++) {
		if (!s_lazy[i].path) continue;
		s_lazy[i].idle += dt;
		if (s_lazy[i].idle > 3.0f) {
			roomFree(s_lazy[i].room);
			s_lazy[i].path = NULL;
		}
	}
}

static bool s_remoteDoorOn;
#include "events_hcz.inc"
#include "events_hcz2.inc"
#include "events_hcz3.inc"
#include "events_lcz2.inc"
#include "events_lcz3.inc"
#include "events_lcz4.inc"
#include "events_lcz5.inc"
#include "events_tunnels.inc"
#include "events_forest.inc"
#include "events_1499.inc"

static const EventDef s_defs[] = {
	{ "173", NULL },
	{ "alarm", NULL },
	{ "pocketdimension", NULL },
	{ "tunnel106", evTunnel106 },
	{ "lockroom173", evLockroom173 },
	{ "room2trick", evRoom2trick },
	{ "1048a", ev1048a },
	{ "room2storage", evRoom2storage },
	{ "lockroom096", evLockroom096 },
	{ "endroom106", evEndroom106 },
	{ "room2poffices2", evRoom2poffices2 },
	{ "room2fan", evRoom2fan },
	{ "room2elevator2", evRoom2elevator2 },
	{ "room2elevator", evRoom2elevator },
	{ "room3storage", evRoom3storage },
	{ "tunnel2smoke", evTunnel2smoke },
	{ "tunnel2", evTunnel2 },
	{ "room2doors173", evRoom2doors173 },
	{ "room2offices2", evRoom2offices2 },
	{ "room2closets", evRoom2closets },
	{ "room2cafeteria", evRoom2cafeteria },
	{ "room3pitduck", evRoom3pitduck },
	{ "room3pit1048", evRoom3pit1048 },
	{ "room2offices3", evRoom2offices3 },
	{ "room2servers", evRoom2servers },
	{ "room3servers", evRoom3servers },
	{ "room3tunnel", evRoom3tunnel },
	{ "room4", evRoom4 },
	{ "682roar", evRoar682 },
	{ "testroom173", evTestroom173 },
	{ "room2tesla", evRoom2tesla },
	{ "room2nuke", evRoom2nuke },
	{ "coffin106", evCoffin },
	{ "coffin", evCoffin },
	{ "checkpoint", evCheckpoint },
	{ "room3door", evRoom3door },
	{ "106victim", ev106victim },
	{ "106sinkhole", ev106sinkhole },
	{ "room079", evRoom079 },
	{ "room049", evRoom049 },
	{ "room012", evRoom012 },
	{ "room035", evRoom035 },
	{ "008", ev008 },
	{ "room106", evRoom106 },
	{ "pj", evPj },
	{ "914", ev914 },
	{ "buttghost", evButtghost },
	{ "toiletguard", evToiletguard },
	{ "room2pipes106", evRoom2pipes106 },
	{ "room2pit", evRoom2pit },
	{ "testroom", evTestroom },
	{ "room2tunnel", evRoom2tunnel },
	{ "room2ccont", evRoom2ccont },
	{ "gateaentrance", evGateaentrance },
	{ "gatea", evGatea },
	{ "exit1", evExit1 },
	{ "room205", evRoom205 },
	{ "room860", evRoom860 },
	{ "room966", evRoom966 },
	{ "room1123", evRoom1123 },
	{ "room2sl", evRoom2sl },
	{ "096spawn", ev096spawn },
	{ "medibay", evMedibay },
	{ "dimension1499", ev1499 },
	{ "room2offices035", evRoom2offices035 },
	{ "room1archive", evRoom1archive },
	{ "room2shaft", evRoom2shaft },
	{ "room1lifts", evRoom1lifts },
	{ "room_gw", evRoomGw },
	{ "room2scps2", evRoom2scps2 },
	{ "room1162", evRoom1162 },
	{ "room2gw_b", evRoom2gwB },
	{ "room4tunnels", evRoom4tunnels },
	{ "room2test1074", NULL },
	{ "room2pit106", evRoom2pit106 },
};
#define NDEFS ((int)(sizeof(s_defs) / sizeof(s_defs[0])))

static const char* defName(const Event* e) { return s_defs[e->def].name; }

static int defIndex(const char* name)
{
	for (int i = 0; i < NDEFS; i++)
		if (strcmp(s_defs[i].name, name) == 0) return i;
	return -1;
}

void eventsLoad(void)
{
	s_nobj = 0;
	FILE* f = fopen("romfs:/fill.txt", "r");
	if (!f) return;
	char line[256];
	while (s_nobj < MAX_OBJECTS && fgets(line, sizeof(line), f)) {
		if (line[0] != 'O') continue;
		if (sscanf(line, "O\t%31[^\t]\t%d\t%f\t%f\t%f", s_obj[s_nobj].room, &s_obj[s_nobj].idx, &s_obj[s_nobj].pos[0],
		           &s_obj[s_nobj].pos[1], &s_obj[s_nobj].pos[2]) == 5)
			s_nobj++;
	}
	fclose(f);
}

static bool roomHasEvent(int room)
{
	for (int i = 0; i < s_nevents; i++)
		if (s_events[i].room == room) return true;
	return false;
}

static void createEvent(const World* w, const char* name, const char* roomname, int id, float prob)
{
	int def = defIndex(name);
	if (def < 0) return;
	int i = 0;
	for (int r = 0; r < w->map.nrooms && s_nevents < MAX_EVENTS; r++) {
		if (strcmp(roomName(w, r), roomname) != 0) continue;
		bool taken = roomHasEvent(r);
		bool pick;
		if (prob == 0.0f) {
			i++;
			pick = i >= id && !taken;
		} else {
			pick = frand(0, 1) < prob && !taken;
		}
		if (!pick) continue;
		Event* e = &s_events[s_nevents++];
		memset(e, 0, sizeof(*e));
		e->def = def;
		e->room = r;
		e->chan = e->chan2 = -1;
		for (int k = 0; k < 4; k++) e->npc[k] = -1;
		if (prob == 0.0f) return;
	}
}

void eventsInit(World* w)
{
	s_nevents = 0;
	s_914Setting = 2;
	s_914Hover = 0;
	s_914Room = -1;
	s_914Event = NULL;
	s_gateaDoor = -1;
	s_ending = NULL;
	s_nukeOn = false;
	s_contained106 = false;
	s_soundTransmission = false;
	s_remoteDoorOn = true;
	s_gatesOpened = false;
	s_frArea = s_mtArea = s_1499Area = -1;
	memset(s_1499Prev, 0, sizeof(s_1499Prev));
	memset(s_1499Back, 0, sizeof(s_1499Back));
	s_inForest = false;
	leversInit(w);
	const float aggressive = 0.0f;
	createEvent(w, "173", "173", 0, 0);
	createEvent(w, "alarm", "start", 0, 0);
	createEvent(w, "pocketdimension", "pocketdimension", 0, 0);
	createEvent(w, "tunnel106", "tunnel", 0, 0.07f + 0.1f * aggressive);
	if (rand() % 3 < 2) createEvent(w, "lockroom173", "lockroom", 0, 0);
	createEvent(w, "lockroom173", "lockroom", 0, 0.3f + 0.5f * aggressive);
	createEvent(w, "room2trick", "room2", 0, 0.15f);
	createEvent(w, "1048a", "room2", 0, 1.0f);
	createEvent(w, "room2storage", "room2storage", 0, 0);
	createEvent(w, "lockroom096", "lockroom2", 0, 0);
	createEvent(w, "endroom106", "endroom", irand(0, 1), 0);
	createEvent(w, "room2poffices2", "room2poffices2", 0, 0);
	createEvent(w, "room2fan", "room2_2", 0, 1.0f);
	createEvent(w, "room2elevator2", "room2elevator", 0, 0);
	createEvent(w, "room2elevator", "room2elevator", irand(1, 2), 0);
	createEvent(w, "room3storage", "room3storage", 0, 0);
	createEvent(w, "tunnel2smoke", "tunnel2", 0, 0.2f);
	createEvent(w, "tunnel2", "tunnel2", irand(0, 2), 0);
	createEvent(w, "tunnel2", "tunnel2", 0, 0.2f * aggressive);
	createEvent(w, "room2doors173", "room2doors", 0, 0.5f + 0.4f * aggressive);
	createEvent(w, "room2offices2", "room2offices2", 0, 0.7f);
	createEvent(w, "room2closets", "room2closets", 0, 0);
	createEvent(w, "room2cafeteria", "room2cafeteria", 0, 0);
	createEvent(w, "room3pitduck", "room3pit", 0, 0);
	createEvent(w, "room3pit1048", "room3pit", 1, 0);
	createEvent(w, "room2offices3", "room2offices3", 0, 1.0f);
	createEvent(w, "room2servers", "room2servers", 0, 0);
	createEvent(w, "room3servers", "room3servers", 0, 0);
	createEvent(w, "room3servers", "room3servers2", 0, 0);
	createEvent(w, "room3tunnel", "room3tunnel", 0, 0.08f);
	createEvent(w, "room4", "room4", 0, 0);
	createEvent(w, "682roar", "tunnel", irand(0, 2), 0);
	createEvent(w, "682roar", "room3pit", irand(0, 2), 0);
	createEvent(w, "682roar", "room2z3", 0, 0);
	createEvent(w, "testroom173", "room2testroom2", 0, 1.0f);
	createEvent(w, "room2tesla", "room2tesla", 0, 0.9f);
	createEvent(w, "room2nuke", "room2nuke", 0, 0);
	createEvent(w, "coffin106", "coffin", 0, 0);
	createEvent(w, "coffin", "coffin", 0, 0);
	createEvent(w, "checkpoint", "checkpoint1", 0, 1.0f);
	createEvent(w, "checkpoint", "checkpoint2", 0, 1.0f);
	createEvent(w, "room3door", "room3", 0, 0.1f);
	createEvent(w, "room3door", "room3tunnel", 0, 0.1f);
	createEvent(w, "106victim", "room3", irand(1, 2), 0);
	createEvent(w, "106sinkhole", "room3_2", irand(2, 3), 0);
	createEvent(w, "106victim", "room3_2", irand(1, 2), 0);
	createEvent(w, "106sinkhole", "room3", irand(2, 3), 0);
	createEvent(w, "106sinkhole", "room4", irand(1, 2), 0);
	createEvent(w, "room079", "room079", 0, 0);
	createEvent(w, "room049", "room049", 0, 0);
	createEvent(w, "room012", "room012", 0, 0);
	createEvent(w, "room035", "room035", 0, 0);
	createEvent(w, "008", "008", 0, 0);
	createEvent(w, "room106", "room106", 0, 0);
	createEvent(w, "pj", "roompj", 0, 0);
	createEvent(w, "914", "914", 0, 0);
	createEvent(w, "buttghost", "room2toilets", 0, 0);
	createEvent(w, "toiletguard", "room2toilets", 1, 0);
	createEvent(w, "room2pipes106", "room2pipes", irand(0, 3), 0);
	createEvent(w, "room2pit", "room2pit", 0, 0.4f + 0.4f * aggressive);
	createEvent(w, "testroom", "testroom", 0, 0);
	createEvent(w, "room2tunnel", "room2tunnel", 0, 0);
	createEvent(w, "room2ccont", "room2ccont", 0, 0);
	createEvent(w, "gateaentrance", "gateaentrance", 0, 0);
	createEvent(w, "gatea", "gatea", 0, 0);
	createEvent(w, "exit1", "exit1", 0, 0);
	createEvent(w, "room205", "room205", 0, 0);
	createEvent(w, "room860", "room860", 0, 0);
	createEvent(w, "room966", "room966", 0, 0);
	createEvent(w, "room1123", "room1123", 0, 0);
	createEvent(w, "room2tesla", "room2tesla_lcz", 0, 0.9f);
	createEvent(w, "room2tesla", "room2tesla_hcz", 0, 0.9f);

	createEvent(w, "room4tunnels", "room4tunnels", 0, 0);
	createEvent(w, "room_gw", "room2gw", 0, 1.0f);
	createEvent(w, "dimension1499", "dimension1499", 0, 0);
	createEvent(w, "room1162", "room1162", 0, 0);
	createEvent(w, "room2scps2", "room2scps2", 0, 0);
	createEvent(w, "room_gw", "room3gw", 0, 1.0f);
	createEvent(w, "room2sl", "room2sl", 0, 0);
	createEvent(w, "medibay", "medibay", 0, 0);
	createEvent(w, "room2shaft", "room2shaft", 0, 0);
	createEvent(w, "room1lifts", "room1lifts", 0, 0);
	createEvent(w, "room2gw_b", "room2gw_b", irand(0, 1), 0);
	createEvent(w, "096spawn", "room4pit", 0, 0.6f + 0.2f * aggressive);
	createEvent(w, "096spawn", "room3pit", 0, 0.6f + 0.2f * aggressive);
	createEvent(w, "096spawn", "room2pipes", 0, 0.4f + 0.2f * aggressive);
	createEvent(w, "096spawn", "room2pit", 0, 0.5f + 0.2f * aggressive);
	createEvent(w, "096spawn", "room3tunnel", 0, 0.6f + 0.2f * aggressive);
	createEvent(w, "096spawn", "room4tunnels", 0, 0.7f + 0.2f * aggressive);
	createEvent(w, "096spawn", "tunnel", 0, 0.6f + 0.2f * aggressive);
	createEvent(w, "096spawn", "tunnel2", 0, 0.4f + 0.2f * aggressive);
	createEvent(w, "096spawn", "room3z2", 0, 0.7f + 0.2f * aggressive);
	createEvent(w, "room2pit", "room2_4", 0, 0.4f + 0.4f * aggressive);
	createEvent(w, "room2offices035", "room2offices", 0, 0);
	createEvent(w, "room2pit106", "room2pit", 0, 0.07f + 0.1f * aggressive);
	createEvent(w, "room1archive", "room1archive", 0, 1.0f);

	for (int i = 0; i < s_nevents; i++)
		if (s_defs[s_events[i].def].fn == evRoom2tunnel) tunnelsGenerate(w, s_events[i].room);
		else if (s_defs[s_events[i].def].fn == evRoom860) forestGenerate(w, s_events[i].room);

	for (int i = 0; i < s_nevents; i++)
		if (s_defs[s_events[i].def].fn == ev1499) dimension1499Generate(w, s_events[i].room);
}

const char* eventsUpdate(World* w, EvPlayer* p, float dt)
{
	sweepModels(dt);
	s_lightBlink = fmaxf(0.0f, s_lightBlink - dt * 2.0f);
	if (w->isolated || w->introActive) return NULL;
	const char* death = NULL;
	leversUpdate(p);
	gateDoors(w, p);
	s_useEvent = NULL;
	for (int i = 0; i < s_nevents; i++) {
		Event* e = &s_events[i];
		if (e->removed) continue;
		EventFn fn = s_defs[e->def].fn;
		if (!fn) continue;
		const char* d = fn(w, e, p, dt);
		if (d && !death) death = d;
	}
	return death;
}

int eventsCount(void) { return NDEFS; }

int eventsImplemented(void)
{
	int n = 0;
	for (int i = 0; i < NDEFS; i++) n += s_defs[i].fn != NULL;
	return n + 2;
}

bool eventsDebugGate(const World* w, char gate, float pos[3])
{

	const char* room = gate == 'A' ? "gateaentrance" : gate == 'a' ? "gatea" : "exit1";
	int obj = gate == 'A' ? 0 : gate == 'B' ? 8 : gate == 'b' ? 10 : 11;
	int r = findRoom(w, room);
	if (r < 0) return false;
	if (!eventsRoomObject(w, r, obj, pos)) return false;
	pos[1] += 0.4f;
	return true;
}

bool eventsInteract(World* w, EvPlayer* p)
{
	(void)p;
	return interact914(w) || leversInteract() || useInteract();
}

bool eventsHover(void) { return s_914Hover != 0 || s_leverHover >= 0 || s_useEvent != NULL; }

bool eventsDebug914(const World* w, float pos[3], float* yaw)
{
	int r = findRoom(w, "914");
	float knob[3], c[3];
	if (r < 0 || !eventsRoomObject(w, r, 1, knob)) return false;
	roomCentre(w, r, c);
	float dx = c[0] - knob[0], dz = c[2] - knob[2], l = sqrtf(dx * dx + dz * dz);
	pos[0] = knob[0] + dx / l * 0.8f;
	pos[2] = knob[2] + dz / l * 0.8f;
	pos[1] = 0.35f;
	*yaw = atan2f(knob[0] - pos[0], -(knob[2] - pos[2])) / DEG2RAD;
	return true;
}

void eventsDebug914Feed(const World* w, const char* name, const char* tempname)
{
	int r = findRoom(w, "914");
	float intake[3];
	if (r < 0 || !eventsRoomObject(w, r, 2, intake)) return;
	itemsSpawnAt(name, tempname, intake);
}

void eventsSave(FILE* f)
{
	fwrite(&s_nevents, sizeof(s_nevents), 1, f);
	fwrite(s_events, sizeof(Event), s_nevents, f);
	fwrite(&s_914Setting, sizeof(s_914Setting), 1, f);
	fwrite(&s_nukeOn, sizeof(s_nukeOn), 1, f);
	fwrite(&s_contained106, sizeof(s_contained106), 1, f);
	fwrite(&s_gateaDoor, sizeof(s_gateaDoor), 1, f);
	fwrite(&s_nlevers, sizeof(s_nlevers), 1, f);
	fwrite(s_levers, sizeof(Lever), s_nlevers, f);
	fwrite(&s_soundTransmission, sizeof(s_soundTransmission), 1, f);
	fwrite(&s_remoteDoorOn, sizeof(s_remoteDoorOn), 1, f);
	fwrite(&s_gatesOpened, sizeof(s_gatesOpened), 1, f);
	fwrite(s_1499Prev, sizeof(s_1499Prev), 1, f);
	fwrite(s_1499Back, sizeof(s_1499Back), 1, f);
	fwrite(&s_1499In, sizeof(s_1499In), 1, f);
}

bool eventsRestore(FILE* f)
{
	if (fread(&s_nevents, sizeof(s_nevents), 1, f) != 1 || s_nevents < 0 || s_nevents > MAX_EVENTS) return false;
	if ((int)fread(s_events, sizeof(Event), s_nevents, f) != s_nevents) return false;
	for (int i = 0; i < s_nevents; i++) {
		s_events[i].chan = s_events[i].chan2 = -1;
		for (int k = 0; k < 4; k++) s_events[i].npc[k] = -1;
	}
	fread(&s_914Setting, sizeof(s_914Setting), 1, f);
	fread(&s_nukeOn, sizeof(s_nukeOn), 1, f);
	fread(&s_contained106, sizeof(s_contained106), 1, f);
	fread(&s_gateaDoor, sizeof(s_gateaDoor), 1, f);
	if (fread(&s_nlevers, sizeof(s_nlevers), 1, f) != 1 || s_nlevers < 0 || s_nlevers > MAX_LEVERS) return false;
	if ((int)fread(s_levers, sizeof(Lever), s_nlevers, f) != s_nlevers) return false;
	fread(&s_soundTransmission, sizeof(s_soundTransmission), 1, f);
	fread(&s_remoteDoorOn, sizeof(s_remoteDoorOn), 1, f);
	fread(&s_gatesOpened, sizeof(s_gatesOpened), 1, f);
	fread(s_1499Prev, sizeof(s_1499Prev), 1, f);
	fread(s_1499Back, sizeof(s_1499Back), 1, f);
	fread(&s_1499In, sizeof(s_1499In), 1, f);
	s_leverHover = -1;
	s_useEvent = NULL;
	s_914Event = NULL;
	s_914Room = -1;
	return true;
}

void eventsDebugInfo(const World* w, const float pos[3], char* out, int size)
{
	out[0] = 0;
	int room = worldRoomIndexAt(w, pos);
	for (int i = 0; i < s_nevents; i++) {
		const Event* e = &s_events[i];
		if (e->room != room || e->removed) continue;
		snprintf(out, size, "%s %.0f/%.0f/%.0f c%d%c", s_defs[e->def].name, e->state, e->state2, e->state3, e->chan,
		         sfxPlaying(e->chan) ? 'p' : '-');
		return;
	}
}
