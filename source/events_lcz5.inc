static void giveHidden(const char* name, const char* tempname, int slot)
{
	int t = itemsFindTemplate(name, tempname);
	if (t < 0 || slot < 0 || slot >= INV_SLOTS) return;
	if (g_items.selected == slot) g_items.selected = -1;
	g_items.inv[slot] = t;
	g_items.invNote[slot][0] = 0;
}

static const char* evRoom2storage(World* w, Event* e, EvPlayer* p, float dt)
{
	int door[6];
	for (int i = 0; i < 6; i++) door[i] = worldRoomDoor(w, e->room, i);
	Npc* corpse = npcGet(e->npc[0]);
	Npc* guard = npcGet(e->npc[1]);
	if (inRoom(w, e, p)) {
		if (e->state2 <= 0) {
			float s[3];
			npc173GetPos(s);
			Npc* o = npcFind(NPC_106);
			bool danger = dist3(p->pos, s) < 8.0f || (o && !o->hidden && dist3(p->pos, o->pos) < 8.0f);
			if (door[1] >= 0) w->doors[door[1]].locked = danger;
			if (door[4] >= 0) w->doors[door[4]].locked = danger;
			e->state2 = 70 * 5;
		} else {
			e->state2 -= FRAMES(dt);
		}
		float local[3];
		worldWorldToRoom(w, e->room, p->pos, local);
		int dir = local[0] > 730 * RSC ? -1 : local[0] < -730 * RSC ? 1 : 0;
		if (dir) {

			for (int i = 1; i <= 2; i++) {
				int from = dir < 0 ? door[i + 2] : door[i], to = dir < 0 ? door[i] : door[i + 2];
				if (from < 0 || to < 0) continue;
				w->doors[to].open = w->doors[from].open;
				w->doors[to].openstate = w->doors[from].openstate;
				w->doors[from].open = false;
				w->doors[from].openstate = 0;
			}
			local[0] += dir * 1024 * RSC;
			float wp[3];
			worldRoomToWorld(w, e->room, local, wp);
			p->pos[0] = wp[0];
			p->pos[2] = wp[2];

			for (int i = 0; i < g_items.nitems; i++) {
				WorldItem* it = &g_items.items[i];
				if (!it->active) continue;
				float il[3];
				worldWorldToRoom(w, e->room, it->pos, il);
				if (fabsf(il[2] - local[2]) > 5.0f) continue;
				if (dir < 0 && il[0] > 264 * RSC) il[0] -= 1024 * RSC;
				else if (dir > 0 && il[0] < -264 * RSC) il[0] += 1024 * RSC;
				else continue;
				worldRoomToWorld(w, e->room, il, it->pos);
			}
			e->state += 1;
			int lap = (int)e->state;
			float c[3];
			roomCentre(w, e->room, c);
			c[1] = w->map.rooms[e->room].y;
			if (lap == 2) {
				int s = rand() % INV_SLOTS;
				if (g_items.inv[s] >= 0) {
					if (g_items.selected == s) g_items.selected = -1;
					g_items.inv[s] = -1;
					g_items.invNote[s][0] = 0;
				}
			} else if (lap == 5) {
				*p->injuries += 0.3f;
			} else if (lap == 14) {
				for (int s = 0; s < INV_SLOTS; s++)
					if (g_items.inv[s] >= 0 && !strcmp(g_items.tpl[g_items.inv[s]].tempname, "paper")) {
						int t = -1;
						for (int k = 0; k < g_items.ntpl && t < 0; k++)
							if (!strcmp(g_items.tpl[k].tempname, "paper") && rand() % 6 == 0) t = k;
						g_items.inv[s] = t;
						g_items.invNote[s][0] = 0;
						if (t < 0 && g_items.selected == s) g_items.selected = -1;
						break;
					}
			} else if (lap == 18) {
				float l[3] = { -344 * RSC, 176 * RSC, -272 * RSC }, wp2[3];
				worldRoomToWorld(w, e->room, l, wp2);
				itemsSpawnAt("strange note", "paper", wp2);
			} else if (lap == 25 && !corpse) {
				float l[3] = { 0, 0, 760 * RSC }, wp2[3];
				worldRoomToWorld(w, e->room, l, wp2);
				e->npc[0] = spawnBody(w, NPC_CLASSD, wp2, (float)w->map.rooms[e->room].angle - 200, 80);
			} else if (lap == 30) {
				giveHidden("strange note", "paper", rand() % INV_SLOTS);
			} else if (lap == 40) {
				sfxPlay("radio/franklin4", 1.0f);
			} else if (lap == 50 && !guard) {
				float l[3] = { 0, 0, -600 * RSC }, wp2[3];
				worldRoomToWorld(w, e->room, l, wp2);
				e->npc[1] = npcSpawn(NPC_GUARD, wp2, yawTo(wp2, p->pos));
				guard = npcGet(e->npc[1]);
				if (guard) {
					guard->scripted = true;
					guard->frame = 77;
					npcSnapToFloor(w, guard);
				}
			} else if (lap == 52 && guard) {
				npcRemove(e->npc[1]);
				e->npc[1] = -1;
				guard = NULL;
			}
			if (rand() % 10 == 0) {
				char snd[40];
				snprintf(snd, sizeof(snd), "ambient/zone1/ambient%d", 1 + rand() % 8);
				sfxPlay(snd, 1.0f);
			}
		} else if (guard) {
			npcAnimate(guard, 77, 201, 0.2f, true, dt);
			npcFace(guard, p->pos, 35.0f / 70.0f * 10, dt);
		}
	}
	if (e->state > 26 && corpse && roomDist(w, e, p) < 8.0f) {
		sfxLoop3D(&e->chan, "scp/970/corpse", corpse->pos, 10.0f, 1.0f);
		if (e->state > 60) {
			corpse->frame = fmaxf(61, corpse->frame - 0.02f * FRAMES(dt));
			float base = w->map.rooms[e->room].y;
			float want = base + 1.5f + sinf(osGetTime() / 20.0f * DEG2RAD) * 0.1f - 0.32f;
			corpse->pos[1] += (want - corpse->pos[1]) * fminf(1, FRAMES(dt) / 50);
			corpse->yaw += 0.1f * FRAMES(dt);
		}
	}
	return NULL;
}

