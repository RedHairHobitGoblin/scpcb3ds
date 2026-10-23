// Port of SCP - Containment Breach by Regalis and contributors (CC BY-SA 3.0).

#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "collide.h"
#include "mapgen.h"

#define DEG2RAD ((float)M_PI / 180.0f)
#define ROOM_SPACING 8.0f
#define ROOM_SCALE (8.0f / 2048.0f)

#define LOAD_RADIUS 2
#define NEAR_RADIUS 1

#define DOOR_SPEED 140.0f
#define BIGDOOR_SPEED 56.0f

enum {
	MODEL_DOOR, MODEL_FRAME, MODEL_HEAVY1, MODEL_HEAVY2, MODEL_BUTTON, MODEL_BUTTONKEY, MODEL_BUTTONCODE,
	MODEL_BUTTONSCANNER, MODEL_BIG1, MODEL_BIG2, MODEL_DOORCOLL, MODEL_COUNT
};
static const char* s_modelFiles[MODEL_COUNT] = {
	"door", "doorframe", "heavydoor1", "heavydoor2", "button", "buttonkey", "buttoncode", "buttonscanner",
	"bigdoor1", "bigdoor2", "doorcoll",
};
static Room s_models[MODEL_COUNT];

typedef struct {
	float x, y, z;
	float angle;
	float slide;
} Xform;

static void toLocal(const Xform* xf, const float w[3], float l[3])
{
	float dx = w[0] - xf->x, dz = w[2] - xf->z;
	float c = cosf(xf->angle), s = sinf(xf->angle);
	l[0] = dx * c - dz * s - xf->slide;
	l[1] = w[1] - xf->y;
	l[2] = dx * s + dz * c;
}

static void toWorld(const Xform* xf, const float l[3], float w[3])
{
	float lx = l[0] + xf->slide, lz = l[2];
	float c = cosf(xf->angle), s = sinf(xf->angle);
	w[0] = xf->x + lx * c + lz * s;
	w[1] = xf->y + l[1];
	w[2] = xf->z - lx * s + lz * c;
}

static bool collideXform(const Room* r, const Xform* xf, float pos[3])
{
	float l[3], out[3];
	toLocal(xf, pos, l);
	bool g = collidePlayer(r, l);
	toWorld(xf, l, out);
	if (!isfinite(out[0]) || !isfinite(out[1]) || !isfinite(out[2])) return false;
	pos[0] = out[0];
	pos[1] = out[1];
	pos[2] = out[2];
	return g;
}

static void drawXform(Room* r, const Xform* xf, const C3D_Mtx* view, int uLoc, C3D_Tex* white, bool alpha)
{
	C3D_Mtx mv;
	Mtx_Copy(&mv, view);
	Mtx_Translate(&mv, xf->x, xf->y, xf->z, true);
	Mtx_RotateY(&mv, xf->angle, true);
	if (xf->slide != 0.0f) Mtx_Translate(&mv, xf->slide, 0, 0, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc, &mv);
	roomDraw(r, white, alpha);
}

static Door* addDoor(World* w);

int worldAddRoomDoor(World* w, int room, const float local[3], int angle, int type)
{
	Door* d = addDoor(w);
	if (!d) return -1;
	float wp[3];
	worldRoomToWorld(w, room, local, wp);
	d->x = wp[0];
	d->y = wp[1];
	d->z = wp[2];
	d->angle = (angle + w->map.rooms[room].angle) % 360;
	d->type = type;
	d->room = room;
	return (int)(d - w->doors);
}

int worldAddDoorAt(World* w, const float pos[3], int angle, int type, int room)
{
	Door* d = addDoor(w);
	if (!d) return -1;
	d->x = pos[0];
	d->y = pos[1];
	d->z = pos[2];
	d->angle = ((angle % 360) + 360) % 360;
	d->type = type;
	d->room = room;
	return (int)(d - w->doors);
}

int worldAddExtra(World* w, Room* model, float x, float y, float z, float yawDeg, bool collide)
{
	if (w->nextras >= MAX_EXTRAS || !model) return -1;
	WorldExtra* e = &w->extras[w->nextras];
	e->model = model;
	e->x = x;
	e->y = y;
	e->z = z;
	e->yaw = yawDeg;
	e->sx = e->sy = e->sz = 1.0f;
	e->collide = collide;
	e->visible = true;
	return w->nextras++;
}

void worldClearExtras(World* w) { w->nextras = 0; }

int worldAreaAt(const World* w, const float p[3])
{
	for (int i = 0; i < w->nareas; i++) {
		const OverlayArea* a = &w->areas[i];
		if (a->count > 0 && p[1] > a->min[1] && p[1] < a->max[1] && p[0] > a->min[0] && p[0] < a->max[0] &&
		    p[2] > a->min[2] && p[2] < a->max[2])
			return i;
	}
	return -1;
}

int worldBeginArea(World* w, int room)
{
	if (w->nareas >= MAX_AREAS) return -1;
	OverlayArea* a = &w->areas[w->nareas];
	memset(a, 0, sizeof(*a));
	a->room = room;
	a->first = w->ntunnel;
	return w->nareas++;
}

