static const char* s_ending;
static bool s_nukeOn;
static Room s_exit1Terrain, s_gateaTunnel;
static bool s_gateMeshes;

const char* eventsTakeEnding(void)
{
	const char* e = s_ending;
	s_ending = NULL;
	return e;
}

void eventsSetNuke(bool on) { s_nukeOn = on; }
bool eventsNuke(void) { return s_nukeOn; }

static void loadGateMeshes(void)
{
	if (s_gateMeshes) return;
	s_gateMeshes = true;
	roomLoad(&s_exit1Terrain, "romfs:/models/exit1_terrain.scr");
	roomLoad(&s_gateaTunnel, "romfs:/models/gatea_tunnel.scr");
}

#define CAB_HALF (280.0f * RSC)

static bool inCab(const float pos[3], const float cab[3])
{
	return fabsf(pos[0] - cab[0]) < CAB_HALF && fabsf(pos[2] - cab[2]) < CAB_HALF && fabsf(pos[1] - cab[1]) < CAB_HALF + 0.4f;
}

static void elevator(World* w, Event* e, int d1, int d2, const float cab1[3], const float cab2[3], float* state,
                     EvPlayer* p, float dt)
{
	if (d1 < 0 || d2 < 0) return;
	Door* a = &w->doors[d1];
	Door* b = &w->doors[d2];
	a->elevator = b->elevator = true;
	if (*state == 0) *state = a->open ? -1 : 1;

	bool atA = a->open && !b->open && a->openstate >= 180;
	bool atB = b->open && !a->open && b->openstate >= 180;
	if (a->pressed) {
		if (atA) worldToggleDoor(w, d1);
		else if (atB) worldToggleDoor(w, d2);
	}
	if (b->pressed) {
		if (atB) worldToggleDoor(w, d2);
		else if (atA) worldToggleDoor(w, d1);
	}
	a->pressed = b->pressed = false;
	if (atA) *state = -1;
	if (atB) *state = 1;

	if (a->open || b->open || a->openstate > 0 || b->openstate > 0) return;
	bool inside;
	if (*state < 0) {
		*state -= FRAMES(dt);
		inside = inCab(p->pos, cab1);
		if (inside && !sfxPlaying(e->chan2)) e->chan2 = sfxPlay("general/elevator/moving", 1.0f);
		if (*state < -500) {
			if (inside) {
				p->pos[0] = cab2[0] + fmaxf(fminf(p->pos[0] - cab1[0], CAB_HALF - 0.22f), -CAB_HALF + 0.22f);
				p->pos[2] = cab2[2] + fmaxf(fminf(p->pos[2] - cab1[2], CAB_HALF - 0.22f), -CAB_HALF + 0.22f);
				p->pos[1] = cab2[1] + (p->pos[1] - cab1[1]) + 0.1f;
				worldStream(w, p->pos);
			}
			worldToggleDoor(w, d2);
			*state = 1;
		}
	} else {
		*state += FRAMES(dt);
		inside = inCab(p->pos, cab2);
		if (inside && !sfxPlaying(e->chan2)) e->chan2 = sfxPlay("general/elevator/moving", 1.0f);
		if (*state > 500) {
			if (inside) {
				p->pos[0] = cab1[0] + fmaxf(fminf(p->pos[0] - cab2[0], CAB_HALF - 0.22f), -CAB_HALF + 0.22f);
				p->pos[2] = cab1[2] + fmaxf(fminf(p->pos[2] - cab2[2], CAB_HALF - 0.22f), -CAB_HALF + 0.22f);
				p->pos[1] = cab1[1] + (p->pos[1] - cab2[1]) + 0.1f;
				worldStream(w, p->pos);
			}
			worldToggleDoor(w, d1);
			*state = -1;
		}
	}
}

static int findRoom(const World* w, const char* name)
{
	for (int i = 0; i < w->map.nrooms; i++)
		if (strcmp(roomName(w, i), name) == 0) return i;
	return -1;
}

static void clearNPCs(void)
{
	npcsReset();
	npc173SetIdle(true);
	float far[3] = { 0, -100, 0 };
	npc173Place(far, 0);
}

static void outdoors(World* w)
{
	w->fogColor = 0xC8C8C8;
	w->fogNear = 5.0f;
	w->fogFar = 45.0f;
}

static void indoors(World* w)
{
	w->fogColor = 0;
	w->fogNear = w->fogFar = 0;
}

