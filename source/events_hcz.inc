#define MAX_LEVERS 32

typedef struct {
	int room, k;
	float pos[3];
	float baseYaw, handleYaw, pitch, scale;
	bool locked;
} Lever;

static Lever s_levers[MAX_LEVERS];
static int s_nlevers;
static int s_leverHover = -1;

static const struct {
	const char* room;
	int k;
	float x, y, z, baseYaw, handleYaw, pitch, scale;
} s_leverDefs[] = {
	{ "room2nuke", 0, -975, 1712, -502, -270, -90, 10, 1 },
	{ "room2nuke", 1, -975, 1712, -370, -270, -90, 10, 1 },
	{ "room035", 0, 210, 224, -208, -270, -90, -80, 1 },
	{ "room035", 1, 210, 224, -132, -270, -90, -80, 1 },
	{ "room3storage", 0, 3101, -5461, 6568, 0, -180, -10, 1 },
	{ "room3storage", 1, 1209, -5461, 3164, 0, -180, -10, 1 },
	{ "room049", 0, 852, -3374, -854, 270, 90, 81, 0.75f },
	{ "room049", 1, -834, -3400, 1093, 180, 0, -11, 0.75f },
	{ "room012", 0, 240, -512, -364, 0, -180, 10, 1 },
	{ "room2servers", 0, -1260, 234, 750, 0, -180, 81, 0.75f },
	{ "room2servers", 1, -920, 164, 898, 0, -180, -81, 0.75f },
	{ "room2servers", 2, -837, 152, 886, 0, -180, -81, 0.75f },
	{ "coffin", 0, -800, 180, -336, 180, 0, 10, 1 },
	{ "room2ccont", 0, -240, 1104, 632, -90, 90, 10, 1 },
	{ "room2ccont", 1, -240, 1104, 568, -90, 90, 10, 1 },
	{ "room2ccont", 2, -240, 1104, 504, -90, 90, 10, 1 },
	{ "room106", 0, -555, -576, 3040, 0, -180, 81, 1 },
	{ "room106", 1, -474, -576, 3040, 0, -180, -81, 1 },
	{ "room2sl", 0, -49, 689, 912, 0, -180, 10, 1 },
};

static Room s_leverBase, s_leverHandle, s_lid008, s_cage106, s_button, s_box012, s_comp012, s_pc079;

static void leversInit(const World* w)
{
	s_nlevers = 0;
	s_leverHover = -1;
	for (int r = 0; r < w->map.nrooms; r++) {
		const char* name = roomName(w, r);
		for (size_t i = 0; i < sizeof(s_leverDefs) / sizeof(s_leverDefs[0]) && s_nlevers < MAX_LEVERS; i++) {
			if (strcmp(s_leverDefs[i].room, name) != 0) continue;
			Lever* l = &s_levers[s_nlevers++];
			float local[3] = { s_leverDefs[i].x * RSC, s_leverDefs[i].y * RSC, -s_leverDefs[i].z * RSC };
			worldRoomToWorld(w, r, local, l->pos);
			l->room = r;
			l->k = s_leverDefs[i].k;
			l->baseYaw = s_leverDefs[i].baseYaw + w->map.rooms[r].angle;
			l->handleYaw = s_leverDefs[i].handleYaw + w->map.rooms[r].angle;
			l->pitch = s_leverDefs[i].pitch > 0 ? 80 : -80;
			l->scale = s_leverDefs[i].scale;
			l->locked = false;
		}
	}
}

static Lever* lever(int room, int k)
{
	for (int i = 0; i < s_nlevers; i++)
		if (s_levers[i].room == room && s_levers[i].k == k) return &s_levers[i];
	return NULL;
}

static bool leverOn(int room, int k)
{
	Lever* l = lever(room, k);
	return l && l->pitch > 0;
}

static void leverLock(int room, int k, bool locked)
{
	Lever* l = lever(room, k);
	if (l) l->locked = locked;
}

static float aimAt(const EvPlayer* p, const float target[3], float reach);

static void leversUpdate(const EvPlayer* p)
{
	s_leverHover = -1;
	float best = 0.9f;
	for (int i = 0; i < s_nlevers; i++) {
		Lever* l = &s_levers[i];
		if (l->locked) continue;
		float a = aimAt(p, l->pos, 0.9f);
		if (a > best) {
			best = a;
			s_leverHover = i;
		}
	}
}