void worldAddAreaPiece(World* w, int area, Room* model, float x, float y, float z, float yawDeg, bool collide)
{
	if (area < 0 || w->ntunnel >= MAX_TUNNEL_CELLS || !model) return;
	TunnelCell* c = &w->tunnel[w->ntunnel++];
	c->model = model;
	c->x = x;
	c->y = y;
	c->z = z;
	c->yaw = yawDeg;
	c->collide = collide;
	w->areas[area].count = w->ntunnel - w->areas[area].first;
}

void worldEndArea(World* w, int area, const float min[3], const float max[3])
{
	if (area < 0) return;
	memcpy(w->areas[area].min, min, sizeof(float) * 3);
	memcpy(w->areas[area].max, max, sizeof(float) * 3);
}

static Xform tunnelXform(const TunnelCell* c)
{
	Xform xf = { c->x, c->y, c->z, c->yaw * DEG2RAD, 0 };
	return xf;
}

static bool tunnelNear(const TunnelCell* c, const float p[3], float r)
{
	return fabsf(c->x - p[0]) < r && fabsf(c->z - p[2]) < r;
}

static Xform extraXform(const WorldExtra* e)
{
	Xform xf = { e->x, e->y, e->z, e->yaw * DEG2RAD, 0 };
	return xf;
}

static bool extraUnscaled(const WorldExtra* e) { return e->sx == 1.0f && e->sy == 1.0f && e->sz == 1.0f; }

void worldDrawModel(Room* model, float x, float y, float z, float yawDeg, const C3D_Mtx* view, int uLoc,
                    C3D_Tex* white)
{
	Xform xf = { x, y, z, yawDeg * DEG2RAD, 0 };
	drawXform(model, &xf, view, uLoc, white, false);
}

void worldDrawModelEx(Room* model, float x, float y, float z, float yawDeg, float pitchDeg, float scale,
                      const C3D_Mtx* view, int uLoc, C3D_Tex* white)
{
	C3D_Mtx mv;
	Mtx_Copy(&mv, view);
	Mtx_Translate(&mv, x, y, z, true);
	Mtx_RotateY(&mv, yawDeg * DEG2RAD, true);
	Mtx_RotateX(&mv, -pitchDeg * DEG2RAD, true);
	if (scale != 1.0f) Mtx_Scale(&mv, scale, scale, scale);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc, &mv);
	roomDraw(model, white, false);
	roomDraw(model, white, true);
}

static void loadFill(World* w)
{
	FILE* f = fopen("romfs:/fill.txt", "r");
	if (!f) return;
	char line[256];
	while (w->nfill < MAX_FILL && fgets(line, sizeof(line), f)) {
		if (line[0] != 'D') continue;
		FillDoor* d = &w->fill[w->nfill];
		char code[16];
		d->noButtons = 0;
		d->roomDoor = -1;
		int n = sscanf(line, "D\t%31[^\t]\t%f\t%f\t%f\t%d\t%d\t%d\t%d\t%d\t%15s\t%d\t%d", d->room, &d->x, &d->y,
		               &d->z, &d->angle, &d->open, &d->big, &d->keycard, &d->locked, code, &d->noButtons,
		               &d->roomDoor);
		if (n >= 10) {
			snprintf(d->code, sizeof(d->code), "%.7s", strcmp(code, "-") ? code : "");
			w->nfill++;
		}
	}
	fclose(f);
}

bool worldLoadTemplates(World* w)
{
	memset(w, 0, sizeof(*w));
	FILE* f = fopen("romfs:/rooms.txt", "r");
	if (!f) return false;
	char line[160];
	while (w->ntpl < MAX_TEMPLATES && fgets(line, sizeof(line), f)) {
		RoomTemplateInfo* t = &w->tpl[w->ntpl].info;
		if (sscanf(line, "%31s %d %d %d %d %d %d %d %d %31s", t->key, &t->shape, &t->commonness,
		           &t->zones[0], &t->zones[1], &t->zones[2], &t->zones[3], &t->zones[4], &t->large, t->name) == 10)
			w->ntpl++;
	}
	fclose(f);

	for (int i = 0; i < MODEL_COUNT; i++) {
		char path[64];
		snprintf(path, sizeof(path), "romfs:/models/%s.scr", s_modelFiles[i]);
		roomLoad(&s_models[i], path);
	}
	loadFill(w);
	return w->ntpl > 0;
}

static Xform roomXform(const MapRoomPlacement* r)
{
	Xform xf = { r->gx * ROOM_SPACING, r->y, -r->gy * ROOM_SPACING, r->angle * DEG2RAD, 0 };
	return xf;
}

void worldRoomToWorld(const World* w, int room, const float local[3], float out[3])
{
	Xform xf = roomXform(&w->map.rooms[room]);
	toWorld(&xf, local, out);
}

void worldWorldToRoom(const World* w, int room, const float world[3], float out[3])
{
	Xform xf = roomXform(&w->map.rooms[room]);
	toLocal(&xf, world, out);
}

static Door* addDoor(World* w)
{
	if (w->ndoors >= MAX_DOORS) return NULL;
	Door* d = &w->doors[w->ndoors++];
	memset(d, 0, sizeof(*d));
	d->room = -1;
	d->roomDoor = -1;
	return d;
}

