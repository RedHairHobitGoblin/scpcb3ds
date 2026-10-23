#pragma once
#include <stdbool.h>

#include "world.h"

typedef struct {
	float* pos;
	float* yaw;
	float* pitch;
	float* blinkTimer;
	float* fade;
	bool* frozen;
	bool eyesClosed;
	const float* eye;
} IntroPlayer;

void introStart(World* w, IntroPlayer* p);

const char* introUpdate(World* w, IntroPlayer* p, float dt);
bool introRunning(void);

void introFinish(World* w, IntroPlayer* p);

void introDebugToChamber(World* w, IntroPlayer* p);
