#include "hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "items.h"
#include "wear.h"

#define W 320
#define H 240

#define COL_BG      C2D_Color32(10, 11, 13, 255)
#define COL_BG2     C2D_Color32(4, 4, 5, 255)
#define COL_PANEL   C2D_Color32(28, 29, 33, 255)
#define COL_PANEL2  C2D_Color32(17, 18, 20, 255)
#define COL_EDGE    C2D_Color32(62, 64, 70, 255)
#define COL_LIGHT   C2D_Color32(92, 95, 102, 255)
#define COL_TEXT    C2D_Color32(226, 226, 220, 255)
#define COL_DIM     C2D_Color32(128, 131, 137, 255)
#define COL_FAINT   C2D_Color32(52, 54, 58, 255)
#define COL_ACCENT  C2D_Color32(236, 172, 58, 255)
#define COL_ACCENT2 C2D_Color32(120, 82, 22, 255)
#define COL_RED     C2D_Color32(214, 58, 48, 255)
#define COL_GREEN   C2D_Color32(108, 190, 112, 255)

Hud g_hud;

static C3D_Tex s_docTex;
static bool s_docLoaded;
static float s_docAspect, s_docX, s_docY, s_docZoom;
static int s_dragX = -1, s_dragY = -1;

static Tex3DS_SubTexture s_sub64 = { 64, 64, 0.0f, 1.0f, 1.0f, 0.0f };
static Tex3DS_SubTexture s_sub256 = { 256, 256, 0.0f, 1.0f, 1.0f, 0.0f };

static C3D_Tex s_menuBack, s_endingTex;
static bool s_menuOk[1], s_endingOk;
static const Tex3DS_SubTexture s_subEnding = { 240, 240, 0.0f, 1.0f, 240.0f / 256, 1.0f - 240.0f / 256 };
static const Tex3DS_SubTexture s_subBack = { 400, 240, 0.0f, 1.0f, 400.0f / 512, 1.0f - 240.0f / 256 };

static bool loadT3D(C3D_Tex* tex, const char* path)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	uint8_t hdr[12];
	bool ok = false;
	if (fread(hdr, 1, 12, f) == 12 && memcmp(hdr, "T3D1", 4) == 0) {
		uint16_t w = hdr[4] | (hdr[5] << 8), h = hdr[6] | (hdr[7] << 8);
		if (C3D_TexInit(tex, w, h, hdr[8] == 1 ? GPU_RGBA4 : GPU_RGB565)) {
			fread(tex->data, 1, (size_t)w * h * 2, f);
			C3D_TexFlush(tex);
			C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
			ok = true;
		}
	}
	fclose(f);
	return ok;
}

void hudInit(void)
{
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2);
	C2D_Prepare();
	fontInit();
	s_menuOk[0] = loadT3D(&s_menuBack, "romfs:/menu/back.t3d");
	s_endingOk = loadT3D(&s_endingTex, "romfs:/menu/ending.t3d");
	memset(&g_hud, 0, sizeof(g_hud));
	g_hud.docTpl = -1;
	g_hud.lookSpeed = 5;
	g_hud.keypadDoor = -1;
}

void hudExit(void)
{
	if (s_docLoaded) C3D_TexDelete(&s_docTex);
	fontExit();
	if (s_menuOk[0]) C3D_TexDelete(&s_menuBack);
	C2D_Fini();
}

void hudMessage(const char* text, float seconds)
{
	snprintf(g_hud.message, sizeof(g_hud.message), "%s", text);
	g_hud.messageTime = seconds;
}

