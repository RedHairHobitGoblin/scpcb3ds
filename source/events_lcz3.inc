static const char* evRoom2cafeteria(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	if (e->state == 0) {
		if (!npcFind(NPC_066)) {
			float c[3];
			roomCentre(w, e->room, c);
			c[1] = w->map.rooms[e->room].y;
			npcSpawn(NPC_066, c, 0);
		}
		e->state = 1;
	}
	float o0[3], o1[3];
	if (!inRoom(w, e, p) || !eventsRoomObject(w, e->room, 0, o0) || !eventsRoomObject(w, e->room, 1, o1)) return NULL;
	if (dist3(o0, p->pos) < 1.5f) offerUse(e, p, o0, 1.5f, 1);
	if (e->pressed != 1) return NULL;
	e->pressed = 0;
	if (sfxPlaying(e->chan)) return NULL;

	bool inserted = false;
	int sel = g_items.selected;
	if (e->state2 < 2 && sel >= 0 && g_items.inv[sel] >= 0) {
		const char* t = g_items.tpl[g_items.inv[sel]].tempname;
		if (!strcmp(t, "25ct") || !strcmp(t, "coin")) {
			g_items.inv[sel] = -1;
			g_items.invNote[sel][0] = 0;
			g_items.selected = -1;
			e->state2 += 1;
			sfxPlay("scp/294/coin_drop", 1.0f);
			inserted = true;
		}
	}
	if (e->state2 == 1 && !inserted) {
		hudMessage("You need to insert another Quarter in order to use this machine.", 5);
		return NULL;
	}
	if (e->state2 < 2) {
		if (!inserted) hudMessage("You need to insert two Quarters in order to use this machine.", 5);
		return NULL;
	}

	SwkbdState kb;
	char input[16] = { 0 };
	swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, 15);
	swkbdSetHintText(&kb, "SCP-294: type a drink");
	if (swkbdInputText(&kb, input, sizeof(input)) != SWKBD_BUTTON_CONFIRM || !input[0]) return NULL;
	int d = scp294Find(input);
	if (d < 0) {
		e->chan = sfxPlay("scp/294/outofrange", 1.0f);
		hudMessage("OUT OF RANGE", 4);
		return NULL;
	}
	const char* snd = scp294Get(d, "dispensesound");
	char name[64];
	if (snd[0]) {
		const char* q = snd;
		if (!strncasecmp(q, "SFX", 3)) q += 4;
		int n = 0;
		for (; *q && n < 63; q++) name[n++] = *q == '\\' ? '/' : (char)tolower((unsigned char)*q);
		name[n] = 0;
		char* dot = strrchr(name, '.');
		if (dot) *dot = 0;
	} else {
		snprintf(name, sizeof(name), "scp/294/dispense1");
	}
	e->chan = sfxPlay(name, 1.0f);
	e->state2 = 0;

	char drink[24];
	const char* t = input;
	while (*t == ' ') t++;
	if (!strncasecmp(t, "a cup of ", 9)) t += 9;
	else if (!strncasecmp(t, "cup of ", 7)) t += 7;
	int n = 0;
	for (; *t && n < 23; t++) drink[n++] = (char)tolower((unsigned char)*t);
	drink[n] = 0;
	int it = itemsSpawnAt("cup", "cup", o1);
	if (it >= 0) snprintf(g_items.items[it].note, sizeof(g_items.items[it].note), "%s", drink);
	if (!strcasecmp(scp294Get(d, "explosion"), "true")) {
		const char* why = scp294Get(d, "deathmessage");
		return why[0] ? why : "Subject D-9341 was killed in an explosion at SCP-294.";
	}
	return NULL;
}

