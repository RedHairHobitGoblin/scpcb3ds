#define MT_GRID 19
#define MT_SPACING 2.0f

static Room s_mtModels[7];
static float s_mtEnd[2][3];
static float s_mtCab[2][3];
static int s_mtArea = -1;
static float s_mtGenItems[2][3];
static bool s_mtHasGen;
static float s_mtWaypoints[MT_GRID * MT_GRID][3];
static int s_mtNway;

static uint32_t s_mtRng;
static int mtRand(int lo, int hi)
{
	s_mtRng = s_mtRng * 1664525u + 1013904223u;
	return lo + (int)((s_mtRng >> 8) % (uint32_t)(hi - lo + 1));
}

static void mtUseModels(void)
{
	static const char* paths[7] = { "romfs:/rooms/mt1.scr", "romfs:/rooms/mt2.scr", "romfs:/rooms/mt2c.scr",
	                                "romfs:/rooms/mt3.scr", "romfs:/rooms/mt4.scr", "romfs:/rooms/mt_elevator.scr",
	                                "romfs:/rooms/mt_generator.scr" };
	for (int i = 0; i < 7; i++) useModel(&s_mtModels[i], paths[i]);
}

static void mtPoint(float bx, float bz, float yaw, float along, float y, float side, float out[3])
{
	float c = cosf(yaw * DEG2RAD), s = sinf(yaw * DEG2RAD);
	float x = bx + (c * along - s * side) * RSC;
	float z = bz + (s * along + c * side) * RSC;
	out[0] = x;
	out[1] = 8.0f + y * RSC;
	out[2] = -z;
}