static bool leversInteract(void)
{
	if (s_leverHover < 0) return false;
	Lever* l = &s_levers[s_leverHover];
	if (l->locked) return false;
	l->pitch = l->pitch > 0 ? -80 : 80;
	playAt("interact/leverflip", l->pos, 4.0f);
	return true;
}

static Event* s_useEvent;
static int s_useAction;

static void offerUse(Event* e, const EvPlayer* p, const float spot[3], float reach, int action)
{
	if (aimAt(p, spot, reach) > 0.9f) {
		s_useEvent = e;
		s_useAction = action;
	}
}

static bool useInteract(void)
{
	if (!s_useEvent) return false;
	s_useEvent->pressed = s_useAction;
	return true;
}

static float curveValue(float target, float cur, float smooth, float dt)
{
	return cur + (target - cur) * fminf(1.0f, FRAMES(dt) / smooth);
}

static Npc* npcNear(NpcType type, const float pos[3], float r)
{
	for (int i = 0; i < MAX_NPCS; i++) {
		Npc* n = npcGet(i);
		if (n && n->type == type && dist3(n->pos, pos) < r) return n;
	}
	return NULL;
}

static int npcIndex(const Npc* n)
{
	for (int i = 0; i < MAX_NPCS; i++)
		if (npcGet(i) == n) return i;
	return -1;
}

static bool s_soundTransmission;

static const char* evRoom106(World* w, Event* e, EvPlayer* p, float dt)
{
	float o5[3], o6[3], button[3];
	if (!eventsRoomObject(w, e->room, 5, o5) || !eventsRoomObject(w, e->room, 6, o6)) {
		e->removed = true;
		return NULL;
	}
	float bl[3] = { -146 * RSC, -576 * RSC, -3045 * RSC };
	worldRoomToWorld(w, e->room, bl, button);
	if (e->state4 == 0) e->state4 = -1280 * RSC;

	if (s_soundTransmission && e->state == 1) e->state3 = fminf(e->state3 + FRAMES(dt), 4000);

	Npc* d = npcGet(e->npc[0]);
	if (!d) d = npcNear(NPC_CLASSD, o5, 1.0f);
	if (!d) {
		int i = npcSpawn(NPC_CLASSD, o5, (float)w->map.rooms[e->room].angle + 90);
		d = npcGet(i);
	}
	if (d) {
		e->npc[0] = npcIndex(d);
		d->scripted = true;
		d->pos[0] = o5[0];
		d->pos[1] = o5[1] + 0.1f - 0.32f;
		d->pos[2] = o5[2];
	}

	if (!inRoom(w, e, p)) {
		if (e->chan >= 0 && !s_soundTransmission) {
			sfxStop(e->chan);
			e->chan = -1;
		}
		return NULL;
	}
	if (d) {
		if (d->state3 == 0) {
			npcAnimate(d, 17, 19, 0.01f, false, dt);
			if (d->frame >= 19) d->state3 = 1;
		} else {
			d->frame = fmaxf(17, d->frame - 0.01f * FRAMES(dt));
			if (d->frame <= 17) d->state3 = 0;
		}
	}

	float cy = e->state4;
	leverLock(e->room, 0, cy < -990 * RSC && cy > -1275 * RSC);

	bool magnets = leverOn(e->room, 0);
	if (e->state5 == 0) e->state5 = magnets ? 1 : 2;
	if ((magnets ? 1 : 2) != e->state5) {
		e->state5 = magnets ? 1 : 2;
		e->state2 = magnets;
		sfxPlay(magnets ? "room/106chamber/magnetup" : "room/106chamber/magnetdown", 1.0f);
	}
	bool lockTransmission = e->state == 1 && e->state3 >= 2500 && e->state3 <= 3200;
	leverLock(e->room, 1, lockTransmission);
	s_soundTransmission = leverOn(e->room, 1);
	if (s_soundTransmission) {
		if (!sfxPlaying(e->chan)) e->chan = sfxPlay("radio/static", 0.6f);
	} else {
		sfxStop(e->chan);
		e->chan = -1;
		sfxStop(e->chan2);
		e->chan2 = -1;
	}

	if (e->state == 0) {
		if (s_soundTransmission && rand() % 100 == 0 && !sfxPlaying(e->chan2)) {
			char snd[40];
			snprintf(snd, sizeof(snd), "character/luresubject/idle%d", 1 + rand() % 6);
			e->chan2 = sfxPlay(snd, 1.0f);
		}
		if (s_soundTransmission) {
			offerUse(e, p, button, 1.0f, 1);
			if (e->pressed == 1) {
				sfxPlay("interact/button", 1.0f);
				e->state = 1;
				sfxStop(e->chan2);
				e->chan2 = sfxPlay("room/106chamber/femurbreaker", 1.0f);
			}
		}
	} else if (e->state == 1) {
		if (s_soundTransmission && e->state3 < 2000 && !sfxPlaying(e->chan2))
			e->chan2 = sfxPlay("character/luresubject/sniffling", 1.0f);
		Npc* o = npcFind(NPC_106);
		float prev = e->state3 - FRAMES(dt);
		if (e->state3 >= 2500 && o) {
			if (e->state2 != 0 && prev < 2500) {
				o->pos[0] = o6[0];
				o->pos[1] = o6[1];
				o->pos[2] = o6[2];
				release106(o, true);
				e->state = 2;
			} else if (e->state3 > 3200) {
				if (e->state2 != 0) {
					s_contained106 = true;
					o->scripted = true;
					o->hidden = true;
					sfxStop(o->chan);
					o->chan = -1;
				} else {
					o->pos[0] = o6[0];
					o->pos[1] = o6[1];
					o->pos[2] = o6[2];
					release106(o, true);
				}
				e->state = 2;
			} else {
				o->scripted = true;
				o->hidden = false;
				o->pos[0] = o5[0];
				o->pos[2] = o5[2];
				o->pos[1] = w->map.rooms[e->room].y + (700.0f + 108.0f * (fminf(e->state3 - 2500, 800) / 320.0f)) * RSC;
				o->yaw = (float)w->map.rooms[e->room].angle + 180;
				npcAnimate(o, 206, 250, 0.1f, true, dt);
				if (prev < 2500) {
					sfxStop(e->chan2);
					e->chan2 = sfxPlay("character/luresubject/106bait", 1.0f);
				}
			}
		}
	}

	float target = e->state2 != 0 ? -980 * RSC : -1280 * RSC;
	e->state4 = curveValue(target, e->state4, 200, dt);
	if (fabsf(e->state4 - target) < 0.002f) e->state4 = target;
	e->pressed = 0;
	return NULL;
}