void hudShowDocument(int tpl)
{
	if (s_docLoaded) C3D_TexDelete(&s_docTex);
	s_docLoaded = false;
	g_hud.docTpl = tpl;
	g_hud.mode = HUD_DOCUMENT;
	if (tpl < 0 || !g_items.tpl[tpl].doc[0]) return;
	char path[96];
	snprintf(path, sizeof(path), "romfs:/docs/%s.t3d", g_items.tpl[tpl].doc);
	FILE* f = fopen(path, "rb");
	if (!f) return;
	uint8_t hdr[12];
	if (fread(hdr, 1, 12, f) == 12 && memcmp(hdr, "T3D1", 4) == 0) {
		uint16_t w = hdr[4] | (hdr[5] << 8), h = hdr[6] | (hdr[7] << 8);
		if (C3D_TexInit(&s_docTex, w, h, hdr[8] == 1 ? GPU_RGBA4 : GPU_RGB565)) {
			fread(s_docTex.data, 1, (size_t)w * h * 2, f);
			C3D_TexFlush(&s_docTex);
			C3D_TexSetFilter(&s_docTex, GPU_LINEAR, GPU_LINEAR);
			s_sub256.width = w;
			s_sub256.height = h;
			uint16_t a = hdr[10] | (hdr[11] << 8);
			s_docAspect = a ? a / 1000.0f : (float)h / w;
			s_docX = s_docY = 0;
			s_docZoom = 1;
			s_docLoaded = true;
		}
	}
	fclose(f);
}

void hudUpdate(float dt)
{
	if (g_hud.messageTime > 0) g_hud.messageTime -= dt;
	if (g_hud.mode == HUD_DEAD) g_hud.deathTime += dt;
	if (g_hud.mode == HUD_ENDING) g_hud.endingTime += dt;
}

static float pixelScale(float scale) { return scale >= 0.75f ? 2.0f : 1.0f; }

static void text(const char* str, float x, float y, float scale, u32 color, float wrap)
{
	fontDraw(str, x, y, pixelScale(scale), color, wrap);
}

static void textCentered(const char* str, float cx, float y, float scale, u32 color)
{
	float s = pixelScale(scale);
	fontDraw(str, cx - fontWidth(str, s) / 2, y, s, color, 0);
}

static void frame(float x, float y, float w, float h, u32 fill, u32 edge)
{
	C2D_DrawRectSolid(x, y, 0.5f, w, h, edge);
	C2D_DrawRectSolid(x + 1, y + 1, 0.5f, w - 2, h - 2, fill);
}

static void panel(float x, float y, float w, float h)
{
	C2D_DrawRectSolid(x, y, 0.5f, w, h, COL_EDGE);
	C2D_DrawRectangle(x + 1, y + 1, 0.5f, w - 2, h - 2, COL_PANEL, COL_PANEL, COL_PANEL2, COL_PANEL2);
	C2D_DrawRectSolid(x + 1, y + 1, 0.5f, w - 2, 1, COL_LIGHT);
}

static void label(const char* str, float x, float y)
{
	C2D_DrawRectSolid(x, y + 1, 0.5f, 2, 6, COL_ACCENT);
	text(str, x + 5, y, 0.4f, COL_DIM, 0);
}

static void backdrop(void)
{
	C2D_DrawRectangle(0, 0, 0.5f, W, H, COL_BG, COL_BG, COL_BG2, COL_BG2);
	for (int y = 0; y < H; y += 3) C2D_DrawRectSolid(0, y, 0.5f, W, 1, C2D_Color32(255, 255, 255, 6));
}

static bool buttonEx(const char* str, float x, float y, float w, float h, bool touched, int tx, int ty, bool on)
{
	bool hit = touched && tx >= x && tx < x + w && ty >= y && ty < y + h;
	bool lit = hit || on;
	C2D_DrawRectSolid(x, y, 0.5f, w, h, lit ? COL_ACCENT : COL_EDGE);
	u32 top = lit ? C2D_Color32(70, 52, 20, 255) : C2D_Color32(44, 46, 51, 255);
	u32 bot = lit ? C2D_Color32(40, 30, 12, 255) : C2D_Color32(24, 25, 28, 255);
	C2D_DrawRectangle(x + 1, y + 1, 0.5f, w - 2, h - 2, top, top, bot, bot);
	C2D_DrawRectSolid(x + 1, y + 1, 0.5f, w - 2, 1, lit ? COL_ACCENT : COL_LIGHT);
	textCentered(str, x + w / 2, y + h / 2 - 4, 0.55f, lit ? COL_ACCENT : COL_TEXT);
	return hit;
}