static void tunnelsGenerate(World* w, int room)
{
	s_mtArea = -1;
	int g[MT_GRID * MT_GRID];
	memset(g, 0, sizeof(g));
#define G(x, y) g[(x) + (y) * MT_GRID]
	s_mtRng = w->seed * 2654435761u + (uint32_t)room * 97u + 1u;

	int dir = mtRand(0, 1) << 1;
	int ix = MT_GRID / 2 + mtRand(-2, 2), iy = MT_GRID / 2 + mtRand(-2, 2);
	G(ix, iy) = 1;
	if (dir == 2) G(ix + 1, iy) = 1;
	else G(ix - 1, iy) = 1;
	int count = 2;
	while (count < 100) {
		int n = mtRand(1, 5) << mtRand(1, 2);
		for (int i = 1; i <= n; i++) {
			bool ok = true;
			switch (dir) {
			case 0: if (ix < MT_GRID - 2 - (i % 2)) ix++; else ok = false; break;
			case 1: if (iy < MT_GRID - 2 - (i % 2)) iy++; else ok = false; break;
			case 2: if (ix > 1 + (i % 2)) ix--; else ok = false; break;
			case 3: if (iy > 1 + (i % 2)) iy--; else ok = false; break;
			}
			if (!ok) break;
			if (G(ix, iy) == 0) {
				G(ix, iy) = 1;
				count++;
			}
		}
		dir += (mtRand(0, 1) << 1) - 1;
		while (dir < 0) dir += 4;
		while (dir > 3) dir -= 4;
	}

	int n2[MT_GRID * MT_GRID];
	for (int y = 0; y < MT_GRID; y++)
		for (int x = 0; x < MT_GRID; x++) {
			int v = 0;
			if (G(x, y) > 0) {
				if (y + 1 < MT_GRID && G(x, y + 1) > 0) v++;
				if (y > 0 && G(x, y - 1) > 0) v++;
				if (x + 1 < MT_GRID && G(x + 1, y) > 0) v++;
				if (x > 0 && G(x - 1, y) > 0) v++;
			}
			n2[x + y * MT_GRID] = v;
		}
	memcpy(g, n2, sizeof(g));
#define GS(x, y) (((x) >= 0 && (y) >= 0 && (x) < MT_GRID && (y) < MT_GRID) ? G(x, y) : 0)

	for (int x = 0; x < MT_GRID - 1; x++) {
		bool found = false, candidate = false, placed = false;
		for (int y = 0; y < MT_GRID && !placed; y++) {
			if (GS(x + 1, y) <= 0) continue;
			found = true;
			if (GS(x + 1, y + 1) < 3 && GS(x + 1, y - 1) < 3) {
				candidate = true;
				if (mtRand(0, 1) == 1) {
					G(x + 1, y) += 1;
					G(x, y) = 7;
					placed = true;
				}
			}
		}
		if (!found) continue;
		if (!placed && candidate) x--;
		else break;
	}

	int firstX = -1, firstY = -1, lastX = -1, lastY = -1;
	for (int y = 0; y < MT_GRID; y++)
		for (int x = 0; x < MT_GRID; x++) {
			if (G(x, y) != 2) continue;
			bool horiz = GS(x + 1, y) > 0 && GS(x - 1, y) > 0, vert = GS(x, y + 1) > 0 && GS(x, y - 1) > 0;
			if (!horiz && !vert) continue;
			bool quiet = GS(x - 1, y) < 3 && GS(x + 1, y) < 3 && GS(x, y - 1) < 3 && GS(x, y + 1) < 3 &&
			             GS(x - 1, y - 1) < 1 && GS(x + 1, y - 1) < 1 && GS(x - 1, y + 1) < 1;
			if (!quiet) continue;
			if (firstX == -1) {
				firstX = x;
				firstY = y;
			}
			lastX = x;
			lastY = y;
		}
	if (firstX < 0 || (lastX == firstX && lastY == firstY)) return;

	float rx = w->map.rooms[room].gx * 8.0f, rz = w->map.rooms[room].gy * 8.0f;
	int area = worldBeginArea(w, room);
	if (area < 0) return;
	s_mtNway = 0;
	s_mtHasGen = false;
	int roomDoorTunnel[2] = { -1, -1 };
	float cab[2][3];
	for (int y = 0; y < MT_GRID; y++)
		for (int x = 0; x < MT_GRID; x++) {
			int v = G(x, y);
			if (v <= 0) continue;
			int mesh = 0;
			float yaw = 0;
			int ia = GS(x, y + 1), ib = GS(x, y - 1), ic = GS(x + 1, y), id = GS(x - 1, y);
			int kind = v;
			switch (v) {
			case 1:
			case 7:
				mesh = v - 1;
				yaw = ic > 0 ? 90 : id > 0 ? 270 : ia > 0 ? 180 : 0;
				break;
			case 2:
				if ((x == firstX && y == firstY) || (x == lastX && y == lastY)) kind = 6;
				if (ic > 0 && id > 0) {
					mesh = kind - 1;
					yaw = mtRand(0, 1) * 180.0f + 90;
				} else if (ia > 0 && ib > 0) {
					mesh = kind - 1;
					yaw = mtRand(0, 1) * 180.0f;
				} else {
					mesh = kind;
					yaw = (ia > 0 && ic > 0) ? 0 : (ia > 0 && id > 0) ? 90 : (ib > 0 && ic > 0) ? 270 : 180;
				}
				if (x == firstX && y == firstY) kind = 5;
				break;
			case 3:
				mesh = 3;
				yaw = (ia > 0 && ic > 0 && id > 0) ? 90 : (ib > 0 && ic > 0 && id > 0) ? 270 : (ic > 0 && ia > 0 && ib > 0) ? 0 : 180;
				break;
			default:
				mesh = 4;
				yaw = mtRand(0, 3) * 90.0f;
				break;
			}
			if (mesh < 0 || mesh > 6) mesh = 1;
			float bx = rx + x * MT_SPACING, bz = rz + y * MT_SPACING;
			worldAddAreaPiece(w, area, &s_mtModels[mesh], bx, 8.0f, -bz, yaw, true);
			float by = yaw > 180 ? yaw - 360 : yaw;
			if (v == 7) {
				mtPoint(bx, bz, by, -208, 80, 1226, s_mtGenItems[0]);
				mtPoint(bx, bz, by, 16, 80, 504, s_mtGenItems[1]);
				s_mtHasGen = true;
			}
			if (kind == 5 || kind == 6) {
				int k = kind == 6 ? 0 : 1;
				float dp[3];
				mtPoint(bx, bz, by, 240, 0, 0, dp);
				dp[1] = 8.0f;
				if (roomDoorTunnel[k] < 0) {
					int d = worldAddDoorAt(w, dp, (int)by + 90, 3, room);
					if (d >= 0) {
						w->doors[d].roomDoor = k ? 3 : 1;
						int pair = worldRoomDoor(w, room, k ? 2 : 0);
						w->doors[d].open = pair >= 0 ? !w->doors[pair].open : false;
						w->doors[d].openstate = w->doors[d].open ? 180 : 0;
					}
					roomDoorTunnel[k] = d;
					mtPoint(bx, bz, by, 552, 240, 0, cab[k]);
				}
				s_mtEnd[k][0] = bx;
				s_mtEnd[k][1] = 8.0f;
				s_mtEnd[k][2] = -bz;
			}
			s_mtWaypoints[s_mtNway][0] = bx;
			s_mtWaypoints[s_mtNway][1] = 8.2f;
			s_mtWaypoints[s_mtNway][2] = -bz;
			s_mtNway++;
		}
	memcpy(s_mtCab, cab, sizeof(cab));
	float mn[3] = { rx - 6.0f, 6.0f, -(rz + MT_SPACING * MT_GRID + 6.0f) };
	float mx[3] = { rx + MT_SPACING * MT_GRID + 6.0f, 13.0f, -(rz - 6.0f) };
	worldEndArea(w, area, mn, mx);
	s_mtArea = area;
#undef GS
#undef G
}

