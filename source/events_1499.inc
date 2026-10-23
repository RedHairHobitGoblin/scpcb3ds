#define C1499_SIZE 40.0f
#define C1499_RING 2
#define C1499_WANDERERS 6

static Room s_1499Castle, s_1499Plane, s_1499Obj[16];
static int s_1499Area = -1, s_1499Room = -1, s_1499Fixed;
static int s_1499ChunkX = 1 << 30, s_1499ChunkZ = 1 << 30;
static uint8_t s_1499Data[64][64];
static float s_1499Prev[3];
static float s_1499Back[3];
static bool s_1499In;
static int s_1499Role[MAX_NPCS];

static struct {
	int n;
	int obj[4];
	float x[4], z[4], yaw[4];
} s_1499Chunks[5] = {
	{ 2, { 3, 12 }, { -6, 1 }, { 18, -12 }, { 90, -1 } },
	{ 3, { 2, 6, 9 }, { 10, -16, 16 }, { -12, 11, -4 }, { 0, -1, -1 } },
	{ 0 }, { 0 }, { 0 },
};

static void load1499Chunks(void)
{
	FILE* f = fopen("romfs:/1499chunks.ini", "r");
	if (!f) return;
	char line[128];
	int chunk = -1;
	while (fgets(line, sizeof(line), f)) {
		int k, v;
		float fv;
		char word[16];
		if (sscanf(line, "[chunk%d]", &k) == 1) {
			chunk = k >= 0 && k < 5 ? k : -1;
			continue;
		}
		if (chunk < 0) continue;
		if (sscanf(line, "count = %d", &v) == 1) s_1499Chunks[chunk].n = v + 1 > 4 ? 4 : v + 1;
		else if (sscanf(line, "obj%d-x = %f", &k, &fv) == 2 && k < 4) s_1499Chunks[chunk].x[k] = fv;
		else if (sscanf(line, "obj%d-z = %f", &k, &fv) == 2 && k < 4) s_1499Chunks[chunk].z[k] = fv;
		else if (sscanf(line, "obj%d-yaw = %15s", &k, word) == 2 && k < 4) s_1499Chunks[chunk].yaw[k] = (word[0] == 'R' || word[0] == 'r') ? -1 : (float)atof(word);
		else if (sscanf(line, "obj%d = %d", &k, &v) == 2 && k < 4) s_1499Chunks[chunk].obj[k] = v;
	}
	fclose(f);
}

static void use1499Models(void)
{
	static char paths[16][40];
	useModel(&s_1499Castle, "romfs:/rooms/dimension1499.scr");
	useModel(&s_1499Plane, "romfs:/models/1499plane.scr");
	for (int i = 1; i <= 15; i++) {
		if (!paths[i][0]) snprintf(paths[i], sizeof(paths[i]), "romfs:/models/1499obj%d.scr", i);
		useModel(&s_1499Obj[i], paths[i]);
	}
}

static void dimension1499Generate(World* w, int room)
{
	s_1499Area = -1;
	s_1499Room = room;
	s_1499ChunkX = s_1499ChunkZ = 1 << 30;
	s_1499In = false;
	for (int i = 0; i < MAX_NPCS; i++) s_1499Role[i] = -1;
	uint32_t rng = w->seed * 3266489917u + 1499u;
	for (int i = 0; i < 64; i++)
		for (int j = 0; j < 64; j++) {
			rng = rng * 1664525u + 1013904223u;
			s_1499Data[i][j] = (uint8_t)((rng >> 8) % 5);
		}
	load1499Chunks();
	int area = worldBeginArea(w, room);
	if (area < 0) return;
	const MapRoomPlacement* r = &w->map.rooms[room];
	float rx = r->gx * 8.0f, ry = r->y, rz = -r->gy * 8.0f;
	worldAddAreaPiece(w, area, &s_1499Castle, rx, ry, rz, (float)r->angle, true);

	for (int k = 0; k < 2; k++)
		for (int i = -2; i <= 0; i += 2) {
			float bx = rx * (i * 2.5f), bz = -rz - 40.0f * k;
			worldAddAreaPiece(w, area, &s_1499Plane, bx + 20, ry, -(bz + 20), 0, true);
		}
	s_1499Fixed = w->areas[area].count;
	float mn[3] = { -100000, ry - 10, -100000 }, mx[3] = { 100000, ry + 60, 100000 };
	worldEndArea(w, area, mn, mx);
	s_1499Area = area;
}

