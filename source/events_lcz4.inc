static const char* ev1048a(World* w, Event* e, EvPlayer* p, float dt)
{
	float c[3];
	roomCentre(w, e->room, c);
	c[1] = w->map.rooms[e->room].y;
	Npc* n = npcGet(e->npc[0]);
	if (n && n->type != NPC_1048A) n = NULL;
	if (e->state == 0) {
		if (!inRoom(w, e, p) && *p->blinkTimer < -10 && distXZ(p->pos, c) < 16.0f) {
			e->npc[0] = npcSpawn(NPC_1048A, c, frand(0, 360));
			n = npcGet(e->npc[0]);
			if (!n) return NULL;
			n->scripted = true;
			n->frame = 2;
			npcSnapToFloor(w, n);
			e->state = 1;
		}
		return NULL;
	}
	if (!n) {
		e->removed = true;
		return NULL;
	}
	const char* death = NULL;
	if (e->state3 == 0) e->state3 = 1;
	if (e->state == 1) {
		npcAnimate(n, 2, 395, 1.0f, true, dt);
		if (dist3(p->pos, n->pos) < 2.5f) e->state = 2;
	} else if (e->state == 2) {
		float prev = n->frame;
		npcAnimate(n, 2, 647, 1.0f, false, dt);
		if (prev <= 400 && n->frame > 400) e->chan = sfxPlay("scp/1048a/shriek", 1.0f);
		npcFace(n, p->pos, 10.0f, dt);
		if (prev > 646) {
			if (inRoom(w, e, p)) {
				e->state = 3;
				sfxPlay("scp/1048a/growth", 1.0f);
				hudMessage("Something is growing all around your body.", 3);
			} else {
				e->state3 = 70 * 30;
			}
		}
	} else if (e->state == 3) {
		float prev = e->state2;
		e->state2 += FRAMES(dt);
		if (e->state2 > 250 && prev <= 250) {
			static const char* m[] = { "Ears are growing all over your body.", "Ear-like organs are growing all over your body.",
			                           "Ears are growing all over your body. They are crawling on your skin." };
			hudMessage(m[rand() % 3], 3);
		} else if (e->state2 > 600 && prev <= 600) {
			static const char* m[] = { "It is becoming difficult to breathe.", "You have excellent hearing now. Also, you are dying.",
			                           "The ears are growing inside your body.", "\"Can't... Breathe...\"" };
			hudMessage(m[rand() % 4], 5);
		}
		if (e->state2 > 70 * 15) {
			death = "A dead body covered in ears was found in [REDACTED]. Subject was presumably attacked by an instance of "
			        "SCP-1048-A and suffocated to death by the ears. Body was sent for autopsy.";
			e->state = 4;
			e->removed = true;
		}
	}
	if (!e->removed && !inRoom(w, e, p) && e->state3 > 0) {
		e->state3 += FRAMES(dt);
		if (e->state3 > 70 * 25) {
			npcRemove(e->npc[0]);
			e->npc[0] = -1;
			e->removed = true;
		}
	}
	return death;
}

static const char* evRoomGw(World* w, Event* e, EvPlayer* p, float dt)
{
	int d0 = worldRoomDoor(w, e->room, 0), d1 = worldRoomDoor(w, e->room, 1);
	if (d0 >= 0) w->doors[d0].locked = true;
	if (d1 >= 0) w->doors[d1].locked = true;
	float o0[3], o1[3];
	if (!eventsRoomObject(w, e->room, 0, o0)) return NULL;
	bool broken = eventsRoomObject(w, e->room, 1, o1);
	if (!inRoom(w, e, p)) {
		e->state3 = 0;
		return NULL;
	}
	if (e->state == 0) {
		float d = dist3(o0, p->pos);
		if (d < 1.4f && e->state3 == 0) {
			e->state = 1;
			if (broken) sfxPlay3D("door/doorsparks", o1, 5.0f, 1.0f);
			sfxStop(e->chan);
			e->chan = -1;
			if (d0 >= 0 && w->doors[d0].open) worldToggleDoor(w, d0);
			if (d1 >= 0 && w->doors[d1].open) worldToggleDoor(w, d1);
			sfxPlay("alarm/alarm5", 1.0f);
		} else if (d > 2.4f) {
			e->state3 = 0;
		}
	} else if (e->state2 < 70 * 7) {
		float prev = e->state2;
		e->state2 += FRAMES(dt);
		if (e->state2 > 70 * 3 && prev <= 70 * 3) e->chan = sfxPlay3D("door/airlock", o0, 5.0f, 1.0f);
	} else {
		e->state = 0;
		e->state2 = 0;
		e->state3 = 1;
		if (d0 >= 0 && !w->doors[d0].open) worldToggleDoor(w, d0);
		if (d1 >= 0 && !w->doors[d1].open) worldToggleDoor(w, d1);
	}
	return NULL;
}