static const char* ev008(World* w, Event* e, EvPlayer* p, float dt)
{
	if (e->state3 == 0) e->state3 = 89;
	if (!inRoom(w, e, p) || e->state >= 3) return NULL;
	float o0[3], o3[3], o4[3];
	if (!eventsRoomObject(w, e->room, 0, o0) || !eventsRoomObject(w, e->room, 3, o3) ||
	    !eventsRoomObject(w, e->room, 4, o4)) {
		e->removed = true;
		return NULL;
	}
	float ry = w->map.rooms[e->room].y;
	if (e->state == 0) {
		if (!npc173IsIdle() && npc173Distance() > 15.0f) {
			float s[3] = { o3[0], ry, o3[2] };
			npc173Place(s, 0);
		}
		e->state = 1;
	} else if (e->state == 1) {
		sfxLoop3D(&e->chan, "alarm/alarm", o0, 5.0f, 1.0f);
		float dist = dist3(p->pos, o0);
		if (dist < 2.0f) {
			int d0 = worldRoomDoor(w, e->room, 0), d1 = worldRoomDoor(w, e->room, 1);
			if (d0 >= 0) w->doors[d0].locked = true;
			if (d1 >= 0) w->doors[d1].locked = true;
			if (e->state2 == 0) {
				float s[3];
				npc173GetPos(s);
				if (dist3(s, o4) < 3.0f && (p->eyesClosed || !scp173InView(p)) && !npc173IsIdle()) {
					float t[3] = { o4[0], ry, o4[2] };
					npc173Place(t, 0);
					if (!g_wear.hazmat) {
						*p->injuries += 0.1f;
						if (g_wear.infect == 0) g_wear.infect = 1;
						hudMessage("The window shattered and a piece of glass cut your arm.", 8);
					}
					playAt("general/glassbreak", o0, 10.0f);
					e->state2 = 1;
				}
			}
			if (dist < 1.0f) {
				offerUse(e, p, o0, 1.2f, 1);
				if (e->pressed == 1) e->state4 = 1;
			}
		}
		if (e->state4 != 0) e->state3 = fmaxf(35, e->state3 - FRAMES(dt) * 1.2f);
		if (e->state3 < 40) {
			e->state = 2;
			sfxPlay("interact/leverflip", 1.0f);
			sfxStop(e->chan);
			e->chan = -1;
		}
	} else {
		for (int k = 0; k < 3; k++) {
			int d = worldRoomDoor(w, e->room, k);
			if (d >= 0) w->doors[d].locked = false;
		}
		e->state3 = curveValue(1, e->state3, 15, dt);
		if (e->state3 <= 1.5f) e->state = 3;
	}
	e->pressed = 0;
	return NULL;
}