static bool button(const char* str, float x, float y, float w, float h, bool touched, int tx, int ty)
{
	return buttonEx(str, x, y, w, h, touched, tx, ty, false);
}

static void meter(const char* name, float x, float y, float w, float value)
{
	panel(x, y, w, 18);
	text(name, x + 5, y + 5, 0.4f, COL_DIM, 0);
	const int blocks = 16;
	float bx = x + 52, bw = (w - 58) / blocks;
	int lit = (int)ceilf(fmaxf(0, fminf(1, value)) * blocks);
	u32 c = value < 0.2f ? COL_RED : value < 0.45f ? COL_ACCENT : COL_TEXT;
	for (int i = 0; i < blocks; i++)
		C2D_DrawRectSolid(bx + i * bw, y + 5, 0.5f, bw - 1.5f, 8, i < lit ? c : COL_FAINT);
}

static float tag(const char* str, float right, float y, u32 color)
{
	float w = fontWidth(str, 1) + 8;
	C2D_DrawRectSolid(right - w, y, 0.5f, w, 12, color);
	C2D_DrawRectSolid(right - w + 1, y + 1, 0.5f, w - 2, 10, COL_BG2);
	text(str, right - w + 4, y + 2, 0.4f, color, 0);
	return right - w - 4;
}

static void drawMap(const HudInfo* info)
{
	const World* w = info->world;
	int pgx, pgy;
	worldCell(info->pos, &pgx, &pgy);
	const float cell = 8.0f, ox = W / 2 - (MAP_SIZE + 1) * cell / 2, oy = 48;
	panel(ox - 4, oy - 4, (MAP_SIZE + 1) * cell + 8, (MAP_SIZE + 1) * cell + 8);
	for (int y = 0; y <= MAP_SIZE; y++)
		for (int x = 0; x <= MAP_SIZE; x++) {
			int r = w->map.grid[x][y];
			if (!r) continue;
			const MapRoomPlacement* mr = &w->map.rooms[r - 1];
					u32 c = mr->zone == 1 ? C2D_Color32(110, 112, 118, 255) : mr->zone == 2 ? C2D_Color32(150, 108, 60, 255)
			                                                                         : C2D_Color32(80, 104, 140, 255);
			float px = ox + x * cell, py = oy + (MAP_SIZE - y) * cell;
			C2D_DrawRectSolid(px + 1, py + 1, 0.5f, cell - 2, cell - 2, c);
		}
	float px = ox + pgx * cell + cell / 2, py = oy + (MAP_SIZE - pgy) * cell + cell / 2;
	float a = info->yaw * (float)M_PI / 180.0f;
	C2D_DrawTriangle(px + sinf(a) * 6, py + cosf(a) * 6 * -1, C2D_Color32(255, 60, 60, 255),
	                 px + sinf(a + 2.5f) * 4, py - cosf(a + 2.5f) * 4, C2D_Color32(255, 60, 60, 255),
	                 px + sinf(a - 2.5f) * 4, py - cosf(a - 2.5f) * 4, C2D_Color32(255, 60, 60, 255), 0.5f);
}

static const char* zoneName(const HudInfo* info)
{
	switch (worldRoomZone(info->world, info->pos)) {
	case 2: return "HEAVY CONTAINMENT ZONE";
	case 3: return "ENTRANCE ZONE";
	default: return "LIGHT CONTAINMENT ZONE";
	}
}

static void header(const HudInfo* info)
{
	C2D_DrawRectangle(0, 0, 0.5f, W, 18, C2D_Color32(30, 31, 35, 255), C2D_Color32(30, 31, 35, 255), COL_BG,
	                  COL_BG);
	C2D_DrawRectSolid(0, 18, 0.5f, W, 1, COL_EDGE);
	C2D_DrawRectSolid(6, 5, 0.5f, 8, 8, COL_ACCENT);
	C2D_DrawRectSolid(8, 7, 0.5f, 4, 4, COL_BG2);
	text(zoneName(info), 18, 5, 0.4f, COL_TEXT, 0);
	float right = W - 6;
	if (info->injuries > 0.5f) right = tag(info->bleeding ? "BLEEDING" : "INJURED", right, 3, COL_RED);
	if (info->crouch) right = tag("CROUCHING", right, 3, COL_ACCENT);
	if (info->nvg) right = tag("NVG", right, 3, COL_GREEN);
}