static const char* evRoom2closets(World* w, Event* e, EvPlayer* p, float dt)
{
	float o0[3], o1[3];
	if (!eventsRoomObject(w, e->room, 0, o0) || !eventsRoomObject(w, e->room, 1, o1)) {
		e->removed = true;
		return NULL;
	}
	Npc* a = npcGet(e->npc[0]);
	Npc* b = npcGet(e->npc[1]);
	if (e->state == 0) {
		if (!inRoom(w, e, p) || npc173IsIdle()) return NULL;
		e->npc[0] = npcSpawn(NPC_CLASSD, o0, 0);
		e->npc[1] = npcSpawn(NPC_CLASSD, o1, 0);
		a = npcGet(e->npc[0]);
		b = npcGet(e->npc[1]);
		if (!a || !b) return NULL;
		a->scripted = b->scripted = true;
		npcSnapToFloor(w, a);
		npcSnapToFloor(w, b);
		a->chan = sfxPlay3D("room/storeroom/escape1", a->pos, 12.0f, 1.0f);
		e->state = 1;
		return NULL;
	}
	if (!a || !b) {
		e->removed = true;
		return NULL;
	}
	float c[3];
	roomCentre(w, e->room, c);
	float prev = e->state;
	e->state += FRAMES(dt);
	float ra = (float)w->map.rooms[e->room].angle;
	if (e->state < 70 * 3.5f) {
		npcAnimate(a, 210, 235, 0.1f, true, dt);
		npcAnimate(b, 210, 235, 0.1f, true, dt);
		b->yaw += ((ra + 90) - b->yaw) * fminf(1.0f, FRAMES(dt) / 100);
		if (e->state > 70 * 3.2f && prev <= 70 * 3.2f) playAt("room/intro/173vent", c, 15.0f);
	} else if (e->state < 70 * 6.5f) {
		if (prev < 70 * 3.5f) b->chan = sfxPlay3D("room/storeroom/escape2", b->pos, 12.0f, 1.0f);
		if (e->state > 70 * 4.5f) npcFace(a, c, 30.0f / 70.0f * 10, dt);
		npcFace(b, c, 30.0f / 70.0f * 10, dt);
		b->yaw += sinf(e->state * DEG2RAD) * 25 * 0.02f;
	} else {
		if (prev < 70 * 6.5f) {
			sfxPlay("horror/horror0", 1.0f);
			sfxPlay("general/lightswitch", 1.0f);
		}

		*p->blinkTimer = fmaxf((70 * 6.5f - e->state) / 5.0f - frand(0, 2), -10);
		if (*p->blinkTimer <= -10) {
			if (e->state > 70 * 7.5f && prev <= 70 * 7.5f) {
				sfxPlay3D("scp/173/necksnap1", a->pos, 8.0f, 1.0f);
				float wp[3] = { a->pos[0], a->pos[1] + 0.3f, a->pos[2] };
				itemsSpawnAt("wallet", "wallet", wp);
				wp[0] += 0.15f;
				itemsSpawnAt("quarter", "25ct", wp);
				wp[2] += 0.15f;
				itemsSpawnAt("quarter", "25ct", wp);
			}
			if (e->state > 70 * 8.0f && prev <= 70 * 8.0f) sfxPlay3D("scp/173/necksnap2", b->pos, 8.0f, 1.0f);
			a->dead = b->dead = true;
			a->frame = 60;
			b->frame = 19;
		}
		if (e->state > 70 * 8.5f) {
			float s[3] = { (o0[0] + o1[0]) / 2, o0[1] - 0.3f, (o0[2] + o1[2]) / 2 };
			npc173Place(s, yawTo(s, p->pos));
			e->removed = true;
		}
	}
	return NULL;
}