bool eventsInfectScene(World* w, float pos[3], float* yaw)
{
	int r = findRoom(w, "008");
	float o6[3], o7[3];
	if (r < 0 || !eventsRoomObject(w, r, 6, o6) || !eventsRoomObject(w, r, 7, o7)) return false;
	pos[0] = o7[0];
	pos[1] = w->map.rooms[r].y + 0.32f;
	pos[2] = o7[2];
	float d[3] = { o6[0], w->map.rooms[r].y, o6[2] };
	Npc* n = npcGet(npcSpawn(NPC_CLASSD, d, yawTo(d, pos)));
	if (n) {
		n->scripted = true;
		n->frame = 357;
	}
	*yaw = atan2f(o6[0] - pos[0], -(o6[2] - pos[2])) / DEG2RAD;
	sfxPlay("scp/008/killscientist1", 1.0f);
	return true;
}

static const char* evRoom2tesla(World* w, Event* e, EvPlayer* p, float dt)
{
	float spot[3][3], o2[3], o3[3];
	for (int i = 0; i < 3; i++)
		if (!eventsRoomObject(w, e->room, i, spot[i])) {
			e->removed = true;
			return NULL;
		}
	eventsRoomObject(w, e->room, 2, o2);
	o3[0] = o2[0];
	o3[1] = o2[1] + 0.8f;
	o3[2] = o2[2];
	float ry = w->map.rooms[e->room].y;
	const char* death = NULL;
	float dist = roomDist(w, e, p);

	bool armed = !(e->state2 > 70 * 3.5f && e->state2 < 70 * 90);
	if (armed && p->pos[1] > ry && p->pos[1] < ry + 4.0f) {
		Npc* o = npcFind(NPC_106);
		bool o106 = o && !o->hidden && !o->scripted;
		if (e->state == 0) {
			if (dist < 8) {
				if (!sfxPlaying(e->chan)) e->chan = sfxPlay3D("room/tesla/idle", o3, 4.0f, 0.5f);
				for (int i = 0; i < 3; i++)
					if (distXZ(p->pos, spot[i]) < 300 * RSC) {
						sfxStop(e->chan);
						e->chan = sfxPlay3D("room/tesla/windup", o3, 4.0f, 0.5f);
						e->state = 1;
						break;
					}
			}
			if (o106 && e->state == 0)
				for (int i = 0; i < 3; i++)
					if (distXZ(o->pos, spot[i]) < 300 * RSC) {
						sfxStop(e->chan);
						e->chan = sfxPlay3D("room/tesla/windup", o3, 4.0f, 0.5f);
						e->state = 1;
						break;
					}
		} else {
			float prev = e->state;
			e->state += FRAMES(dt);
			if (e->state > 40) {
				if (prev <= 40) {
					if (dist < 2) sfxPlay("room/tesla/shock", 1.0f);
					else sfxPlay3D("room/tesla/shock", o2, 10.0f, 1.0f);
				}
				if (e->state < 70) {
					for (int i = 0; i < 3; i++)
						if (distXZ(p->pos, spot[i]) < 250 * RSC && !death)
							death = "Subject D-9341 killed by the Tesla gate at [REDACTED].";
					if (o106)
						for (int i = 0; i < 3; i++)
							if (distXZ(o->pos, spot[i]) < 250 * RSC) {
								o->state = 0;
								o->hidden = true;
								o->timer = 70 * 60 * (10 + rand() % 4);
								sfxStop(o->chan);
								o->chan = -1;
								break;
							}
				} else {
					if (prev < 70) {
						sfxStop(e->chan);
						e->chan = sfxPlay3D("room/tesla/powerup", o3, 4.0f, 0.5f);
					}
					if (e->state > 150) e->state = 0;
				}
			}
		}
	}

	if (!w->isolated) {
		if (e->state2 == 0) {
			if (e->state3 <= 0) {
				bool mtf = false;
				float c[3];
				roomCentre(w, e->room, c);
				for (int i = 0; i < MAX_NPCS; i++) {
					Npc* n = npcGet(i);
					if (!n || n->type != NPC_MTF) continue;
					if (fabsf(n->pos[0] - c[0]) < 4.0f && fabsf(n->pos[2] - c[2]) < 4.0f) {
						mtf = true;
						sfxPlay3D("character/mtf/tesla0", n->pos, 10.0f, 1.0f);
						e->state2 = 70 * 100;
						break;
					}
				}
				if (!mtf) e->state2 = 70 * 3.5f;
				e->state3 += 140;
			} else {
				e->state3 -= FRAMES(dt);
			}
		} else {
			float prev = e->state2;
			e->state2 = fmaxf(e->state2 - FRAMES(dt), 0);
			if (prev >= 70 * 92 && e->state2 < 70 * 92) {
				char snd[40];
				snprintf(snd, sizeof(snd), "character/mtf/tesla%d", 1 + rand() % 3);
				sfxPlay(snd, 1.0f);
			}
		}
	}
	bool clerkUsed = false;
	for (int i = 0; i < s_nevents; i++)
		if (s_events[i].def == e->def && s_events[i].state4 != 0) clerkUsed = true;
	Npc* clerk = npcGet(e->npc[0]);
	if (clerk && clerk->type != NPC_CLERK) clerk = NULL;
	if (!clerkUsed && inRoom(w, e, p)) {
		float o5[3], o6[3];
		if (eventsRoomObject(w, e->room, 5, o5) && eventsRoomObject(w, e->room, 6, o6)) {
			float* from = dist3(o5, p->pos) < dist3(o6, p->pos) ? o6 : o5;
			from[1] = ry;
			e->npc[0] = npcSpawn(NPC_CLERK, from, yawTo(from, o2));
			clerk = npcGet(e->npc[0]);
			if (clerk) {
				clerk->scripted = true;
				npcSnapToFloor(w, clerk);
			}
			e->state4 = 1;
		}
	}
	if (clerk && e->state4 == 1) {
		clerk->currSpeed = curveValue(0.03f, clerk->currSpeed, 5, dt);
		npcAnimate(clerk, 301, 319, clerk->currSpeed * 18, true, dt);
		npcWalkTo(w, clerk, o2, FRAMES(clerk->currSpeed), dt);
		if (e->state == 0)
			for (int i = 0; i < 3; i++)
				if (distXZ(clerk->pos, spot[i]) < 400 * RSC) {
					sfxStop(e->chan);
					e->chan = sfxPlay3D("room/tesla/windup", o3, 4.0f, 0.5f);
					e->state = 1;
					break;
				}
		if (e->state > 40 && e->state < 70) e->state4 = 2;
	} else if (clerk && e->state4 == 2) {
		npcAnimate(clerk, 41, 60, 0.5f, false, dt);
		if (clerk->frame >= 60) {
			clerk->dead = true;
			e->state4 = 3;
		}
	}
	return death;
}