void worldGenerate(World* w, uint32_t seed, bool intro)
{
	for (int i = 0; i < w->ntpl; i++)
		if (w->tpl[i].loaded) {
			roomFree(&w->tpl[i].room);
			w->tpl[i].loaded = false;
		}

	static RoomTemplateInfo infos[MAX_TEMPLATES];
	for (int i = 0; i < w->ntpl; i++) infos[i] = w->tpl[i].info;
	w->seed = seed;
	generateMap(&w->map, infos, w->ntpl, seed);

	w->introRoom = -1;
	w->introActive = false;
	w->nareas = 0;
	w->ntunnel = 0;
	if (intro && w->map.nrooms < MAX_MAP_ROOMS) {
		for (int t = 0; t < w->ntpl; t++)
			if (strcmp(w->tpl[t].info.name, "173") == 0) {
				MapRoomPlacement* r = &w->map.rooms[w->map.nrooms];
				memset(r, 0, sizeof(*r));
				r->tpl = t;
				r->gx = 1;
				r->gy = MAP_SIZE - 1;
				r->zone = 1;
				r->offGrid = true;
				w->introRoom = w->map.nrooms++;
				w->introActive = true;
				break;
			}
	}

	worldAddOffGridRoom(w, "gatea", MAP_SIZE - 1, 1, 500.0f, 0);

	worldAddOffGridRoom(w, "dimension1499", 1, 0, 800.0f, 0);

	w->ndoors = 0;
	for (int i = 0; i < w->map.ndoors; i++) {
		const MapDoorPlacement* p = &w->map.doors[i];
		Door* d = addDoor(w);
		if (!d) break;
		d->x = p->x;
		d->z = -p->z;
		d->angle = p->angle;
		d->type = p->type == 2 ? DOOR_HEAVY : DOOR_NORMAL;
		d->open = p->open;
		d->openstate = p->open ? 180.0f : 0.0f;
	}

	for (int r = 0; r < w->map.nrooms; r++) {
		const MapRoomPlacement* mr = &w->map.rooms[r];
		const char* name = w->tpl[mr->tpl].info.name;
		for (int i = 0; i < w->nfill; i++) {
			const FillDoor* fd = &w->fill[i];
			if (strcmp(fd->room, name) != 0) continue;
			Door* d = addDoor(w);
			if (!d) break;
			float local[3] = { fd->x, fd->y, fd->z }, wp[3];
			worldRoomToWorld(w, r, local, wp);
			d->x = wp[0];
			d->y = wp[1];
			d->z = wp[2];
			d->angle = (fd->angle + mr->angle) % 360;
			d->type = fd->big == 1 ? DOOR_BIG : (fd->big == 2 ? DOOR_HEAVY : DOOR_NORMAL);
			d->keycard = fd->keycard;
			d->locked = fd->locked;
			snprintf(d->code, sizeof(d->code), "%s", fd->code);
			d->open = fd->open;
			d->noButtons = fd->noButtons;
			d->roomDoor = fd->roomDoor;
			d->openstate = fd->open ? 180.0f : 0.0f;
			d->room = r;
		}
	}
}

void worldSpawnPoint(const World* w, float pos[3], float* yaw)
{
	pos[0] = pos[2] = 0;
	pos[1] = 0.6f;
	*yaw = 0;
	int pick = w->map.nrooms > 0 ? 0 : -1;
	for (int i = 0; i < w->map.nrooms; i++)
		if (strcmp(w->tpl[w->map.rooms[i].tpl].info.name, "start") == 0) pick = i;
	if (pick < 0) return;
	const MapRoomPlacement* r = &w->map.rooms[pick];
	*yaw = (float)r->angle;
	if (strcmp(w->tpl[r->tpl].info.name, "start") == 0) {
		float local[3] = { 3584 * ROOM_SCALE, 704 * ROOM_SCALE, -1024 * ROOM_SCALE };
		worldRoomToWorld(w, pick, local, pos);
		return;
	}
	pos[0] = r->gx * ROOM_SPACING;
	pos[2] = -r->gy * ROOM_SPACING;
}

void worldCell(const float pos[3], int* gx, int* gy)
{
	*gx = (int)floorf(pos[0] / ROOM_SPACING + 0.5f);
	*gy = (int)floorf(-pos[2] / ROOM_SPACING + 0.5f);
}

static int cellDist(const MapRoomPlacement* r, int gx, int gy)
{
	int dx = abs(r->gx - gx), dy = abs(r->gy - gy);
	return dx > dy ? dx : dy;
}

static float s_viewX, s_viewY, s_viewZ;

static bool roomNear(const World* w, int i, int gx, int gy, int radius)
{
	const MapRoomPlacement* r = &w->map.rooms[i];
	if (w->isolated) return false;
	if (w->introActive) return r->offGrid && i == w->introRoom;
	if (r->offGrid && i == w->introRoom) return false;
	if (fabsf(s_viewY - r->y) > 60.0f) return false;
	if (cellDist(r, gx, gy) <= radius) return true;

	const RoomTemplate* rt = &w->tpl[r->tpl];
	if (!rt->loaded || rt->room.radius < 6.0f) return false;
	float dx = s_viewX - r->gx * ROOM_SPACING, dz = s_viewZ + r->gy * ROOM_SPACING;
	return sqrtf(dx * dx + dz * dz) < rt->room.radius + radius * ROOM_SPACING;
}