static const char* evExit1(World* w, Event* e, EvPlayer* p, float dt)
{
	if (s_ending) return NULL;
	int d0 = worldRoomDoor(w, e->room, 0), d1 = worldRoomDoor(w, e->room, 1);
	float cab1[3], cab2[3];
	bool haveCabs = eventsRoomObject(w, e->room, 8, cab1) && eventsRoomObject(w, e->room, 9, cab2);
	if (haveCabs) {
		cab1[1] += 0.3f;
		cab2[1] += 0.3f;
		elevator(w, e, d0, d1, cab1, cab2, &e->state2, p, dt);
	}

	float local[3];
	worldWorldToRoom(w, e->room, p->pos, local);
	bool surface = inRoom(w, e, p) && local[1] > 1040.0f * RSC;
	if (!surface) {
		if (e->state >= 1) indoors(w);
		return NULL;
	}
	outdoors(w);

	if (e->state == 0) {
		loadGateMeshes();
		clearNPCs();
		float o0[3];
		if (eventsRoomObject(w, e->room, 0, o0))
			worldAddExtra(w, &s_exit1Terrain, o0[0], o0[1], o0[2], (float)w->map.rooms[e->room].angle, true);
		e->state = 1;
		e->chan = sfxPlay("character/apache/propeller", 0.6f);
	}

	if (e->state == 1) {
		float o10[3];
		if (eventsRoomObject(w, e->room, 10, o10) && distXZ(p->pos, o10) < 320 * RSC) {
			e->state = 2;
			e->state3 = 0;
			for (int k = 2; k <= 3; k++) {
				int dk = worldRoomDoor(w, e->room, k);
				if (dk >= 0) {
					w->doors[dk].open = false;
					w->doors[dk].locked = true;
				}
			}
			sfxPlay("ending/gateb/682battle", 1.0f);
		}
		return NULL;
	}

	float before = e->state3;
	e->state3 += FRAMES(dt);
	float t = e->state3;
#define PASSED(sec) (before < (sec) * 70 && t >= (sec) * 70)
	if (PASSED(35.0f)) sfxPlay("ending/gateb/detonatingalphawarheads", 1.0f);
	if (PASSED(26.5f)) sfxPlay("character/apache/crash1", 1.0f);
	if (t >= 45 * 70 && t < 75 * 70 && !sfxPlaying(e->chan)) e->chan = sfxPlay("ending/gateb/siren", 1.0f);
	if (PASSED(75.0f)) {
		sfxStop(e->chan);
		if (s_nukeOn) {
			sfxPlay("ending/gateb/nuke1", 1.0f);
			s_ending = "B2";
			return NULL;
		}
		sfxPlay("ending/gateb/alphawarheadsfail", 1.0f);

		float o18[3];
		if (eventsRoomObject(w, e->room, 18, o18))
			for (int i = 0; i < 3; i++) {
				float q[3] = { o18[0] + i * 0.4f, o18[1], o18[2] };
				e->npc[i] = npcSpawn(NPC_GUARD, q, yawTo(q, p->pos));
				Npc* n = npcGet(e->npc[i]);
				if (n) n->scripted = true;
			}
		int d5 = worldRoomDoor(w, e->room, 5);
		if (d5 >= 0 && !w->doors[d5].open) worldToggleDoor(w, d5);
		e->state = 3;
		e->state4 = 0;
	}
#undef PASSED
	if (e->state == 3) {
		bool caught = false;
		for (int i = 0; i < 3; i++) {
			Npc* n = npcGet(e->npc[i]);
			if (!n) continue;
			float d = distXZ(n->pos, p->pos);
			if (d < 3.0f) caught = true;
			if (d > 2.0f) {
				npcWalkTo(w, n, p->pos, 0.02f * 70.0f, dt);
				npcAnimate(n, 39, 76, 0.8f, true, dt);
			} else {
				npcFace(n, p->pos, 8, dt);
				npcAnimate(n, 302, 344, 0.35f, true, dt);
			}
		}
		if (caught) {
			if (e->state4 == 0) sfxPlay("ending/gateb/playerdetect", 1.0f);
			e->state4 += FRAMES(dt);
			if (e->state4 > 500) {
				sfxPlay("ending/gateb/gunshot", 1.0f);
				s_ending = "B3";
			}
		}
	}
	return NULL;
}

static int s_gateaDoor = -1;

static const char* evGateaentrance(World* w, Event* e, EvPlayer* p, float dt)
{
	int ga = findRoom(w, "gatea");
	if (ga < 0) {
		e->removed = true;
		return NULL;
	}
	if (s_gateaDoor < 0) {
		float dl[3] = { 1544 * RSC, 0, 64 * RSC };
		s_gateaDoor = worldAddRoomDoor(w, ga, dl, 90, DOOR_NORMAL);
	}
	float cab1[3], cab2[3];
	if (!eventsRoomObject(w, e->room, 0, cab1)) return NULL;
	cab1[1] += 0.3f;
	float cl[3] = { 1848 * RSC, 240 * RSC, 64 * RSC };
	worldRoomToWorld(w, ga, cl, cab2);
	elevator(w, e, worldRoomDoor(w, e->room, 0), s_gateaDoor, cab1, cab2, &e->state, p, dt);
	return NULL;
}

