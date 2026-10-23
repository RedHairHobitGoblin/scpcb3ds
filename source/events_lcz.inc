static bool s_wantPD;
static float s_lightBlink;

bool eventsTakePocketDimension(void)
{
	bool v = s_wantPD;
	s_wantPD = false;
	return v;
}

float eventsLightBlink(void) { return s_lightBlink; }

#define RSC (8.0f / 2048.0f)

static int spawnBody(World* w, NpcType type, const float pos[3], float yaw, float frame)
{
	int i = npcSpawn(type, pos, yaw);
	Npc* n = npcGet(i);
	if (n) {
		n->scripted = true;
		n->dead = true;
		n->frame = frame;
		npcSnapToFloor(w, n);
	}
	return i;
}

static Npc* waiting106(void)
{
	Npc* n = npcFind(NPC_106);
	return n && n->hidden && n->state == 0 ? n : NULL;
}

static void release106(Npc* n, bool chase)
{
	if (!n) return;
	n->scripted = false;
	if (chase) {
		n->hidden = false;
		n->state = 2;
	} else {
		n->hidden = true;
		n->state = 0;
		if (n->timer < 2000) n->timer = 2000;
	}
}

static float yawTo(const float from[3], const float to[3])
{
	return atan2f(-(to[0] - from[0]), -(to[2] - from[2])) / DEG2RAD;
}

static const char* ev106victim(World* w, Event* e, EvPlayer* p, float dt)
{
	float c[3];
	roomCentre(w, e->room, c);
	if (inRoom(w, e, p) && e->state == 0) {
		float top[3] = { c[0], 799 * RSC, c[2] };
		playAt("scp/106/decay3", top, 15.0f);
		e->state = 1;
	}
	if (e->state <= 0) return NULL;
	Npc* n = npcGet(e->npc[0]);
	if (!n) e->state += FRAMES(dt);
	if (e->state <= 200) return NULL;
	if (!n) {
		float spawn[3] = { c[0], 900 * RSC, c[2] };
		e->npc[0] = npcSpawn(NPC_CLASSD, spawn, frand(0, 360));
		n = npcGet(e->npc[0]);
		if (!n) {
			e->removed = true;
			return NULL;
		}
		n->scripted = true;
		n->frame = 1;
		sfxPlay("horror/horror0", 1.0f);
		playAt("scp/106/decay2", spawn, 15.0f);
		e->state2 = 0;
	}
	float probe[3] = { n->pos[0], n->pos[1], n->pos[2] };
	float floorY = worldRayDown(w, probe, 5.0f);
	if (!isfinite(floorY)) floorY = 0;
	if (n->pos[1] > floorY + 0.02f) {
		e->state2 += 9.0f * dt;
		n->pos[1] = fmaxf(floorY, n->pos[1] - e->state2 * dt);
		npcAnimate(n, 1, 10, 0.12f, false, dt);
	} else {
		e->state += FRAMES(dt);
		npcAnimate(n, 11, 19, 0.25f, false, dt);
		if (e->state3 == 0) {
			sfxPlay("general/bodyfall", 1.0f);
			e->state3 = 1;
		}
		if (e->state > 400) {
			n->dead = true;
			e->removed = true;
		}
	}
	return NULL;
}

static const char* ev106sinkhole(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p)) {
		e->state2 = 0;
		return NULL;
	}
	float c[3];
	roomCentre(w, e->room, c);
	sfxLoop3D(&e->chan, "room/sinkhole", c, 4.5f, 1.0f);
	if (distXZ(p->pos, c) >= 0.5f) return NULL;
	if (e->state2 == 0) sfxPlay("room/sinkholefall", 1.0f);
	float k = fminf(1.0f, FRAMES(dt) / 10.0f);
	p->pos[0] += (c[0] - p->pos[0]) * k;
	p->pos[2] += (c[2] - p->pos[2]) * k;
	e->state2 = fminf(e->state2 + FRAMES(dt) / 200.0f, 2.0f);
	s_lightBlink = fmaxf(s_lightBlink, fminf(e->state2 * 5, 10.0f) / 10.0f);
	if (e->state2 >= 2.0f) {
		sfxStop(e->chan);
		e->chan = -1;
		e->state2 = 0;
		s_wantPD = true;
	}
	return NULL;
}

