static void lightFlicker(float lo, float hi)
{
	if (s_lightBlink > 0.05f) return;
	s_lightBlink = fminf(0.6f, frand(lo, hi) * 0.15f);
}

static void closeAndLock(World* w, int d)
{
	if (d < 0) return;
	if (w->doors[d].open) worldToggleDoor(w, d);
	w->doors[d].locked = true;
}

static const char* evRoom2servers(World* w, Event* e, EvPlayer* p, float dt)
{
	float o6[3], o7[3], c[3];
	if (!eventsRoomObject(w, e->room, 6, o6) || !eventsRoomObject(w, e->room, 7, o7)) {
		e->removed = true;
		return NULL;
	}
	roomCentre(w, e->room, c);
	int d0 = worldRoomDoor(w, e->room, 0), d1 = worldRoomDoor(w, e->room, 1);
	bool in = inRoom(w, e, p);
	Npc* s096 = npcFind(NPC_096);
	Npc* guard = npcGet(e->npc[0]);
	if (guard && guard->type != NPC_GUARD) guard = NULL;

	if (e->state == 0) {
		if (!in) return NULL;
		closeAndLock(w, d0);
		closeAndLock(w, d1);
		float sp[3] = { o6[0], o6[1] - 0.4f, o6[2] };
		if (!s096) s096 = npcGet(npcSpawn(NPC_096, sp, 0));
		if (s096) {
			memcpy(s096->pos, sp, sizeof(sp));
			s096->yaw = (float)w->map.rooms[e->room].angle + 270;
			s096->scripted = true;
			s096->state = 6;
			s096->frame = 472;
			npcSnapToFloor(w, s096);
		}
		e->chan = sfxPlay("character/guard/096serverroom1", 1.0f);
		e->npc[0] = npcSpawn(NPC_GUARD, o7, 0);
		guard = npcGet(e->npc[0]);
		if (guard) {
			guard->scripted = true;
			guard->frame = 77;
			npcSnapToFloor(w, guard);
		}
		e->state = 1;
		return NULL;
	}

	if (e->state < 70 * 45) {
		if (rand() % 200 < 5 && in) {
			lightFlicker(1.0f, 2.0f);
			if (rand() % 5 == 0) {
				char snd[32];
				snprintf(snd, sizeof(snd), "room/intro/light%d", 1 + rand() % 3);
				sfxPlay3D(snd, c, 8.0f, frand(0.1f, 0.3f));
			}
		}
		float prev = e->state;
		e->state = fminf(e->state + FRAMES(dt), 70 * 43);
		if (guard && s096) {
			float head[3] = { s096->pos[0], s096->pos[1] + 1.5f, s096->pos[2] };
			if (e->state < 70 * 8) {
				npcAnimate(s096, 472, 520, 0.25f, true, dt);
				npcAnimate(guard, 77, 201, 0.2f, true, dt);
				npcFace(guard, s096->pos, 3.0f, dt);
			} else if (e->state < 70 * 10) {
				npcAnimate(s096, 521, 555, 0.25f, false, dt);
				npcAnimate(guard, 77, 201, 0.2f, true, dt);
				if (s096->frame >= 554.5f) {
					e->state = 70 * 10;
					s096->frame = 677;
					s096->yaw += 180;
					float a = s096->yaw * DEG2RAD;
					s096->pos[0] -= sinf(a) * 0.3f;
					s096->pos[2] -= cosf(a) * 0.3f;
				}
				npcFace(guard, s096->pos, 3.0f, dt);
			} else if (e->state < 70 * 22) {
				if (s096->frame < 737) npcAnimate(s096, 677, 736, 0.3f, false, dt);
				else npcAnimate(s096, 737, 822, 0.3f, true, dt);
				sfxLoop3D(&s096->chan, "music/096angered", head, 10.0f, 1.0f);
				if (e->state > 70 * 15 && dist3(guard->pos, s096->pos) > 2.5f && !worldLineOfSight(w, guard->pos, head)) {
					guard->currSpeed = 0.015f;
					npcAnimate(guard, 1, 38, guard->currSpeed * 40, true, dt);
					npcWalkTo(w, guard, s096->pos, FRAMES(guard->currSpeed), dt);
				} else {
					npcAnimate(guard, 77, 201, 0.2f, true, dt);
					npcFace(guard, s096->pos, 3.0f, dt);
				}
			} else {
				sfxLoop3D(&s096->chan, "scp/096/scream", head, 7.5f, 1.0f);
				if (s096->frame > 193 || s096->frame < 2) s096->frame = 2;
				npcAnimate(s096, 2, 193, 0.7f, true, dt);
				npcFace(s096, guard->pos, 10.0f, dt);
				float dx = guard->pos[0] - s096->pos[0], dz = guard->pos[2] - s096->pos[2], l = sqrtf(dx * dx + dz * dz);
				float step = FRAMES(s096->speed * 2.0f) * dt;
				if (l > 0.8f) {
					s096->pos[0] += dx / l * fminf(step, l);
					s096->pos[2] += dz / l * fminf(step, l);
					npcSnapToFloor(w, s096);
				}
				npcAnimate(guard, 77, 201, 0.2f, true, dt);
				npcFace(guard, s096->pos, 6.0f, dt);
				if (fmodf(e->state, 20.0f) < fmodf(prev, 20.0f)) {
					float gp[3] = { guard->pos[0], guard->pos[1] + 1.3f, guard->pos[2] };
					sfxPlay3D("general/gunshot", gp, 15.0f, 1.0f);
					if (in && rand() % 3 == 0) lightFlicker(0.5f, 2.0f);
				}
				if (l <= 0.8f) {
					sfxStop(e->chan);
					e->chan = sfxPlay("character/guard/096serverroom2", 1.0f);
					npcRemove(e->npc[0]);
					e->npc[0] = -1;
					sfxStop(s096->chan);
					s096->chan = -1;
					s096->scripted = false;
					s096->state = 5;
					s096->state2 = 0;
					s096->frame = 312;
					s096->timer = s096->yaw;
				}
			}
		} else {
			if (e->state >= 70 * 40 && prev < 70 * 40) {
				if (d0 >= 0 && !w->doors[d0].open) worldToggleDoor(w, d0);
				if (d1 >= 0 && !w->doors[d1].open) worldToggleDoor(w, d1);
				if (d0 >= 0) w->doors[d0].locked = true;
				if (d1 >= 0) w->doors[d1].locked = true;
			}
			if (in) {
				if (sfxPlaying(e->chan)) {
					if (rand() % 40 == 0) lightFlicker(0.5f, 6.0f);
					if (rand() % 50 == 0) {
						char snd[32];
						snprintf(snd, sizeof(snd), "room/intro/light%d", 1 + rand() % 3);
						sfxPlay3D(snd, c, 8.0f, frand(0.1f, 0.3f));
					}
				}
				int a = w->map.rooms[e->room].angle;
				float off = (a == 0 || a == 180) ? fabsf(p->pos[0] - c[0]) : fabsf(p->pos[2] - c[2]);
				if (off > 1.3f) e->state = 70 * 50;
			}
		}
		return NULL;
	}

	if (!in) return NULL;
	bool power = leverOn(e->room, 0), pump = leverOn(e->room, 1), gen = leverOn(e->room, 2);
	float o3[3], o5[3];
	Lever* l1 = lever(e->room, 1);
	Lever* l2 = lever(e->room, 2);
	if (l1) memcpy(o3, l1->pos, sizeof(o3)); else memcpy(o3, c, sizeof(o3));
	if (l2) memcpy(o5, l2->pos, sizeof(o5)); else memcpy(o5, c, sizeof(o5));
	if (pump) {
		e->state2 = fminf(1.0f, e->state2 + FRAMES(dt) / 350);
		e->state3 = gen ? fminf(1.0f, e->state3 + FRAMES(dt) / 450) : fmaxf(0.0f, e->state3 - FRAMES(dt) / 450);
	} else {
		e->state2 = fmaxf(0, e->state2 - FRAMES(dt) / 350);
		e->state3 = fmaxf(0, e->state3 - FRAMES(dt) / 450);
	}
	if (e->state2 > 0) sfxLoop3D(&e->chan, "ambient/room ambience/fuelpump", o3, 5.0f, e->state2 * 0.8f);
	if (e->state3 > 0) sfxLoop3D(&e->chan2, "general/generatoron", o5, 6.0f, e->state3);
	if (!power && pump && gen) {
		if (d0 >= 0) w->doors[d0].locked = false;
		if (d1 >= 0) w->doors[d1].locked = false;
	} else {
		if (rand() % 200 < 5) lightFlicker(0.5f, 1.0f);
		closeAndLock(w, d0);
		closeAndLock(w, d1);
	}
	return NULL;
}