static const char* evRoom2sl(World* w, Event* e, EvPlayer* p, float dt)
{
	static const int route[3] = { 16, 15, 17 };
	static const char* lines[3] = { "scp/049/room2sl1", NULL, "scp/049/room2sl2" };
	bool in = inRoom(w, e, p);
	Npc* s = npcGet(e->npc[0]);
	if (s && s->type != NPC_049) s = NULL;
	if (e->state == 0) {
		if (!in) return NULL;
		e->state = 1;
		if (npcFind(NPC_049)) {
			e->state2 = 7;
			return NULL;
		}
		float o7[3];
		if (!eventsRoomObject(w, e->room, 7, o7)) {
			e->removed = true;
			return NULL;
		}
		e->npc[0] = npcSpawn(NPC_049, o7, (float)w->map.rooms[e->room].angle + 180);
		s = npcGet(e->npc[0]);
		if (s) {
			s->scripted = true;
			s->hidden = true;
			s->placed = true;
		}
		e->state2 = -70 * 5;
		return NULL;
	}
	if (!s || e->state2 >= 7) return NULL;
	int d0 = worldRoomDoor(w, e->room, 0), d1 = worldRoomDoor(w, e->room, 1);
	if (e->state2 < 0) {
		if (in) e->state2 = fminf(e->state2 + FRAMES(dt), 0);
		return NULL;
	}
	if (e->state2 == 0) {
		float c[3];
		roomCentre(w, e->room, c);
		if (d0 >= 0) {
			s->pos[0] = w->doors[d0].x + (c[0] - w->doors[d0].x) * 0.2f;
			s->pos[2] = w->doors[d0].z + (c[2] - w->doors[d0].z) * 0.2f;
			s->pos[1] = w->doors[d0].y;
		}
		npcSnapToFloor(w, s);
		s->hidden = false;
		e->state2 = 1;
		e->state3 = 0;
		return NULL;
	}
	float head[3] = { s->pos[0], s->pos[1] + 1.4f, s->pos[2] };
	if (dist3(s->pos, p->pos) < 6.0f && worldLineOfSight(w, head, p->eye) && inView(p, head)) {
		s->scripted = false;
		s->state = 2;
		e->state2 = 7;
		return NULL;
	}

	if (d0 >= 0) {
		float dp[3] = { w->doors[d0].x, w->doors[d0].y, w->doors[d0].z };
		if (dist3(s->pos, dp) < 5.0f && !w->doors[d0].open) {
			sfxPlay3D("door/dooropen079", dp, 10.0f, 1.0f);
			worldToggleDoor(w, d0);
			if (d1 >= 0 && !w->doors[d1].open) worldToggleDoor(w, d1);
		}
	}
	if (e->state2 == 4) {
		npcAnimate(s, 269, 345, 0.2f, true, dt);
		if (!sfxPlaying(e->chan)) e->state2 = 1;
		return NULL;
	}
	int k = (int)e->state3;
	if (k >= 3) {
		s->scripted = false;
		s->state = 2;
		e->state2 = 7;
		return NULL;
	}
	float wp[3];
	if (!eventsRoomObject(w, e->room, route[k], wp)) {
		e->state3 += 1;
		return NULL;
	}
	s->currSpeed = s->speed;
	npcAnimate(s, fmaxf(fminf(s->frame, 358), 346), 393, s->currSpeed * 38, true, dt);
	if (npcWalkTo(w, s, wp, FRAMES(s->currSpeed), dt) || distXZ(s->pos, wp) < 0.4f) {
		e->state3 += 1;
		if (lines[k]) {
			e->chan = sfxPlay3D(lines[k], head, 10.0f, 1.0f);
			e->state2 = 4;
		}
	}
	return NULL;
}

