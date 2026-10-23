#define FR_GRID 10
#define FR_TILE 12.0f
#define FR_Y 30.0f

static Room s_frTiles[5], s_frWall, s_860Door, s_860Frame;
static int s_frGrid[FR_GRID * FR_GRID + 11];
static int s_frArea = -1;
static int s_frRoom = -1;
static float s_frDoor[2][3];
static float s_frDoorYaw[2];
static float s_frLog[3][3];
static int s_frNlogs;

static uint32_t s_frRng;
static int frRand(int lo, int hi)
{
	s_frRng = s_frRng * 1103515245u + 12345u;
	return lo + (int)((s_frRng >> 8) % (uint32_t)(hi - lo + 1));
}
static bool frChance(int c) { return frRand(0, 100) <= c; }

static int frGet(int i) { return i >= 0 && i < FR_GRID * FR_GRID + 11 ? s_frGrid[i] : 0; }
static void frSet(int i, int v)
{
	if (i >= 0 && i < FR_GRID * FR_GRID + 11) s_frGrid[i] = v;
}
#define ROW(y) ((FR_GRID - 1 - (y)) * FR_GRID)

static int frMoveX(int dir, int px) { return dir == 1 ? px : px - 1 + dir; }
static int frMoveY(int dir, int py) { return dir == 1 ? py + 1 : py; }
static int frTurn(int pathx, int dir, bool* deviated)
{
	int dev = 5 - pathx;
	*deviated = false;
	if ((dir == 0 && dev >= 3) || (dir == 2 && dev <= -3)) {
		dir = (dir + 2) % 4;
		*deviated = true;
	}
	return dir;
}

static void forestGrid(void)
{
	memset(s_frGrid, 0, sizeof(s_frGrid));
	int door1 = frRand(3, 7), door2 = frRand(3, 7);
	frSet(door1, 3);
	frSet((FR_GRID - 1) * FR_GRID + door2, 3);
	int px = door2, py = 1, dir = 1;
	frSet(ROW(py) + px, 1);
	bool dev;
	while (py < FR_GRID - 4) {
		if (dir == 1) {
			if (frChance(40)) {
				dir = 2 * frRand(0, 1);
				dir = frTurn(px, dir, &dev);
				if (dev) frSet(ROW(py) + px, 1);
				px = frMoveX(dir, px);
				py = frMoveY(dir, py);
			}
		} else {
			dir = frTurn(px, dir, &dev);
			if (dev || frChance(27)) dir = 1;
			px = frMoveX(dir, px);
			py = frMoveY(dir, py);
			if (dir == 1) {
				frSet(ROW(py) + px, 1);
				px = frMoveX(dir, px);
				py = frMoveY(dir, py);
			}
		}
		frSet(ROW(py) + px, 1);
	}
	dir = 1;
	while (py < FR_GRID - 2) {
		px = frMoveX(dir, px);
		py = frMoveY(dir, py);
		frSet(ROW(py) + px, 1);
	}
	if (px != door1) {
		dir = door1 > px ? 2 : 0;
		while (px != door1) {
			px = frMoveX(dir, px);
			py = frMoveY(dir, py);
			frSet(ROW(py) + px, 1);
		}
	}

	int ny = -3;
	while (ny < FR_GRID - 6) {
		ny += 4;
		int ty = ny, nx = 0;
		if (!frChance(65)) continue;
		int type = frChance(0) ? -2 : -1;
		int pos = 2 * frRand(0, 1);
		int left = FR_GRID, right = 0;
		for (int i = 0; i <= FR_GRID; i++)
			if (frGet(ROW(ny) + i) == 1) {
				if (i < left) left = i;
				if (i > right) right = i;
			}
		nx = pos == 0 ? left - 1 : right + 1;
		if ((ty != 0 && frGet((FR_GRID - 1 - ty + 1) * FR_GRID + nx) == 1) || frGet((FR_GRID - 1 - ty - 1) * FR_GRID + nx) == 1)
			break;
		frSet(ROW(ty) + nx, type);
		nx = pos == 0 ? left - 2 : right + 2;
		frSet(ROW(ty) + nx, type);
		int i = 2;
		while (i < 4) {
			i++;
			if (frChance(18)) break;
			if (frRand(0, 3) == 0) nx += pos == 0 ? -1 : 1;
			else ty++;
			int n = (FR_GRID - 1 - ty + 1) * FR_GRID + nx;
			if (n < FR_GRID - 1 && ty != 0 && frGet(n) == 1) break;
			n = (FR_GRID - 1 - ty - 1) * FR_GRID + nx;
			if (n > 0 && frGet(n) == 1) break;
			frSet(ROW(ty) + nx, type);
			if (ty >= FR_GRID - 2) break;
		}
	}
	for (int i = 0; i < FR_GRID * FR_GRID; i++)
		if (s_frGrid[i] < 0) s_frGrid[i] = 1;
}