static void update1499Chunks(World* w, const float pos[3])
{
	OverlayArea* a = &w->areas[s_1499Area];
	float bxp = pos[0], bzp = -pos[2];
	int cx = (int)(bxp / C1499_SIZE), cz = (int)(bzp / C1499_SIZE);
	if (cx == s_1499ChunkX && cz == s_1499ChunkZ) return;
	s_1499ChunkX = cx;
	s_1499ChunkZ = cz;
	w->ntunnel = a->first + s_1499Fixed;
	a->count = s_1499Fixed;
	const MapRoomPlacement* r = &w->map.rooms[s_1499Room];
	float ry = r->y, rx = r->gx * 8.0f, rz = r->gy * 8.0f;
	for (int dz = -C1499_RING; dz <= C1499_RING; dz++)
		for (int dx = -C1499_RING; dx <= C1499_RING; dx++) {
			float x = (cx + dx) * C1499_SIZE, z = (cz + dz) * C1499_SIZE;
			bool spawn = false;
			for (int k = 0; k < 2 && !spawn; k++)
				for (int i = -2; i <= 0 && !spawn; i += 2)
					if (fabsf(x - rx * (i * 2.5f)) < 1 && fabsf(z - (rz - 40.0f * k)) < 1) spawn = true;
			if (spawn) continue;
			int ix = abs((int)lroundf((x + 32) / 40)) % 64, iz = abs((int)lroundf((z + 32) / 40)) % 64;
			int type = s_1499Data[ix][iz];
			float px = x + 20, pz = z + 20;
			worldAddAreaPiece(w, s_1499Area, &s_1499Plane, px, ry, -pz, 0, true);
			uint32_t h = (uint32_t)(ix * 73856093) ^ (uint32_t)(iz * 19349663);
			for (int j = 0; j < s_1499Chunks[type].n; j++) {
				int o = s_1499Chunks[type].obj[j];
				if (o < 1 || o > 15) continue;
				float yaw = s_1499Chunks[type].yaw[j];
				if (yaw < 0) {
					h = h * 1664525u + 1013904223u;
					yaw = (float)((h >> 8) % 360);
				}
				worldAddAreaPiece(w, s_1499Area, &s_1499Obj[o], px + s_1499Chunks[type].x[j], ry, -(pz + s_1499Chunks[type].z[j]), yaw, true);
			}
		}
}

static int spawn1499(World* w, const float pos[3], float yaw, int role)
{
	int i = npcSpawn(NPC_1499, pos, yaw);
	Npc* n = npcGet(i);
	if (!n) return -1;
	n->scripted = true;
	n->placed = true;
	n->timer = yaw;
	n->speed *= frand(0.9f, 1.1f);
	npcSnapToFloor(w, n);
	s_1499Role[i] = role;
	return i;
}

static void clear1499(void)
{
	for (int i = 0; i < MAX_NPCS; i++)
		if (s_1499Role[i] >= 0) {
			npcRemove(i);
			s_1499Role[i] = -1;
		}
}

static void walk1499(World* w, Npc* n, float dt)
{
	float a = n->yaw * DEG2RAD, step = FRAMES(n->currSpeed) * dt;
	float c[3] = { n->pos[0] - sinf(a) * step, n->pos[1] + 0.5f, n->pos[2] - cosf(a) * step };
	worldCollide(w, c);
	n->pos[0] = c[0];
	n->pos[2] = c[2];
	float probe[3] = { n->pos[0], n->pos[1] + 1.0f, n->pos[2] };
	float fy = worldRayDown(w, probe, 4.0f);
	if (isfinite(fy)) n->pos[1] = fy;
}

static void walkAnim1499(Npc* n, int i, float dt)
{
	if (i % 2 == 0) npcAnimate(n, 1, 62, n->currSpeed * 28, true, dt);
	else npcAnimate(n, 100, 167, n->currSpeed * 28, true, dt);
}