static const char* evRoom2tunnel(World* w, Event* e, EvPlayer* p, float dt)
{
	if (s_mtArea < 0 || s_mtArea >= w->nareas || w->areas[s_mtArea].room != e->room) return NULL;
	if (e->state == 0) {
		if (s_mtHasGen) {
			itemsSpawnAt("scp-500-01", "scp500", s_mtGenItems[0]);
			itemsSpawnAt("night vision goggles", "nvgoggles", s_mtGenItems[1]);
		}
		e->state = 1;
	}
	float o2[3], o4[3];
	if (eventsRoomObject(w, e->room, 2, o2) && eventsRoomObject(w, e->room, 4, o4)) {
		elevator(w, e, worldRoomDoor(w, e->room, 0), worldRoomDoor(w, e->room, 1), o2, s_mtCab[0], &e->state2, p, dt);
		elevator(w, e, worldRoomDoor(w, e->room, 2), worldRoomDoor(w, e->room, 3), o4, s_mtCab[1], &e->state3, p, dt);
	}
	if (worldAreaAt(w, p->pos) == s_mtArea || roomDist(w, e, p) < 12.0f) mtUseModels();
	if (!worldInTunnels(w, p->pos)) return NULL;
	if (e->state == 1) {
		e->state = 2;
		int k = dist3(p->pos, s_mtEnd[0]) < dist3(p->pos, s_mtEnd[1]) ? 0 : 1;
		Npc* o = npcFind(NPC_106);
		if (!s_contained106 && o && o->hidden && !o->scripted) {
			o->pos[0] = s_mtEnd[k][0];
			o->pos[1] = p->pos[1] - 0.3f;
			o->pos[2] = s_mtEnd[k][2];
			o->frame = 110;
			o->state = 1;
			o->hidden = false;
		}
		for (int i = 0; i < 2 && s_mtNway > 0; i++) {
			int from = i * (MT_GRID * MT_GRID) / 5;
			if (from >= s_mtNway) from = 0;
			int at = from + rand() % (s_mtNway - from);
			Npc* m = npcGet(npcSpawn(NPC_966, s_mtWaypoints[at], 0));
			if (m) m->placed = true;
		}
	}
	return NULL;
}