static HudAction drawLookPad(bool touched, int tx, int ty)
{
	HudAction act = { ACT_NONE, 0 };
	panel(PAD_X, PAD_Y, PAD_W, PAD_H);

	for (float x = PAD_X + 19; x < PAD_X + PAD_W; x += 19) C2D_DrawRectSolid(x, PAD_Y + 2, 0.5f, 1, PAD_H - 4, C2D_Color32(255, 255, 255, 10));
	for (float y = PAD_Y + 21; y < PAD_Y + PAD_H; y += 21) C2D_DrawRectSolid(PAD_X + 2, y, 0.5f, PAD_W - 4, 1, C2D_Color32(255, 255, 255, 10));
	float cx = W / 2, cy = PAD_Y + PAD_H / 2;
	C2D_DrawRectSolid(cx - 18, cy - 1, 0.5f, 12, 2, COL_ACCENT);
	C2D_DrawRectSolid(cx + 6, cy - 1, 0.5f, 12, 2, COL_ACCENT);
	C2D_DrawRectSolid(cx - 1, cy - 18, 0.5f, 2, 12, COL_ACCENT);
	C2D_DrawRectSolid(cx - 1, cy + 6, 0.5f, 2, 12, COL_ACCENT);
	textCentered("DRAG TO LOOK", cx, PAD_Y + PAD_H - 16, 0.45f, COL_DIM);
	if (button("ITEMS", PAD_X, PAD_BTN_Y, PAD_BTN_W, PAD_BTN_H, touched, tx, ty)) act.kind = ACT_LOOK_PAD;
	if (button("USE", W / 2 - PAD_BTN_W / 2, PAD_BTN_Y, PAD_BTN_W, PAD_BTN_H, touched, tx, ty)) act.kind = ACT_PAD_USE;
	button("BLINK", PAD_BLINK_X, PAD_BTN_Y, PAD_BTN_W, PAD_BTN_H, touched, tx, ty);
	return act;
}

