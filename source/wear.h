#pragma once
#include <stdbool.h>

typedef struct {
	int gasMask;
	int mask1499;
	int hazmat;
	int vest;
	bool ring714;
	int nvg;
	float nvgBattery;
	bool locket427;
	float timer427;
	float staminaEffect, staminaEffectTime;
	float healTimer;
	float infect;
	bool radioOn;
	int radioChannel;
	int radioChan;
	float radioTimer;
	float bell513;
} Wear;

extern Wear g_wear;

void wearReset(void);

const char* wearToggle(const char* tempname, int slot);

bool wearIsWorn(int slot);
