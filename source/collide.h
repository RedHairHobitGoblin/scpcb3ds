#pragma once
#include "room.h"

bool collidePlayer(const Room* room, float pos[3]);

float roomRayDown(const Room* room, const float p[3], float maxDrop);

bool roomSegmentBlocked(const Room* room, const float a[3], const float b[3]);