static HudAction drawGame(const HudInfo* info, bool touched, int tx, int ty)
{
	HudAction act = { ACT_NONE, 0 };
	if (g_hud.lookPad) {
		meter("BLINK", 6, 2, 152, info->blink);
		meter("STAMINA", 162, 2, 152, info->stamina / 100.0f);
		return drawLookPad(touched, tx, ty);
	}
	header(info);
	meter("BLINK", 6, 22, 152, info->blink);
	meter("STAMINA", 162, 22, 152, info->stamina / 100.0f);

	if (!info->hasNav) g_hud.mapTab = false;
	if (g_hud.mapTab) {
		drawMap(info);
	} else {
		panel(6, 44, 308, 124);
		int held = 0;
		for (int i = 0; i < INV_SLOTS; i++) held += g_items.inv[i] >= 0;
		label("INVENTORY", 12, 49);
		char count[16];
		snprintf(count, sizeof(count), "%d/%d", held, INV_SLOTS);
		text(count, 308 - fontWidth(count, 1), 49, 0.4f, COL_DIM, 0);
		for (int i = 0; i < INV_SLOTS; i++) {
			float x = 12 + (i % 5) * 60, y = 61 + (i / 5) * 53;
			int tpl = g_items.inv[i];
			bool sel = g_items.selected == i && tpl >= 0, worn = wearIsWorn(i);

			C2D_DrawRectSolid(x, y, 0.5f, 56, 50, sel ? COL_ACCENT : C2D_Color32(8, 8, 9, 255));
			C2D_DrawRectangle(x + 1, y + 1, 0.5f, 54, 48, C2D_Color32(14, 14, 16, 255), C2D_Color32(14, 14, 16, 255),
			                  C2D_Color32(22, 23, 26, 255), C2D_Color32(22, 23, 26, 255));
			C2D_DrawRectSolid(x + 1, y + 48, 0.5f, 54, 1, COL_EDGE);
			if (sel) {
				C2D_DrawRectSolid(x + 1, y + 1, 0.5f, 54, 48, C2D_Color32(236, 172, 58, 28));
			}
			if (tpl >= 0) {
				C3D_Tex* icon = itemsIcon(tpl);
				if (icon) {
					C2D_Image img = { icon, &s_sub64 };
					C2D_DrawImageAt(img, x + 6, y + 3, 0.5f, NULL, 44.0f / 64, 44.0f / 64);
				} else {
					text(g_items.tpl[tpl].display, x + 3, y + 3, 0.35f, COL_TEXT, 50);
				}
			} else {
				char n[4];
				snprintf(n, sizeof(n), "%d", i + 1);
				text(n, x + 4, y + 3, 0.4f, COL_FAINT, 0);
			}
			if (worn) {
				C2D_DrawRectSolid(x + 30, y + 39, 0.5f, 25, 10, COL_GREEN);
				text("WORN", x + 31, y + 40, 0.4f, COL_BG2, 0);
			}
			if (touched && tx >= x && tx < x + 56 && ty >= y && ty < y + 50) {
				act.kind = ACT_SELECT_SLOT;
				act.arg = i;
			}
		}
	}

	panel(6, 172, 308, 26);
	int sel = g_items.selected;
	if (!g_hud.mapTab && sel >= 0 && g_items.inv[sel] >= 0) {
		C2D_DrawRectSolid(12, 181, 0.5f, 6, 6, COL_ACCENT);
		text(itemsSlotName(sel), 22, 180, 0.45f, COL_TEXT, 180);
		if (buttonEx("USE", 206, 175, 50, 20, touched, tx, ty, false)) act.kind = ACT_USE;
		if (buttonEx("DROP", 260, 175, 50, 20, touched, tx, ty, false)) act.kind = ACT_DROP;
	} else {
		text(g_hud.mapTab ? "S-NAV: you are the arrow" : "Tap an item to hold it", 12, 180, 0.45f, COL_DIM, 0);
	}

	panel(6, 202, 244, 34);
	if (g_hud.messageTime > 0) {
		C2D_DrawRectSolid(7, 203, 0.5f, 2, 32, COL_ACCENT);
		u8 a = (u8)(fminf(1.0f, g_hud.messageTime) * 255);
		text(g_hud.message, 13, 206, 0.4f, C2D_Color32(226, 226, 220, a), 232);
	} else if (info->debug) {
		text(info->debug, 12, 206, 0.4f, COL_DIM, 234);
	} else {
		text("A USE   X BLINK   Y SPRINT", 12, 207, 0.4f, COL_DIM, 0);
		text("B CROUCH   START PAUSE", 12, 220, 0.4f, COL_DIM, 0);
	}
	if (buttonEx("LOOK", 254, 202, 60, info->hasNav ? 16 : 34, touched, tx, ty, false)) act.kind = ACT_LOOK_PAD;
	if (info->hasNav && buttonEx(g_hud.mapTab ? "ITEMS" : "S-NAV", 254, 220, 60, 16, touched, tx, ty, g_hud.mapTab))
		act.kind = g_hud.mapTab ? ACT_TAB_INV : ACT_TAB_MAP;
	return act;
}