static const char* s_1499Death = "All personnel situated within Evacuation Shelter LC-2 during the breach have been "
                                 "administered Class-B amnestics due to Incident 1499-E. The Class D subject involved "
                                 "in the event died shortly after being shot by Agent [REDACTED].";

static const char* update1499(World* w, Npc* n, int i, EvPlayer* p, float dt)
{
	int role = s_1499Role[i];
	float dist = dist3(n->pos, p->pos);
	float prev = n->frame;
	float head[3] = { n->pos[0], n->pos[1] + 1.5f, n->pos[2] };
	switch (n->state) {
	case 0:
		if (role == 0) {
			n->lastSeen += FRAMES(dt);
			if (n->lastSeen < 500 * 1.5f) {
				n->currSpeed = 0;
			} else if (n->lastSeen < 10000) {
				n->currSpeed = curveValue(n->speed, n->currSpeed, 10, dt);
				if (rand() % 200 == 0) n->timer += frand(-45, 45);
				float a = n->yaw * DEG2RAD, ahead[3] = { head[0] - sinf(a) * 1.5f, head[1], head[2] - cosf(a) * 1.5f };
				if (!worldLineOfSight(w, head, ahead)) n->timer = n->yaw + frand(80, 110);
			} else {
				n->currSpeed = curveValue(0, n->currSpeed, 50, dt);
				if (n->currSpeed < 0.001f) n->lastSeen = 0;
			}
			float d = n->timer - n->yaw;
			while (d > 180) d -= 360;
			while (d < -180) d += 360;
			n->yaw += d * fminf(1, FRAMES(dt) / 10);
			if (rand() % 5000 == 0 && dist < 20) {
				char snd[32];
				snprintf(snd, sizeof(snd), "scp/1499/idle%d", 1 + rand() % 4);
				n->chan = sfxPlay3D(snd, head, 20.0f, 1.0f);
			}
			if (i % 2 == 0 && dist < 10.0f && worldLineOfSight(w, head, p->eye)) {
				n->state = 2;
				n->state2 = 0;
				if (dist < 5.0f) {
					n->chan = sfxPlay3D("scp/1499/triggered", head, 20.0f, 1.0f);
					n->state2 = 1;
					for (int k = 0; k < MAX_NPCS; k++) {
						Npc* o = npcGet(k);
						if (o && k != i && s_1499Role[k] == 0) o->state = 1;
					}
				}
				n->frame = 203;
			}
		} else {
			n->currSpeed = 0;
		}
		if (n->currSpeed > 0.0001f) walkAnim1499(n, i, dt);
		else if (role == 2) npcAnimate(n, 509, 601, 0.2f, true, dt);
		else npcAnimate(n, 296, 320, 0.2f, true, dt);
		break;
	case 1:
		npcFace(n, p->pos, 20.0f / 70.0f * 10, dt);
		n->currSpeed = curveValue(n->speed * (role == 1 ? 2.0f : 1.75f), n->currSpeed, 10, dt);
		walkAnim1499(n, i, dt);
		if (dist < 0.75f) {
			if (i % 2 == 0 || role >= 1) {
				n->state2 = 1 + rand() % 2;
				n->state = 3;
				n->frame = n->state2 == 1 ? 63 : 168;
			} else {
				n->state = 4;
			}
		}
		break;
	case 2:
		n->currSpeed = 0;
		npcAnimate(n, 203, 295, 0.1f, false, dt);
		if (n->frame > 294.0f) n->state = (int)n->state2;
		break;
	case 3: {
		n->currSpeed = curveValue(0, n->currSpeed, 5, dt);
		bool first = n->state2 == 1;
		npcAnimate(n, first ? 63 : 168, first ? 100 : 202, 0.6f, false, dt);
		float hit = first ? 89 : 189;
		if (prev < hit && n->frame >= hit && dist <= 0.85f) {
			*p->injuries += frand(0.75f, 1.5f);
			sfxPlay3D(rand() % 2 ? "general/slash1" : "general/slash2", n->pos, 10.0f, 1.0f);
			if (*p->injuries > 10.0f) return s_1499Death;
		}
		if (n->frame >= (first ? 99 : 201)) {
			n->state2 = 0;
			n->state = 1;
		}
		break;
	}
	case 4:
		n->currSpeed = curveValue(0, n->currSpeed, 5, dt);
		npcAnimate(n, 296, 320, 0.2f, true, dt);
		npcFace(n, p->pos, 20.0f / 70.0f * 10, dt);
		if (dist > 0.85f) n->state = 1;
		break;
	}
	walk1499(w, n, dt);
	if (role == 0 && dist > C1499_SIZE * (C1499_RING + 1)) {
		npcRemove(i);
		s_1499Role[i] = -1;
	}
	return NULL;
}

