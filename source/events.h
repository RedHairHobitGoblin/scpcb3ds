#pragma once
#include <stdbool.h>
#include <stdio.h>

#include "world.h"

typedef struct {
	float* pos;
	float* yaw;
	float* pitch;
	float* blinkTimer;
	float* injuries;
	const float* eye;
	bool eyesClosed;
	bool crouch;
	float noise;
} EvPlayer;

void eventsLoad(void);

void eventsSave(FILE* f);
bool eventsRestore(FILE* f);

void eventsInit(World* w);

const char* eventsUpdate(World* w, EvPlayer* p, float dt);
int eventsCount(void);

bool eventsTakePocketDimension(void);

float eventsLightBlink(void);
int eventsImplemented(void);

bool eventsInteract(World* w, EvPlayer* p);

bool eventsHover(void);
bool eventsDebug914(const World* w, float pos[3], float* yaw);
void eventsDebug914Feed(const World* w, const char* name, const char* tempname);

const char* eventsTakeEnding(void);
void eventsSetNuke(bool on);
bool eventsNuke(void);
bool eventsContained106(void);

void eventsDraw(const World* w, const C3D_Mtx* view, int uLoc, C3D_Tex* white, const float cam[3]);

bool eventsInfectScene(World* w, float pos[3], float* yaw);

const char* eventsDebugSpot(const World* w, int i, float pos[3], float* yaw);

void eventsDebugInfo(const World* w, const float pos[3], char* out, int size);

bool eventsTouch1123(const World* w, const float pos[3]);

const char* eventsMusicOverride(void);

bool eventsEnter1499(World* w, float pos[3], float* yaw);
bool eventsLeave1499(World* w, float pos[3]);

const char* eventsDebugArea(World* w, int k, float pos[3], float* yaw);

bool eventsDebugGate(const World* w, char gate, float pos[3]);

bool eventsRoomObject(const World* w, int room, int k, float out[3]);