static HudAction drawDocument(bool touched, int tx, int ty)
{
	HudAction act = { ACT_NONE, 0 };
	bool onButtons = ty >= 210 && tx >= 196;
	if (s_docLoaded) {
		float pw = 312 * s_docZoom, ph = pw * s_docAspect;
		touchPosition t;
		hidTouchRead(&t);
		if ((hidKeysHeld() & KEY_TOUCH) && !(touched && onButtons)) {
			if (s_dragX >= 0) {
				s_docX += t.px - s_dragX;
				s_docY += t.py - s_dragY;
			}
			s_dragX = t.px;
			s_dragY = t.py;
		} else {
			s_dragX = s_dragY = -1;
		}
		float minX = fminf(0, W - 4 - pw), minY = fminf(0, 236 - ph);
		s_docX = fmaxf(minX, fminf(0, s_docX));
		s_docY = fmaxf(minY, fminf(0, s_docY));
		C2D_Image img = { &s_docTex, &s_sub256 };
		C2D_DrawImageAt(img, 4 + s_docX, 2 + s_docY, 0.5f, NULL, pw / s_sub256.width, ph / s_sub256.height);
		if (buttonEx("ZOOM", 196, 212, 58, 24, touched, tx, ty, s_docZoom > 1)) {
			float cx = (160 - 4 - s_docX) / s_docZoom, cy = (120 - 2 - s_docY) / s_docZoom;
			s_docZoom = s_docZoom > 1 ? 1 : 2.2f;
			s_docX = 156 - cx * s_docZoom;
			s_docY = 118 - cy * s_docZoom;
		}
		if (ph > 240 && s_docZoom == 1) text("Drag to scroll", 8, 222, 0.4f, COL_DIM, 0);
	} else if (g_hud.docTpl >= 0) {
		textCentered(g_items.tpl[g_hud.docTpl].display, W / 2, 100, 0.6f, COL_TEXT);
	}
	if (button("CLOSE", 258, 212, 58, 24, touched, tx, ty)) act.kind = ACT_CLOSE;
	return act;
}

static HudAction drawKeypad(bool touched, int tx, int ty)
{
	HudAction act = { ACT_NONE, 0 };
	frame(90, 8, 140, 30, COL_BG, COL_EDGE);
	char shown[8] = "____";
	for (int i = 0; i < 4 && g_hud.keypad[i]; i++) shown[i] = g_hud.keypad[i];
	textCentered(shown, W / 2, 12, 0.8f, COL_TEXT);
	static const char* keys = "123456789C0E";
	for (int i = 0; i < 12; i++) {
		char label[2] = { keys[i], 0 };
		float x = 90 + (i % 3) * 48, y = 46 + (i / 3) * 44;
		if (!button(label, x, y, 44, 40, touched, tx, ty)) continue;
		size_t n = strlen(g_hud.keypad);
		if (keys[i] == 'C') g_hud.keypad[0] = 0;
		else if (keys[i] == 'E') act.kind = ACT_KEYPAD_OK;
		else if (n < 4) {
			g_hud.keypad[n] = keys[i];
			g_hud.keypad[n + 1] = 0;
		}
	}
	if (button("BACK", 250, 210, 64, 24, touched, tx, ty)) act.kind = ACT_CLOSE;
	return act;
}

static HudAction drawTitle(bool touched, int tx, int ty)
{
	HudAction act = { ACT_NONE, 0 };
	label("MAIN MENU", 80, 8);
	if (button("NEW GAME", 80, 24, 160, 38, touched, tx, ty)) act.kind = ACT_NEW_GAME;
	if (button("SKIP INTRO", 80, 70, 160, 38, touched, tx, ty)) {
		act.kind = ACT_NEW_GAME;
		act.arg = 1;
	}
	if (button("QUIT", 80, 116, 160, 38, touched, tx, ty)) act.kind = ACT_QUIT;
	if (g_hud.hasSave && button("CONTINUE", 80, 158, 160, 30, touched, tx, ty)) act.kind = ACT_LOAD;
	if (!g_hud.hasSave) textCentered("Press A to start", W / 2, 162, 0.5f, COL_DIM);
	text("Based on SCP - Containment Breach by Regalis and contributors, and the SCP Foundation "
	     "(scp-wiki.net). CC BY-SA 3.0.", 12, 196, 0.4f, COL_DIM, 296);
	return act;
}

void hudStartEnding(const char* code)
{
	snprintf(g_hud.ending, sizeof(g_hud.ending), "%s", code);
	g_hud.endingTime = 0;
	g_hud.mode = HUD_ENDING;
}