int worldAddOffGridRoom(World* w, const char* name, int gx, int gy, float y, int angle)
{
	if (w->map.nrooms >= MAX_MAP_ROOMS) return -1;
	for (int t = 0; t < w->ntpl; t++)
		if (strcmp(w->tpl[t].info.name, name) == 0) {
			MapRoomPlacement* r = &w->map.rooms[w->map.nrooms];
			memset(r, 0, sizeof(*r));
			r->tpl = t;
			r->gx = gx;
			r->gy = gy;
			r->y = y;
			r->angle = angle;
			r->zone = 3;
			r->offGrid = true;
			return w->map.nrooms++;
		}
	return -1;
}

void worldStream(World* w, const float pos[3])
{
	s_viewX = pos[0];
	s_viewY = pos[1];
	s_viewZ = pos[2];
	int gx, gy;
	worldCell(pos, &gx, &gy);
	static bool needed[MAX_TEMPLATES];
	memset(needed, 0, sizeof(needed));
	for (int i = 0; i < w->map.nrooms; i++)
		if (roomNear(w, i, gx, gy, LOAD_RADIUS)) needed[w->map.rooms[i].tpl] = true;
	for (int t = 0; t < w->ntpl; t++) {
		RoomTemplate* rt = &w->tpl[t];
		if (needed[t] && !rt->loaded) {
			char path[80];
			snprintf(path, sizeof(path), "romfs:/rooms/%s.scr", rt->info.key);
			rt->loaded = roomLoad(&rt->room, path);
		} else if (!needed[t] && rt->loaded) {
			roomFree(&rt->room);
			rt->loaded = false;
		}
	}
}

static float doorSlide(const Door* d, int panel)
{
	float k = 1.0f - cosf(d->openstate * DEG2RAD);
	switch (d->type) {
	case DOOR_BIG: return k * (panel == 0 ? 0.398f : -0.398f);
	case DOOR_HEAVY: return k * (panel == 0 ? 0.337f : 0.239f);
	default: return k * 0.358f;
	}
}

static void doorParts(const Door* d, Xform parts[3], int models[3])
{
	float a = d->angle * DEG2RAD;
	Xform frame = { d->x, d->y, d->z, a, 0 };
	parts[0] = frame;
	models[0] = d->type == DOOR_BIG ? MODEL_DOORCOLL : MODEL_FRAME;
	parts[1] = frame;
	parts[1].slide = doorSlide(d, 0);
	parts[2] = frame;
	parts[2].slide = doorSlide(d, 1);
	if (d->type == DOOR_BIG) {
		models[1] = MODEL_BIG1;
		models[2] = MODEL_BIG2;
	} else {
		parts[2].angle = a + (float)M_PI;
		models[1] = d->type == DOOR_HEAVY ? MODEL_HEAVY1 : MODEL_DOOR;
		models[2] = d->type == DOOR_HEAVY ? MODEL_HEAVY2 : MODEL_DOOR;
	}
}

static int buttonModel(const Door* d)
{
	if (d->code[0]) return MODEL_BUTTONCODE;
	if (d->keycard > 0) return MODEL_BUTTONKEY;
	if (d->keycard < 0) return MODEL_BUTTONSCANNER;
	return MODEL_BUTTON;
}

static void buttonXform(const Door* d, int i, Xform* out)
{

	Xform frame = { d->x, d->y, d->z, d->angle * DEG2RAD, 0 };
	float local[3];
	float yaw;
	if (d->type == DOOR_BIG) {
		local[0] = i == 0 ? -432.0f * ROOM_SCALE : 432.0f * ROOM_SCALE;
		local[2] = i == 0 ? -192.0f * ROOM_SCALE : 192.0f * ROOM_SCALE;
		yaw = i == 0 ? 90.0f : 270.0f;
	} else {
		local[0] = i == 0 ? 0.6f : -0.6f;
		local[2] = i == 0 ? 0.1f : -0.1f;
		yaw = i == 0 ? 0.0f : 180.0f;
	}
	local[1] = 0.7f;
	float wp[3];
	toWorld(&frame, local, wp);
	Xform b = { wp[0], wp[1], wp[2], frame.angle + yaw * DEG2RAD, 0 };
	*out = b;
}

static void doorSound(const Door* d, bool opening)
{
	char name[48];
	float pos[3] = { d->x, d->y + 1.0f, d->z };
	switch (d->type) {
	case DOOR_BIG:
		snprintf(name, sizeof(name), "door/bigdoor%s%d", opening ? "open" : "close", rand() % 2 + 1);
		break;
	case DOOR_HEAVY:
		snprintf(name, sizeof(name), "door/door2%s%d", opening ? "open" : "close", rand() % 3 + 1);
		break;
	default:
		snprintf(name, sizeof(name), "door/door%s%d", opening ? "open" : "close", rand() % 3 + 1);
		break;
	}
	sfxPlay3D(name, pos, 10.0f, 1.0f);
}

void worldToggleDoor(World* w, int door)
{
	if (door < 0 || door >= w->ndoors) return;
	Door* d = &w->doors[door];
	d->open = !d->open;
	doorSound(d, d->open);
}

