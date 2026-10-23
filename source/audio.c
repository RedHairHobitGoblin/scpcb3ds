#include "audio.h"

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MUSIC_CHANNEL 0
#define FIRST_SFX_CHANNEL 1
#define NUM_CHANNELS 24
#define MAX_CACHED 160
#define STREAM_MIN_BYTES (96 * 1024)
#define MAX_SFX_STREAMS 3
#define CACHE_BUDGET (3 * 1024 * 1024)

static const int s_indexTable[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const int s_stepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
	107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
	5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
	27086, 29794, 32767
};

static int decodeBlock(const uint8_t* in, int blockBytes, int16_t* out)
{
	if (blockBytes < 4) return 0;
	int pred = (int16_t)(in[0] | (in[1] << 8));
	int index = in[2];
	if (index > 88) index = 88;
	int n = 0;
	out[n++] = (int16_t)pred;
	for (int i = 4; i < blockBytes; i++) {
		for (int half = 0; half < 2; half++) {
			int nib = half ? (in[i] >> 4) : (in[i] & 15);
			int step = s_stepTable[index];
			int diff = step >> 3;
			if (nib & 1) diff += step >> 2;
			if (nib & 2) diff += step >> 1;
			if (nib & 4) diff += step;
			if (nib & 8) pred -= diff; else pred += diff;
			if (pred > 32767) pred = 32767;
			if (pred < -32768) pred = -32768;
			index += s_indexTable[nib];
			if (index < 0) index = 0;
			if (index > 88) index = 88;
			out[n++] = (int16_t)pred;
		}
	}
	return n;
}

typedef struct {
	FILE* f;
	uint32_t rate, blockAlign, dataStart, dataSize, totalSamples;
} WavFile;