static void forestUseModels(void)
{
	static const char* paths[5] = { "romfs:/models/forest1.scr", "romfs:/models/forest2.scr", "romfs:/models/forest2c.scr",
	                                "romfs:/models/forest3.scr", "romfs:/models/forest4.scr" };
	for (int i = 0; i < 5; i++) useModel(&s_frTiles[i], paths[i]);
	useModel(&s_frWall, "romfs:/models/forest_wall.scr");
}

static void forestGenerate(World* w, int room)
{
	s_frArea = -1;
	s_frRoom = room;
	s_frRng = w->seed * 2246822519u + (uint32_t)room * 131u + 7u;
	forestGrid();
	int area = worldBeginArea(w, room);
	if (area < 0) return;
	float rx = w->map.rooms[room].gx * 8.0f, rz = w->map.rooms[room].gy * 8.0f;
	float y = w->map.rooms[room].y + FR_Y;
	bool logPlaced[4] = { false };
	s_frNlogs = 0;
#define FG(x, yy) frGet((yy) * FR_GRID + (x))
	for (int tx = 1; tx < FR_GRID; tx++)
		for (int ty = 1; ty < FR_GRID; ty++) {
			if (FG(tx, ty) != 1) continue;
			int n = 0;
			if (tx + 1 < FR_GRID) n += FG(tx + 1, ty) > 0;
			if (tx - 1 >= 0) n += FG(tx - 1, ty) > 0;
			if (ty + 1 < FR_GRID) n += FG(tx, ty + 1) > 0;
			if (ty - 1 >= 0) n += FG(tx, ty - 1) > 0;
			int tile = -1;
			float angle = 0;
			switch (n) {
			case 1:
				tile = 0;
				angle = FG(tx, ty + 1) > 0 ? 180 : FG(tx - 1, ty) > 0 ? 270 : FG(tx + 1, ty) > 0 ? 90 : 0;
				break;
			case 2:
				if (FG(tx, ty - 1) > 0 && FG(tx, ty + 1) > 0) {
					tile = 1;
				} else if (FG(tx + 1, ty) > 0 && FG(tx - 1, ty) > 0) {
					tile = 1;
					angle = 90;
				} else {
					tile = 2;
					if (FG(tx - 1, ty) > 0 && FG(tx, ty + 1) > 0) angle = 180;
					else if (FG(tx + 1, ty) > 0 && FG(tx, ty - 1) > 0) angle = 0;
					else if (FG(tx - 1, ty) > 0 && FG(tx, ty - 1) > 0) angle = 270;
					else angle = 90;
				}
				break;
			case 3:
				tile = 3;
				angle = FG(tx, ty - 1) == 0 ? 180 : FG(tx - 1, ty) == 0 ? 90 : FG(tx + 1, ty) == 0 ? 270 : 0;
				break;
			case 4: tile = 4; break;
			}
			if (tile < 0) continue;
			float bx = rx + tx * FR_TILE, bz = rz + ty * FR_TILE;
			worldAddAreaPiece(w, area, &s_frTiles[tile], bx, y, -bz, angle, true);
			if (ty % 3 == 2 && !logPlaced[ty / 3] && s_frNlogs < 3) {
				logPlaced[ty / 3] = true;
				s_frLog[s_frNlogs][0] = bx;
				s_frLog[s_frNlogs][1] = y + 0.5f;
				s_frLog[s_frNlogs][2] = -bz;
				s_frNlogs++;
			}
		}

	for (int i = 0; i < 2; i++) {
		int ty = (FR_GRID - 1) * i;
		for (int tx = 1; tx < FR_GRID; tx++) {
			if (FG(tx, ty) != 3) continue;
			float bx = rx + tx * FR_TILE, bz = rz + ty * FR_TILE + FR_TILE / 2 - FR_TILE * i;
			float yaw = 180.0f * i;
			worldAddAreaPiece(w, area, &s_frWall, bx, y, -bz, yaw, true);

			float c = cosf(yaw * DEG2RAD), s = sinf(yaw * DEG2RAD);
			float lx = 72 * RSC;
			s_frDoor[i][0] = bx + lx * c;
			s_frDoor[i][1] = y + 32 * RSC;
			s_frDoor[i][2] = -(bz + lx * s);
			s_frDoorYaw[i] = yaw + 180;
		}
	}
#undef FG
	float mn[3] = { rx, y - 12.0f, -(rz + FR_GRID * FR_TILE + 6) };
	float mx[3] = { rx + FR_GRID * FR_TILE + 6, y + 12.0f, -(rz - 6) };
	worldEndArea(w, area, mn, mx);
	s_frArea = area;
}