bool worldDoorPassable(const World* w, int door) { return w->doors[door].openstate >= 179.0f; }

void worldUpdateDoors(World* w, float dt)
{
	for (int i = 0; i < w->ndoors; i++) {
		Door* d = &w->doors[i];
		float speed = d->type == DOOR_BIG ? BIGDOOR_SPEED : DOOR_SPEED;
		if (d->open) d->openstate = fminf(180.0f, d->openstate + speed * dt);
		else d->openstate = fmaxf(0.0f, d->openstate - speed * dt);
	}
}

int worldUsableDoor(const World* w, const float pos[3], float yaw)
{
	float fx = sinf(yaw * DEG2RAD), fz = -cosf(yaw * DEG2RAD);
	int best = -1;
	float bestDist = 0.8f;
	for (int i = 0; i < w->ndoors; i++) {
		const Door* d = &w->doors[i];
		if (fabsf(d->x - pos[0]) > 3.0f || fabsf(d->z - pos[2]) > 3.0f || fabsf(d->y - pos[1]) > 2.0f) continue;
		if (d->noButtons || (d->openstate > 0.0f && d->openstate < 180.0f)) continue;
		for (int b = 0; b < 2; b++) {
			Xform bx;
			buttonXform(d, b, &bx);
			float dx = bx.x - pos[0], dz = bx.z - pos[2];
			float dist = sqrtf(dx * dx + dz * dz);
			if (dist >= bestDist || dist < 1e-3f) continue;
			if ((dx * fx + dz * fz) / dist < 0.4f) continue;
			best = i;
			bestDist = dist;
		}
	}
	return best;
}

const char* worldUseDoor(World* w, int door, const char* held, bool* consumed)
{
	static char msg[128];
	*consumed = false;
	if (door < 0 || door >= w->ndoors) return NULL;
	Door* d = &w->doors[door];
	if (d->elevator) {
		d->pressed = true;
		sfxPlay("interact/button", 1.0f);
		return NULL;
	}
	if (d->keycard > 0) {
		int level = -1;
		if (held && strncmp(held, "key", 3) == 0 && held[3] >= '1' && held[3] <= '6' && held[4] == 0)
			level = held[3] - '0';
		if (level < 0) return "A keycard is required to operate this door.";
		*consumed = true;
		if (level >= d->keycard) {
			if (d->locked) {
				sfxPlay("interact/keycarduse2", 1.0f);
				return "The keycard was inserted into the slot but nothing happened.";
			}
			sfxPlay("interact/keycarduse1", 1.0f);
			worldToggleDoor(w, door);
			return "The keycard was inserted into the slot.";
		}
		sfxPlay("interact/keycarduse2", 1.0f);
		if (d->locked) return "The keycard was inserted into the slot but nothing happened.";
		snprintf(msg, sizeof(msg), "A keycard with security clearance %d or higher is required to operate this door.",
		         d->keycard);
		return msg;
	}
	if (d->keycard < 0) {
		bool ok = held && ((strcmp(held, "hand") == 0 && d->keycard == -1) || (strcmp(held, "hand2") == 0 && d->keycard == -2));
		*consumed = held && held[0];
		if (ok) {
			sfxPlay("interact/scanneruse1", 1.0f);
			worldToggleDoor(w, door);
			return "You place the palm of the hand onto the scanner. The scanner reads: \"DNA verified. Access granted.\"";
		}
		sfxPlay("interact/scanneruse2", 1.0f);
		return "You placed your palm onto the scanner. The scanner reads: \"DNA does not match known sample. Access denied.\"";
	}
	if (d->locked) {
		sfxPlay("interact/button2", 1.0f);
		return "The door appears to be locked.";
	}
	if (d->code[0]) return NULL;
	sfxPlay("interact/button", 1.0f);
	worldToggleDoor(w, door);
	return NULL;
}

bool worldCollide(World* w, float pos[3])
{
	s_viewX = pos[0];
	s_viewY = pos[1];
	s_viewZ = pos[2];
	int gx, gy;
	worldCell(pos, &gx, &gy);
	bool grounded = false;
	int area = worldAreaAt(w, pos);
	bool tunnels = area >= 0;
	for (int i = tunnels ? w->areas[area].first : 0; tunnels && i < w->areas[area].first + w->areas[area].count; i++) {
		if (!w->tunnel[i].collide || !tunnelNear(&w->tunnel[i], pos, w->tunnel[i].model->radius + 1.0f)) continue;
		Xform xf = tunnelXform(&w->tunnel[i]);
		grounded |= collideXform(w->tunnel[i].model, &xf, pos);
	}
	for (int i = 0; i < w->map.nrooms && !tunnels; i++) {
		const MapRoomPlacement* r = &w->map.rooms[i];
		RoomTemplate* rt = &w->tpl[r->tpl];
		if (!rt->loaded || !roomNear(w, i, gx, gy, NEAR_RADIUS)) continue;
		Xform xf = roomXform(r);
		grounded |= collideXform(&rt->room, &xf, pos);
	}
	for (int i = 0; i < w->nextras && !tunnels; i++) {
		const WorldExtra* e = &w->extras[i];
		if (!e->collide || !e->visible || !extraUnscaled(e)) continue;
		Xform xf = extraXform(e);
		grounded |= collideXform(e->model, &xf, pos);
	}
	for (int i = 0; i < w->ndoors && !w->isolated; i++) {
		const Door* d = &w->doors[i];
		if (fabsf(d->x - pos[0]) > 3.0f || fabsf(d->z - pos[2]) > 3.0f) continue;
		if ((d->room >= 0 && d->room == w->introRoom) != w->introActive) continue;
		Xform parts[3];
		int models[3];
		doorParts(d, parts, models);
		for (int p = 0; p < 3; p++) grounded |= collideXform(&s_models[models[p]], &parts[p], pos);
	}
	return grounded;
}