static const char* evRoom3door(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (!inRoom(w, e, p)) return NULL;
	float c[3];
	roomCentre(w, e->room, c);
	if (distXZ(c, p->pos) >= 2.5f) return NULL;
	for (int i = 0; i < w->ndoors; i++) {
		Door* d = &w->doors[i];
		if (fabsf(d->x - p->pos[0]) >= 2.0f || fabsf(d->z - p->pos[2]) >= 2.0f) continue;
		float dp[3] = { d->x, d->y + 1.0f, d->z };
		if (!inView(p, dp) && d->open) {
			d->open = false;
			d->openstate = 0;
			playAt("door/doorclose1", dp, 8.0f);
		}
		break;
	}
	e->removed = true;
	return NULL;
}

static const char* evRoom2pit(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (npc173IsIdle()) return NULL;
	float d = roomDist(w, e, p);
	if (d >= 8.0f || d <= 0) return NULL;
	float o[3];
	if (!eventsRoomObject(w, e->room, 6, o)) {
		e->removed = true;
		return NULL;
	}
	float s[3];
	npc173GetPos(s);
	s[1] += 0.6f;
	float oe[3] = { o[0], o[1] + 0.3f, o[2] };
	bool see173 = inView(p, s) && worldLineOfSight(w, p->eye, s);
	bool seeSpot = inView(p, oe) && worldLineOfSight(w, p->eye, oe);
	if (!see173 && !seeSpot) {
		o[1] = 0;
		npc173Place(o, 0);
		e->removed = true;
	}
	return NULL;
}

static const char* evRoom2pipes106(World* w, Event* e, EvPlayer* p, float dt)
{
	Npc* old = npcFind(NPC_106);
	if (!old) {
		e->removed = true;
		return NULL;
	}
	if (e->state == 0) {
		if (inRoom(w, e, p) && waiting106()) e->state = 1;
		return NULL;
	}
	float o0[3], o1[3];
	if (!eventsRoomObject(w, e->room, 0, o0) || !eventsRoomObject(w, e->room, 1, o1)) {
		e->removed = true;
		return NULL;
	}
	float prev = e->state;
	e->state += FRAMES(dt) * 0.7f;
	old->scripted = true;
	old->hidden = false;
	float t;
	if (e->state < 50) {
		t = 0.5f * (e->state / 50.0f);
		npcAnimate(old, 284, 333, 0.02f * 35, true, dt);
	} else if (e->state < 200) {
		t = 0.5f;
		npcAnimate(old, 334, 494, 0.2f, true, dt);
		if (distXZ(p->pos, old->pos) < 4.0f) {
			sfxPlay("horror/horror10", 1.0f);
			release106(old, true);
			e->removed = true;
			return NULL;
		}
	} else {
		t = 0.5f + 0.5f * fminf(1.0f, (e->state - 200) / 50.0f);
		npcAnimate(old, 284, 333, 0.02f * 35, true, dt);
	}
	old->pos[0] = o0[0] + (o1[0] - o0[0]) * t;
	old->pos[2] = o0[2] + (o1[2] - o0[2]) * t;
	old->pos[1] = p->pos[1] - 0.3f;
	old->yaw = yawTo(o0, o1);
	if (prev / 250.0f <= 0.3f && e->state / 250.0f > 0.3f) sfxPlay("horror/horror6", 1.0f);
	if (e->state > 250) {
		release106(old, false);
		e->removed = true;
	}
	return NULL;
}

static const char* evRoom2pit106(World* w, Event* e, EvPlayer* p, float dt)
{
	Npc* old = npcFind(NPC_106);
	if (!old) {
		e->removed = true;
		return NULL;
	}
	if (e->state == 0) {
		if (inRoom(w, e, p) && waiting106()) e->state = 1;
		return NULL;
	}
	float o[3];
	if (!eventsRoomObject(w, e->room, 7, o)) {
		e->removed = true;
		return NULL;
	}
	e->state += 1;
	old->scripted = true;
	old->hidden = false;
	memcpy(old->pos, o, sizeof(o));
	old->pos[1] -= 0.15f;
	npcFace(old, p->pos, 20.0f, dt);
	npcAnimate(old, 334, 494, 0.3f, true, dt);
	if (e->state > 800 && *p->blinkTimer < -5) {
		release106(old, false);
		e->removed = true;
	}
	return NULL;
}