static void frTileOf(const World* w, const float p[3], int* tx, int* tz)
{
	float rx = w->map.rooms[s_frRoom].gx * 8.0f, rz = w->map.rooms[s_frRoom].gy * 8.0f;
	*tx = (int)floorf((p[0] - rx + 6.0f) / FR_TILE);
	*tz = (int)floorf((-p[2] - rz + 6.0f) / FR_TILE);
}

static void frTileCentre(const World* w, float tx, float tz, float out[3])
{
	float rx = w->map.rooms[s_frRoom].gx * 8.0f, rz = w->map.rooms[s_frRoom].gy * 8.0f;
	out[0] = rx + tx * FR_TILE;
	out[1] = w->map.rooms[s_frRoom].y + FR_Y;
	out[2] = -(rz + tz * FR_TILE);
}

static void frHeading(const World* w, const Npc* n, const float player[3], float out[3])
{
	int ptx, ptz, xt, zt;
	frTileOf(w, player, &ptx, &ptz);
	frTileOf(w, n->pos, &xt, &zt);
	if (xt == ptx && zt == ptz) {
		memcpy(out, player, sizeof(float) * 3);
		return;
	}
	for (int x2 = xt - 1 < 0 ? 0 : xt - 1; x2 <= xt + 1 && x2 < FR_GRID; x2++)
		for (int z2 = zt - 1 < 0 ? 0 : zt - 1; z2 <= zt + 1 && z2 < FR_GRID; z2++)
			if (frGet(z2 * FR_GRID + x2) > 0 && (x2 != xt || z2 != zt) && (x2 == xt || z2 == zt) &&
			    abs(ptx - x2) + abs(ptz - z2) < abs(ptx - xt) + abs(ptz - zt)) {
				frTileCentre(w, (float)x2, (float)z2, out);
				return;
			}
	memcpy(out, player, sizeof(float) * 3);
}

static void frStep(const float pos[3])
{
	char snd[32];
	snprintf(snd, sizeof(snd), "step/stepforest%d", 1 + rand() % 3);
	sfxPlay3D(snd, pos, 15.0f, 0.5f);
}

static void frCancer(const float pos[3], int lo, int hi, float range)
{
	char snd[32];
	snprintf(snd, sizeof(snd), "scp/860/cancer%d", lo + rand() % (hi - lo + 1));
	sfxPlay3D(snd, pos, range, 1.0f);
}

static void frWalk(World* w, Npc* n, const float to[3], float speed, float dt)
{
	n->yaw = yawTo(n->pos, to);
	float a = n->yaw * DEG2RAD, step = FRAMES(speed) * dt;
	n->pos[0] -= sinf(a) * step;
	n->pos[2] -= cosf(a) * step;
	float probe[3] = { n->pos[0], n->pos[1] + 3.0f, n->pos[2] };
	float fy = worldRayDown(w, probe, 8.0f);
	if (isfinite(fy)) n->pos[1] = fy;
}