static const char* evRoom205(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p)) return NULL;
	int d0 = worldRoomDoor(w, e->room, 0), d1 = worldRoomDoor(w, e->room, 1);
	float o0[3];
	if (!eventsRoomObject(w, e->room, 0, o0)) return NULL;
	if (e->state == 0) {
		if (d1 >= 0 && w->doors[d1].open) e->state = 1;
		return NULL;
	}
	if (e->state < 65) {
		if (distXZ(p->pos, o0) < 2.0f) {
			sfxPlay("scp/205/enter", 1.0f);
			e->state = 65;
			if (d0 >= 0 && w->doors[d0].open) worldToggleDoor(w, d0);
		}
		if (e->state > 7 && rand() % 300 == 0 && d0 >= 0) worldToggleDoor(w, d0);
		e->state2 += FRAMES(dt);
	}
	sfxLoop3D(&e->chan, "music/205", o0, 12.0f, 0.8f);
	int s = (int)e->state;
	static const float next[] = { 0, 20, 0, 30, 0, 40, 0, 60, 0, 80, 0, 85 };
	if (s >= 1 && s <= 11 && (s & 1)) {
		if (e->state2 > next[s] * 70) {
			e->state += 1;
			if (s >= 5 && s <= 9) sfxPlay3D("scp/205/horror", o0, 10.0f, s == 9 ? 1.0f : 0.3f + (s - 5) * 0.1f);
		}
	} else if (s == 13) {

	} else if (s == 65) {
		e->state3 += FRAMES(dt);
		if (e->state3 > 70) {
			e->state = 67;
			e->state3 = 0;
		}
	} else if (s == 67) {
		if (rand() % 150 == 0) {
			*p->injuries += frand(0.4f, 0.8f);
			sfxPlay(rand() % 2 ? "ambient/general/damage2" : "ambient/general/damage3", 1.0f);
			e->state2 = frand(-0.1f, 0.1f);
			e->state3 = frand(-0.1f, 0.1f);
			s_lightBlink = 0.6f;
			if (*p->injuries > 5.0f)
				return "The SCP-205 cycle seems to have resumed its normal course after the anomalies observed during "
				       "[REDACTED]. The body of subject D-9341 was discovered inside the chamber. The subject exhibits signs "
				       "of blunt force trauma typical for personnel who have entered the chamber when the lights are off.";
		}
		p->pos[0] += e->state2 * FRAMES(dt) * 0.2f;
		p->pos[2] += e->state3 * FRAMES(dt) * 0.2f;
		e->state2 = curveValue(0, e->state2, 10, dt);
		e->state3 = curveValue(0, e->state3, 10, dt);
	} else if (s < 65) {
		if (rand() % 3 == 0) s_lightBlink = fmaxf(s_lightBlink, 0.3f);
		e->state3 += FRAMES(dt);
		if (e->state3 > 50) {
			e->state += 1;
			e->state3 = 0;
		}
	}
	return NULL;
}