static const char* ev1499(World* w, Event* e, EvPlayer* p, float dt)
{
	s_1499Music = NULL;
	if (s_1499Area < 0 || s_1499Room != e->room) return NULL;
	bool in = worldAreaAt(w, p->pos) == s_1499Area;
	if (!in) {
		if (e->state == 2) {
			clear1499();
			if (e->state3 < 70 * 30) e->state3 = 0;
			e->state = 1;
			w->fogNear = w->fogFar = 0;
			w->fogColor = 0;
			s_1499ChunkX = s_1499ChunkZ = 1 << 30;
		}
		return NULL;
	}
	use1499Models();
	const MapRoomPlacement* r = &w->map.rooms[e->room];
	float rx = r->gx * 8.0f, ry = r->y, rz = -r->gy * 8.0f;
	update1499Chunks(w, p->pos);
	if (e->state == 2) {
		bool any = false;
		for (int i = 0; i < MAX_NPCS; i++) any |= s_1499Role[i] >= 0;
		if (!any) {
			for (int i = 0; i < MAX_NPCS; i++) {
				Npc* n = npcGet(i);
				if (n && n->type == NPC_1499) npcRemove(i);
			}
			e->state = 1;
		}
	}
	if (e->state < 2) {
		e->state = 2;
		if (e->state3 < 70 * 30) {
			struct {
				float x, y, z, yaw;
				int role;
			} cast[6] = {
				{ -1917, 1904, 2308, 270, 2 }, { -1917, 1904, 2052, 270, 1 }, { 4055, 240, 1884, 270, 3 },
				{ 4055, 240, 2876, 270, 3 },   { -1877, 192, 1071, 270, 3 },  { -1877, 192, 3503, 270, 3 },
			};
			for (int k = 0; k < 4; k++) {
				float at[3] = { rx + cast[k].x * RSC, ry + cast[k].y * RSC, rz - cast[k].z * RSC };
				e->npc[k] = spawn1499(w, at, cast[k].yaw, cast[k].role);
			}
		}
		for (int k = 0; k < C1499_WANDERERS; k++) {
			float at[3] = { p->pos[0] + frand(-20, 20), p->pos[1] + 0.1f, p->pos[2] + frand(-20, 20) };
			int idx = spawn1499(w, at, frand(0, 360), 0);
			Npc* n = npcGet(idx);
			if (n && dist3(at, p->pos) < 10.0f) n->state = 0;
		}
	}

	w->fogColor = (96u << 16) | (97u << 8) | 104u;
	w->fogNear = 20.0f;
	w->fogFar = 45.0f;
	if (p->pos[1] < ry) {
		p->pos[1] = ry + 0.5f;
	}
	const char* death = NULL;
	bool chased = false;
	for (int i = 0; i < MAX_NPCS; i++) {
		if (s_1499Role[i] < 0) continue;
		Npc* n = npcGet(i);
		if (!n || n->type != NPC_1499) {
			s_1499Role[i] = -1;
			continue;
		}
		const char* d = update1499(w, n, i, p, dt);
		if (d && !death) death = d;
		if (n->state == 1 || n->state == 3) chased = true;
	}

	int count = 0;
	for (int i = 0; i < MAX_NPCS; i++) count += s_1499Role[i] == 0;
	if (count < C1499_WANDERERS && rand() % 120 == 0) {
		float a = frand(0, 360) * DEG2RAD, d = frand(40, 70);
		float at[3] = { p->pos[0] + sinf(a) * d, ry + 0.5f, p->pos[2] + cosf(a) * d };
		spawn1499(w, at, frand(0, 360), 0);
	}

	Npc* king = npcGet(e->npc[0]);
	if (king && king->type != NPC_1499) king = NULL;
	if (e->state3 < 70 * 10) {
		float cx = rx - 56 * RSC, cz = rz - 2287 * RSC;
		if (fabsf(p->pos[0] - cx) < 2160 * RSC && fabsf(p->pos[2] - cz) < 1408 * RSC) e->state3 += FRAMES(dt);
	} else if (e->state3 < 70 * 20) {
		e->state3 = 70 * 20;
		if (king) king->frame = 713;
	} else if (e->state3 == 70 * 20) {
		if (king) {
			npcAnimate(king, 713, 855, 0.2f, false, dt);
			npcFace(king, p->pos, 10.0f, dt);
		}
		if (!king || king->frame > 854.5f) {
			for (int k = 2; k < 4; k++) {
				Npc* g = npcGet(e->npc[k]);
				if (!g || g->type != NPC_1499) continue;
				if (k == 2) sfxPlay3D("scp/1499/triggered", g->pos, 50.0f, 1.0f);
				g->state = 1;
				g->frame = 203;
			}
			e->state3 = 70 * 30;
		}
	}
	s_1499Music = e->state3 < 70 * 30 && king ? "music/havemercyonme(nochoir)" : chased ? "music/1499danger" : "music/1499";
	return death;
}