void hudDrawEndingTop(C3D_RenderTarget* top)
{
	for (int i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_CullFace(GPU_CULL_NONE);
	C2D_Prepare();
	C2D_SceneBegin(top);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
	               GPU_ONE_MINUS_SRC_ALPHA);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	float t = g_hud.endingTime;

	bool white = g_hud.ending[0] == 'A' || strcmp(g_hud.ending, "B2") == 0;
	float k = fminf(1.0f, t / 2.5f);
	u8 v = white ? 255 : 0;
	C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, C2D_Color32(v, v, v, (u8)(k * 255)));
	if (t > 3.0f && s_endingOk) {

		if (t > 9.0f || rand() % 150 < (int)((t - 3.0f) * 25))
			C2D_DrawImageAt((C2D_Image){ &s_endingTex, &s_subEnding }, 80, 0, 0.5f, NULL, 1, 1);
	}
}

static HudAction drawEnding(bool touched, int tx, int ty)
{
	HudAction act = { ACT_NONE, 0 };
	if (g_hud.endingTime > 15.0f) {
		textCentered("THE END", W / 2, 40, 0.9f, COL_TEXT);
		char line[32];
		snprintf(line, sizeof(line), "Ending %s", g_hud.ending);
		textCentered(line, W / 2, 80, 0.5f, COL_DIM);
		if (button("MAIN MENU", 80, 150, 160, 40, touched, tx, ty)) act.kind = ACT_MAIN_MENU;
	}
	return act;
}

static HudAction drawControls(bool touched, int tx, int ty)
{
	HudAction act = { ACT_NONE, 0 };
	textCentered("CONTROLS", W / 2, 6, 0.8f, COL_ACCENT);
	static const char* rows[][2] = {
		{ "Circle Pad", "Move" },
		{ "C-Stick", "Look (or tap LOOK and drag)" },
		{ "A / L", "Use: doors, items, buttons" },
		{ "X / R", "Blink (hold to keep eyes shut)" },
		{ "Y / D-Up", "Sprint" },
		{ "B / D-Down", "Crouch" },
		{ "Touch", "Inventory, documents, S-NAV" },
		{ "START", "Pause" },
	};
	panel(6, 30, 308, 140);
	for (int i = 0; i < 8; i++) {
		text(rows[i][0], 14, 36 + i * 16.5f, 0.45f, COL_ACCENT, 0);
		text(rows[i][1], 104, 36 + i * 16.5f, 0.45f, COL_TEXT, 0);
	}
	char speed[24];
	snprintf(speed, sizeof(speed), "LOOK SPEED  %d", g_hud.lookSpeed);
	text(speed, 14, 182, 0.55f, COL_TEXT, 0);
	if (button("-", 150, 176, 40, 26, touched, tx, ty) && g_hud.lookSpeed > 1) {
		g_hud.lookSpeed--;
		act.kind = ACT_SETTINGS;
	}
	if (button("+", 196, 176, 40, 26, touched, tx, ty) && g_hud.lookSpeed < 10) {
		g_hud.lookSpeed++;
		act.kind = ACT_SETTINGS;
	}
	if (button("BACK", 80, 208, 160, 28, touched, tx, ty)) g_hud.controls = false;
	return act;
}

static HudAction drawPause(bool touched, int tx, int ty)
{
	if (g_hud.controls) return drawControls(touched, tx, ty);
	HudAction act = { ACT_NONE, 0 };
	textCentered("PAUSED", W / 2, 10, 0.9f, COL_ACCENT);
	float y = 38;
	if (button("RESUME", 80, y, 160, 28, touched, tx, ty)) act.kind = ACT_RESUME;
	if (button("SAVE", 80, y += 33, 160, 28, touched, tx, ty)) act.kind = ACT_SAVE;
	if (g_hud.hasSave && button("LOAD", 80, y += 33, 160, 28, touched, tx, ty)) act.kind = ACT_LOAD;
	if (button("CONTROLS", 80, y += 33, 160, 28, touched, tx, ty)) g_hud.controls = true;
	if (button("MAIN MENU", 80, y += 33, 160, 28, touched, tx, ty)) act.kind = ACT_MAIN_MENU;
	if (button("QUIT", 80, y += 33, 160, 28, touched, tx, ty)) act.kind = ACT_QUIT;
	return act;
}