static Event* s_1123;

bool eventsTouch1123(const World* w, const float pos[3])
{
	for (int i = 0; i < s_nevents; i++) {
		Event* e = &s_events[i];
		if (e->removed || strcmp(defName(e), "room1123") != 0) continue;
		if (worldRoomIndexAt(w, pos) != e->room) return false;
		if (e->state == 0) sfxPlay("scp/1123/touch", 1.0f);
		if (e->state < 1) e->state = 1;
		s_lightBlink = 1.0f;
		return true;
	}
	return false;
}

static void put1123(World* w, EvPlayer* p, const float at[3])
{
	p->pos[0] = at[0];
	p->pos[1] = at[1] + 0.1f;
	p->pos[2] = at[2];
	worldStream(w, p->pos);
}

static const char* evRoom1123(World* w, Event* e, EvPlayer* p, float dt)
{
	s_1123 = e;
	if (!inRoom(w, e, p) && e->state < 2) return NULL;
	float o[14][3];
	for (int i = 4; i <= 13; i++)
		if (!eventsRoomObject(w, e->room, i, o[i])) return NULL;
	Npc* off = npcGet(e->npc[0]);
	float prev = e->state2;
	switch ((int)e->state) {
	case 1:
		e->state4 = *p->injuries;
		e->npc[0] = npcSpawn(NPC_GUARD, o[6], 0);
		off = npcGet(e->npc[0]);
		if (off) {
			off->scripted = true;
			npcSnapToFloor(w, off);
		}
		put1123(w, p, o[4]);
		*p->injuries = 1.0f;
		s_lightBlink = 1.0f;
		e->state = 2;
		e->state2 = 0;
		break;
	case 2:
		e->state2 += FRAMES(dt);
		if (off) {
			npcFace(off, p->pos, 3.0f, dt);
			npcAnimate(off, 77, 201, 0.2f, true, dt);
		}
		if (e->state2 > 200 && prev <= 200) e->chan = sfxPlay("music/1123", 1.0f);
		if (e->state2 > 1000) {
			if (prev <= 1000) sfxPlay("door/1123dooropen", 1.0f);
			if (e->state2 >= 1040 && prev < 1040 && off) sfxPlay3D("scp/1123/officer1", off->pos, 10.0f, 1.0f);
			if (e->state2 >= 1400 && prev < 1400 && off) sfxPlay3D("scp/1123/officer2", off->pos, 10.0f, 1.0f);
			if (dist3(p->pos, o[4]) > 392 * RSC) {
				*p->blinkTimer = -10;
				put1123(w, p, o[5]);
				*p->yaw = (float)w->map.rooms[e->room].angle + 180;
				e->state = 3;
				e->state2 = 0;
			}
		}
		break;
	case 3:
		e->state2 += FRAMES(dt);
		if (e->state2 > 70 * 3) {
			if (!sfxPlaying(e->chan)) e->chan = sfxPlay("music/1123", 1.0f);
			if (off) {
				memcpy(off->pos, o[7], sizeof(off->pos));
				npcSnapToFloor(w, off);
			}
			e->state = 4;
		}
		break;
	case 4:
		if (off) {
			npcFace(off, p->pos, 3.0f, dt);
			npcAnimate(off, 77, 201, 0.2f, true, dt);
		}
		if (e->state3 == 0) {
			offerUse(e, p, o[12], 1.0f, 1);
			if (e->pressed == 1) {
				sfxPlay("scp/1123/horror", 1.0f);
				e->state3 = 1;
			}
		} else {
			e->state3 += FRAMES(dt);
			if (e->state3 > 60) {
				float c[3];
				roomCentre(w, e->room, c);
				c[1] = w->map.rooms[e->room].y + 0.3f;
				put1123(w, p, c);
				*p->blinkTimer = -10;
				*p->injuries = 1.5f;
				e->state = 5;
				e->state2 = 0;
			}
		}
		break;
	case 5:
		e->state2 += FRAMES(dt);
		if (e->state2 > 500) {
			float at[3] = { (o[8][0] + o[12][0]) / 2, o[5][1], (o[8][2] + o[12][2]) / 2 };
			put1123(w, p, at);
			if (off) {
				off->pos[0] = (at[0] + o[12][0]) / 2;
				off->pos[1] = at[1];
				off->pos[2] = (at[2] + o[12][2]) / 2;
				npcSnapToFloor(w, off);
				off->chan = sfxPlay3D("scp/1123/officer3", off->pos, 7.0f, 1.0f);
			}
			*p->injuries = 1.5f;
			*p->blinkTimer = -10;
			e->state = 6;
		}
		break;
	case 6:
		if (off) {
			npcFace(off, p->pos, 3.0f, dt);
			npcAnimate(off, 77, 201, 0.2f, true, dt);
		}
		if (!off || !sfxPlaying(off->chan)) {
			sfxPlay("scp/1123/gunshot", 1.0f);
			e->state = 7;
		}
		break;
	case 7: {
		float c[3];
		roomCentre(w, e->room, c);
		c[1] = w->map.rooms[e->room].y + 0.3f;
		put1123(w, p, c);
		s_lightBlink = 1.0f;
		*p->injuries = e->state4;
		for (int s = 0; s < INV_SLOTS; s++)
			if (g_items.inv[s] >= 0 && !strcmp(g_items.tpl[g_items.inv[s]].name, "leaflet")) {
				if (g_items.selected == s) g_items.selected = -1;
				g_items.inv[s] = -1;
				break;
			}
		npcRemove(e->npc[0]);
		e->npc[0] = -1;
		e->removed = true;
		break;
	}
	}
	e->pressed = 0;
	return NULL;
}

