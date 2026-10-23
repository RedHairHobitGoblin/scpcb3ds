#include "items.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "hud.h"

#define DEG2RAD ((float)M_PI / 180.0f)
#define PICK_RANGE 1.2f

Items g_items;

static bool loadTex(C3D_Tex* tex, const char* path)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	uint8_t hdr[12];
	bool ok = false;
	if (fread(hdr, 1, 12, f) == 12 && memcmp(hdr, "T3D1", 4) == 0) {
		uint16_t w = hdr[4] | (hdr[5] << 8), h = hdr[6] | (hdr[7] << 8);
		GPU_TEXCOLOR fmt = hdr[8] == 1 ? GPU_RGBA4 : GPU_RGB565;
		if (C3D_TexInit(tex, w, h, fmt)) {
			fread(tex->data, 1, (size_t)w * h * 2, f);
			C3D_TexFlush(tex);
			C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
			C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
			ok = true;
		}
	}
	fclose(f);
	return ok;
}

void itemsLoadTemplates(void)
{
	memset(&g_items, 0, sizeof(g_items));
	for (int i = 0; i < INV_SLOTS; i++) g_items.inv[i] = -1;
	g_items.selected = -1;
	FILE* f = fopen("romfs:/items.txt", "r");
	if (!f) return;
	char line[256];
	while (g_items.ntpl < MAX_ITEM_TEMPLATES && fgets(line, sizeof(line), f)) {
		ItemTemplate* t = &g_items.tpl[g_items.ntpl];
		memset(t, 0, sizeof(*t));
		if (sscanf(line, "%47[^\t]\t%19[^\t]\t%47[^\t]\t%31[^\t]\t%31[^\t]\t%47[^\t\r\n]", t->name, t->tempname,
		           t->model, t->inv, t->doc, t->display) == 6) {
			if (strcmp(t->doc, "-") == 0) t->doc[0] = 0;
			if (strcmp(t->model, "-") == 0) t->model[0] = 0;
			g_items.ntpl++;
		}
	}
	fclose(f);
}

int itemsFindTemplate(const char* name, const char* tempname)
{
	for (int i = 0; i < g_items.ntpl; i++)
		if (strcmp(g_items.tpl[i].name, name) == 0 && (!tempname || strcmp(g_items.tpl[i].tempname, tempname) == 0))
			return i;
	return -1;
}

static WorldItem* addItem(int tpl, const float pos[3])
{
	if (tpl < 0 || g_items.nitems >= MAX_WORLD_ITEMS) return NULL;
	WorldItem* it = &g_items.items[g_items.nitems++];
	memset(it, 0, sizeof(*it));
	it->tpl = tpl;
	memcpy(it->pos, pos, sizeof(it->pos));
	it->yaw = (float)(rand() % 360);
	it->active = true;
	return it;
}

void itemsSpawn(const World* w)
{
	g_items.nitems = 0;
	for (int i = 0; i < INV_SLOTS; i++) g_items.inv[i] = -1;
	memset(g_items.invNote, 0, sizeof(g_items.invNote));
	g_items.selected = -1;

	FILE* f = fopen("romfs:/fill.txt", "r");
	if (!f) return;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (line[0] != 'I') continue;
		char room[32], name[48], tempname[20];
		float local[3];
		if (sscanf(line, "I\t%31[^\t]\t%f\t%f\t%f\t%47[^\t]\t%19[^\t\r\n]", room, &local[0], &local[1], &local[2],
		           name, tempname) != 6)
			continue;
		int tpl = itemsFindTemplate(name, tempname);
		if (tpl < 0) continue;
		for (int r = 0; r < w->map.nrooms; r++) {
			if (strcmp(w->tpl[w->map.rooms[r].tpl].info.name, room) != 0) continue;
			float wp[3];
			worldRoomToWorld(w, r, local, wp);
			addItem(tpl, wp);
		}
	}
	fclose(f);
}

