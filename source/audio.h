#pragma once
#include <stdbool.h>

bool audioInit(void);
void audioExit(void);
void audioUpdate(void);

int sfxPlay(const char* name, float volume);

int sfxPlay3D(const char* name, const float pos[3], float range, float volume);

void sfxLoop3D(int* chan, const char* name, const float pos[3], float range, float volume);
void sfxStop(int chan);
bool sfxPlaying(int chan);
void sfxStopAll(void);

int audioBusyChannels(void);

void audioSetListener(const float pos[3], float yawDeg);

void musicPlay(const char* name, float volume);
const char* musicCurrent(void);
