#pragma once
#include <stdbool.h>

#include "world.h"

typedef struct {
	float* pos;
	float* yaw;
	float* pitch;
	float* blinkTimer;
	float* injuries;
	float* fade;
	bool* crouch;
	const float* eye;
} PdPlayer;

void pdLoad(void);
bool pdActive(void);
void pdReset(World* w);

void pdEnter(World* w, PdPlayer* p);

const char* pdUpdate(World* w, PdPlayer* p, float dt);
const char* pdMusic(void);