static const char* evRoom3tunnel(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)p;
	(void)dt;
	float o[3], c[3];
	roomCentre(w, e->room, c);
	if (eventsRoomObject(w, e->room, 0, o)) spawnBody(w, NPC_GUARD, o, yawTo(o, c) + frand(-20, 20), 288);
	e->removed = true;
	return NULL;
}

static const char* evRoom2shaft(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)p;
	(void)dt;
	float o[3];
	if (eventsRoomObject(w, e->room, 1, o)) spawnBody(w, NPC_GUARD, o, (float)w->map.rooms[e->room].angle + 180, 286);
	e->removed = true;
	return NULL;
}

static const char* evRoom2elevator2(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	float d = roomDist(w, e, p);
	if (d >= 8.0f || d <= 0) return NULL;
	float o[3];
	if (eventsRoomObject(w, e->room, 0, o)) spawnBody(w, NPC_CLASSD, o, (float)w->map.rooms[e->room].angle - 80, 19);
	e->removed = true;
	return NULL;
}

static const char* evRoom4tunnels(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	float d = roomDist(w, e, p);
	if (d >= 10.0f || d <= 0) return NULL;
	float c[3];
	roomCentre(w, e->room, c);
	c[0] += 1.0f;
	c[2] -= 1.0f;
	spawnBody(w, NPC_CLASSD, c, (float)(w->map.rooms[e->room].angle - irand(20, 60)), 19);
	e->removed = true;
	return NULL;
}

static const char* evRoom2gwB(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (roomDist(w, e, p) >= 8) {
		if (e->chan >= 0) {
			sfxStop(e->chan);
			e->chan = -1;
		}
		return NULL;
	}
	float o[3], c[3];
	roomCentre(w, e->room, c);
	if (e->state == 0) {
		if (eventsRoomObject(w, e->room, 2, o)) spawnBody(w, NPC_GUARD, o, yawTo(o, c), 288);
		e->state = 1;
	}
	if (eventsRoomObject(w, e->room, 3, o)) sfxLoop3D(&e->chan, "alarm/alarm4", o, 5.0f, 1.0f);
	return NULL;
}

static const char* evRoom2elevator(World* w, Event* e, EvPlayer* p, float dt)
{
	float d = roomDist(w, e, p);
	if (e->state == 0) {
		if (d < 8.0f && d > 0) {
			float c[3];
			roomCentre(w, e->room, c);
			e->npc[0] = npcSpawn(NPC_GUARD, c, yawTo(c, p->pos));
			Npc* g = npcGet(e->npc[0]);
			if (g) {
				g->scripted = true;
				npcSnapToFloor(w, g);
			}
			e->state = 1;
		}
		return NULL;
	}
	Npc* g = npcGet(e->npc[0]);
	float o1[3];
	if (!eventsRoomObject(w, e->room, 1, o1)) {
		e->removed = true;
		return NULL;
	}
	int d0 = worldRoomDoor(w, e->room, 0);
	if (e->state == 1) {
		if (g) npcAnimate(g, 77, 201, 0.2f, true, dt);
		if (d < 5.0f || rand() % 700 == 0) e->state = 2;
	} else if (e->state == 2) {
		if (g) {
			npcWalkTo(w, g, o1, 0.015f * 70.0f, dt);
			npcAnimate(g, 1, 38, 0.6f, true, dt);
		}
		if (!g || distXZ(g->pos, o1) < 2.0f) {
			if (d0 >= 0 && w->doors[d0].open) {
				w->doors[d0].open = false;
				float dp[3] = { w->doors[d0].x, 1.0f, w->doors[d0].z };
				playAt("door/elevatorclose1", dp, 8.0f);
			}
			sfxPlay("room/room2elevatordeath", 1.0f);
			e->state = 2.05f;
		}
	} else if (e->state < 13 * 70) {
		e->state += FRAMES(dt);
		if (e->state > 12.6f * 70 && g) {
			npcRemove(e->npc[0]);
			e->npc[0] = -1;
		}
	} else {
		if (d0 >= 0) w->doors[d0].locked = true;
		e->removed = true;
	}
	return NULL;
}