static bool goodFor1162(const char* t)
{
	static const char* ok[] = { "key1", "key2", "key3", "misc", "420", "cigarette", "vest", "finevest", "gasmask",
	                            "radio", "18vradio", "clipboard", "eyedrops", "nvgoggles", "drawing", "paper" };
	for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
		if (!strcmp(t, ok[i])) return true;
	return false;
}

static int pick1162(const char* given)
{
	for (int tries = 0; tries < 400; tries++) {
		int t = rand() % g_items.ntpl;
		const char* k = g_items.tpl[t].tempname;
		if (!goodFor1162(k)) continue;
		bool want;
		if (!strcmp(given, "key")) want = (!strcmp(k, "key1") || !strcmp(k, "key2")) && rand() % 2;
		else if (!strcmp(given, "paper") || !strcmp(given, "oldpaper")) want = !strcmp(k, "paper") && rand() % 12 == 0;
		else if (!strncmp(given, "gasmask", 7) || !strcmp(given, "supergasmask") || !strncmp(given, "hazmatsuit", 10))
			want = !strncmp(k, "gasmask", 7) && rand() % 2;
		else if (!strcmp(given, "key1") || !strcmp(given, "key2") || !strcmp(given, "key3"))
			want = (!strncmp(k, "key", 3) || !strcmp(k, "misc")) && rand() % 6 == 0;
		else if (!strcmp(given, "vest") || !strcmp(given, "finevest")) want = !strcmp(k, "vest") || !strcmp(k, "finevest");
		else want = !strcmp(k, "misc") && rand() % 6 == 0;
		if (want) return t;
	}
	return -1;
}

static const char* evRoom1162(World* w, Event* e, EvPlayer* p, float dt)
{
	(void)dt;
	float o0[3];
	if (!inRoom(w, e, p) || !eventsRoomObject(w, e->room, 0, o0)) return NULL;
	float l[3] = { 976 * RSC, 128 * RSC, 640 * RSC }, out[3];
	worldRoomToWorld(w, e->room, l, out);
	for (int i = 0; i < g_items.nitems; i++)
		if (g_items.items[i].active && dist3(g_items.items[i].pos, o0) < 0.75f) return NULL;
	if (dist3(o0, p->pos) < 0.75f + 0.5f) offerUse(e, p, o0, 1.25f, 1);
	if (e->pressed != 1) return NULL;
	e->pressed = 0;

	int slot = rand() % INV_SLOTS;
	char snd[48];
	if (g_items.inv[slot] >= 0) {
		char given[20];
		snprintf(given, sizeof(given), "%s", g_items.tpl[g_items.inv[slot]].tempname);
		int t = pick1162(given);
		if (t < 0) return NULL;
		if (g_items.selected == slot) g_items.selected = -1;
		g_items.inv[slot] = -1;
		g_items.invNote[slot][0] = 0;
		itemsSpawnAt(g_items.tpl[t].name, g_items.tpl[t].tempname, out);
		snprintf(snd, sizeof(snd), "scp/1162/exchange%d", rand() % 5);
		sfxPlay(snd, 1.0f);
		return NULL;
	}

	*p->injuries += 5.0f;
	int t = -1;
	for (int tries = 0; tries < 400 && t < 0; tries++) {
		int k = rand() % g_items.ntpl;
		if (goodFor1162(g_items.tpl[k].tempname)) t = k;
	}
	if (t >= 0) itemsSpawnAt(g_items.tpl[t].name, g_items.tpl[t].tempname, out);
	snprintf(snd, sizeof(snd), "scp/1162/bodyhorrorexchange%d", 1 + rand() % 4);
	sfxPlay(snd, 1.0f);
	s_lightBlink = 1.0f;
	if (*p->injuries > 15)
		return "A dead Class D subject was discovered within the containment chamber of SCP-1162. An autopsy revealed "
		       "that his right lung was missing, which suggests interaction with SCP-1162.";
	hudMessage("You feel a sudden overwhelming pain in your chest.", 5);
	return NULL;
}