static const char* evRoom1lifts(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (!inRoom(w, e, p)) return NULL;
	for (int i = 0; i < 2; i++) {
		float l[3] = { (i ? -96 : 96) * RSC, 160 * RSC, -64 * RSC }, b[3];
		worldRoomToWorld(w, e->room, l, b);
		offerUse(e, p, b, 1.0f, i + 1);
		if (e->pressed == i + 1) {
			sfxPlay3D("interact/button2", b, 5.0f, 1.0f);
			hudMessage("The elevator appears to be broken.", 5);
		}
	}
	e->pressed = 0;
	return NULL;
}

static const char* evRoom3pit1048(World* w, Event* e, EvPlayer* p, float dt)
{
	float o0[3];
	if (!inRoom(w, e, p) || e->state >= 3 || !eventsRoomObject(w, e->room, 0, o0)) return NULL;
	e->state2 = yawTo(o0, p->pos);
	if (e->state == 0) {
		if (dist3(p->pos, o0) < 3.0f && inView(p, o0)) e->state = 1;
	} else if (e->state < 2) {
		e->state += dt / 3.0f;
	} else {
		if (dist3(p->pos, o0) < 1.5f) offerUse(e, p, o0, 1.5f, 1);
		if (e->pressed == 1) {
			int slot = -1;
			for (int s = 0; s < INV_SLOTS && slot < 0; s++)
				if (g_items.inv[s] < 0) slot = s;
			if (slot < 0) {
				hudMessage("You cannot carry any more items.", 5);
			} else {
				giveHidden("drawing", "paper", slot);
				hudMessage("Picked up Drawing.", 3);
				e->state = 3;
			}
		}
	}
	e->pressed = 0;
	return NULL;
}

static const char* evAlarm(World* w, Event* e, EvPlayer* p, float dt)
{
	if (e->state == 0 && !inRoom(w, e, p)) return NULL;
	float prev = e->state;
	e->state += FRAMES(dt);
	if (e->state < 2000 && !sfxPlaying(e->chan)) e->chan = sfxPlay("alarm/alarm", 0.6f);
	if (e->state3 < 11 && !sfxPlaying(e->chan2)) {
		char snd[24];
		snprintf(snd, sizeof(snd), "alarm/alarm2_%d", (int)++e->state3);
		e->chan2 = sfxPlay(snd, 1.0f);
	}
	if (fmodf(prev, 600) <= 300 && fmodf(e->state, 600) > 300) {
		int i = (int)floorf((e->state - 5000) / 600) + 1;
		char snd[48];
		if (i == 0) sfxPlay("room/intro/pa/scripted/scripted6", 1.0f);
		if (i > 0 && i < 26) {
			snprintf(snd, sizeof(snd), "room/intro/commotion/commotion%d", i);
			sfxPlay(snd, 1.0f);
		}
		if (i > 26) e->removed = true;
	}
	return NULL;
}