static const char* evRoom3servers(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (!inRoom(w, e, p) || e->state3 != 0 || npc173IsIdle()) return NULL;
	if (*p->blinkTimer < -10) {
		float o[3];
		if (eventsRoomObject(w, e->room, irand(0, 2), o)) {
			o[1] = 0;
			npc173Place(o, 0);
		}
		e->state3 = 1;
		e->removed = true;
	}
	return NULL;
}

static const char* evTunnel2smoke(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (inRoom(w, e, p) && roomDist(w, e, p) < 3.5f) {
		float c[3];
		roomCentre(w, e->room, c);
		playAt("room/tunnelburst", c, 10.0f);
		e->removed = true;
	}
	return NULL;
}

static const char* evTunnel2(World* w, Event* e, EvPlayer* p, float dt)
{
	if (e->state == 0) {
		if (!inRoom(w, e, p)) return NULL;
		if (npc173IsIdle()) {
			e->removed = true;
			return NULL;
		}
		if (roomDist(w, e, p) < 3.5f) {
			sfxPlay("general/lightswitch", 1.0f);
			e->state = 1;
		}
		return NULL;
	}
	if (e->state < 200) {
		float before = e->state;
		e->state += FRAMES(dt);
		*p->blinkTimer = -10;
		s_lightBlink = 1.0f;
		if (before <= 30 && e->state > 30) sfxPlay("ambient/general/ambient3", 1.0f);
		if (before <= 100 && e->state > 100) {
			sfxPlay("ambient/general/ambient6", 1.0f);
			float c[3];
			roomCentre(w, e->room, c);
			npc173Place(c, 0);
			npc173SetIdle(true);
		}
	} else {
		*p->blinkTimer = 500;
		npc173SetIdle(false);
		e->removed = true;
	}
	return NULL;
}

static const char* evRoom2poffices2(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p)) return NULL;
	int d0 = worldRoomDoor(w, e->room, 0);
	if (d0 < 0) {
		e->removed = true;
		return NULL;
	}
	Door* d = &w->doors[d0];
	if (e->state == 0) {
		if (d->open) {
			if (d->openstate >= 180) {
				e->state = 1;
				sfxPlay("horror/horror5", 1.0f);
			}
		} else {
			float dp[3] = { d->x, d->y, d->z };
			if (distXZ(dp, p->pos) < 1.5f) worldToggleDoor(w, d0);
		}
		return NULL;
	}
	float o[3];
	if (eventsRoomObject(w, e->room, 0, o) && distXZ(o, p->pos) < 2.0f) {
		float c[3];
		roomCentre(w, e->room, c);
		sfxLoop3D(&e->chan, "scp/106/breathing", c, 5.0f, 0.3f);
		Npc* old = waiting106();
		if (old) old->timer -= FRAMES(dt) * 3;
	}
	return NULL;
}

static const char* evRoom2scps2(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (roomDist(w, e, p) >= 15) return NULL;
	int d0 = worldRoomDoor(w, e->room, 0);
	Npc* old = npcFind(NPC_106);
	if (old && !old->hidden) e->state = 2;
	float o[3];
	if (!eventsRoomObject(w, e->room, 0, o)) roomCentre(w, e->room, o);
	if (e->state == 0) {
		e->chan = sfxPlay3D("character/scientist/emilyscream", o, 100.0f, 1.0f);
		e->state = 1;
	} else if (e->state == 1) {
		if (!sfxPlaying(e->chan)) e->state = 2;
	} else {
		if (d0 >= 0) w->doors[d0].locked = false;
		e->removed = true;
	}
	return NULL;
}

static const char* evRoom1archive(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)p;
	(void)dt;
	int d0 = worldRoomDoor(w, e->room, 0);
	if (d0 >= 0) w->doors[d0].keycard = irand(1, 3);
	e->removed = true;
	return NULL;
}

static const char* evLockroom096(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (!inRoom(w, e, p)) return NULL;
	if (!npcFind(NPC_096)) {
		float c[3];
		roomCentre(w, e->room, c);
		Npc* n = npcGet(npcSpawn(NPC_096, c, (float)w->map.rooms[e->room].angle + 45));
		if (n) {
			n->frame = 936;
			npcSnapToFloor(w, n);
		}
	}
	e->removed = true;
	return NULL;
}