float worldRayDown(const World* w, const float pos[3], float maxDrop)
{
	s_viewX = pos[0];
	s_viewY = pos[1];
	s_viewZ = pos[2];
	int gx, gy;
	worldCell(pos, &gx, &gy);
	float best = -INFINITY;
	int area = worldAreaAt(w, pos);
	if (area >= 0) {
		for (int i = w->areas[area].first; i < w->areas[area].first + w->areas[area].count; i++) {
			if (!w->tunnel[i].collide || !tunnelNear(&w->tunnel[i], pos, w->tunnel[i].model->radius + 1.0f)) continue;
			Xform xf = tunnelXform(&w->tunnel[i]);
			float l[3];
			toLocal(&xf, pos, l);
			float y = roomRayDown(w->tunnel[i].model, l, maxDrop);
			if (isfinite(y) && y + xf.y > best) best = y + xf.y;
		}
		return best;
	}
	for (int i = 0; i < w->map.nrooms; i++) {
		const MapRoomPlacement* r = &w->map.rooms[i];
		const RoomTemplate* rt = &w->tpl[r->tpl];
		if (!rt->loaded || !roomNear(w, i, gx, gy, 1)) continue;
		Xform xf = roomXform(r);
		float l[3];
		toLocal(&xf, pos, l);
		float y = roomRayDown(&rt->room, l, maxDrop);
		if (y > best) best = y;
	}
	for (int i = 0; i < w->nextras; i++) {
		const WorldExtra* e = &w->extras[i];
		if (!e->collide || !e->visible || !extraUnscaled(e)) continue;
		Xform xf = extraXform(e);
		float l[3];
		toLocal(&xf, pos, l);
		float y = roomRayDown(e->model, l, maxDrop);
		if (isfinite(y) && y + e->y > best) best = y + e->y;
	}
	return best;
}

int g_losReason;

bool worldLineOfSight(const World* w, const float a[3], const float b[3])
{
	s_viewX = a[0];
	s_viewY = a[1];
	s_viewZ = a[2];
	g_losReason = 0;
	int ax, ay, bx, by;
	worldCell(a, &ax, &ay);
	worldCell(b, &bx, &by);
	int area = worldAreaAt(w, a);
	if (area < 0) area = worldAreaAt(w, b);
	bool tunnels = area >= 0;
	if (tunnels) {
		float mid[3] = { (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2 };
		float half = sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[2] - b[2]) * (a[2] - b[2])) * 0.5f;
		for (int i = w->areas[area].first; i < w->areas[area].first + w->areas[area].count; i++) {
			if (!w->tunnel[i].collide || !tunnelNear(&w->tunnel[i], mid, half + w->tunnel[i].model->radius)) continue;
			Xform xf = tunnelXform(&w->tunnel[i]);
			float la[3], lb[3];
			toLocal(&xf, a, la);
			toLocal(&xf, b, lb);
			if (roomSegmentBlocked(w->tunnel[i].model, la, lb)) return false;
		}
	}
	for (int i = 0; i < w->map.nrooms && !tunnels; i++) {
		const MapRoomPlacement* r = &w->map.rooms[i];
		const RoomTemplate* rt = &w->tpl[r->tpl];
		if (!rt->loaded) continue;
		if (!roomNear(w, i, ax, ay, 1) && !roomNear(w, i, bx, by, 1)) continue;
		Xform xf = roomXform(r);
		float la[3], lb[3];
		toLocal(&xf, a, la);
		toLocal(&xf, b, lb);
		if (roomSegmentBlocked(&rt->room, la, lb)) {
			g_losReason = 1 + 10 * (int)(w->tpl[r->tpl].info.shape);
			return false;
		}
	}
	for (int i = 0; i < w->nextras; i++) {
		const WorldExtra* e = &w->extras[i];
		if (!e->collide || !e->visible || !extraUnscaled(e)) continue;
		Xform xf = extraXform(e);
		float la[3], lb[3];
		toLocal(&xf, a, la);
		toLocal(&xf, b, lb);
		if (roomSegmentBlocked(e->model, la, lb)) return false;
	}
	for (int i = 0; i < w->ndoors && !w->isolated; i++) {
		const Door* d = &w->doors[i];
		if (d->openstate > 120.0f) continue;
		float mx = (a[0] + b[0]) * 0.5f, mz = (a[2] + b[2]) * 0.5f;
		float half = sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[2] - b[2]) * (a[2] - b[2])) * 0.5f + 2.0f;
		if (fabsf(d->x - mx) > half || fabsf(d->z - mz) > half) continue;
		Xform parts[3];
		int models[3];
		doorParts(d, parts, models);
		for (int p = 1; p < 3; p++) {
			float la[3], lb[3];
			toLocal(&parts[p], a, la);
			toLocal(&parts[p], b, lb);
			if (roomSegmentBlocked(&s_models[models[p]], la, lb)) {
				g_losReason = 2;
				return false;
			}
		}
	}
	return true;
}