static const char* evGatea(World* w, Event* e, EvPlayer* p, float dt)
{
	if (s_ending) return NULL;
	if (!inRoom(w, e, p)) {
		if (e->state > 0) indoors(w);
		return NULL;
	}
	outdoors(w);
	Npc* old = npcFind(NPC_106);
	bool contained106 = eventsContained106();

	if (e->state == 0) {
		loadGateMeshes();
		float c[3];
		roomCentre(w, e->room, c);
		c[1] = w->map.rooms[e->room].y;
		worldAddExtra(w, &s_gateaTunnel, c[0], c[1], c[2], (float)w->map.rooms[e->room].angle, true);
		npc173SetIdle(true);
		float far[3] = { 0, -100, 0 };
		npc173Place(far, 0);
		if (!contained106) sfxPlay("ending/gatea/106escape", 1.0f);
		e->state = 1;
		e->chan = sfxPlay("character/apache/propeller", 0.6f);
	}
	float before = e->state;
	e->state += FRAMES(dt);

	if (!contained106) {

		float o3[3], o4[3];
		bool have = eventsRoomObject(w, e->room, 3, o3) && eventsRoomObject(w, e->room, 4, o4);
		if (have && old) {
			if (before < 350 && e->state >= 350) {
				old->scripted = true;
				old->hidden = false;
				memcpy(old->pos, o3, sizeof(o3));
				old->frame = 110;
				sfxPlay("horror/horror5", 1.0f);
				sfxPlay("scp/106/decay0", 1.0f);
			}
			if (e->state >= 350 && old->scripted && !old->hidden) {
				float t = e->state - 350;
				if (t < 150) {
					npcAnimate(old, 110, 259, 0.15f, false, dt);
				} else if (distXZ(old->pos, o4) > 1.0f && t < 1800) {
					npcWalkTo(w, old, o4, 0.012f * 70.0f, dt);
					npcAnimate(old, 284, 333, 0.5f, true, dt);
					if (rand() % 40 == 0) sfxPlay3D("general/gunshot", o3, 30.0f, 0.7f);
				} else {
					if (e->state2 == 0) {
						e->chan2 = sfxPlay("ending/gatea/franklin", 1.0f);
						e->state2 = 1;
					} else if (e->state2 == 1 && !sfxPlaying(e->chan2)) {
						e->chan2 = sfxPlay("ending/gatea/hidturret", 1.0f);
						e->state2 = 2;
					}
					npcAnimate(old, 259, 110, -0.1f, false, dt);
					old->pos[1] -= dt * 0.25f;
					if (old->pos[1] < o3[1] - 2.0f) {
						old->hidden = true;
						old->scripted = true;
						sfxPlay("ending/gatea/106retreat", 1.0f);
					}
				}
			}
		}

		float o11[3];
		if (!eventsRoomObject(w, e->room, 11, o11)) return NULL;
		bool sameLevel = fabsf(p->pos[1] - o11[1]) < 1.5f;
		float d = distXZ(p->pos, o11);
		if (e->state3 == 0 && sameLevel && d < 12.0f) {
			int d2 = worldRoomDoor(w, e->room, 2);
			if (d2 >= 0 && !w->doors[d2].open) worldToggleDoor(w, d2);
			e->state3 = 1;
		} else if (e->state3 == 1 && sameLevel && d < 7.0f) {
			sfxPlay("ending/gatea/bell1", 1.0f);
			e->state3 = 2;
		} else if (e->state3 >= 2) {
			float prev = e->state3;
			e->state3 += FRAMES(dt);

			p->pos[0] += (o11[0] - p->pos[0]) * fminf(1.0f, dt * 0.5f);
			p->pos[2] += (o11[2] - p->pos[2]) * fminf(1.0f, dt * 0.5f);
			if (prev < 230 && e->state3 >= 230) e->chan2 = sfxPlay("ending/gatea/ci", 1.0f);
			if (e->state3 >= 230 && !sfxPlaying(e->chan2)) {
				sfxPlay("ending/gatea/bell2", 1.0f);
				s_ending = "A1";
			}
		}
	} else {

		float c[3];
		roomCentre(w, e->room, c);
		if (e->state2 == 0) {
			float o15[3];
			if (!eventsRoomObject(w, e->room, 15, o15)) memcpy(o15, c, sizeof(c));
			for (int i = 0; i < 3; i++) {
				float q[3] = { o15[0] + i * 0.3f, o15[1], o15[2] + i * 0.3f };
				e->npc[i] = npcSpawn(NPC_GUARD, q, 0);
				Npc* n = npcGet(e->npc[i]);
				if (n) n->scripted = true;
			}
			e->state2 = 1;
		}
		bool caught = false;
		for (int i = 0; i < 3; i++) {
			Npc* n = npcGet(e->npc[i]);
			if (!n) continue;
			if (distXZ(n->pos, p->pos) < 6.0f) caught = true;
			if (distXZ(n->pos, p->pos) > 2.0f) {
				npcWalkTo(w, n, p->pos, 0.02f * 70.0f, dt);
				npcAnimate(n, 39, 76, 0.8f, true, dt);
			} else {
				npcFace(n, p->pos, 8, dt);
				npcAnimate(n, 302, 344, 0.35f, true, dt);
			}
		}
		if (caught && e->state2 == 1) {
			e->chan2 = sfxPlay("ending/gatea/stoprightthere", 1.0f);
			e->state2 = 2;
		} else if (e->state2 == 2 && !sfxPlaying(e->chan2)) {
			s_ending = "A2";
		}
	}
	return NULL;
}