void itemsUpdate(const World* w, float dt)
{
	(void)dt;

	for (int i = 0; i < g_items.nitems; i++) {
		WorldItem* it = &g_items.items[i];
		if (!it->active || it->settled) continue;
		float probe[3] = { it->pos[0], it->pos[1] + 0.05f, it->pos[2] };
		float y = worldRayDown(w, probe, 4.0f);
		if (isfinite(y)) {
			it->pos[1] = y + 0.01f;
			it->settled = true;
		}
	}
}

static Room* itemMesh(int tpl)
{
	ItemTemplate* t = &g_items.tpl[tpl];
	if (!t->meshTried) {
		t->meshTried = true;
		if (t->model[0]) {
			char path[96];
			snprintf(path, sizeof(path), "romfs:/models/%s.scr", t->model);
			t->meshOk = roomLoad(&t->mesh, path);
		}
	}
	return t->meshOk ? &t->mesh : NULL;
}

void itemsDraw(const C3D_Mtx* view, int uLoc, const float cam[3], C3D_Tex* white, float viewDist)
{
	for (int i = 0; i < g_items.nitems; i++) {
		WorldItem* it = &g_items.items[i];
		if (!it->active || !it->settled) continue;
		float dx = it->pos[0] - cam[0], dz = it->pos[2] - cam[2];
		if (dx * dx + dz * dz > viewDist * viewDist || fabsf(it->pos[1] - cam[1]) > 4.0f) continue;
		Room* m = itemMesh(it->tpl);
		if (m) worldDrawModel(m, it->pos[0], it->pos[1], it->pos[2], it->yaw, view, uLoc, white);
	}
}