static bool visible(float x, float z, float radius, const float cam[3], float fx, float fz, float viewDist)
{
	float dx = x - cam[0], dz = z - cam[2];
	float dist = sqrtf(dx * dx + dz * dz);
	if (dist - radius > viewDist) return false;
	if (dx * fx + dz * fz < -radius) return false;
	return true;
}

void worldDraw(World* w, const C3D_Mtx* view, int uLoc, const float cam[3], float yaw, C3D_Tex* white, float viewDist)
{
	s_viewX = cam[0];
	s_viewY = cam[1];
	s_viewZ = cam[2];
	int gx, gy;
	worldCell(cam, &gx, &gy);
	float fx = sinf(yaw * DEG2RAD), fz = -cosf(yaw * DEG2RAD);

	for (int pass = 0; pass < 2; pass++) {
		bool alpha = pass == 1;
		if (alpha) {
			C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
			               GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
		} else {
			C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
		}

		int area = worldAreaAt(w, cam);
		bool tunnels = area >= 0;
		for (int i = tunnels ? w->areas[area].first : 0; tunnels && i < w->areas[area].first + w->areas[area].count; i++) {
			const TunnelCell* c = &w->tunnel[i];
			if (!visible(c->x, c->z, c->model->radius + 0.5f, cam, fx, fz, viewDist)) continue;
			Xform xf = tunnelXform(c);
			drawXform(c->model, &xf, view, uLoc, white, alpha);
		}
		for (int i = 0; i < w->map.nrooms && !tunnels; i++) {
			const MapRoomPlacement* r = &w->map.rooms[i];
			RoomTemplate* rt = &w->tpl[r->tpl];
			if (!rt->loaded || !roomNear(w, i, gx, gy, NEAR_RADIUS)) continue;
			Xform xf = roomXform(r);
			if (!visible(xf.x, xf.z, rt->room.radius, cam, fx, fz, viewDist)) continue;
			drawXform(&rt->room, &xf, view, uLoc, white, alpha);
		}
		for (int i = 0; i < w->nextras; i++) {
			WorldExtra* e = &w->extras[i];
			if (!e->visible) continue;
			if (!visible(e->x, e->z, e->model->radius * fmaxf(e->sx, e->sz) + 1.0f, cam, fx, fz, viewDist * 2)) continue;
			C3D_Mtx mv;
			Mtx_Copy(&mv, view);
			Mtx_Translate(&mv, e->x, e->y, e->z, true);
			Mtx_RotateY(&mv, e->yaw * DEG2RAD, true);
			if (!extraUnscaled(e)) Mtx_Scale(&mv, e->sx, e->sy, e->sz);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc, &mv);
			roomDraw(e->model, white, alpha);
		}
		if (alpha) continue;

		for (int i = 0; i < w->ndoors && !w->isolated; i++) {
			const Door* d = &w->doors[i];
			if (fabsf(d->y - cam[1]) > 4.0f || !visible(d->x, d->z, 2.0f, cam, fx, fz, viewDist)) continue;
			if ((d->room >= 0 && d->room == w->introRoom) != w->introActive) continue;
			Xform parts[3];
			int models[3];
			doorParts(d, parts, models);
			for (int p = 0; p < 3; p++) drawXform(&s_models[models[p]], &parts[p], view, uLoc, white, false);
			int bm = buttonModel(d);
			for (int b = 0; b < 2 && !d->noButtons; b++) {
				Xform bx;
				buttonXform(d, b, &bx);
				drawXform(&s_models[bm], &bx, view, uLoc, white, false);
			}
		}
	}
}

void worldDoorViewpoint(const World* w, int door, float pos[3], float* yaw)
{
	const Door* d = &w->doors[door];
	Xform bx;
	buttonXform(d, 0, &bx);

	float ox = sinf(bx.angle), oz = cosf(bx.angle);
	pos[0] = bx.x + ox * 0.55f;
	pos[1] = d->y + 0.3f;
	pos[2] = bx.z + oz * 0.55f;
	*yaw = atan2f(-ox, oz) / DEG2RAD;
}

int worldRoomDoor(const World* w, int room, int k)
{
	for (int i = 0; i < w->ndoors; i++)
		if (w->doors[i].room == room && w->doors[i].roomDoor == k) return i;
	return -1;
}

