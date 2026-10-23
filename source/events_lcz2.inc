static Room s_duck, s_medibayProps, s_1048pp;

static const char* evPj(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (!inRoom(w, e, p) || roomDist(w, e, p) >= 2.5f) return NULL;
	char snd[24];
	snprintf(snd, sizeof(snd), "scp/372/rustle%d", rand() % 3);
	sfxPlay(snd, 1.0f);
	if (!npcFind(NPC_372)) {
		Npc* n = npcGet(npcSpawn(NPC_372, p->pos, 0));
		if (n) {
			n->hidden = true;
			n->timer = 70 * 5;
		}
	}
	e->removed = true;
	return NULL;
}

static const char* evRoom2offices2(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (e->state == 0) {
		e->state = irand(1, 4);
		e->state2 = frand(0, 360);
	}
	if (!inRoom(w, e, p)) return NULL;
	float b = *p->blinkTimer;
	if (b < -8 && b > -12) {
		e->state = irand(1, 4);
		e->state2 = frand(0, 360);
	}
	return NULL;
}

static const char* evRoom966(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)w;
	(void)p;
	(void)dt;
	e->removed = true;
	return NULL;
}

static const char* evRoom4(World* w, Event* e, EvPlayer* p, float dt)
{
	e->state -= FRAMES(dt);
	if (e->state > 0) return NULL;
	e->state = 70 * 5;
	float c[3];
	roomCentre(w, e->room, c);
	if (inRoom(w, e, p) || distXZ(p->pos, c) >= 16.0f) return NULL;
	Npc* n = npcFind(NPC_049);
	if (!n || n->state != 2 || n->scripted || dist3(n->pos, p->pos) <= 16.0f) return NULL;
	float l[3] = { 368 * RSC, 528 * RSC, -176 * RSC };
	worldRoomToWorld(w, e->room, l, n->pos);
	npcSnapToFloor(w, n);
	n->npath = 0;
	e->removed = true;
	return NULL;
}

static const char* evRoom3pitduck(World* w, Event* e, EvPlayer* p, float dt)
{
	float o0[3];
	if (!inRoom(w, e, p) || !eventsRoomObject(w, e->room, 0, o0)) return NULL;
	if (!inView(p, o0)) {
		e->state += FRAMES(dt);
		if (rand() % 200 == 0 && e->state > 300) {
			e->state = 0;
			sfxStop(e->chan);
			e->chan = sfxPlay3D("scp/joke/saxophone", o0, 6.0f, 1.0f);
		}
	} else if (e->chan >= 0) {
		sfxStop(e->chan);
		e->chan = -1;
	}
	return NULL;
}

static const char* evMedibay(World* w, Event* e, EvPlayer* p, float dt)
{
	Npc* s = npcGet(e->npc[0]);
	if (s && s->type != NPC_SURGEON) s = NULL;
	if (!s && e->state > 0) s = npcFind(NPC_SURGEON);
	if (inRoom(w, e, p)) {
		if (e->state == 0) {
			float o3[3];
			if (!eventsRoomObject(w, e->room, 3, o3)) {
				e->removed = true;
				return NULL;
			}
			s = npcGet(npcSpawn(NPC_SURGEON, o3, (float)w->map.rooms[e->room].angle - 90));
			if (s) {
				s->state = -1;
				npcSnapToFloor(w, s);
			}
			e->state = 1;
		}
		if (s) {
			e->npc[0] = npcIndex(s);
			if (dist3(s->pos, p->pos) < 1.2f && e->state2 == 0) {
				s_lightBlink = 1.0f;
				sfxPlay("general/lightswitch", 1.0f);
				e->state2 = FRAMES(dt);
			}
		}
	}
	if (e->state2 > 0 && e->state2 < 70 * 4) {
		e->state2 += FRAMES(dt);
	} else if (e->state2 >= 70 * 4 && s && s->state < 0) {
		s->state = 2;
	}
	return NULL;
}

static const char* evRoom2offices035(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	bool released = false;
	for (int i = 0; i < s_nevents; i++)
		if (&s_events[i] != e && !s_events[i].removed && !strcmp(defName(&s_events[i]), "room035") && s_events[i].state < 0)
			released = true;
	if (!released || roomDist(w, e, p) >= 8) return NULL;
	float c[3];
	roomCentre(w, e->room, c);
	float a = (float)w->map.rooms[e->room].angle * DEG2RAD;
	c[0] += sinf(a) * 0.5f;
	c[2] += cosf(a) * 0.5f;
	spawnBody(w, NPC_CLASSD, c, (float)w->map.rooms[e->room].angle + 180, 19);
	e->removed = true;
	return NULL;
}

static const char* evTestroom(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	float o6[3];
	if (!inRoom(w, e, p) || !eventsRoomObject(w, e->room, 6, o6)) return NULL;
	if (dist3(p->pos, o6) < 2.5f) {
		sfxPlay("scp/079/testroomwarning", 1.0f);
		sfxPlay3D("general/hiss", o6, 8.0f, 1.0f);
		e->removed = true;
	}
	return NULL;
}

static void lcz2Draw(const World* w, const Event* e, const C3D_Mtx* view, int uLoc, C3D_Tex* white, const float cam[3])
{
	const char* name = roomName(w, e->room);
	float o[3];
	if (!strcmp(name, "room2offices2") && eventsRoomObject(w, e->room, (int)e->state, o) && dist3(o, cam) < 10.0f) {
		worldDrawModelEx(useModel(&s_duck, "romfs:/models/duck.scr"), o[0], o[1], o[2], e->state2, 0, 1, view, uLoc, white);
	}
	if (!strcmp(name, "room3pit") && eventsRoomObject(w, e->room, 0, o) && dist3(o, cam) < 10.0f &&
	    !strcmp(defName(e), "room3pitduck")) {
		float c[3];
		roomCentre(w, e->room, c);
		worldDrawModelEx(useModel(&s_duck, "romfs:/models/duck.scr"), o[0], o[1], o[2], yawTo(o, c) + 180, 0, 1, view, uLoc, white);
	}
	if (!strcmp(name, "room3pit") && !strcmp(defName(e), "room3pit1048") && e->state < 3 &&
	    eventsRoomObject(w, e->room, 0, o) && dist3(o, cam) < 10.0f) {
		worldDrawModelEx(useModel(&s_1048pp, "romfs:/models/1048pp.scr"), o[0], o[1] - 0.35f, o[2], e->state2, -90, 1, view, uLoc, white);
	}
	if (!strcmp(name, "room1lifts")) {
		for (int i = 0; i < 2; i++) {
			float l[3] = { (i ? -96 : 96) * RSC, 160 * RSC, -64 * RSC }, b[3];
			worldRoomToWorld(w, e->room, l, b);
			if (dist3(b, cam) < 8.0f) worldDrawModelEx(useModel(&s_button, "romfs:/models/button.scr"), b[0], b[1], b[2], (float)w->map.rooms[e->room].angle, 0, 1, view, uLoc, white);
		}
	}
	if (!strcmp(name, "medibay")) {
		roomCentre(w, e->room, o);
		if (dist3(o, cam) < 9.0f && worldRoomIndexAt(w, cam) == e->room) {
				o[1] = w->map.rooms[e->room].y;
			worldDrawModelEx(useModel(&s_medibayProps, "romfs:/models/medibay_props.scr"), o[0], o[1], o[2], (float)w->map.rooms[e->room].angle, 0, 1, view, uLoc, white);
		}
	}
}