static const char* evRoom2nuke(World* w, Event* e, EvPlayer* p, float dt)
{
	if (e->state3 == 0) {
		float o[3];
		if (eventsRoomObject(w, e->room, 6, o)) spawnBody(w, NPC_CLASSD, o, (float)w->map.rooms[e->room].angle + 90, 40);
		e->state3 = 1;
	}
	if (!inRoom(w, e, p)) return NULL;
	float c1[3], c2[3];
	if (eventsRoomObject(w, e->room, 4, c1) && eventsRoomObject(w, e->room, 5, c2))
		elevator(w, e, worldRoomDoor(w, e->room, 0), worldRoomDoor(w, e->room, 1), c1, c2, &e->state2, p, dt);
	s_nukeOn = leverOn(e->room, 0);
	return NULL;
}

static const char* evRoom049(World* w, Event* e, EvPlayer* p, float dt)
{
	float o[13][3];
	for (int i = 0; i <= 12; i++)
		if (i <= 5 || i >= 10)
			if (!eventsRoomObject(w, e->room, i, o[i])) {
				e->removed = true;
				return NULL;
			}
	int door[7];
	for (int i = 0; i < 7; i++) door[i] = worldRoomDoor(w, e->room, i);
	float ry = w->map.rooms[e->room].y;

	if (!inRoom(w, e, p) || p->pos[1] > ry - 2848 * RSC) {
		elevator(w, e, door[0], door[1], o[0], o[1], &e->state2, p, dt);
		elevator(w, e, door[2], door[3], o[2], o[3], &e->state3, p, dt);
		return NULL;
	}
	Npc* s049 = npcGet(e->npc[0]);
	if (e->state == 0) {
		sfxPlay("room/blackout", 1.0f);
		const float* paper = dist3(o[11], p->pos) < dist3(o[12], p->pos) ? o[11] : o[12];
		itemsSpawnAt("research sector-02 scheme", "paper", paper);
		for (int k = 0; k < 2; k++) {
			Npc* z = npcGet(npcSpawn(NPC_ZOMBIE, o[4 + k], 0));
			if (z) {
				float c[3];
				roomCentre(w, e->room, c);
				z->yaw = yawTo(z->pos, c) + (k ? 20 : 190);
				npcSnapToFloor(w, z);
			}
		}
		s049 = npcFind(NPC_049);
		if (!s049) s049 = npcGet(npcSpawn(NPC_049, o[4], 0));
		if (s049) {
			s049->scripted = true;
			s049->hidden = true;
			e->npc[0] = npcIndex(s049);
		}
		e->state = 1;
		e->state4 = leverOn(e->room, 1);
		return NULL;
	}
	if (!s049) {
		s049 = npcFind(NPC_049);
		if (s049) e->npc[0] = npcIndex(s049);
	}

	bool feedOff = !leverOn(e->room, 0);
	bool gen = leverOn(e->room, 1);
	if (door[1] >= 0) {
		w->doors[door[1]].locked = true;
		w->doors[door[1]].elevator = false;
	}
	if (door[3] >= 0) {
		w->doors[door[3]].locked = true;
		w->doors[door[3]].elevator = false;
	}
	if (gen != (e->state4 != 0)) sfxPlay(gen ? "room/tesla/powerup" : "general/lightswitch", 1.0f);
	e->state4 = gen;

	if (e->state >= 70) {
		if (gen) {
			e->state = fmaxf(e->state, 70 * 180);
			sfxLoop3D(&e->chan, "ambient/room ambience/fuelpump", o[10], 6.0f, 1.0f);
		} else {
			sfxStop(e->chan);
			e->chan = -1;
		}
		for (int i = 4; i <= 6; i++)
			if (door[i] >= 0) w->doors[door[i]].locked = !gen;
	} else {
		e->state = fminf(e->state + FRAMES(dt), 70);
	}

	if (feedOff && gen) {
		if (door[1] >= 0) w->doors[door[1]].locked = false;
		if (door[3] >= 0) w->doors[door[3]].locked = false;
		elevator(w, e, door[0], door[1], o[0], o[1], &e->state2, p, dt);
		elevator(w, e, door[2], door[3], o[2], o[3], &e->state3, p, dt);
		if (s049 && s049->scripted) {
			int i = 0;
			if (door[1] >= 0) {
				float dp[3] = { w->doors[door[1]].x, w->doors[door[1]].y, w->doors[door[1]].z };
				if (dist3(p->pos, dp) < 3.0f) i = 1;
			}
			if (!i && door[3] >= 0) {
				float dp[3] = { w->doors[door[3]].x, w->doors[door[3]].y, w->doors[door[3]].z };
				if (dist3(p->pos, dp) < 3.0f) i = 3;
			}
			if (i) {
				s049->pos[0] = o[i][0];
				s049->pos[1] = o[i][1];
				s049->pos[2] = o[i][2];
				npcSnapToFloor(w, s049);
				playAt("general/elevator/beep", o[i], 4.0f);
				if (!w->doors[door[i]].open) worldToggleDoor(w, door[i]);
				if (w->doors[door[i - 1]].open) worldToggleDoor(w, door[i - 1]);
				sfxPlay3D("scp/049/detectedinchamber", s049->pos, 10.0f, 1.0f);
				s049->scripted = false;
				s049->hidden = false;
				s049->state = 2;
				s049->placed = true;
			}
		}
	}

	if (e->state < 70 * 190) {
		if (e->state >= 70 * 180) {
			for (int i = 0; i < 4; i++) {
				if (door[i] < 0) continue;
				bool open = i == 0 || i == 2;
				if (w->doors[door[i]].open != open) worldToggleDoor(w, door[i]);
			}
			e->state = 70 * 190;
		}
	} else if (e->state < 70 * 240) {
		for (int i = 0; i < MAX_NPCS; i++) {
			Npc* n = npcGet(i);
			if (n && n->type == NPC_ZOMBIE && n->state == 0) {
				n->state = 1;
				n->frame = 155;
			}
		}
		e->state = 70 * 241;
	}
	return NULL;
}

