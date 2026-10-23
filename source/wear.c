#include "wear.h"

#include <string.h>

Wear g_wear;
static int s_slot[8];

enum { W_GASMASK, W_HAZMAT, W_VEST, W_714, W_NVG, W_427, W_1499, W_COUNT };

void wearReset(void)
{
	memset(&g_wear, 0, sizeof(g_wear));
	g_wear.radioChan = -1;
	g_wear.nvgBattery = 1000;
	for (int i = 0; i < W_COUNT; i++) s_slot[i] = -1;
}

bool wearIsWorn(int slot)
{
	for (int i = 0; i < W_COUNT; i++)
		if (s_slot[i] == slot) return true;
	return false;
}

static int level(const char* t, const char* base)
{
	size_t n = strlen(base);
	if (strncmp(t, base, n) != 0) return 0;
	if (!t[n]) return 1;
	if (t[n] == '2') return 2;
	if (t[n] == '3') return 3;
	return 1;
}

const char* wearToggle(const char* t, int slot)
{
	if (!strcmp(t, "scp1499") || !strcmp(t, "super1499")) {
		if (g_wear.hazmat) return "You are not able to wear SCP-1499 and a hazmat suit at the same time.";
		if (g_wear.mask1499) {
			g_wear.mask1499 = 0;
			s_slot[W_1499] = -1;
			return "You took off SCP-1499.";
		}
		g_wear.mask1499 = !strcmp(t, "scp1499") ? 1 : 2;
		g_wear.gasMask = g_wear.nvg = 0;
		s_slot[W_GASMASK] = s_slot[W_NVG] = -1;
		s_slot[W_1499] = slot;
		return "You put on SCP-1499.";
	}
	if (!strcmp(t, "gasmask") || !strcmp(t, "supergasmask") || !strcmp(t, "gasmask3")) {
		if (g_wear.hazmat) return "You can't use that while wearing a hazmat suit.";
		if (g_wear.gasMask) {
			g_wear.gasMask = 0;
			s_slot[W_GASMASK] = -1;
			return "You removed the gas mask.";
		}
		g_wear.gasMask = !strcmp(t, "supergasmask") ? 2 : !strcmp(t, "gasmask3") ? 3 : 1;
		g_wear.nvg = 0;
		s_slot[W_NVG] = -1;
		s_slot[W_GASMASK] = slot;
		return g_wear.gasMask == 2 ? "You put on the gas mask and you can breathe easier." : "You put on the gas mask.";
	}
	if (!strncmp(t, "hazmatsuit", 10)) {
		if (g_wear.vest) return "You can't wear a hazmat suit over a ballistic vest.";
		if (g_wear.hazmat) {
			g_wear.hazmat = 0;
			s_slot[W_HAZMAT] = -1;
			return "You removed the hazmat suit.";
		}
		g_wear.hazmat = level(t, "hazmatsuit");
		g_wear.gasMask = g_wear.nvg = 0;
		s_slot[W_GASMASK] = s_slot[W_NVG] = -1;
		s_slot[W_HAZMAT] = slot;
		return "You put on the hazmat suit.";
	}
	if (!strcmp(t, "vest") || !strcmp(t, "finevest") || !strcmp(t, "veryfinevest")) {
		if (g_wear.vest) {
			g_wear.vest = 0;
			s_slot[W_VEST] = -1;
			return "You removed the vest.";
		}
		g_wear.vest = !strcmp(t, "vest") ? 1 : 2;
		s_slot[W_VEST] = slot;
		return g_wear.vest == 1 ? "You put on the vest and feel slightly encumbered."
		                        : "You put on the vest and feel heavily encumbered.";
	}
	if (!strcmp(t, "scp714")) {
		g_wear.ring714 = !g_wear.ring714;
		s_slot[W_714] = g_wear.ring714 ? slot : -1;
		return g_wear.ring714 ? "You put on the ring." : "You removed the ring.";
	}
	if (!strcmp(t, "nvgoggles") || !strcmp(t, "finenvgoggles") || !strcmp(t, "supernv")) {
		if (g_wear.hazmat) return "You can't use that while wearing a hazmat suit.";
		if (g_wear.nvg) {
			g_wear.nvg = 0;
			s_slot[W_NVG] = -1;
			return "You removed the goggles.";
		}
		g_wear.nvg = !strcmp(t, "nvgoggles") ? 1 : !strcmp(t, "finenvgoggles") ? 2 : 3;
		g_wear.gasMask = 0;
		s_slot[W_GASMASK] = -1;
		s_slot[W_NVG] = slot;
		return "You put on the goggles.";
	}
	if (!strcmp(t, "scp427")) {
		g_wear.locket427 = !g_wear.locket427;
		s_slot[W_427] = g_wear.locket427 ? slot : -1;
		return g_wear.locket427 ? "You opened the locket." : "You closed the locket.";
	}
	return NULL;
}