static const char* ev096spawn(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	float c[3];
	roomCentre(w, e->room, c);
	Npc* o = npcFind(NPC_096);
	if (roomDist(w, e, p) < 15.0f) {
		if (e->state != 2) {
			if (o) {
				if (dist3(o->pos, p->pos) < 40) e->state = 2;
				if (o->state != 5) e->state = 2;
				if (distXZ(o->pos, c) > distXZ(o->pos, p->pos)) e->state = 2;
			}
			for (int i = 0; i < s_nevents; i++) {
				const Event* s = &s_events[i];
				if (s->removed || strcmp(roomName(w, s->room), "room2servers") != 0) continue;
				if (s->state == 0 || (s->state > 0 && npcGet(s->npc[0]))) e->state = 2;
			}
			int cp = findRoom(w, "checkpoint1");
			if (cp >= 0) {
				float cc[3];
				roomCentre(w, cp, cc);
				if (distXZ(cc, p->pos) < 10) e->state = 2;
			}
			if (inRoom(w, e, p)) e->state = 2;
		}
		if (e->state == 0) {
			const char* name = roomName(w, e->room);
			float lx, lz;
			if (!strcmp(name, "room4pit") || !strcmp(name, "room3pit") || !strcmp(name, "room3z2") ||
			    !strcmp(name, "room4tunnels") || !strcmp(name, "room3tunnel")) {
				int place = (!strcmp(name, "room4pit") || !strcmp(name, "room4tunnels")) ? irand(0, 3) : irand(0, 2);
				static const float spots[4][2] = { { -608, 0 }, { 0, -608 }, { 608, 0 }, { 0, 608 } };
				lx = spots[place][0];
				lz = spots[place][1];
			} else {
				lx = frand(-100, 100);
				lz = frand(-100, 100);
			}
			float l[3] = { lx * RSC, 0.2f, -lz * RSC }, wp[3];
			worldRoomToWorld(w, e->room, l, wp);
			if (!o) o = npcGet(npcSpawn(NPC_096, wp, 0));
			if (!o) return NULL;
			memcpy(o->pos, wp, sizeof(wp));
			npcSnapToFloor(w, o);
			o->yaw = yawTo(o->pos, p->pos) + 180;
			o->timer = o->yaw;
			o->scripted = false;
			o->hidden = false;
			o->placed = true;
			o->state = 5;
			o->state2 = 0;
			if (o->frame < 312 || o->frame > 1469) o->frame = 423;
			e->state = 1;
		} else if (e->state == 1 && o) {
			o->yaw = yawTo(o->pos, p->pos) + 180;
			o->timer = o->yaw;
			float head[3] = { o->pos[0], o->pos[1] + 1.5f, o->pos[2] };
			if (dist3(o->pos, p->pos) < 7.5f && worldLineOfSight(w, head, p->eye)) {
				o->yaw = yawTo(o->pos, p->pos) + frand(170, 190);
				o->timer = o->yaw;
				e->state = 2;
			}
		} else if (e->state == 3) {
			e->state = 2;
		}
	} else if (e->state == 2) {
		e->state = irand(-1, 1) > 0 ? 0 : 3;
	}
	return NULL;
}