static void lcz2Draw(const World* w, const Event* e, const C3D_Mtx* view, int uLoc, C3D_Tex* white, const float cam[3]);
static void forestDraw(const World* w, const Event* e, const C3D_Mtx* view, int uLoc, C3D_Tex* white, const float cam[3]);

void eventsDraw(const World* w, const C3D_Mtx* view, int uLoc, C3D_Tex* white, const float cam[3])
{
	if (w->isolated || w->introActive) return;
	for (int i = 0; i < s_nlevers; i++) {
		const Lever* l = &s_levers[i];
		if (dist3(l->pos, cam) > 10.0f) continue;
		float s = l->scale;
		worldDrawModelEx(useModel(&s_leverBase, "romfs:/models/leverbase.scr"), l->pos[0], l->pos[1], l->pos[2], l->baseYaw, 0, s, view, uLoc, white);
		worldDrawModelEx(useModel(&s_leverHandle, "romfs:/models/leverhandle.scr"), l->pos[0], l->pos[1], l->pos[2], l->handleYaw, l->pitch, s, view, uLoc, white);
	}
	for (int i = 0; i < s_nevents; i++) {
		const Event* e = &s_events[i];
		if (e->removed) continue;
		const char* name = roomName(w, e->room);
		float o[3];
		lcz2Draw(w, e, view, uLoc, white, cam);
		forestDraw(w, e, view, uLoc, white, cam);
		if (strcmp(name, "008") == 0 && eventsRoomObject(w, e->room, 1, o) && dist3(o, cam) < 12.0f)
			worldDrawModelEx(useModel(&s_lid008, "romfs:/models/008_lid.scr"), o[0], o[1], o[2], (float)w->map.rooms[e->room].angle, e->state3 ? e->state3 : 89, 1,
			                 view, uLoc, white);
		if (strcmp(name, "room106") == 0 && eventsRoomObject(w, e->room, 6, o) && dist3(o, cam) < 30.0f) {
			float y = w->map.rooms[e->room].y + (e->state4 ? e->state4 : -1280 * RSC);
			worldDrawModelEx(useModel(&s_cage106, "romfs:/models/room1062.scr"), o[0], y, o[2], (float)w->map.rooms[e->room].angle, 0, 1, view, uLoc, white);
			float bl[3] = { -146 * RSC, -576 * RSC, -3045 * RSC }, b[3];
			worldRoomToWorld(w, e->room, bl, b);
			worldDrawModelEx(useModel(&s_button, "romfs:/models/button.scr"), b[0], b[1], b[2], (float)w->map.rooms[e->room].angle, 0, 1, view, uLoc, white);
		}
		float ra = (float)w->map.rooms[e->room].angle;
		if (strcmp(name, "room012") == 0 && eventsRoomObject(w, e->room, 2, o) && dist3(o, cam) < 12.0f) {
			float y = w->map.rooms[e->room].y + (-130 - 448 * sinf(e->state * DEG2RAD)) * RSC;
			worldDrawModelEx(useModel(&s_box012, "romfs:/models/012_box.scr"), o[0], y, o[2], ra, 0, 1, view, uLoc, white);
			worldDrawModelEx(useModel(&s_comp012, "romfs:/models/012_comp.scr"), o[0], y, o[2], ra, 0, 1, view, uLoc, white);
		}
		if (strcmp(name, "room079") == 0 && eventsRoomObject(w, e->room, 0, o) && dist3(o, cam) < 15.0f)
			worldDrawModelEx(useModel(&s_pc079, "romfs:/models/079.scr"), o[0], o[1], o[2], ra + 180, 0, 1, view, uLoc, white);
	}
}