int worldRoomIndexAt(const World* w, const float pos[3])
{
	int area = worldAreaAt(w, pos);
	if (area >= 0) return w->areas[area].room;
	int gx, gy;
	worldCell(pos, &gx, &gy);

	for (int i = 0; i < w->map.nrooms; i++) {
		const MapRoomPlacement* r = &w->map.rooms[i];
		if (r->offGrid && i != w->introRoom && r->gx == gx && r->gy == gy && fabsf(pos[1] - r->y) < 60.0f) return i;
	}
	int r = -1;
	if (gx >= 0 && gy >= 0 && gx <= MAP_SIZE && gy <= MAP_SIZE) r = w->map.grid[gx][gy] - 1;
	if (r >= 0) {
		const RoomTemplate* rt = &w->tpl[w->map.rooms[r].tpl];
		float ry = pos[1] - w->map.rooms[r].y;
		if (fabsf(ry) > 60.0f || (rt->loaded && (ry < rt->room.minY - 2.0f || ry > rt->room.maxY + 2.0f))) r = -1;
	}
	if (r >= 0) return r;

	float best = 1e9f;
	for (int i = 0; i < w->map.nrooms; i++) {
		const MapRoomPlacement* mr = &w->map.rooms[i];
		const RoomTemplate* rt = &w->tpl[mr->tpl];

		if (!rt->loaded || rt->room.radius < 6.0f || (mr->offGrid && i == w->introRoom)) continue;
		float cx = mr->gx * ROOM_SPACING, cz = -mr->gy * ROOM_SPACING;
		float d = sqrtf((pos[0] - cx) * (pos[0] - cx) + (pos[2] - cz) * (pos[2] - cz));
		float reach = rt->room.radius;
		float ry = pos[1] - mr->y;
		if (d < reach && d < best && ry > rt->room.minY - 2.0f && ry < rt->room.maxY + 2.0f) {
			best = d;
			r = i;
		}
	}
	return r;
}

const char* worldRoomAt(const World* w, const float pos[3])
{
	int r = worldRoomIndexAt(w, pos);
	return r >= 0 ? w->tpl[w->map.rooms[r].tpl].info.name : "-";
}

int worldRoomZone(const World* w, const float pos[3])
{
	int r = worldRoomIndexAt(w, pos);
	return r >= 0 ? w->map.rooms[r].zone : 1;
}

int worldLoadedCount(const World* w)
{
	int n = 0;
	for (int i = 0; i < w->ntpl; i++) n += w->tpl[i].loaded;
	return n;
}

bool worldDoorOpenable(const Door* d) { return !d->locked && d->keycard == 0 && d->code[0] == 0 && !d->noButtons; }

int worldBoundaryDoor(const World* w, int ax, int ay, int bx, int by)
{

	float x = (ax + bx) * 0.5f * ROOM_SPACING, z = -(ay + by) * 0.5f * ROOM_SPACING;
	for (int i = 0; i < w->ndoors; i++)
		if (w->doors[i].room < 0 && fabsf(w->doors[i].x - x) < 0.5f && fabsf(w->doors[i].z - z) < 0.5f) return i;
	return -1;
}

bool worldNextCell(const World* w, int sx, int sy, int tx, int ty, int policy, int* nx, int* ny)
{
	static int prev[MAP_SIZE + 1][MAP_SIZE + 1];
	static int queue[(MAP_SIZE + 1) * (MAP_SIZE + 1)];
	if (sx < 0 || sy < 0 || sx > MAP_SIZE || sy > MAP_SIZE) return false;
	if (tx < 0 || ty < 0 || tx > MAP_SIZE || ty > MAP_SIZE) return false;
	for (int x = 0; x <= MAP_SIZE; x++)
		for (int y = 0; y <= MAP_SIZE; y++) prev[x][y] = -1;
	int head = 0, tail = 0;
	queue[tail++] = sx * 64 + sy;
	prev[sx][sy] = sx * 64 + sy;
	static const int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	while (head < tail) {
		int c = queue[head++], cx = c / 64, cy = c % 64;
		if (cx == tx && cy == ty) break;
		for (int k = 0; k < 4; k++) {
			int x = cx + dirs[k][0], y = cy + dirs[k][1];
			if (x < 0 || y < 0 || x > MAP_SIZE || y > MAP_SIZE || prev[x][y] >= 0 || !w->map.grid[x][y]) continue;
			if (policy == 0) {
				int d = worldBoundaryDoor(w, cx, cy, x, y);
				if (d >= 0 && !worldDoorPassable(w, d) && !worldDoorOpenable(&w->doors[d])) continue;
			}
			prev[x][y] = c;
			queue[tail++] = x * 64 + y;
		}
	}
	if (prev[tx][ty] < 0) return false;
	int c = tx * 64 + ty;
	while (prev[c / 64][c % 64] != sx * 64 + sy && c != sx * 64 + sy) c = prev[c / 64][c % 64];
	*nx = c / 64;
	*ny = c % 64;
	return true;
}

void worldSave(const World* w, FILE* f)
{
	fwrite(&w->seed, sizeof(w->seed), 1, f);
	fwrite(&w->ndoors, sizeof(w->ndoors), 1, f);
	fwrite(w->doors, sizeof(Door), w->ndoors, f);
}

bool worldRestoreDoors(World* w, FILE* f)
{
	int n;
	if (fread(&n, sizeof(n), 1, f) != 1 || n < 0 || n > MAX_DOORS) return false;
	if ((int)fread(w->doors, sizeof(Door), n, f) != n) return false;
	w->ndoors = n;
	for (int i = 0; i < n; i++) w->doors[i].pressed = false;
	return true;
}