int itemsPickable(const float eye[3], float yaw, float pitch)
{
	float cp = cosf(pitch * DEG2RAD);
	float f[3] = { sinf(yaw * DEG2RAD) * cp, sinf(pitch * DEG2RAD), -cosf(yaw * DEG2RAD) * cp };
	int best = -1;
	float bestScore = 0.0f;
	for (int i = 0; i < g_items.nitems; i++) {
		WorldItem* it = &g_items.items[i];
		if (!it->active || !it->settled) continue;
		float d[3] = { it->pos[0] - eye[0], it->pos[1] + 0.03f - eye[1], it->pos[2] - eye[2] };
		float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (dist > PICK_RANGE || dist < 1e-3f) continue;
		float facing = (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / dist;
		if (facing < 0.88f) continue;
		float score = facing - dist * 0.05f;
		if (score > bestScore) {
			bestScore = score;
			best = i;
		}
	}
	return best;
}

bool itemsPickUp(int item)
{
	if (item < 0 || item >= g_items.nitems || !g_items.items[item].active) return false;
	for (int s = 0; s < INV_SLOTS; s++) {
		if (g_items.inv[s] >= 0) continue;
		g_items.inv[s] = g_items.items[item].tpl;
		memcpy(g_items.invNote[s], g_items.items[item].note, sizeof(g_items.invNote[s]));
		g_items.items[item].active = false;
		char snd[32];
		snprintf(snd, sizeof(snd), "interact/pickitem%d", rand() % 4);
		sfxPlay(snd, 1.0f);
		return true;
	}
	return false;
}

void itemsDrop(int slot, const float pos[3], float yaw)
{
	if (slot < 0 || slot >= INV_SLOTS || g_items.inv[slot] < 0) return;
	float p[3] = { pos[0] + sinf(yaw * DEG2RAD) * 0.4f, pos[1], pos[2] - cosf(yaw * DEG2RAD) * 0.4f };
	WorldItem* it = addItem(g_items.inv[slot], p);
	if (it) {
		it->settled = false;
		memcpy(it->note, g_items.invNote[slot], sizeof(it->note));
	}
	g_items.inv[slot] = -1;
	g_items.invNote[slot][0] = 0;
	if (g_items.selected == slot) g_items.selected = -1;
}

const char* itemsHeldTempname(void)
{
	if (g_items.selected < 0 || g_items.inv[g_items.selected] < 0) return "";
	return g_items.tpl[g_items.inv[g_items.selected]].tempname;
}

bool itemsHave(const char* prefix)
{
	size_t n = strlen(prefix);
	for (int s = 0; s < INV_SLOTS; s++)
		if (g_items.inv[s] >= 0 && strncmp(g_items.tpl[g_items.inv[s]].tempname, prefix, n) == 0) return true;
	return false;
}

C3D_Tex* itemsIcon(int tpl)
{
	if (tpl < 0 || tpl >= g_items.ntpl) return NULL;
	ItemTemplate* t = &g_items.tpl[tpl];
	if (!t->iconTried) {
		t->iconTried = true;
		char path[96];
		snprintf(path, sizeof(path), "romfs:/inv/%s.t3d", t->inv);
		t->iconOk = loadTex(&t->icon, path);
	}
	return t->iconOk ? &t->icon : NULL;
}

void itemsDebugNearest(const float eye[3], float yaw, float pitch, char* out, int size)
{
	float cp = cosf(pitch * DEG2RAD);
	float f[3] = { sinf(yaw * DEG2RAD) * cp, sinf(pitch * DEG2RAD), -cosf(yaw * DEG2RAD) * cp };
	int best = -1;
	float bestDist = 1e9f, bestFacing = 0;
	for (int i = 0; i < g_items.nitems; i++) {
		WorldItem* it = &g_items.items[i];
		if (!it->active) continue;
		float d[3] = { it->pos[0] - eye[0], it->pos[1] + 0.03f - eye[1], it->pos[2] - eye[2] };
		float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (dist < bestDist) {
			bestDist = dist;
			best = i;
			bestFacing = dist > 1e-3f ? (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / dist : 0;
		}
	}
	if (best < 0) snprintf(out, size, "no items");
	else snprintf(out, size, "%.12s d%.2f f%.2f %s", g_items.tpl[g_items.items[best].tpl].display, bestDist,
	              bestFacing, g_items.items[best].settled ? "ok" : "unsettled");
}

typedef struct {
	char kind;
	char key[48];
	char setting[12];
	char outcome[200];
} Recipe;

static Recipe* s_recipes;
static int s_nrecipes;

static void loadRecipes(void)
{
	if (s_recipes) return;
	FILE* f = fopen("romfs:/914.txt", "r");
	if (!f) return;
	s_recipes = calloc(300, sizeof(Recipe));
	char line[320];
	while (s_nrecipes < 300 && fgets(line, sizeof(line), f)) {
		Recipe* r = &s_recipes[s_nrecipes];
		if (sscanf(line, "%c\t%47[^\t]\t%11[^\t]\t%199[^\r\n]", &r->kind, r->key, r->setting, r->outcome) == 4) s_nrecipes++;
	}
	fclose(f);
}

const char* itemsSlotName(int slot)
{
	static char buf[64];
	if (slot < 0 || slot >= INV_SLOTS || g_items.inv[slot] < 0) return "";
	const ItemTemplate* t = &g_items.tpl[g_items.inv[slot]];
	if (!g_items.invNote[slot][0]) return t->display;
	snprintf(buf, sizeof(buf), "Cup of %s", g_items.invNote[slot]);
	return buf;
}

int itemsSpawnAt(const char* name, const char* tempname, const float pos[3])
{
	char low[48];
	snprintf(low, sizeof(low), "%s", name);
	for (char* c = low; *c; c++) *c = (*c >= 'A' && *c <= 'Z') ? *c + 32 : *c;
	int tpl = itemsFindTemplate(low, tempname);
	if (tpl < 0) tpl = itemsFindTemplate(low, NULL);
	if (tpl < 0) return -1;
	WorldItem* it = addItem(tpl, pos);
	if (!it) return -1;
	it->settled = false;
	return (int)(it - g_items.items);
}

static const char* keycardResult(int level, const char* setting)
{
	if (!strcmp(setting, "rough") || !strcmp(setting, "coarse")) return NULL;
	if (!strcmp(setting, "1:1")) return "Playing Card|misc";
	bool fine = !strcmp(setting, "fine");
	if (!fine || level >= 5)
		return rand() % 40 == 0 ? "Key Card Omni|key6" : "Mastercard|misc";
	switch (level) {
	case 1: return rand() % 5 == 0 ? "Mastercard|misc" : "Level 2 Key Card|key2";
	case 2: return rand() % 4 == 0 ? "Mastercard|misc" : "Level 3 Key Card|key3";
	case 3: return rand() % 15 == 0 ? "Level 4 Key Card|key4" : "Playing Card|misc";
	default: return rand() % 4 == 0 ? "Mastercard|misc" : "Level 5 Key Card|key5";
	}
}

void items914(int item, const char* setting, const float out[3])
{
	loadRecipes();
	if (item < 0 || item >= g_items.nitems || !g_items.items[item].active) return;
	WorldItem* it = &g_items.items[item];
	const ItemTemplate* t = &g_items.tpl[it->tpl];
	const char* outcome = NULL;

	if (!strncmp(t->tempname, "key", 3) && t->tempname[3] >= '1' && t->tempname[3] <= '5' && !t->tempname[4]) {
		outcome = keycardResult(t->tempname[3] - '0', setting);
		if (!outcome) outcome = "destroy";
	}
	for (int pass = 0; pass < 2 && !outcome; pass++)
		for (int i = 0; i < s_nrecipes; i++) {
			const Recipe* r = &s_recipes[i];
			if (r->kind != (pass == 0 ? 'N' : 'T') || strcmp(r->setting, setting) != 0) continue;
			if (strcmp(r->key, pass == 0 ? t->name : t->tempname) == 0) {
				outcome = r->outcome;
				break;
			}
		}
	if (!outcome) outcome = "keep";

	if (!strcmp(outcome, "keep")) {
		memcpy(it->pos, out, sizeof(it->pos));
		it->settled = false;
		return;
	}
	it->active = false;
	if (!strcmp(outcome, "destroy")) return;

	char buf[200];
	snprintf(buf, sizeof(buf), "%s", outcome);
	char* alts[12];
	int n = 0;
	for (char* tok = strtok(buf, ";"); tok && n < 12; tok = strtok(NULL, ";")) alts[n++] = tok;
	char* pick = alts[rand() % n];
	char* bar = strchr(pick, '|');
	if (!bar) return;
	*bar = 0;
	itemsSpawnAt(pick, bar + 1, out);
#ifdef DEBUG_TOOLS
	char msg[120];
	snprintf(msg, sizeof(msg), "914 (%s): %s -> %s", setting, t->display, pick);
	hudMessage(msg, 8);
#endif
}

void itemsSave(FILE* f)
{
	fwrite(&g_items.nitems, sizeof(g_items.nitems), 1, f);
	fwrite(g_items.items, sizeof(WorldItem), g_items.nitems, f);
	fwrite(g_items.inv, sizeof(g_items.inv), 1, f);
	fwrite(g_items.invNote, sizeof(g_items.invNote), 1, f);
	fwrite(&g_items.selected, sizeof(g_items.selected), 1, f);
}

bool itemsRestore(FILE* f)
{
	int n;
	if (fread(&n, sizeof(n), 1, f) != 1 || n < 0 || n > MAX_WORLD_ITEMS) return false;
	if ((int)fread(g_items.items, sizeof(WorldItem), n, f) != n) return false;
	g_items.nitems = n;
	fread(g_items.inv, sizeof(g_items.inv), 1, f);
	fread(g_items.invNote, sizeof(g_items.invNote), 1, f);
	fread(&g_items.selected, sizeof(g_items.selected), 1, f);
	return true;
}
