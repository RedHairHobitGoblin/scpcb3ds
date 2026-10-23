#include "mapgen.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MW MAP_SIZE
#define MH MAP_SIZE
#define ZONEAMOUNT 3
#define NAME_LEN 32

static uint32_t s_rng = 1;

void rngSeed(uint32_t seed) { s_rng = seed ? seed : 1; }

static uint32_t rngNext(void)
{

	s_rng ^= s_rng << 13;
	s_rng ^= s_rng >> 17;
	s_rng ^= s_rng << 5;
	return s_rng;
}

int rngRand(int lo, int hi)
{
	if (hi < lo) { int t = lo; lo = hi; hi = t; }
	return lo + (int)(rngNext() % (uint32_t)(hi - lo + 1));
}

float rngRnd(float lo, float hi) { return lo + (hi - lo) * (rngNext() / 4294967296.0f); }

static int s_temp[MW + 1][MH + 1];
static char s_name[MW + 1][MH + 1][NAME_LEN];

static int mt(int x, int y) { return (x < 0 || y < 0 || x > MW || y > MH) ? 0 : s_temp[x][y]; }
static void setMt(int x, int y, int v) { if (x >= 0 && y >= 0 && x <= MW && y <= MH) s_temp[x][y] = v; }
static int m1(int x, int y) { int v = mt(x, y); return v < 1 ? v : 1; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static int getZone(int y)
{
	int z = (int)floorf((float)(MW - y) / MW * ZONEAMOUNT);
	return imin(z, ZONEAMOUNT - 1);
}

static char (*s_mapRoom)[NAME_LEN];
static int s_maxRooms;
#define MAPROOM(type, i) s_mapRoom[(type) * s_maxRooms + (i)]

static void setMapRoom(int type, int pos, const char* name)
{
	if (pos >= 0 && pos < s_maxRooms) strncpy(MAPROOM(type, pos), name, NAME_LEN - 1);
}

static bool setRoom(const char* name, int type, int pos, int minPos, int maxPos)
{
	if (maxPos < minPos) return false;
	bool looped = false;
	while (pos >= 0 && pos < s_maxRooms && MAPROOM(type, pos)[0]) {
		pos++;
		if (pos > maxPos) {
			if (!looped) { pos = minPos + 1; looped = true; }
			else return false;
		}
	}
	if (pos < 0 || pos >= s_maxRooms) return false;
	setMapRoom(type, pos, name);
	return true;
}

static int findTemplate(const RoomTemplateInfo* tpl, int ntpl, const char* name)
{
	for (int i = 0; i < ntpl; i++)
		if (strcmp(tpl[i].name, name) == 0) return i;
	return -1;
}

static int pickTemplate(const RoomTemplateInfo* tpl, int ntpl, int zone, int shape, const char* name)
{
	if (name && name[0]) {
		int t = findTemplate(tpl, ntpl, name);
		if (t >= 0) return t;
	}
	int total = 0;
	for (int i = 0; i < ntpl; i++)
		for (int z = 0; z < 5; z++)
			if (tpl[i].zones[z] == zone && tpl[i].shape == shape) { total += tpl[i].commonness; break; }
	if (total <= 0) return -1;
	int pick = rngRand(1, total);
	int acc = 0;
	for (int i = 0; i < ntpl; i++)
		for (int z = 0; z < 5; z++)
			if (tpl[i].zones[z] == zone && tpl[i].shape == shape) {
				acc += tpl[i].commonness;
				if (pick > acc - tpl[i].commonness && pick <= acc) return i;
			}
	return -1;
}

static void addRoom(GeneratedMap* map, const RoomTemplateInfo* tpl, int ntpl, int zone, int shape,
                    int x, int y, const char* name, int angle)
{
	int t = pickTemplate(tpl, ntpl, zone, shape, name);
	if (t < 0 || map->nrooms >= MAX_MAP_ROOMS) return;
	MapRoomPlacement* r = &map->rooms[map->nrooms++];
	r->tpl = t;
	r->gx = x;
	r->gy = y;
	r->zone = zone;
	r->angle = ((angle % 360) + 360) % 360;
	map->grid[x][y] = map->nrooms;
}

static void addDoor(GeneratedMap* map, float x, float z, int angle, int type, int room)
{
	if (map->ndoors >= MAX_MAP_DOORS) return;
	MapDoorPlacement* d = &map->doors[map->ndoors++];
	d->x = x;
	d->z = z;
	d->angle = angle;
	d->type = type;
	d->open = imax(rngRand(-3, 1), 0) != 0;
	d->room = room;
}

void generateMap(GeneratedMap* map, const RoomTemplateInfo* tpl, int ntpl, uint32_t seed)
{
	memset(map, 0, sizeof(*map));
	memset(s_temp, 0, sizeof(s_temp));
	memset(s_name, 0, sizeof(s_name));
	rngSeed(seed);

	int x, y, temp = 0, i, x2, y2, width, height;

	x = MW / 2;
	y = MH - 2;
	for (i = y; i <= MH - 1; i++) setMt(x, i, 1);

	do {
		width = rngRand(10, 15);
		if (x > MW * 0.6f) width = -width;
		else if (x > MW * 0.4f) x = x - width / 2;

		if (x + width > MW - 3) width = MW - 3 - x;
		else if (x + width < 2) width = -x + 2;

		x = imin(x, x + width);
		width = abs(width);
		for (i = x; i <= x + width; i++) setMt(imin(i, MW), y, 1);

		height = rngRand(3, 4);
		if (y - height < 1) height = y - 1;
		int yhallways = rngRand(4, 5);
		if (getZone(y - height) != getZone(y - height + 1)) height--;

		for (i = 1; i <= yhallways; i++) {
			x2 = imax(imin(rngRand(x, x + width - 1), MW - 2), 2);
			while (x2 < MW && (mt(x2, y - 1) || mt(x2 - 1, y - 1) || mt(x2 + 1, y - 1))) x2++;

			if (x2 < x + width) {
				int tempheight;
				if (i == 1) {
					tempheight = height;
					x2 = rngRand(1, 2) == 1 ? x : x + width;
				} else {
					tempheight = rngRand(1, height);
				}
				for (y2 = y - tempheight; y2 <= y; y2++) {
					if (getZone(y2) != getZone(y2 + 1)) setMt(x2, y2, 255);
					else setMt(x2, y2, 1);
				}
				if (tempheight == height) temp = x2;
			}
		}
		x = temp;
		y = y - height;
	} while (y >= 2);

	int room1[3] = { 0 }, room2[3] = { 0 }, room2c[3] = { 0 }, room3[3] = { 0 }, room4[3] = { 0 };
	for (y = 1; y <= MH - 1; y++) {
		int zone = getZone(y);
		for (x = 1; x <= MW - 1; x++) {
			if (mt(x, y) <= 0) continue;
			temp = m1(x + 1, y) + m1(x - 1, y) + m1(x, y + 1) + m1(x, y - 1);
			if (mt(x, y) < 255) setMt(x, y, temp);
			switch (mt(x, y)) {
			case 1: room1[zone]++; break;
			case 2:
				if (m1(x + 1, y) + m1(x - 1, y) == 2) room2[zone]++;
				else if (m1(x, y + 1) + m1(x, y - 1) == 2) room2[zone]++;
				else room2c[zone]++;
				break;
			case 3: room3[zone]++; break;
			case 4: room4[zone]++; break;
			}
		}
	}

	for (i = 0; i <= 2; i++) {
		temp = -room1[i] + 5;
		if (temp <= 0) continue;
		int yEnd = (int)((MH / ZONEAMOUNT) * ((2 - i) + 1.0f)) - 2;
		for (y = (MH / ZONEAMOUNT) * (2 - i) + 1; y <= yEnd && temp > 0; y++) {
			for (x = 2; x <= MW - 2 && temp > 0; x++) {
				if (mt(x, y) != 0) continue;
				if (m1(x + 1, y) + m1(x - 1, y) + m1(x, y + 1) + m1(x, y - 1) != 1) continue;
				if (mt(x + 1, y)) { x2 = x + 1; y2 = y; }
				else if (mt(x - 1, y)) { x2 = x - 1; y2 = y; }
				else if (mt(x, y + 1)) { x2 = x; y2 = y + 1; }
				else { x2 = x; y2 = y - 1; }

				bool placed = false;
				if (mt(x2, y2) > 1 && mt(x2, y2) < 4) {
					if (mt(x2, y2) == 2) {
						if (m1(x2 + 1, y2) + m1(x2 - 1, y2) == 2 || m1(x2, y2 + 1) + m1(x2, y2 - 1) == 2) {
							room2[i]--;
							room3[i]++;
							placed = true;
						}
					} else {
						room3[i]--;
						room4[i]++;
						placed = true;
					}
					if (placed) {
						setMt(x2, y2, mt(x2, y2) + 1);
						setMt(x, y, 1);
						room1[i]++;
						temp--;
					}
				}
			}
		}
	}

	for (i = 0; i <= 2; i++) {
		int zy, zy2;
		switch (i) {
		case 2: zy = 2; zy2 = MH / 3; break;
		case 1: zy = MH / 3 + 1; zy2 = (int)(MH * (2.0f / 3.0f)) - 1; break;
		default: zy = (int)(MH * (2.0f / 3.0f)) + 1; zy2 = MH - 2; break;
		}

		if (room4[i] < 1) {
			temp = 0;
			for (y = zy; y <= zy2 && !temp; y++)
				for (x = 2; x <= MW - 2 && !temp; x++) {
					if (mt(x, y) != 3) continue;
					if (!(mt(x + 1, y) || mt(x + 1, y + 1) || mt(x + 1, y - 1) || mt(x + 2, y))) {
						setMt(x + 1, y, 1); temp = 1;
					} else if (!(mt(x - 1, y) || mt(x - 1, y + 1) || mt(x - 1, y - 1) || mt(x - 2, y))) {
						setMt(x - 1, y, 1); temp = 1;
					} else if (!(mt(x, y + 1) || mt(x + 1, y + 1) || mt(x - 1, y + 1) || mt(x, y + 2))) {
						setMt(x, y + 1, 1); temp = 1;
					} else if (!(mt(x, y - 1) || mt(x + 1, y - 1) || mt(x - 1, y - 1) || mt(x, y - 2))) {
						setMt(x, y - 1, 1); temp = 1;
					}
					if (temp) {
						setMt(x, y, 4);
						room4[i]++;
						room3[i]--;
						room1[i]++;
					}
				}
		}

		if (room2c[i] < 1) {
			temp = 0;
			zy++;
			zy2--;
			for (y = zy; y <= zy2 && !temp; y++)
				for (x = 3; x <= MW - 3 && !temp; x++) {
					if (mt(x, y) != 1) continue;
					if (mt(x - 1, y) > 0) {
						if (mt(x, y - 1) + mt(x, y + 1) + mt(x + 2, y) == 0) {
							if (mt(x + 1, y - 2) + mt(x + 2, y - 1) + mt(x + 1, y - 1) == 0) {
								setMt(x, y, 2); setMt(x + 1, y, 2); setMt(x + 1, y - 1, 1); temp = 1;
							} else if (mt(x + 1, y + 2) + mt(x + 2, y + 1) + mt(x + 1, y + 1) == 0) {
								setMt(x, y, 2); setMt(x + 1, y, 2); setMt(x + 1, y + 1, 1); temp = 1;
							}
						}
					} else if (mt(x + 1, y) > 0) {
						if (mt(x, y - 1) + mt(x, y + 1) + mt(x - 2, y) == 0) {
							if (mt(x - 1, y - 2) + mt(x - 2, y - 1) + mt(x - 1, y - 1) == 0) {
								setMt(x, y, 2); setMt(x - 1, y, 2); setMt(x - 1, y - 1, 1); temp = 1;
							} else if (mt(x - 1, y + 2) + mt(x - 2, y + 1) + mt(x - 1, y + 1) == 0) {
								setMt(x, y, 2); setMt(x - 1, y, 2); setMt(x - 1, y + 1, 1); temp = 1;
							}
						}
					} else if (mt(x, y - 1) > 0) {
						if (mt(x - 1, y) + mt(x + 1, y) + mt(x, y + 2) == 0) {
							if (mt(x - 2, y + 1) + mt(x - 1, y + 2) + mt(x - 1, y + 1) == 0) {
								setMt(x, y, 2); setMt(x, y + 1, 2); setMt(x - 1, y + 1, 1); temp = 1;
							} else if (mt(x + 2, y + 1) + mt(x + 1, y + 2) + mt(x + 1, y + 1) == 0) {
								setMt(x, y, 2); setMt(x, y + 1, 2); setMt(x + 1, y + 1, 1); temp = 1;
							}
						}
					} else if (mt(x, y + 1) > 0) {
						if (mt(x - 1, y) + mt(x + 1, y) + mt(x, y - 2) == 0) {
							if (mt(x - 2, y - 1) + mt(x - 1, y - 2) + mt(x - 1, y - 1) == 0) {
								setMt(x, y, 2); setMt(x, y - 1, 2); setMt(x - 1, y - 1, 1); temp = 1;
							} else if (mt(x + 2, y - 1) + mt(x + 1, y - 2) + mt(x + 1, y - 1) == 0) {
								setMt(x, y, 2); setMt(x, y - 1, 2); setMt(x + 1, y - 1, 1); temp = 1;
							}
						}
					}
					if (temp) {
						room2c[i]++;
						room2[i]++;
					}
				}
		}
	}

	s_maxRooms = 55 * MW / 20;
	s_maxRooms = imax(s_maxRooms, room1[0] + room1[1] + room1[2] + 1);
	s_maxRooms = imax(s_maxRooms, room2[0] + room2[1] + room2[2] + 1);
	s_maxRooms = imax(s_maxRooms, room2c[0] + room2c[1] + room2c[2] + 1);
	s_maxRooms = imax(s_maxRooms, room3[0] + room3[1] + room3[2] + 1);
	s_maxRooms = imax(s_maxRooms, room4[0] + room4[1] + room4[2] + 1);
	s_mapRoom = calloc((ROOM4 + 1) * s_maxRooms, NAME_LEN);

	int minPos = 1, maxPos = room1[0] - 1;
	setMapRoom(ROOM1, 0, "start");
	setRoom("roompj", ROOM1, (int)(0.1f * room1[0]), minPos, maxPos);
	setRoom("914", ROOM1, (int)(0.3f * room1[0]), minPos, maxPos);
	setRoom("room1archive", ROOM1, (int)(0.5f * room1[0]), minPos, maxPos);
	setRoom("room205", ROOM1, (int)(0.6f * room1[0]), minPos, maxPos);

	setMapRoom(ROOM2C, 0, "lockroom");

	minPos = 1;
	maxPos = room2[0] - 1;
	setMapRoom(ROOM2, 0, "room2closets");
	setRoom("room2testroom2", ROOM2, (int)(0.1f * room2[0]), minPos, maxPos);
	setRoom("room2scps", ROOM2, (int)(0.2f * room2[0]), minPos, maxPos);
	setRoom("room2storage", ROOM2, (int)(0.3f * room2[0]), minPos, maxPos);
	setRoom("room2gw_b", ROOM2, (int)(0.4f * room2[0]), minPos, maxPos);
	setRoom("room2sl", ROOM2, (int)(0.5f * room2[0]), minPos, maxPos);
	setRoom("room012", ROOM2, (int)(0.55f * room2[0]), minPos, maxPos);
	setRoom("room2scps2", ROOM2, (int)(0.6f * room2[0]), minPos, maxPos);
	setRoom("room1123", ROOM2, (int)(0.7f * room2[0]), minPos, maxPos);
	setRoom("room2elevator", ROOM2, (int)(0.85f * room2[0]), minPos, maxPos);

	setMapRoom(ROOM3, (int)floorf(rngRnd(0.2f, 0.8f) * room3[0]), "room3storage");
	setMapRoom(ROOM2C, (int)(0.5f * room2c[0]), "room1162");
	setMapRoom(ROOM4, (int)(0.3f * room4[0]), "room4info");

	minPos = room1[0];
	maxPos = room1[0] + room1[1] - 1;
	setRoom("room079", ROOM1, room1[0] + (int)(0.15f * room1[1]), minPos, maxPos);
	setRoom("room106", ROOM1, room1[0] + (int)(0.3f * room1[1]), minPos, maxPos);
	setRoom("008", ROOM1, room1[0] + (int)(0.4f * room1[1]), minPos, maxPos);
	setRoom("room035", ROOM1, room1[0] + (int)(0.5f * room1[1]), minPos, maxPos);
	setRoom("coffin", ROOM1, room1[0] + (int)(0.7f * room1[1]), minPos, maxPos);

	minPos = room2[0];
	maxPos = room2[0] + room2[1] - 1;
	setMapRoom(ROOM2, room2[0] + (int)(0.1f * room2[1]), "room2nuke");
	setRoom("room2tunnel", ROOM2, room2[0] + (int)(0.25f * room2[1]), minPos, maxPos);
	setRoom("room049", ROOM2, room2[0] + (int)(0.4f * room2[1]), minPos, maxPos);
	setRoom("room2shaft", ROOM2, room2[0] + (int)(0.6f * room2[1]), minPos, maxPos);
	setRoom("testroom", ROOM2, room2[0] + (int)(0.7f * room2[1]), minPos, maxPos);
	setRoom("room2servers", ROOM2, room2[0] + (int)(0.9f * room2[1]), minPos, maxPos);

	setMapRoom(ROOM3, room3[0] + (int)(0.3f * room3[1]), "room513");
	setMapRoom(ROOM3, room3[0] + (int)(0.6f * room3[1]), "room966");
	setMapRoom(ROOM2C, room2c[0] + (int)(0.5f * room2c[1]), "room2cpit");

	setMapRoom(ROOM1, room1[0] + room1[1] + room1[2] - 2, "exit1");
	setMapRoom(ROOM1, room1[0] + room1[1] + room1[2] - 1, "gateaentrance");
	setMapRoom(ROOM1, room1[0] + room1[1], "room1lifts");

	minPos = room2[0] + room2[1];
	maxPos = room2[0] + room2[1] + room2[2] - 1;
	setMapRoom(ROOM2, minPos + (int)(0.1f * room2[2]), "room2poffices");
	setRoom("room2cafeteria", ROOM2, minPos + (int)(0.2f * room2[2]), minPos, maxPos);
	setRoom("room2sroom", ROOM2, minPos + (int)(0.3f * room2[2]), minPos, maxPos);
	setRoom("room2servers2", ROOM2, minPos + (int)(0.4f * room2[2]), minPos, maxPos);
	setRoom("room2offices", ROOM2, minPos + (int)(0.45f * room2[2]), minPos, maxPos);
	setRoom("room2offices4", ROOM2, minPos + (int)(0.5f * room2[2]), minPos, maxPos);
	setRoom("room860", ROOM2, minPos + (int)(0.6f * room2[2]), minPos, maxPos);
	setRoom("medibay", ROOM2, minPos + (int)(0.7f * room2[2]), minPos, maxPos);
	setRoom("room2poffices2", ROOM2, minPos + (int)(0.8f * room2[2]), minPos, maxPos);
	setRoom("room2offices2", ROOM2, minPos + (int)(0.9f * room2[2]), minPos, maxPos);

	setMapRoom(ROOM2C, room2c[0] + room2c[1], "room2ccont");
	setMapRoom(ROOM2C, room2c[0] + room2c[1] + 1, "lockroom2");

	setMapRoom(ROOM3, room3[0] + room3[1] + (int)(0.3f * room3[2]), "room3servers");
	setMapRoom(ROOM3, room3[0] + room3[1] + (int)(0.7f * room3[2]), "room3servers2");
	setMapRoom(ROOM3, room3[0] + room3[1] + (int)(0.5f * room3[2]), "room3offices");

	int roomId[ROOM4 + 1] = { 0 };
	for (y = MH - 1; y >= 1; y--) {
		int zone;
		if (y < MH / 3 + 1) zone = 3;
		else if (y < MH * (2.0f / 3.0f)) zone = 2;
		else zone = 1;

		for (x = 1; x <= MW - 2; x++) {
			if (mt(x, y) == 255) {
				addRoom(map, tpl, ntpl, zone, ROOM2, x, y, y > MH / 2 ? "checkpoint1" : "checkpoint2", 0);
				continue;
			}
			if (mt(x, y) <= 0) continue;

			temp = m1(x + 1, y) + m1(x - 1, y) + m1(x, y + 1) + m1(x, y - 1);
			char* name = s_name[x][y];
			int type = 0, angle = 0;
			switch (temp) {
			case 1:
				type = ROOM1;
				if (mt(x, y + 1)) angle = 180;
				else if (mt(x - 1, y)) angle = 270;
				else if (mt(x + 1, y)) angle = 90;
				break;
			case 2:
				if (mt(x - 1, y) > 0 && mt(x + 1, y) > 0) {
					type = ROOM2;
					angle = rngRand(1, 2) == 1 ? 90 : 270;
				} else if (mt(x, y - 1) > 0 && mt(x, y + 1) > 0) {
					type = ROOM2;
					angle = rngRand(1, 2) == 1 ? 180 : 0;
				} else {
					type = ROOM2C;
					if (mt(x - 1, y) > 0 && mt(x, y + 1) > 0) angle = 180;
					else if (mt(x + 1, y) > 0 && mt(x, y + 1) > 0) angle = 90;
					else if (mt(x - 1, y) > 0 && mt(x, y - 1) > 0) angle = 270;
				}
				break;
			case 3:
				type = ROOM3;
				if (!mt(x, y - 1)) angle = 180;
				else if (!mt(x - 1, y)) angle = 90;
				else if (!mt(x + 1, y)) angle = 270;
				break;
			case 4:
				type = ROOM4;
				break;
			default:
				continue;
			}
			if (roomId[type] < s_maxRooms && name[0] == 0 && MAPROOM(type, roomId[type])[0])
				strcpy(name, MAPROOM(type, roomId[type]));
			addRoom(map, tpl, ntpl, zone, type, x, y, name, angle);
			roomId[type]++;
		}
	}
	free(s_mapRoom);
	s_mapRoom = NULL;

	for (y = 0; y <= MH; y++)
		for (x = 0; x <= MW; x++) setMt(x, y, m1(x, y));

	const int transition0 = 13, transition1 = 7;
	const float spacing = 8.0f;
	for (y = MH; y >= 0; y--) {
		int zone;
		if (y < transition1 - 1) zone = 3;
		else if (y < transition0 - 1) zone = 2;
		else zone = 1;

		for (x = MW; x >= 0; x--) {
			if (mt(x, y) <= 0 || map->grid[x][y] == 0) continue;
			int ri = map->grid[x][y] - 1;
			const MapRoomPlacement* r = &map->rooms[ri];
			int shape = tpl[r->tpl].shape;
			int a = r->angle;
			int doorType = zone == 2 ? 2 : 0;

			bool spawn;
			switch (shape) {
			case ROOM1:  spawn = a == 90; break;
			case ROOM2:  spawn = a == 90 || a == 270; break;
			case ROOM2C: spawn = a == 0 || a == 90; break;
			case ROOM3:  spawn = a == 0 || a == 180 || a == 90; break;
			default:     spawn = true; break;
			}
			if (spawn && x + 1 < MW + 1 && mt(x + 1, y) > 0)
				addDoor(map, x * spacing + spacing / 2.0f, y * spacing, 90, doorType, ri);

			switch (shape) {
			case ROOM1:  spawn = a == 180; break;
			case ROOM2:  spawn = a == 0 || a == 180; break;
			case ROOM2C: spawn = a == 180 || a == 90; break;
			case ROOM3:  spawn = a == 180 || a == 90 || a == 270; break;
			default:     spawn = true; break;
			}
			if (spawn && y + 1 < MH + 1 && mt(x, y + 1) > 0)
				addDoor(map, x * spacing, y * spacing + spacing / 2.0f, 0, doorType, ri);
		}
	}
}