static HudAction drawDead(bool touched, int tx, int ty)
{
	HudAction act = { ACT_NONE, 0 };
	textCentered("YOU DIED", W / 2, 16, 1.0f, COL_RED);
	if (g_hud.deathMessage[0]) {
		panel(6, 44, 308, 144);
		label("INCIDENT REPORT", 12, 50);
		text(g_hud.deathMessage, 12, 64, 0.48f, COL_TEXT, 296);
	}
	if (g_hud.deathTime > 2.0f) {
		if (g_hud.hasSave) {
			if (button("LOAD GAME", 10, 196, 96, 32, touched, tx, ty)) act.kind = ACT_LOAD;
		} else if (button("RETRY", 10, 196, 96, 32, touched, tx, ty)) {
			act.kind = ACT_RESUME;
		}
		if (button("NEW GAME", 112, 196, 96, 32, touched, tx, ty)) {
			act.kind = ACT_NEW_GAME;
			act.arg = 1;
		}
		if (button("MAIN MENU", 214, 196, 96, 32, touched, tx, ty)) act.kind = ACT_MAIN_MENU;
	}
	return act;
}

void hudDrawTop(C3D_RenderTarget* top, const HudInfo* info)
{

	for (int i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_CullFace(GPU_CULL_NONE);
	C2D_Prepare();
	C2D_SceneBegin(top);

	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
	               GPU_ONE_MINUS_SRC_ALPHA);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	if (info->crosshair) {

		C2D_DrawCircleSolid(200, 120, 0.5f, 5, C2D_Color32(230, 230, 230, 200));
		C2D_DrawCircleSolid(200, 120, 0.5f, 3, C2D_Color32(0, 0, 0, 160));
	}
	if (info->nvg) C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, C2D_Color32(20, 160, 40, 70));
	if (info->eyelid > 0.001f) {
		float k = fminf(info->eyelid, 1.0f);
		float lid = k * 132.0f, soft = 26.0f;
		u32 black = C2D_Color32(0, 0, 0, 255), clear = C2D_Color32(0, 0, 0, 0);
		C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, C2D_Color32(0, 0, 0, (u8)(k * 90)));
		C2D_DrawRectSolid(0, 0, 0.5f, 400, lid - soft > 0 ? lid - soft : 0, black);
		C2D_DrawRectangle(0, fmaxf(0, lid - soft), 0.5f, 400, soft, black, black, clear, clear);
		C2D_DrawRectSolid(0, 240 - fmaxf(0, lid - soft), 0.5f, 400, fmaxf(0, lid - soft), black);
		C2D_DrawRectangle(0, 240 - lid, 0.5f, 400, soft, clear, clear, black, black);
	}
	if (info->dark > 0.001f) {
		u8 a = (u8)(fminf(info->dark, 1.0f) * 255);
		u32 c = g_hud.mode == HUD_DEAD ? C2D_Color32(40, 0, 0, a) : C2D_Color32(0, 0, 0, a);
		C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, c);
	}
}

void hudDrawTitleTop(C3D_RenderTarget* top)
{
	for (int i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_CullFace(GPU_CULL_NONE);
	C2D_Prepare();
	C2D_SceneBegin(top);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
	               GPU_ONE_MINUS_SRC_ALPHA);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	if (s_menuOk[0]) C2D_DrawImageAt((C2D_Image){ &s_menuBack, &s_subBack }, 0, 0, 0.5f, NULL, 1, 1);
}

HudAction hudDrawBottom(C3D_RenderTarget* bottom, const HudInfo* info, bool touched, int tx, int ty)
{
	C2D_TargetClear(bottom, COL_BG);
	C2D_SceneBegin(bottom);
	if (g_hud.mode != HUD_DOCUMENT) backdrop();
	switch (g_hud.mode) {
	case HUD_TITLE: return drawTitle(touched, tx, ty);
	case HUD_ENDING: return drawEnding(touched, tx, ty);
	case HUD_DOCUMENT: return drawDocument(touched, tx, ty);
	case HUD_KEYPAD: return drawKeypad(touched, tx, ty);
	case HUD_PAUSE: return drawPause(touched, tx, ty);
	case HUD_DEAD: return drawDead(touched, tx, ty);
	default: return drawGame(info, touched, tx, ty);
	}
}