static const char* update860(World* w, Npc* n, EvPlayer* p, float dt)
{
	float dist = dist3(n->pos, p->pos);
	float prev = n->frame;
	float head[3] = { n->pos[0], n->pos[1] + 1.8f, n->pos[2] };
	switch (n->state) {
	case 0:
		n->hidden = true;
		n->state2 = 0;
		break;
	case 1:
		if (n->hidden) {
			int x, z;
			frTileOf(w, p->pos, &x, &z);
			bool placed = false;
			for (int x2 = x - 1 < 1 ? 1 : x - 1; x2 <= x + 1 && x2 <= FR_GRID && !placed; x2 += 2)
				for (int z2 = z - 1 < 1 ? 1 : z - 1; z2 <= z + 1 && z2 <= FR_GRID && !placed; z2 += 2) {
					if (frGet(z2 * FR_GRID + x2) != 0) continue;
					float at[3];
					frTileCentre(w, (x2 + x) / 2.0f, (z2 + z) / 2.0f, at);
					if (inView(p, at)) continue;
					memcpy(n->pos, at, sizeof(at));
					placed = true;
				}
			if (!placed) {
				n->state = 0;
				break;
			}
			float probe[3] = { n->pos[0], n->pos[1] + 12.0f, n->pos[2] };
			float fy = worldRayDown(w, probe, 24.0f);
			if (isfinite(fy)) n->pos[1] = fy;
			n->hidden = false;
			frStep(n->pos);
			int k = rand() % 3;
			n->yaw = yawTo(n->pos, p->pos) + (k == 2 ? 90 : 0);
			n->frame = k == 0 ? 2 : k == 1 ? 201 : 299;
			n->state2 = 0;
		} else if (n->state2 == 0) {
			if (inView(p, head) && worldLineOfSight(w, p->eye, head)) {
				n->state2 = 1;
				if (rand() % 8 == 0) frCancer(n->pos, 0, 2, 20.0f);
			}
		} else if (n->frame <= 199) {
			npcAnimate(n, 2, 199, 0.5f, false, dt);
			if (n->frame >= 199) {
				n->frame = 298;
				frStep(n->pos);
			}
		} else if (n->frame <= 297) {
			npcFace(n, p->pos, 10.0f, dt);
			npcAnimate(n, 200, 297, 0.5f, false, dt);
			if (n->frame >= 297) {
				n->frame = 298;
				frStep(n->pos);
			}
		} else {
			n->currSpeed = curveValue(n->speed, n->currSpeed, 10, dt);
			npcAnimate(n, 298, 316, n->currSpeed * 10, true, dt);
			frWalk(w, n, p->pos, n->currSpeed, dt);
			if (dist > 15.0f) {
				n->hidden = true;
				n->state = 0;
			}
		}
		break;
	case 2:
		if (n->hidden) {
			int x, z;
			frTileOf(w, p->pos, &x, &z);
			bool placed = false;
			for (int x2 = x - 1 < 1 ? 1 : x - 1; x2 <= x + 1 && x2 <= FR_GRID && !placed; x2++)
				for (int z2 = z - 1 < 1 ? 1 : z - 1; z2 <= z + 1 && z2 <= FR_GRID && !placed; z2++) {
					if (frGet(z2 * FR_GRID + x2) <= 0 || (x2 == x && z2 == z) || (x2 != x && z2 != z)) continue;
					float at[3];
					frTileCentre(w, (float)x2, (float)z2, at);
					memcpy(n->pos, at, sizeof(at));
					if (inView(p, at)) *p->blinkTimer = -10;
					placed = true;
				}
			if (!placed) {
				n->state = 0;
				break;
			}
			float probe[3] = { n->pos[0], n->pos[1] + 12.0f, n->pos[2] };
			float fy = worldRayDown(w, probe, 24.0f);
			if (isfinite(fy)) n->pos[1] = fy;
			n->hidden = false;
			n->frame = 494;
		} else {
			float to[3];
			frHeading(w, n, p->pos, to);
			n->currSpeed = curveValue(n->speed * 0.3f, n->currSpeed, 50, dt);
			frWalk(w, n, to, n->currSpeed, dt);
			npcAnimate(n, 494, 569, n->currSpeed * 25, true, dt);
			if (n->state2 == 0 && dist < 8.0f && inView(p, head)) {
				sfxPlay(rand() % 2 ? "scp/860/chase1" : "scp/860/chase2", 1.0f);
				frCancer(n->pos, 0, 2, 20.0f);
				n->state2 = 1;
			}
			if (p->noise > 3.0f) {
				n->state3 += FRAMES(dt);
				if (frand(0, 5000) < n->state3 && !sfxPlaying(n->chan)) {
					char snd[32];
					snprintf(snd, sizeof(snd), "scp/860/cancer%d", rand() % 3);
					n->chan = sfxPlay3D(snd, n->pos, 20.0f, 1.0f);
				}
			} else {
				n->state3 = fmaxf(n->state3 - FRAMES(dt), 0);
			}
			if (dist < 4.5f || n->state3 > frand(200, 250)) {
				frCancer(n->pos, 3, 5, 20.0f);
				n->state = 3;
			}
			if (dist > 20.0f) {
				n->state = 0;
				n->state2 = 0;
				n->hidden = true;
			}
			if ((prev < 533 && n->frame >= 533) || (prev > 568 && n->frame < 497)) frStep(n->pos);
		}
		break;
	case 3: {
		float to[3];
		frHeading(w, n, p->pos, to);
		if (dist < 1.1f || (n->frame > 451 && n->frame < 493)) {
			n->currSpeed = curveValue(0, n->currSpeed, 5, dt);
			if (n->frame < 451 || n->frame > 493) n->frame = 451;
			npcAnimate(n, 451, 493, 0.5f, false, dt);
			npcFace(n, p->pos, 10.0f, dt);
			if (prev < 461 && n->frame >= 461) {
				sfxPlay("general/slash1", 1.0f);
				return "";
			}
			if ((prev < 476 && n->frame >= 476) || (prev < 486 && n->frame >= 486)) sfxPlay("general/slash2", 1.0f);
		} else {
			n->currSpeed = curveValue(n->speed * 0.8f, n->currSpeed, 10, dt);
			npcAnimate(n, 298, 316, n->currSpeed * 10, true, dt);
			if (prev < 307 && n->frame >= 307) frStep(n->pos);
			frWalk(w, n, to, n->currSpeed, dt);
		}
		break;
	}
	}
	return NULL;
}