static bool wavOpen(WavFile* w, const char* name)
{
	char path[160];
	snprintf(path, sizeof(path), "romfs:/sfx/%s.wav", name);
	memset(w, 0, sizeof(*w));
	w->f = fopen(path, "rb");
	if (!w->f) return false;
	uint8_t hdr[12];
	if (fread(hdr, 1, 12, w->f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) goto fail;
	bool fmtOk = false;
	for (;;) {
		uint8_t ch[8];
		if (fread(ch, 1, 8, w->f) != 8) goto fail;
		uint32_t size = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
		if (!memcmp(ch, "fmt ", 4)) {
			uint8_t fmt[20] = { 0 };
			fread(fmt, 1, size < 20 ? size : 20, w->f);
			if (size > 20) fseek(w->f, size - 20, SEEK_CUR);
			uint16_t tag = fmt[0] | (fmt[1] << 8), chans = fmt[2] | (fmt[3] << 8);
			w->rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
			w->blockAlign = fmt[12] | (fmt[13] << 8);
			fmtOk = tag == 0x11 && chans == 1 && w->blockAlign > 4;
		} else if (!memcmp(ch, "fact", 4) && size >= 4) {
			uint8_t v[4];
			fread(v, 1, 4, w->f);
			w->totalSamples = v[0] | (v[1] << 8) | (v[2] << 16) | ((uint32_t)v[3] << 24);
			if (size > 4) fseek(w->f, size - 4, SEEK_CUR);
		} else if (!memcmp(ch, "data", 4)) {
			w->dataStart = ftell(w->f);
			w->dataSize = size;
			break;
		} else {
			fseek(w->f, (size + 1) & ~1u, SEEK_CUR);
		}
	}
	if (!fmtOk) goto fail;
	return true;
fail:
	fclose(w->f);
	w->f = NULL;
	return false;
}

static int samplesPerBlock(const WavFile* w) { return (w->blockAlign - 4) * 2 + 1; }

typedef struct {
	char name[64];
	int16_t* pcm;
	uint32_t samples, rate;
	u64 lastUsed;
	bool failed;
	bool streamed;
	uint32_t bytes;
} Sound;

static Sound s_cache[MAX_CACHED];

typedef struct {
	ndspWaveBuf wb;
	Sound* snd;
	bool active;
	int stream;
	u64 started;
} Channel;

static bool s_ok;
static float s_listener[3], s_listenerYaw;

static Channel s_chan[NUM_CHANNELS];

static bool chanBusy(int c)
{
	return s_chan[c].active && (ndspChnIsPlaying(c) || osGetTime() - s_chan[c].started < 100);
}

static uint32_t s_cacheBytes;

static void soundDrop(Sound* s)
{
	if (s->pcm) linearFree(s->pcm);
	s_cacheBytes -= s->bytes;
	memset(s, 0, sizeof(*s));
}

static bool soundInUse(const Sound* s);

static bool evictOne(const Sound* keep)
{
	Sound* victim = NULL;
	for (int i = 0; i < MAX_CACHED; i++) {
		Sound* c = &s_cache[i];
		if (c == keep || !c->pcm || soundInUse(c)) continue;
		if (!victim || c->lastUsed < victim->lastUsed) victim = c;
	}
	if (!victim) return false;
	soundDrop(victim);
	return true;
}

static bool soundInUse(const Sound* s)
{
	for (int c = FIRST_SFX_CHANNEL; c < NUM_CHANNELS; c++)
		if (s_chan[c].snd == s && chanBusy(c)) return true;
	return false;
}

static Sound* soundGet(const char* name)
{
	Sound* freeSlot = NULL;
	Sound* oldest = NULL;
	for (int i = 0; i < MAX_CACHED; i++) {
		Sound* s = &s_cache[i];
		if (s->name[0] && strcmp(s->name, name) == 0) {
			s->lastUsed = osGetTime();
			return s->failed ? NULL : s;
		}
		if (!s->name[0] && !freeSlot) freeSlot = s;
		if (s->name[0] && !soundInUse(s) && (!oldest || s->lastUsed < oldest->lastUsed)) oldest = s;
	}
	Sound* s = freeSlot ? freeSlot : oldest;
	if (!s) return NULL;
	soundDrop(s);
	snprintf(s->name, sizeof(s->name), "%s", name);
	s->lastUsed = osGetTime();

	WavFile w;
	if (!wavOpen(&w, name)) {
		s->failed = true;
		return NULL;
	}
	if (w.dataSize > STREAM_MIN_BYTES) {
		fclose(w.f);
		s->streamed = true;
		s->rate = w.rate;
		return s;
	}
	uint32_t blocks = (w.dataSize + w.blockAlign - 1) / w.blockAlign;
	uint32_t maxSamples = blocks * samplesPerBlock(&w);
	uint32_t need = maxSamples * sizeof(int16_t);
	while (s_cacheBytes + need > CACHE_BUDGET && evictOne(s)) {}
	s->pcm = linearAlloc(need);
	while (!s->pcm && evictOne(s)) s->pcm = linearAlloc(need);
	if (s->pcm) {
		s->bytes = need;
		s_cacheBytes += need;
	}
	uint8_t* block = malloc(w.blockAlign);
	uint32_t n = 0;
	if (s->pcm && block) {
		fseek(w.f, w.dataStart, SEEK_SET);
		for (uint32_t b = 0; b < blocks; b++) {
			int got = fread(block, 1, w.blockAlign, w.f);
			if (got <= 4) break;
			n += decodeBlock(block, got, s->pcm + n);
		}
	}
	free(block);
	fclose(w.f);
	if (w.totalSamples && w.totalSamples < n) n = w.totalSamples;
	if (!s->pcm || n == 0) {
		bool noMemory = !s->pcm;
		if (s->pcm) linearFree(s->pcm);
		s_cacheBytes -= s->bytes;
		s->bytes = 0;
		s->pcm = NULL;
		s->failed = true;
		if (noMemory) s->name[0] = 0;
		return NULL;
	}
	s->samples = n;
	s->rate = w.rate;
	DSP_FlushDataCache(s->pcm, n * sizeof(int16_t));
	return s;
}

#define STREAM_BUFS 4
#define STREAM_BLOCKS 4

typedef struct {
	WavFile w;
	char name[64];
	ndspWaveBuf wb[STREAM_BUFS];
	int16_t* pcm[STREAM_BUFS];
	int pcmSamples;
	uint8_t* block;
	uint32_t pos;
	float volume;
	int chan;
	bool loop, eof, active;
} Stream;

static Stream s_music = { .chan = MUSIC_CHANNEL, .loop = true };
static Stream s_sfxStream[MAX_SFX_STREAMS];

static void setMix(int chan, float l, float r)
{
	float mix[12] = { 0 };
	mix[0] = l;
	mix[1] = r;
	ndspChnSetMix(chan, mix);
}

static void streamFill(Stream* st, int i)
{
	int spb = samplesPerBlock(&st->w);
	int16_t* pcm = st->pcm[i];
	int n = 0;
	for (int b = 0; b < STREAM_BLOCKS; b++) {
		if (st->pos >= st->w.dataSize) {
			if (!st->loop) {
				st->eof = true;
				break;
			}
			st->pos = 0;
			fseek(st->w.f, st->w.dataStart, SEEK_SET);
		}
		int want = st->w.blockAlign;
		if (st->pos + want > st->w.dataSize) want = st->w.dataSize - st->pos;
		int got = fread(st->block, 1, want, st->w.f);
		st->pos += want;
		if (got <= 4) continue;
		n += decodeBlock(st->block, got, pcm + n);
		if (n + spb > spb * STREAM_BLOCKS) break;
	}
	if (n == 0) {
		if (st->eof) return;
		memset(pcm, 0, sizeof(int16_t) * 64);
		n = 64;
	}
	ndspWaveBuf* wb = &st->wb[i];
	wb->data_vaddr = pcm;
	wb->nsamples = n;
	wb->looping = false;
	DSP_FlushDataCache(pcm, n * sizeof(int16_t));
	ndspChnWaveBufAdd(st->chan, wb);
}

static void streamClose(Stream* st)
{
	if (st->active) ndspChnWaveBufClear(st->chan);
	if (st->w.f) fclose(st->w.f);
	st->w.f = NULL;
	st->name[0] = 0;
	st->active = false;
}

static bool streamOpen(Stream* st, const char* name, float l, float r)
{
	streamClose(st);
	if (!wavOpen(&st->w, name)) return false;
	snprintf(st->name, sizeof(st->name), "%s", name);
	st->pos = 0;
	st->eof = false;
	free(st->block);
	st->block = malloc(st->w.blockAlign);
	int samples = samplesPerBlock(&st->w) * STREAM_BLOCKS;
	for (int i = 0; i < STREAM_BUFS; i++) {
		if (!st->pcm[i] || st->pcmSamples != samples) {
			if (st->pcm[i]) linearFree(st->pcm[i]);
			st->pcm[i] = linearAlloc(samples * sizeof(int16_t));
		}
		memset(&st->wb[i], 0, sizeof(ndspWaveBuf));
		if (!st->pcm[i] || !st->block) {
			fclose(st->w.f);
			st->w.f = NULL;
			return false;
		}
	}
	st->pcmSamples = samples;
	ndspChnWaveBufClear(st->chan);
	ndspChnReset(st->chan);
	ndspChnSetInterp(st->chan, NDSP_INTERP_LINEAR);
	ndspChnSetRate(st->chan, st->w.rate);
	ndspChnSetFormat(st->chan, NDSP_FORMAT_MONO_PCM16);
	setMix(st->chan, l, r);
	fseek(st->w.f, st->w.dataStart, SEEK_SET);
	st->active = true;
	for (int i = 0; i < STREAM_BUFS; i++) streamFill(st, i);
	return true;
}

static void streamUpdate(Stream* st)
{
	if (!st->active || !st->w.f) return;
	bool allDone = true;
	for (int i = 0; i < STREAM_BUFS; i++) {
		ndspWaveBuf* wb = &st->wb[i];
		bool done = wb->status == NDSP_WBUF_DONE || wb->status == NDSP_WBUF_FREE;
		if (done && !st->eof) streamFill(st, i);
		if (wb->status != NDSP_WBUF_DONE && wb->status != NDSP_WBUF_FREE) allDone = false;
	}
	if (st->eof && allDone) streamClose(st);
}

void musicPlay(const char* name, float volume)
{
	if (!s_ok) return;
	if (name && s_music.w.f && strcmp(name, s_music.name) == 0) {
		s_music.volume = volume;
		setMix(MUSIC_CHANNEL, volume, volume);
		return;
	}
	streamClose(&s_music);
	if (!name) return;
	s_music.volume = volume;
	streamOpen(&s_music, name, volume, volume);
}

const char* musicCurrent(void) { return s_music.name; }

bool audioInit(void)
{
	s_ok = R_SUCCEEDED(ndspInit());
	if (!s_ok) return false;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	for (int c = 0; c < NUM_CHANNELS; c++) s_chan[c].stream = -1;
	return true;
}

void audioExit(void)
{
	if (!s_ok) return;
	musicPlay(NULL, 0);
	sfxStopAll();
	for (int i = 0; i < MAX_CACHED; i++)
		soundDrop(&s_cache[i]);
	for (int i = 0; i < STREAM_BUFS; i++)
		if (s_music.pcm[i]) linearFree(s_music.pcm[i]);
	free(s_music.block);
	for (int k = 0; k < MAX_SFX_STREAMS; k++) {
		streamClose(&s_sfxStream[k]);
		for (int i = 0; i < STREAM_BUFS; i++)
			if (s_sfxStream[k].pcm[i]) linearFree(s_sfxStream[k].pcm[i]);
		free(s_sfxStream[k].block);
	}
	ndspExit();
	s_ok = false;
}

void audioUpdate(void)
{
	if (!s_ok) return;
	streamUpdate(&s_music);
	for (int k = 0; k < MAX_SFX_STREAMS; k++) {
		Stream* st = &s_sfxStream[k];
		streamUpdate(st);
		if (!st->active && st->chan > 0 && s_chan[st->chan].stream == k) {
			s_chan[st->chan].active = false;
			s_chan[st->chan].stream = -1;
		}
	}
	for (int c = FIRST_SFX_CHANNEL; c < NUM_CHANNELS; c++)
		if (s_chan[c].active && s_chan[c].stream < 0 && s_chan[c].wb.status == NDSP_WBUF_DONE && !s_chan[c].wb.looping)
			s_chan[c].active = false;
}

void audioSetListener(const float pos[3], float yawDeg)
{
	memcpy(s_listener, pos, sizeof(s_listener));
	s_listenerYaw = yawDeg;
}

static void spatial(const float pos[3], float range, float volume, float* l, float* r)
{
	float dx = pos[0] - s_listener[0], dy = pos[1] - s_listener[1], dz = pos[2] - s_listener[2];
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	float v = volume * fmaxf(0.0f, 1.0f - dist / range);
	float yr = s_listenerYaw * (float)M_PI / 180.0f;
	float pan = 0.0f;
	if (dist > 0.01f) pan = (dx * cosf(yr) + dz * sinf(yr)) / dist;
	*l = v * fminf(1.0f, 1.0f - pan * 0.7f);
	*r = v * fminf(1.0f, 1.0f + pan * 0.7f);
}

int audioBusyChannels(void)
{
	int n = 0;
	for (int c = FIRST_SFX_CHANNEL; c < NUM_CHANNELS; c++) n += chanBusy(c);
	return n;
}

static int startSound(const char* name, float l, float r, bool loop)
{
	if (!s_ok) return -1;
	Sound* s = soundGet(name);
	if (!s) return -1;
	int chan = -1;
	for (int c = FIRST_SFX_CHANNEL; c < NUM_CHANNELS; c++)
		if (!chanBusy(c)) { chan = c; break; }
	if (chan < 0) return -1;
	Channel* ch = &s_chan[chan];
	if (ch->stream >= 0) streamClose(&s_sfxStream[ch->stream]);
	ch->stream = -1;
	if (s->streamed) {
		int k = 0;
		while (k < MAX_SFX_STREAMS && s_sfxStream[k].active) k++;
		if (k == MAX_SFX_STREAMS) return -1;
		Stream* st = &s_sfxStream[k];
		st->chan = chan;
		st->loop = loop;
		if (!streamOpen(st, name, l, r)) return -1;
		ch->snd = s;
		ch->stream = k;
		ch->active = true;
		ch->started = osGetTime();
		return chan;
	}
	ndspChnWaveBufClear(chan);
	ndspChnReset(chan);
	ndspChnSetInterp(chan, NDSP_INTERP_LINEAR);
	ndspChnSetRate(chan, s->rate);
	ndspChnSetFormat(chan, NDSP_FORMAT_MONO_PCM16);
	setMix(chan, l, r);
	memset(&ch->wb, 0, sizeof(ch->wb));
	ch->wb.data_vaddr = s->pcm;
	ch->wb.nsamples = s->samples;
	ch->wb.looping = loop;
	ch->snd = s;
	ch->active = true;
	ch->started = osGetTime();
	ndspChnWaveBufAdd(chan, &ch->wb);
	return chan;
}

int sfxPlay(const char* name, float volume) { return startSound(name, volume, volume, false); }

int sfxPlay3D(const char* name, const float pos[3], float range, float volume)
{
	float l, r;
	spatial(pos, range, volume, &l, &r);
	if (l <= 0.001f && r <= 0.001f) return -1;
	return startSound(name, l, r, false);
}

void sfxLoop3D(int* chan, const char* name, const float pos[3], float range, float volume)
{
	float l, r;
	spatial(pos, range, volume, &l, &r);
	bool running = *chan >= 0 && s_chan[*chan].active && s_chan[*chan].snd &&
	               strcmp(s_chan[*chan].snd->name, name) == 0;
	if (l <= 0.001f && r <= 0.001f) {
		if (running) sfxStop(*chan);
		*chan = -1;
		return;
	}
	if (!running) *chan = startSound(name, l, r, true);
	else setMix(*chan, l, r);
}

void sfxStop(int chan)
{
	if (chan < FIRST_SFX_CHANNEL || chan >= NUM_CHANNELS || !s_ok) return;
	if (s_chan[chan].stream >= 0) streamClose(&s_sfxStream[s_chan[chan].stream]);
	s_chan[chan].stream = -1;
	ndspChnWaveBufClear(chan);
	s_chan[chan].active = false;
}

bool sfxPlaying(int chan)
{
	return chan >= FIRST_SFX_CHANNEL && chan < NUM_CHANNELS && chanBusy(chan);
}

void sfxStopAll(void)
{
	for (int c = FIRST_SFX_CHANNEL; c < NUM_CHANNELS; c++) sfxStop(c);
}