const char* eventsDebugSpot(const World* w, int i, float pos[3], float* yaw)
{
	static const struct {
		const char* room;
		int lever, obj;
		float lx, ly, lz;
		const char* label;
		float step;
	} spots[] = {
		{ "room106", 0, -1, 0, 0, 0, "femur breaker: magnet lever" },
		{ "room106", 1, -1, 0, 0, 0, "femur breaker: sound transmission" },
		{ "room106", -1, -1, -146, -576, 3045, "femur breaker: button" },
		{ "room049", -1, 0, 0, 0, 0, "room049 elevator" },
		{ "room049", 1, -1, 0, 0, 0, "room049 generator" },
		{ "room049", 0, -1, 0, 0, 0, "room049 power feed" },
		{ "room2tesla", -1, 5, 0, 0, 0, "Tesla gate" },
		{ "room2tesla_hcz", -1, 5, 0, 0, 0, "Tesla gate (HCZ)" },
		{ "room2nuke", 0, -1, 0, 0, 0, "warhead lever" },
		{ "room2servers", -1, 6, 0, 0, 0, "server room (096)", 1.25f },
		{ "008", -1, 0, 0, 0, 0, "SCP-008 lid" },
		{ "room2ccont", 2, -1, 0, 0, 0, "remote door control" },
		{ "room2ccont", 1, -1, 0, 0, 0, "electrical centre lights" },
		{ "room012", -1, 3, 0, 0, 0, "SCP-012", 1.5f },
		{ "room035", -1, 3, 0, 0, 0, "SCP-035", 0.5f },
		{ "room079", -1, 0, 0, 0, 0, "SCP-079 (remote doors off)", 2.0f },
		{ "coffin", -1, 1, 0, 0, 0, "SCP-895", 2.5f },
		{ "room2tunnel", -1, 2, 0, 0, 0, "maintenance tunnel elevator", 0.01f },
		{ "room860", -1, 3, 0, 0, 0, "SCP-860 door (needs the key)", 0.8f },
		{ "room2cafeteria", -1, 0, 0, 0, 0, "SCP-294", 1.0f },
		{ "room1162", -1, 0, 0, 0, 0, "SCP-1162", 1.0f },
		{ "room2storage", -1, -1, -600, 0, 0, "SCP-970 corridor", 0.01f },
		{ "room2closets", -1, 0, 0, 0, 0, "storeroom (Class-Ds)", 2.0f },
		{ "room205", -1, 0, 0, 0, 0, "SCP-205", 3.0f },
		{ "room1123", -1, 3, 0, 0, 0, "SCP-1123", 1.0f },
		{ "room3pit", -1, 0, 0, 0, 0, "room3pit (1048 / duck)", 2.0f },
		{ "room2sl", -1, 7, 0, 0, 0, "room2sl (049)", 3.0f },
	};
	int n = (int)(sizeof(spots) / sizeof(spots[0]));
	for (int t = 0; t < n; t++) {
		int s = (((i + t) % n) + n) % n;
		int r = findRoom(w, spots[s].room);
		if (r < 0) continue;
		float target[3];
		if (spots[s].lever >= 0) {
			Lever* l = lever(r, spots[s].lever);
			if (!l) continue;
			memcpy(target, l->pos, sizeof(target));
		} else if (spots[s].obj >= 0) {
			if (!eventsRoomObject(w, r, spots[s].obj, target)) continue;
		} else {
			float l[3] = { spots[s].lx * RSC, spots[s].ly * RSC, -spots[s].lz * RSC };
			worldRoomToWorld(w, r, l, target);
		}
		float c[3];
		roomCentre(w, r, c);
		float dx = c[0] - target[0], dz = c[2] - target[2], len = sqrtf(dx * dx + dz * dz);
		if (spots[s].lever >= 0) {
			float a = lever(r, spots[s].lever)->baseYaw * DEG2RAD;
			dx = sinf(a);
			dz = cosf(a);
			len = 1;
		}
		float step = spots[s].step ? spots[s].step : spots[s].obj >= 0 ? 0.0f : 0.7f;
		if (len > 1e-3f) {
			pos[0] = target[0] + dx / len * step;
			pos[2] = target[2] + dz / len * step;
		} else {
			pos[0] = target[0];
			pos[2] = target[2] + step;
		}
		pos[1] = target[1] + 0.35f;
		*yaw = atan2f(target[0] - pos[0], -(target[2] - pos[2])) / DEG2RAD;
		if (!strcmp(spots[s].room, "room079")) s_remoteDoorOn = false;
		return spots[s].label;
	}
	return NULL;
}