static bool s_inForest;
static float s_forestFogX = 1.0f;

static const char* s_1499Music;

const char* eventsMusicOverride(void)
{
	if (s_1499Music) return s_1499Music;
	if (!s_inForest) return NULL;
	Npc* n = npcFind(NPC_860);
	if (n && ((n->state2 == 1 && n->state > 1) || n->state > 2)) return "music/8601cancer";
	return "music/8601";
}

static void forestAtmosphere(World* w, bool on)
{
	if (on) {
		float x = s_forestFogX;
		w->fogColor = ((uint32_t)(98 * x) << 16) | ((uint32_t)(133 * x) << 8) | (uint32_t)(162 * x);
		w->fogNear = 0.5f;
		w->fogFar = 8.0f;
	} else if (s_inForest) {
		w->fogColor = 0;
		w->fogNear = w->fogFar = 0;
	}
	s_inForest = on;
}

static const char* evRoom860(World* w, Event* e, EvPlayer* p, float dt)
{
	if (s_frArea < 0 || s_frRoom != e->room) return NULL;
	if (e->state4 == 0) {
		for (int i = 0; i < s_frNlogs; i++) {
			char name[16];
			snprintf(name, sizeof(name), "log #%d", i + 1);
			itemsSpawnAt(name, "paper", s_frLog[i]);
		}
		e->state4 = 1;
	}
	Npc* m = npcGet(e->npc[0]);
	if (m && m->type != NPC_860) m = NULL;
	if (!m) m = npcFind(NPC_860);
	if (m) e->npc[0] = npcIndex(m);
	const char* death = NULL;

	if (e->state == 1) {
		forestUseModels();
		if (worldAreaAt(w, p->pos) != s_frArea) {
			e->state = 0;
			forestAtmosphere(w, false);
			return NULL;
		}
		Npc* o = npcFind(NPC_106);
		if (o && o->hidden && o->timer < 70 * 30) o->timer = 70 * 30;
		if (!m) {
			float at[3] = { 0, -100, 0 };
			m = npcGet(npcSpawn(NPC_860, at, 0));
			if (m) {
				m->scripted = true;
				m->hidden = true;
				m->placed = true;
				m->state = 0;
				e->npc[0] = npcIndex(m);
			}
		}
		float base = w->map.rooms[e->room].y + FR_Y;
		if (p->pos[1] <= base - 1.5f) {
			*p->blinkTimer = -2;
			return "";
		}
		if (m) {
			if (m->state == 0 || dist3(p->pos, m->pos) > 20.0f) {
				float prev = e->state3;
				e->state3 += (1 + p->noise * 0.1f) * FRAMES(dt);
				if (fmodf(e->state3, 500) < 10.0f && fmodf(prev, 500) > 490.0f) {
					m->hidden = true;
					if (e->state3 > 3000 && frand(0, 10000) < e->state3) {
						m->state = 2;
						e->state3 -= frand(1000, 2000);
					} else {
						m->state = 1;
					}
				}
			}
			death = update860(w, m, p, dt);
			s_forestFogX = fmaxf(1.0f - m->state3 / 300.0f, 0.1f);
		}
		forestAtmosphere(w, true);
		for (int i = 0; i < 2; i++) {
			float at[3] = { s_frDoor[i][0], s_frDoor[i][1] + 1.0f, s_frDoor[i][2] };
			if (dist3(at, p->pos) < 1.5f) offerUse(e, p, at, 1.5f, i + 1);
			if (e->pressed != i + 1) continue;
			if (i == (int)e->state2) {
				*p->blinkTimer = -10;
				sfxPlay("door/woodendooropen", 1.0f);
				float o2[3];
				if (eventsRoomObject(w, e->room, 2, o2)) {
					float yaw = (float)w->map.rooms[e->room].angle + e->state2 * 180;
					*p->yaw = yaw;
					p->pos[0] = o2[0] + sinf(yaw * DEG2RAD) * 1.5f;
					p->pos[1] = w->map.rooms[e->room].y + 0.5f;
					p->pos[2] = o2[2] - cosf(yaw * DEG2RAD) * 1.5f;
					worldStream(w, p->pos);
				}
				e->state = 0;
				e->state3 = 0;
				forestAtmosphere(w, false);
				if (m) {
					m->state = 0;
					m->hidden = true;
				}
			} else {
				sfxPlay("door/woodendoorbudge", 1.0f);
				hudMessage("The door will not budge.", 5);
			}
		}
		e->pressed = 0;
		return death;
	}

	forestAtmosphere(w, false);
	float o3[3];
	if (!inRoom(w, e, p) || !eventsRoomObject(w, e->room, 3, o3)) return NULL;
	if (distXZ(o3, p->pos) < 1.0f) {
		float at[3] = { o3[0], o3[1] + 1.0f, o3[2] };
		offerUse(e, p, at, 1.5f, 1);
	}
	if (e->pressed == 1) {
		e->pressed = 0;
		if (strcmp(itemsHeldTempname(), "scp860") != 0) {
			sfxPlay("door/woodendoorbudge", 1.0f);
			hudMessage("The door will not budge.", 5);
			return NULL;
		}
		sfxPlay("door/woodendooropen", 1.0f);
		g_items.selected = -1;
		*p->blinkTimer = -10;
		e->state = 1;
		e->state3 = 0;
		if (m) m->state = 0;

		float c[3];
		roomCentre(w, e->room, c);
		float toRoom = yawTo(p->pos, c) - (float)w->map.rooms[e->room].angle;
		while (toRoom < 0) toRoom += 360;
		while (toRoom >= 360) toRoom -= 360;
		int k = toRoom > 90 && toRoom < 270 ? 0 : 1;
		e->state2 = k ? 0 : 1;
		float yaw = s_frDoorYaw[k] - 180;
		p->pos[0] = s_frDoor[k][0] - sinf(yaw * DEG2RAD) * 0.5f + cosf(yaw * DEG2RAD) * 0.5f;
		p->pos[1] = s_frDoor[k][1] + 0.6f;
		p->pos[2] = s_frDoor[k][2] + cosf(yaw * DEG2RAD) * 0.5f + sinf(yaw * DEG2RAD) * 0.5f;
		*p->yaw = yaw;
		worldStream(w, p->pos);
		forestAtmosphere(w, true);
	}
	e->pressed = 0;
	return NULL;
}