static const char* evEndroom106(World* w, Event* e, EvPlayer* p, float dt)
{
	if (s_contained106) return NULL;
	float c[3];
	roomCentre(w, e->room, c);
	c[1] = w->map.rooms[e->room].y;
	int d0 = worldRoomDoor(w, e->room, 0);
	Npc* j = npcGet(e->npc[0]);
	Npc* o = npcFind(NPC_106);
	float d = roomDist(w, e, p);
	if (e->state == 0) {
		if (d < 8 && d > 0 && d0 >= 0) {
			if (!o || !o->hidden) {
				e->removed = true;
				return NULL;
			}
			Door* door = &w->doors[d0];
			float dp[3] = { door->x, door->y, door->z };
			e->npc[0] = npcSpawn(NPC_CLASSD, dp, yawTo(dp, c));
			j = npcGet(e->npc[0]);
			if (j) {
				j->scripted = true;
				float yr = j->yaw * DEG2RAD;
				j->pos[0] -= sinf(yr) * 0.5f;
				j->pos[2] -= cosf(yr) * 0.5f;
				npcSnapToFloor(w, j);
			}
			playAt("door/endroomdoor", c, 15.0f);
			e->state = 1;
		}
		return NULL;
	}
	if (!j) {
		e->removed = true;
		return NULL;
	}
	if (e->state == 1) {
		if (inRoom(w, e, p)) {
			e->state = 2;
			sfxStop(e->chan);
			e->chan = sfxPlay("character/janitor/106abduct", 1.0f);
		} else if (d < 8) {
			sfxLoop3D(&e->chan, "character/janitor/idle", j->pos, 15.0f, 1.0f);
		}
		npcAnimate(j, 210, 235, 0.1f, true, dt);
	} else if (e->state == 2) {
		j->currSpeed = 0.01f;
		npcAnimate(j, 1, 38, j->currSpeed * 40, true, dt);
		npcWalkTo(w, j, c, FRAMES(j->currSpeed), dt);
		if (distXZ(j->pos, c) < 1.5f) e->state = 3;
	} else {
		float dist = distXZ(j->pos, c);
		if (o) {
			o->scripted = true;
			o->hidden = false;
			o->pos[0] = c[0];
			o->pos[1] = c[1];
			o->pos[2] = c[2];
			o->yaw = yawTo(o->pos, j->pos);
		}
		if (dist < 0.4f) {
			if (e->state < 4) j->frame = 41;
			e->state += FRAMES(dt) / 2;
			j->pos[0] += (c[0] - j->pos[0]) * fminf(1, FRAMES(dt) / 25);
			j->pos[2] += (c[2] - j->pos[2]) * fminf(1, FRAMES(dt) / 25);
			j->pos[1] = c[1] - (e->state - 3) / 70.0f;
			npcAnimate(j, 41, 58, 0.1f, false, dt);
			if (o) {
				if (o->frame > 206 || o->frame < 112) o->frame = 206;
				o->frame = fmaxf(112, o->frame - FRAMES(dt));
			}
		} else {
			j->currSpeed = 0.01f;
			npcAnimate(j, 1, 38, j->currSpeed * 40, true, dt);
			npcWalkTo(w, j, c, FRAMES(j->currSpeed), dt);
			if (o) {
				if (o->frame < 112 || o->frame > 206) o->frame = 112;
				o->frame = fminf(206, o->frame + 1.5f * FRAMES(dt));
			}
		}
		if (e->state > 100) {
			npcRemove(e->npc[0]);
			e->npc[0] = -1;
			if (o) {
				o->scripted = false;
				if (distXZ(p->pos, c) < 2.5f) {
					o->pos[0] = p->pos[0];
					o->pos[2] = p->pos[2];
					o->frame = 110;
					o->state = 1;
					o->hidden = false;
				} else {
					o->hidden = true;
					o->state = 0;
				}
			}
			e->removed = true;
		}
	}
	return NULL;
}

static const char* evRoom3storage(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p)) return NULL;
	float o[4][3];
	for (int i = 0; i < 4; i++)
		if (!eventsRoomObject(w, e->room, i, o[i])) return NULL;
	elevator(w, e, worldRoomDoor(w, e->room, 0), worldRoomDoor(w, e->room, 1), o[0], o[1], &e->state2, p, dt);
	elevator(w, e, worldRoomDoor(w, e->room, 2), worldRoomDoor(w, e->room, 3), o[2], o[3], &e->state3, p, dt);
	float ry = w->map.rooms[e->room].y;
	if (p->pos[1] - ry >= -4600 * RSC) return NULL;
	int d4 = worldRoomDoor(w, e->room, 4);
	if (d4 >= 0) {
		Door* door = &w->doors[d4];
		if (!door->open && (leverOn(e->room, 0) || leverOn(e->room, 1))) {
			worldToggleDoor(w, d4);
			float dp[3] = { door->x, door->y, door->z };
			sfxPlay3D("door/door2open1_dist", dp, 400.0f, 1.0f);
		}
		leverLock(e->room, 0, door->open);
		leverLock(e->room, 1, door->open);
	}
	if (p->pos[1] - ry < -6400 * RSC) {
		sfxPlay("room/pocketdimension/impact", 1.0f);
		return "";
	}
	return NULL;
}