bool eventsEnter1499(World* w, float pos[3], float* yaw)
{
	if (s_1499Area < 0) return false;
	memcpy(s_1499Prev, pos, sizeof(s_1499Prev));
	if (s_1499Back[0] == 0 && s_1499Back[1] == 0 && s_1499Back[2] == 0) {
		float l[3] = { 6086 * RSC, 304 * RSC, -2292.5f * RSC };
		worldRoomToWorld(w, s_1499Room, l, pos);
		*yaw = 90;
	} else {
		memcpy(pos, s_1499Back, sizeof(s_1499Back));
		pos[1] += 0.05f;
	}
	memset(s_1499Back, 0, sizeof(s_1499Back));
	s_1499In = true;
	sfxPlay("scp/1499/enter", 1.0f);
	worldStream(w, pos);
	return true;
}

bool eventsLeave1499(World* w, float pos[3])
{
	if (!s_1499In || s_1499Area < 0 || worldAreaAt(w, pos) != s_1499Area) return false;
	memcpy(s_1499Back, pos, sizeof(s_1499Back));
	memcpy(pos, s_1499Prev, sizeof(s_1499Prev));
	pos[1] += 0.05f;
	s_1499In = false;
	sfxPlay("scp/1499/exit", 1.0f);
	worldStream(w, pos);
	return true;
}

const char* eventsDebugArea(World* w, int k, float pos[3], float* yaw)
{
	switch (((k % 3) + 3) % 3) {
	case 0:
		if (s_mtArea < 0 || s_mtNway == 0) return NULL;
		for (int i = 0; i < s_nevents; i++)
			if (!strcmp(defName(&s_events[i]), "room2tunnel")) s_events[i].state = 2;
		memcpy(pos, s_mtWaypoints[s_mtNway / 2], sizeof(float) * 3);
		pos[1] += 0.4f;
		*yaw = 0;
		return "maintenance tunnels";
	case 1:
		if (s_frArea < 0) return NULL;
		for (int i = 0; i < s_nevents; i++)
			if (!strcmp(defName(&s_events[i]), "room860")) {
				s_events[i].state = 1;
				s_events[i].state2 = 1;
			}
		pos[0] = s_frDoor[0][0];
		pos[1] = s_frDoor[0][1] + 1.0f;
		pos[2] = s_frDoor[0][2] - 1.0f;
		*yaw = 180;
		return "SCP-860 forest";
	default:
		return eventsEnter1499(w, pos, yaw) ? "dimension 1499" : NULL;
	}
}