static void forestDraw(const World* w, const Event* e, const C3D_Mtx* view, int uLoc, C3D_Tex* white, const float cam[3])
{
	if (strcmp(roomName(w, e->room), "room860") != 0 || s_frArea < 0) return;
	Room* door = useModel(&s_860Door, "romfs:/models/860_door.scr");
	float ra = (float)w->map.rooms[e->room].angle;
	float o[3];
	if (worldAreaAt(w, cam) == s_frArea) {
		for (int i = 0; i < 2; i++)
			if (dist3(s_frDoor[i], cam) < 12.0f)
				worldDrawModelEx(door, s_frDoor[i][0], s_frDoor[i][1], s_frDoor[i][2], s_frDoorYaw[i], 0, 48.0f / 46.0f, view, uLoc, white);
		return;
	}
	if (eventsRoomObject(w, e->room, 2, o) && dist3(o, cam) < 12.0f) worldDrawModelEx(useModel(&s_860Frame, "romfs:/models/860_frame.scr"), o[0], o[1], o[2], ra, 0, 1, view, uLoc, white);
	if (eventsRoomObject(w, e->room, 3, o) && dist3(o, cam) < 12.0f) worldDrawModelEx(door, o[0], o[1], o[2], ra, 0, 1, view, uLoc, white);
	if (eventsRoomObject(w, e->room, 4, o) && dist3(o, cam) < 12.0f) worldDrawModelEx(door, o[0], o[1], o[2], ra + 180, 0, 1, view, uLoc, white);
}
