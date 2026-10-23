// Port of SCP - Containment Breach by Regalis and contributors (CC BY-SA 3.0).

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "audio.h"
#include "events.h"
#include "hud.h"
#include "intro.h"
#include "items.h"
#include "npc.h"
#include "npc173.h"
#include "pd.h"
#include "vshader_shbin.h"
#include "wear.h"
#include "scp294.h"
#include "world.h"

#define CLEAR_COLOR 0x000000FF
#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define FOG_NEAR_DEFAULT 2.5f
#define FOG_FAR_DEFAULT  6.0f
#define FOG_NEAR (world.fogNear > 0 ? world.fogNear : FOG_NEAR_DEFAULT)
#define FOG_FAR  (world.fogFar > 0 ? world.fogFar : FOG_FAR_DEFAULT)

#define EYE_OFFSET 0.6f
#define CROUCH_EYE 0.3f

#define WALK_SPEED (0.018f * 70.0f)
#define SPRINT_MULT 2.5f
#define LOOK_SPEED 120.0f
#define GRAVITY    9.0f
#define FPS70(x)   ((x) * 70.0f)

static DVLB_s* vshader_dvlb;
static shaderProgram_s program;
static int uLoc_projection, uLoc_modelView, uLoc_fog;
static C3D_Tex whiteTex;

static World world;

static struct {
	float pos[3], vel[3];
	float yaw, pitch;
	bool noclip, crouch;
	float crouchState;
	float blinkTimer, blinkFreq;
	float blinkEffect, blinkEffectTime;
	float stamina;
	float shake;
	float injuries, bloodloss;
	float noise;
	bool dead;
	float dark;
	float fade;
	bool frozen;
} P;

float g_playerNoise, g_playerInjuries, g_staminaDrain;

static int usableDoor = -1, usableItem = -1;
static float ambientTimer = 10.0f;
#ifdef DEBUG_TOOLS
static int debugDoor = -1;
#endif
static bool quit;

static float frand(float lo, float hi) { return lo + (hi - lo) * (rand() / (float)RAND_MAX); }

static float eyeHeight(void) { return P.pos[1] + EYE_OFFSET - CROUCH_EYE * P.crouchState; }

static float s_eye[3];

static IntroPlayer introPlayer(void)
{
	s_eye[0] = P.pos[0];
	s_eye[1] = eyeHeight();
	s_eye[2] = P.pos[2];
	IntroPlayer ip = { P.pos, &P.yaw, &P.pitch, &P.blinkTimer, &P.fade, &P.frozen, false, s_eye };
	return ip;
}

static void resetPlayer(void)
{
	worldSpawnPoint(&world, P.pos, &P.yaw);
	P.pitch = 0;
	P.vel[0] = P.vel[1] = P.vel[2] = 0;
	P.crouch = false;
	P.crouchState = 0;
	P.blinkFreq = 455 + rand() % 211;
	P.blinkTimer = P.blinkFreq;
	P.blinkEffect = 1.0f;
	P.blinkEffectTime = 0;
	P.stamina = 100;
	P.injuries = 0;
	P.bloodloss = 0;
	P.dead = false;
	wearReset();
	P.dark = 0;
	P.fade = 0;
	P.frozen = false;
	worldStream(&world, P.pos);
}

static void newGame(uint32_t seed, bool intro)
{
	sfxStopAll();
	npcsReset();
	pdReset(&world);
	worldGenerate(&world, seed, intro);
	eventsInit(&world);
	itemsSpawn(&world);
	resetPlayer();
	npc173Spawn(&world, P.pos);
	g_hud.mode = HUD_GAME;
	if (intro && world.introRoom >= 0) {
		IntroPlayer ip = introPlayer();
		introStart(&world, &ip);
		worldStream(&world, P.pos);
	} else {
		npcsSpawnFacility(&world, P.pos);
		hudMessage("Find a way out of the facility. Don't take your eyes off SCP-173.", 6.0f);
	}
}

#define SAVE_DIR "sdmc:/3ds/scpcb3ds"
#define SAVE_PATH SAVE_DIR "/save.bin"
#define SAVE_MAGIC 0x34535053

static bool saveExists(void)
{
	FILE* f = fopen(SAVE_PATH, "rb");
	if (!f) return false;
	uint32_t m = 0;
	fread(&m, 4, 1, f);
	fclose(f);
	return m == SAVE_MAGIC;
}

static bool saveGame(void)
{
	if (world.introActive || pdActive() || P.dead) {
		hudMessage("You can't save here.", 3);
		return false;
	}
	mkdir("sdmc:/3ds", 0777);
	mkdir(SAVE_DIR, 0777);
	FILE* f = fopen(SAVE_PATH, "wb");
	if (!f) {
		hudMessage("Saving failed: can't write to the SD card.", 4);
		return false;
	}
	uint32_t magic = SAVE_MAGIC;
	fwrite(&magic, 4, 1, f);
	fwrite(P.pos, sizeof(P.pos), 1, f);
	fwrite(&P.yaw, sizeof(P.yaw), 1, f);
	fwrite(&P.pitch, sizeof(P.pitch), 1, f);
	fwrite(&P.crouch, sizeof(P.crouch), 1, f);
	fwrite(&P.blinkTimer, sizeof(P.blinkTimer), 1, f);
	fwrite(&P.blinkFreq, sizeof(P.blinkFreq), 1, f);
	fwrite(&P.stamina, sizeof(P.stamina), 1, f);
	fwrite(&P.injuries, sizeof(P.injuries), 1, f);
	fwrite(&P.bloodloss, sizeof(P.bloodloss), 1, f);
	fwrite(&g_wear, sizeof(g_wear), 1, f);
	worldSave(&world, f);
	itemsSave(f);
	eventsSave(f);
	npc173Save(f);
	npcsSave(f);
	fclose(f);
	g_hud.hasSave = true;
	sfxPlay("general/save1", 1.0f);
	hudMessage("Game progress saved.", 3);
	return true;
}

static bool loadGame(void)
{
	FILE* f = fopen(SAVE_PATH, "rb");
	if (!f) return false;
	uint32_t magic = 0;
	fread(&magic, 4, 1, f);
	bool ok = magic == SAVE_MAGIC;
	float pos[3], yaw, pitch, blinkTimer, blinkFreq, stamina, injuries, bloodloss;
	bool crouch;
	uint32_t seed = 0;
	if (ok) {
		fread(pos, sizeof(pos), 1, f);
		fread(&yaw, sizeof(yaw), 1, f);
		fread(&pitch, sizeof(pitch), 1, f);
		fread(&crouch, sizeof(crouch), 1, f);
		fread(&blinkTimer, sizeof(blinkTimer), 1, f);
		fread(&blinkFreq, sizeof(blinkFreq), 1, f);
		fread(&stamina, sizeof(stamina), 1, f);
		fread(&injuries, sizeof(injuries), 1, f);
		Wear wsave;
		ok = fread(&bloodloss, sizeof(bloodloss), 1, f) == 1 && fread(&wsave, sizeof(wsave), 1, f) == 1 &&
		     fread(&seed, sizeof(seed), 1, f) == 1;
		if (ok) {
			wsave.radioChan = -1;
			g_wear = wsave;
		}
	}
	if (ok) {
		sfxStopAll();
		npcsReset();
		pdReset(&world);
		worldGenerate(&world, seed, false);
		eventsInit(&world);
		itemsSpawn(&world);
		ok = worldRestoreDoors(&world, f) && itemsRestore(f) && eventsRestore(f);
		if (ok) {
			npc173Restore(f);
			ok = npcsRestore(f);
		}
	}
	fclose(f);
	if (!ok) {
		hudMessage("The save file is damaged.", 4);
		return false;
	}
	Wear keep = g_wear;
	resetPlayer();
	g_wear = keep;
	memcpy(P.pos, pos, sizeof(pos));
	P.yaw = yaw;
	P.pitch = pitch;
	P.crouch = crouch;
	P.crouchState = crouch ? 1.0f : 0.0f;
	P.blinkTimer = blinkTimer;
	P.blinkFreq = blinkFreq;
	P.stamina = stamina;
	P.injuries = injuries;
	P.bloodloss = bloodloss;
	worldStream(&world, P.pos);
	g_hud.mode = HUD_GAME;
	hudMessage("Game loaded.", 3);
	return true;
}

static void die(const char* msg)
{
	if (P.dead) return;
#ifdef DEBUG_TOOLS
	if (P.noclip) return;
#endif
	P.dead = true;
	snprintf(g_hud.deathMessage, sizeof(g_hud.deathMessage), "%s", msg);
	g_hud.deathTime = 0;
	g_hud.mode = HUD_DEAD;
	musicPlay(NULL, 0);
	P.yaw += (rand() % 2 ? 90.0f : -90.0f);
}

static void consumeSelected(void)
{
	g_items.inv[g_items.selected] = -1;
	g_items.invNote[g_items.selected][0] = 0;
	g_items.selected = -1;
}

static const char* sfxName(const char* blitzPath)
{
	static char out[64];
	const char* p = blitzPath;
	if (!strncasecmp(p, "SFX\\", 4) || !strncasecmp(p, "SFX/", 4)) p += 4;
	int n = 0;
	for (; *p && n < 63; p++) out[n++] = *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
	out[n] = 0;
	char* dot = strrchr(out, '.');
	if (dot) *dot = 0;
	return out;
}

static const char* pickMsg(const char* const* msgs, int n) { return msgs[rand() % n]; }

static void useSelected(void)
{
	int slot = g_items.selected;
	if (slot < 0 || g_items.inv[slot] < 0) return;
	ItemTemplate* t = &g_items.tpl[g_items.inv[slot]];
	const char* k = t->tempname;
	int was1499 = g_wear.mask1499;
	const char* worn = wearToggle(k, slot);
	if (g_wear.mask1499 && !was1499) eventsEnter1499(&world, P.pos, &P.yaw);
	else if (!g_wear.mask1499 && was1499) eventsLeave1499(&world, P.pos);
	if (worn) {
		hudMessage(worn, 5);
		g_items.selected = -1;
		return;
	}
	if (t->doc[0] || !strcmp(k, "paper") || !strcmp(k, "badge") || !strcmp(k, "oldpaper") || !strcmp(k, "ticket")) {
		hudShowDocument(g_items.inv[slot]);
	} else if (!strncmp(k, "key", 3) || !strcmp(k, "hand") || !strcmp(k, "hand2")) {
		hudMessage("Hold it and press a door's button (A) to use it.", 4);
	} else if (!strcmp(k, "firstaid") || !strcmp(k, "finefirstaid") || !strcmp(k, "firstaid2")) {
		if (P.injuries == 0 && P.bloodloss == 0) {
			hudMessage("You do not need to use a first aid kit right now.", 5);
			return;
		}
		if (!strcmp(k, "finefirstaid")) {
			P.injuries = 0;
			P.bloodloss = 0;
			hudMessage("You bandaged the wounds. The bleeding has stopped completely.", 5);
		} else {
			P.bloodloss = 0;
			P.injuries = fmaxf(0, P.injuries - 2.0f);
			hudMessage(P.injuries > 1.0f ? "You bandaged the wounds but were unable to stop the bleeding completely."
			                             : "You managed to stop the bleeding.", 5);
		}
		consumeSelected();
	} else if (!strcmp(k, "veryfinefirstaid")) {
		switch (rand() % 5) {
		case 0: P.injuries = 3.5f; hudMessage("You started bleeding heavily.", 7); break;
		case 1: P.injuries = P.bloodloss = 0; hudMessage("Your wounds are healing up rapidly.", 7); break;
		case 2:
			P.injuries = fmaxf(0, P.injuries - frand(0.5f, 3.5f));
			P.bloodloss = fmaxf(0, P.bloodloss - frand(10, 100));
			hudMessage("You feel much better.", 7);
			break;
		case 3: P.bloodloss = 0; hudMessage("You feel nauseated.", 7); break;
		default: {
			consumeSelected();
			PdPlayer pp = { P.pos, &P.yaw, &P.pitch, &P.blinkTimer, &P.injuries, &P.fade, &P.crouch, s_eye };
			pdEnter(&world, &pp);
			hudMessage("For some inexplicable reason, you find yourself inside the pocket dimension.", 8);
			return;
		}
		}
		consumeSelected();
	} else if (strstr(k, "eyedrops")) {
		if (!strcmp(k, "supereyedrops")) {
			P.blinkEffect = 0.0f;
			P.blinkEffectTime = 60.0f;
			hudMessage("You used the eyedrops. Your eyes feel very moisturized.", 5);
		} else {
			P.blinkEffect = !strcmp(k, "fineeyedrops") ? 0.4f : 0.6f;
			P.blinkEffectTime = !strcmp(k, "fineeyedrops") ? 40.0f : 30.0f;
			hudMessage("You used the eyedrops. Your eyes feel moisturized.", 5);
		}
		consumeSelected();
	} else if (!strcmp(k, "scp500")) {
		hudMessage(g_wear.infect > 0 ? "You swallowed the pill. Your nausea is fading." : "You swallowed the pill.", 7);
		g_wear.infect = 0;
		P.stamina = 100;
		P.injuries = P.bloodloss = 0;
		g_wear.staminaEffect = 1.0f;
		consumeSelected();
	} else if (!strcmp(k, "pill")) {
		hudMessage("You swallowed the pill.", 7);
		consumeSelected();
	} else if (!strcmp(k, "scp500death")) {
		hudMessage("You swallowed the pill.", 7);
		g_wear.timer427 = 70 * 360;
		g_wear.locket427 = true;
		consumeSelected();
	} else if (!strcmp(k, "syringe")) {
		g_wear.healTimer = 30;
		g_wear.staminaEffect = 0.5f;
		g_wear.staminaEffectTime = 20;
		hudMessage("You injected yourself with the syringe and feel a slight adrenaline rush.", 8);
		consumeSelected();
	} else if (!strcmp(k, "finesyringe")) {
		g_wear.healTimer = frand(20, 40);
		g_wear.staminaEffect = frand(0.5f, 0.8f);
		g_wear.staminaEffectTime = frand(20, 30);
		hudMessage("You injected yourself with the syringe and feel an adrenaline rush.", 8);
		consumeSelected();
	} else if (!strcmp(k, "veryfinesyringe")) {
		if (rand() % 3 == 0) {
			g_wear.staminaEffect = 0.1f;
			g_wear.staminaEffectTime = 30;
			hudMessage("You injected yourself with the syringe and feel a huge adrenaline rush.", 8);
		} else {
			g_wear.infect = fmaxf(g_wear.infect, 1);
			hudMessage("You injected yourself with the syringe and feel a pain in your arm.", 8);
		}
		consumeSelected();
	} else if (!strcmp(k, "scp1025")) {
		static const char* diseases[] = {
			"The book opens on a page about the common cold. You feel a tickle in your throat.",
			"The book opens on a page about chicken pox. Your skin starts to itch.",
			"The book opens on a page about appendicitis. You feel a dull pain in your abdomen.",
			"The book opens on a page about asthma. Your breathing feels tighter.",
			"The book opens on a page about cardiac arrest. Your heart skips a beat.",
			"The book opens on a page about secondary polycythemia. You feel strangely energized.",
		};
		hudMessage(g_wear.ring714 ? "The book's pages are blank to you." : pickMsg(diseases, 6), 8);
	} else if (!strcmp(k, "scp513")) {
		char snd[24];
		snprintf(snd, sizeof(snd), "scp/513/bell%d", 1 + rand() % 3);
		sfxPlay(snd, 1.0f);
		g_wear.bell513 = frand(20, 40);
		npcsSpawn5131(P.pos);
		hudMessage("The bell rings, but there is no sound from its clapper.", 5);
	} else if (!strncmp(k, "radio", 5) || !strcmp(k, "18vradio") || !strcmp(k, "fineradio") || !strcmp(k, "veryfineradio")) {
		if (!g_wear.radioOn) {
			g_wear.radioOn = true;
			g_wear.radioChannel = 0;
		} else if (++g_wear.radioChannel > 4) {
			g_wear.radioOn = false;
		}
		sfxStop(g_wear.radioChan);
		g_wear.radioChan = -1;
		g_wear.radioTimer = 0;
		char msg[48];
		if (g_wear.radioOn) snprintf(msg, sizeof(msg), "Radio: channel %d.", g_wear.radioChannel + 1);
		else snprintf(msg, sizeof(msg), "You turned off the radio.");
		hudMessage(msg, 3);
	} else if (!strcmp(k, "cigarette")) {
		static const char* msgs[] = {
			"\"I don't have anything to light it with. Umm, what about that... Nevermind.\"",
			"You are unable to get lit.",
			"You think about smoking it, but decide against it.",
			"You decide not to smoke it.",
			"You fiddle with the cigarette.",
			"\"Smoking is bad for you, you know.\"",
		};
		hudMessage(pickMsg(msgs, 6), 5);
	} else if (!strcmp(k, "1123")) {
		if (!g_wear.ring714 && !eventsTouch1123(&world, P.pos)) {
			sfxPlay("scp/1123/touch", 1.0f);
			die("Subject D-9341 was shot dead after attempting to attack a member of Nine-Tailed Fox. Surveillance tapes "
			    "show that the subject had been wandering around the site approximately 9 minutes prior, shouting the "
			    "phrase \"get rid of the four pests\" in chinese. SCP-1123 was found in [REDACTED] nearby, suggesting the "
			    "subject had come into physical contact with it. How exactly SCP-1123 was removed from its containment "
			    "chamber is still unknown.");
		}
	} else if (!strcmp(k, "emptycup")) {
		hudMessage("The cup is empty.", 5);
	} else if (!strcmp(k, "cup")) {
		int d = scp294Find(g_items.invNote[slot]);
		const char* refuse = scp294Get(d, "refusemessage");
		if (refuse[0]) {
			hudMessage(refuse, 5);
			return;
		}
		const char* snd = scp294Get(d, "sound");
		if (snd[0]) sfxPlay(sfxName(snd), 1.0f);
		const char* msg = scp294Get(d, "message");
		hudMessage(msg[0] ? msg : "You drank the liquid.", 6);
		P.injuries = fmaxf(0, P.injuries + (float)atof(scp294Get(d, "damage")));
		P.bloodloss = fmaxf(0, P.bloodloss + (float)atof(scp294Get(d, "blood loss")));
		const char* stam = scp294Get(d, "stamina effect");
		if (stam[0]) {
			g_wear.staminaEffect = (float)atof(stam);
			g_wear.staminaEffectTime = (float)atof(scp294Get(d, "stamina effect timer"));
		}
		bool lethal = !strcasecmp(scp294Get(d, "lethal"), "true");
		const char* why = scp294Get(d, "deathmessage");
		int empty = itemsFindTemplate("empty cup", "emptycup");
		g_items.invNote[slot][0] = 0;
		if (empty >= 0) {
			g_items.inv[slot] = empty;
			g_items.selected = -1;
		} else {
			consumeSelected();
		}
		if (lethal) die(why[0] ? why : "Subject D-9341 found dead next to a polystyrene cup.");
	} else if (!strcmp(k, "scp860")) {
		hudMessage("A blue key. It must open a wooden door somewhere.", 5);
	} else if (!strncmp(k, "nav", 3)) {
		g_hud.mapTab = true;
	} else if (!strcmp(k, "bat") || !strcmp(k, "18vbat") || !strcmp(k, "killbat")) {
		if (g_wear.nvg) {
			g_wear.nvgBattery = 1000;
			hudMessage("You replaced the goggles' battery.", 4);
			consumeSelected();
		} else {
			hudMessage("Put on something that needs batteries first.", 4);
		}
	} else {
		hudMessage("You can't use that here.", 3);
	}
}

static const char* updateWear(float dt)
{
	const char* death = NULL;
	if (g_wear.staminaEffectTime > 0 && (g_wear.staminaEffectTime -= dt) <= 0) g_wear.staminaEffect = 1.0f;
	if (g_wear.vest == 2) P.stamina = fminf(P.stamina, 60);
	if (g_wear.ring714) P.stamina = fminf(P.stamina, 10);
	if (g_wear.healTimer > 0) {
		g_wear.healTimer -= dt;
		P.injuries = fmaxf(0, P.injuries - dt / 30.0f);
	}
	if (g_wear.locket427) {
		g_wear.timer427 += FPS70(dt);
		P.injuries = fmaxf(0, P.injuries - dt * 0.03f);
		P.bloodloss = fmaxf(0, P.bloodloss - dt * 0.3f);
		g_wear.infect = fmaxf(0, g_wear.infect - dt);
		if (g_wear.timer427 > 70 * 360) {
			sfxPlay("scp/427/transform", 1.0f);
			death = "Subject D-9341 found transformed into a mass of flesh. The subject was wearing SCP-427.";
		}
	}
	if (g_wear.nvg) {
		g_wear.nvgBattery -= dt * (g_wear.nvg == 3 ? 0 : 3.0f);
		if (g_wear.nvgBattery <= 0) {
			g_wear.nvgBattery = 0;
			hudMessage("The batteries of the goggles died.", 3);
			g_wear.nvg = 0;
		}
	}

	world.fogFar = g_wear.nvg && !world.fogNear ? 9.0f : (world.fogNear ? world.fogFar : 0);

	if (g_wear.bell513 > 0 && (g_wear.bell513 -= dt) <= 0) {
		sfxPlay("scp/513/bell2", 0.6f);
		g_wear.bell513 = frand(30, 60);
	}
	if (g_wear.radioOn) {
		bool haveRadio = itemsHave("radio") || itemsHave("18vradio") || itemsHave("fineradio") || itemsHave("veryfineradio");
		if (!haveRadio) {
			g_wear.radioOn = false;
		} else if (!sfxPlaying(g_wear.radioChan)) {
			g_wear.radioTimer -= dt;
			if (g_wear.radioTimer <= 0) {
				char snd[32];
				switch (g_wear.radioChannel) {
				case 0: snprintf(snd, sizeof(snd), "radio/scpradio%d", rand() % 9); break;
				case 1: snprintf(snd, sizeof(snd), "radio/chatter%d", 1 + rand() % 4); break;
				case 2: snprintf(snd, sizeof(snd), "radio/franklin%d", 1 + rand() % 4); break;
				case 3: snprintf(snd, sizeof(snd), rand() % 2 ? "radio/radioalarm" : "radio/radioalarm2"); break;
				default: snprintf(snd, sizeof(snd), "radio/static"); break;
				}
				g_wear.radioChan = sfxPlay(snd, 0.6f);
				g_wear.radioTimer = frand(2, 6);
			}
		}
	}

	if (g_wear.infect > 0 && !death) {
		float prev = g_wear.infect;
		if (g_wear.infect < 93.0f) {
			if (!g_wear.locket427) g_wear.infect = fminf(g_wear.infect + FPS70(dt) * 0.002f, 100);
			for (int i = 0; i <= 6; i++)
				if (g_wear.infect > i * 15 + 10 && prev <= i * 15 + 10) {
					char snd[32];
					snprintf(snd, sizeof(snd), "scp/008/voices%d", i);
					sfxPlay(snd, 1.0f);
				}
			if (g_wear.infect > 20 && prev <= 20) hudMessage("You feel kinda feverish.", 6);
			else if (g_wear.infect > 40 && prev <= 40) hudMessage("You feel nauseated.", 6);
			else if (g_wear.infect > 60 && prev <= 60) hudMessage("The nausea's getting worse.", 6);
			else if (g_wear.infect > 80 && prev <= 80) hudMessage("You feel very faint.", 6);
			else if (g_wear.infect >= 91.5f) P.blinkTimer = fmaxf(fminf(-10 * (g_wear.infect - 91.5f), P.blinkTimer), -10);
			if (g_wear.infect >= 92.7f && prev < 92.7f && !world.isolated) {
				float pos[3], yaw;
				if (eventsInfectScene(&world, pos, &yaw)) {
					P.pos[0] = pos[0];
					P.pos[1] = pos[1];
					P.pos[2] = pos[2];
					P.yaw = yaw;
					worldStream(&world, P.pos);
				}
			}
		} else {
			g_wear.infect = fminf(g_wear.infect + FPS70(dt) * 0.004f, 100);
			P.injuries = fmaxf(P.injuries, 2.5f);
			P.bloodloss = 0;
			if (g_wear.infect >= 94.7f && prev < 94.7f) {
				sfxPlay("scp/008/killscientist2", 1.0f);
				death = "Subject D-9341 found ingesting Dr. [REDACTED] at Sector [REDACTED]. Subject was immediately "
				        "terminated by Nine-Tailed Fox and sent for autopsy. SCP-008 infection was confirmed, after "
				        "which the body was incinerated.";
			}
		}
	}
	return death;
}

static void interact(void)
{
	EvPlayer ep = { P.pos, &P.yaw, &P.pitch, &P.blinkTimer, &P.injuries, s_eye, false, P.crouch, P.noise };
	if (eventsInteract(&world, &ep)) return;
	if (usableDoor >= 0) {
		const Door* d = &world.doors[usableDoor];
		bool consumed = false;
		const char* msg = worldUseDoor(&world, usableDoor, itemsHeldTempname(), &consumed);
		if (consumed) g_items.selected = -1;
		if (msg) {
			hudMessage(msg, 6);
		} else if (d->code[0] && !d->locked && d->keycard == 0) {
			g_hud.mode = HUD_KEYPAD;
			g_hud.keypad[0] = 0;
			g_hud.keypadDoor = usableDoor;
		}
	} else if (usableItem >= 0) {
		int tpl = g_items.items[usableItem].tpl;
		if (itemsPickUp(usableItem)) {
			char msg[96];
			snprintf(msg, sizeof(msg), "Picked up %s.", g_items.tpl[tpl].display);
			hudMessage(msg, 3);
		} else {
			hudMessage("You cannot carry any more items.", 3);
		}
	}
}

static void handleHud(HudAction a)
{
	switch (a.kind) {
	case ACT_SELECT_SLOT:
		g_items.selected = g_items.selected == a.arg ? -1 : a.arg;
		break;
	case ACT_USE: useSelected(); break;
	case ACT_DROP:
		if (wearIsWorn(g_items.selected)) {
			const char* m = wearToggle(itemsHeldTempname(), g_items.selected);
			if (m) hudMessage(m, 3);
		}
		itemsDrop(g_items.selected, P.pos, P.yaw);
		break;
	case ACT_CLOSE: g_hud.mode = HUD_GAME; break;
	case ACT_KEYPAD_OK: {
		Door* d = &world.doors[g_hud.keypadDoor];
		if (strcmp(g_hud.keypad, d->code) == 0) {
			sfxPlay("interact/keycarduse1", 1.0f);
			worldToggleDoor(&world, g_hud.keypadDoor);
			g_hud.mode = HUD_GAME;
		} else {
			sfxPlay("interact/keycarduse2", 1.0f);
			g_hud.keypad[0] = 0;
			hudMessage("ACCESS DENIED", 2);
		}
		break;
	}
	case ACT_RESUME:
		if (g_hud.mode == HUD_DEAD) {
			if (world.introActive) {
				newGame(world.seed, true);
				break;
			}
			resetPlayer();
			npc173Spawn(&world, P.pos);
			npcsReset();
			npcsSpawnFacility(&world, P.pos);
		}
		g_hud.mode = HUD_GAME;
		break;
	case ACT_NEW_GAME: newGame((uint32_t)osGetTime() * 2654435761u, a.arg == 0); break;
	case ACT_QUIT: quit = true; break;
	case ACT_SAVE:
		if (saveGame()) g_hud.mode = HUD_GAME;
		break;
	case ACT_LOAD: loadGame(); break;
	case ACT_MAIN_MENU:
		sfxStopAll();
		g_hud.mode = HUD_TITLE;
		break;
	case ACT_TAB_MAP: g_hud.mapTab = true; break;
	case ACT_TAB_INV: g_hud.mapTab = false; break;
	case ACT_LOOK_PAD: g_hud.lookPad = !g_hud.lookPad; break;
	case ACT_PAD_USE:
		if (g_hud.mode == HUD_GAME && !P.dead) interact();
		break;
	default: break;
	}
}

static void updateBlink(float dt, u32 kDown, u32 kHeld)
{
	float frames = FPS70(dt);
	if (kDown & KEY_R) P.blinkTimer = 0;
	if ((kHeld & KEY_R) && P.blinkTimer < -10) P.blinkTimer = -10;
	P.dark = 0;
	if (P.blinkTimer < 0) {
		float t = P.blinkTimer;
		if (t > -5) P.dark = sinf(fabsf(t * 18.0f) * (float)M_PI / 180.0f);
		else if (t > -15) P.dark = 1.0f;
		else P.dark = fabsf(sinf(t * 18.0f * (float)M_PI / 180.0f));
		if (P.blinkTimer <= -20) {
			P.blinkFreq = 455 + rand() % 211;
			P.blinkTimer = P.blinkFreq;
		}
		P.blinkTimer -= frames;
	} else {
		P.blinkTimer -= frames * 0.6f * P.blinkEffect;
	}
	if (P.blinkEffectTime > 0 && (P.blinkEffectTime -= dt) <= 0) P.blinkEffect = 1.0f;
}

static bool eyesClosed(void) { return P.blinkTimer <= -6 && P.blinkTimer >= -16; }

static void footstep(bool running)
{
	char snd[24];
	snprintf(snd, sizeof(snd), "step/%s%d", running ? "run" : "step", 1 + rand() % 8);
	sfxPlay(snd, P.crouch ? 0.4f : 1.0f);
}

static void updatePlayer(float dt, u32 kDown, u32 kHeld)
{
#ifdef DEBUG_TOOLS
	if (kDown & KEY_SELECT) P.noclip = !P.noclip;
#endif
	if ((kDown & KEY_DDOWN) && !P.noclip) P.crouch = !P.crouch;
	P.crouchState += ((P.crouch ? 1.0f : 0.0f) - P.crouchState) * fminf(1.0f, dt * 7.0f);

#ifdef DEBUG_TOOLS

	if (P.noclip && (kHeld & KEY_R) && (kDown & KEY_DUP)) {
		eventsDebug914Feed(&world, "Level 1 Key Card", "key1");
		hudMessage("Keycard placed in the intake booth.", 3);
		kDown &= ~KEY_DUP;
	}

	if (P.noclip && (kHeld & KEY_R) && (kDown & KEY_DDOWN)) {
		if (eventsDebug914(&world, P.pos, &P.yaw)) {
			P.noclip = false;
			P.pitch = -20;
			P.vel[1] = 0;
			worldStream(&world, P.pos);
		}
		kDown &= ~KEY_DDOWN;
	}

	if (P.noclip && (kHeld & KEY_R) && (kDown & (KEY_DRIGHT | KEY_DLEFT))) {
		if (eventsDebugGate(&world, (kDown & KEY_DRIGHT) ? 'b' : 'a', P.pos)) {
			P.noclip = false;
			P.vel[1] = 0;
			worldStream(&world, P.pos);
		}
		kDown &= ~(KEY_DRIGHT | KEY_DLEFT);
	}

	if (P.noclip && (kHeld & KEY_L) && (kDown & (KEY_DRIGHT | KEY_DLEFT))) {
		if (eventsDebugGate(&world, (kDown & KEY_DRIGHT) ? 'B' : 'A', P.pos)) {
			P.noclip = false;
			P.vel[1] = 0;
			worldStream(&world, P.pos);
		}
		kDown &= ~(KEY_DRIGHT | KEY_DLEFT);
	}

	static int debugArea = -1;
	if (P.noclip && (kDown & KEY_ZL) && world.introActive) {
		IntroPlayer ip = { P.pos, &P.yaw, &P.pitch, &P.blinkTimer, &P.fade, &P.frozen, false, s_eye };
		introDebugToChamber(&world, &ip);
		P.noclip = false;
		kDown &= ~KEY_ZL;
	}
	if (P.noclip && (kDown & KEY_ZL)) {
		const char* label = NULL;
		for (int t = 0; t < 3 && !label; t++) label = eventsDebugArea(&world, ++debugArea, P.pos, &P.yaw);
		if (!label) hudMessage("None of the areas is on this map.", 3);
		if (label) {
			hudMessage(label, 3);
			P.noclip = false;
			P.pitch = 0;
			P.vel[1] = 0;
			worldStream(&world, P.pos);
		}
		kDown &= ~KEY_ZL;
	}

	static int debugSpot = -2;
	if (debugSpot == -2) {
		debugSpot = -1;
		FILE* f = fopen("sdmc:/3ds/scpcb3ds/spot.txt", "r");
		if (f) {
			if (fscanf(f, "%d", &debugSpot) == 1) debugSpot--;
			fclose(f);
		}
	}
	if (P.noclip && (kDown & (KEY_ZR | KEY_ZL))) {
		debugSpot += (kDown & KEY_ZR) ? ((kHeld & KEY_R) ? 5 : 1) : -1;
		const char* label = eventsDebugSpot(&world, debugSpot, P.pos, &P.yaw);
		if (label) {
			hudMessage(label, 3);
			P.noclip = false;
			P.pitch = -15;
			P.vel[1] = 0;
			worldStream(&world, P.pos);
		}
	}

	if (P.noclip && (kDown & (KEY_DRIGHT | KEY_DLEFT)) && world.ndoors > 0) {
		debugDoor = (debugDoor + ((kDown & KEY_DRIGHT) ? 1 : -1) + world.ndoors) % world.ndoors;
		worldDoorViewpoint(&world, debugDoor, P.pos, &P.yaw);
		P.pitch = 0;
		P.vel[1] = 0;
		worldStream(&world, P.pos);
	}

	if (P.noclip && (kDown & KEY_DDOWN)) {
		static const char* names[] = { "SCP-106 (rising)", "SCP-049", "SCP-096", "guard", "Class-D", "MTF", "SCP-939",
		                               "SCP-966", "SCP-049-2", "SCP-008-1", "SCP-066", "SCP-372", "SCP-513-1", "SCP-1048-A", "SCP-035", "SCP-860-2", "SCP-1499-1" };
		int t = npcDebugNext(&world, P.pos, &P.yaw, (kHeld & KEY_L) != 0);
		P.pitch = 0;
		if (t >= 0) hudMessage(names[t], 3);
		worldStream(&world, P.pos);
	}

	if (P.noclip && (kDown & KEY_DUP) && (kHeld & KEY_L)) {
		P.noclip = false;
		PdPlayer pp = { P.pos, &P.yaw, &P.pitch, &P.blinkTimer, &P.injuries, &P.fade, &P.crouch, s_eye };
		pdEnter(&world, &pp);
		kDown &= ~KEY_DUP;
	}

	static int debugItem = -1;
	if (P.noclip && (kDown & KEY_DUP) && g_items.nitems > 0) {
		for (int n = 0; n < g_items.nitems; n++) {
			debugItem = (debugItem + 1) % g_items.nitems;
			if (g_items.items[debugItem].active) break;
		}
		int tpl = g_items.items[debugItem].tpl;
		if (itemsPickUp(debugItem)) {
			char msg[96];
			snprintf(msg, sizeof(msg), "Picked up %s.", g_items.tpl[tpl].display);
			hudMessage(msg, 3);
		}
	}
#endif

	updateBlink(dt, kDown, kHeld);

	circlePosition cs;
	hidCstickRead(&cs);
	float lx = cs.dx / 156.0f, ly = cs.dy / 156.0f;
	P.yaw += lx * LOOK_SPEED * dt;
	P.pitch += ly * LOOK_SPEED * 0.75f * dt;
	if (P.pitch > 85.0f) P.pitch = 85.0f;
	if (P.pitch < -85.0f) P.pitch = -85.0f;

	circlePosition cp;
	hidCircleRead(&cp);
	float mx = cp.dx / 156.0f, mz = cp.dy / 156.0f;
	if (fabsf(mx) < 0.15f) mx = 0;
	if (fabsf(mz) < 0.15f) mz = 0;
	float mag = sqrtf(mx * mx + mz * mz);
	if (mag > 1.0f) {
		mx /= mag;
		mz /= mag;
		mag = 1.0f;
	}
	bool moving = mag > 0;

	float sprint = 1.0f;
	if (moving && !P.crouch && (kHeld & KEY_DUP) && P.stamina > 0) {
		sprint = SPRINT_MULT;
		P.stamina -= FPS70(dt) * 0.4f * g_wear.staminaEffect;
		if (P.stamina <= 0) P.stamina = -20;
	} else {
		P.stamina = fminf(P.stamina + FPS70(dt) * 0.15f * (P.crouch ? 1.25f : 1.0f / 1.25f), 100.0f);
	}
	float speed = WALK_SPEED * sprint / (1.0f + P.crouchState);
	if (g_wear.vest == 2 || g_wear.hazmat) speed *= 0.85f;

	float yr = P.yaw * (float)M_PI / 180.0f;
	float fx = sinf(yr), fz = -cosf(yr);
	float rx = cosf(yr), rz = sinf(yr);

	if (P.noclip) {
		float pr = P.pitch * (float)M_PI / 180.0f;
		P.pos[0] += (fx * cosf(pr) * mz + rx * mx) * speed * 2 * dt;
		P.pos[1] += sinf(pr) * mz * speed * 2 * dt;
		P.pos[2] += (fz * cosf(pr) * mz + rz * mx) * speed * 2 * dt;
		worldStream(&world, P.pos);
		return;
	}

	P.pos[0] += (fx * mz + rx * mx) * speed * dt;
	P.pos[2] += (fz * mz + rz * mx) * speed * dt;
	P.noise = !moving ? 0.0f : (sprint > 1.0f ? 4.0f : 2.5f - (P.crouch ? 0.6f : 0.0f));
	P.vel[1] -= GRAVITY * dt;
	if (P.vel[1] < -10.0f) P.vel[1] = -10.0f;
	P.pos[1] += P.vel[1] * dt;

	worldStream(&world, P.pos);
	if (worldCollide(&world, P.pos) && P.vel[1] < 0) P.vel[1] = 0;
	if (P.pos[1] < -20.0f) resetPlayer();

	if (moving) {
		float before = fmodf(P.shake, 360.0f);
		P.shake = fmodf(P.shake + FPS70(dt) * fminf(sprint, 1.5f) * 7.0f * mag, 720.0f);
		if (before < 180.0f && fmodf(P.shake, 360.0f) >= 180.0f) footstep(sprint > 1.0f);
	}
}

static void updateAudio(float dt)
{
	float eye[3] = { P.pos[0], eyeHeight(), P.pos[2] };
	audioSetListener(eye, P.yaw);
	if (g_hud.mode == HUD_TITLE) {
		musicPlay("music/menu", 0.6f);
		return;
	}
	if (g_hud.mode == HUD_ENDING) {

		static float lastT;
		float t = g_hud.endingTime;
		if (t > 200 / 70.0f) musicPlay("music/ending", 0.6f);
		if (lastT <= 450 / 70.0f && t > 450 / 70.0f) {
			char snd[48];
			snprintf(snd, sizeof(snd), "ending/gate%c/ending%s", g_hud.ending[0] == 'A' ? 'a' : 'b', g_hud.ending);
			for (char* c = snd; *c; c++) *c = (*c >= 'A' && *c <= 'Z') ? *c + 32 : *c;
			sfxPlay(snd, 1.0f);
		}
		lastT = t;
		return;
	}
	if (P.dead) return;
	static const char* zoneMusic[4] = { NULL, "music/the dread", "music/heavycontainment", "music/entrancezone" };
	int zone = worldRoomZone(&world, P.pos);
	if (zone < 1 || zone > 3) zone = 1;
	const char* override = pdMusic() ? pdMusic() : eventsMusicOverride() ? eventsMusicOverride() : npcsMusicOverride();
	musicPlay(override ? override : zoneMusic[zone], 0.5f);

	ambientTimer -= dt;
	if (ambientTimer <= 0) {
		char snd[40];
		snprintf(snd, sizeof(snd), "ambient/zone%d/ambient%d", zone, 1 + rand() % (zone == 3 ? 12 : 11));
		sfxPlay(snd, 0.6f);
		ambientTimer = 15.0f + rand() % 30;
	}
}

static void bind3D(void)
{
	C3D_BindProgram(&program);
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2);
	AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 2);
	AttrInfo_AddLoader(attrInfo, 3, GPU_UNSIGNED_BYTE, 4);
	for (int i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_CullFace(GPU_CULL_FRONT_CCW);
}

static void sceneInit(void)
{
	vshader_dvlb = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
	shaderProgramInit(&program);
	shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0]);
	uLoc_projection = shaderInstanceGetUniformLocation(program.vertexShader, "projection");
	uLoc_modelView  = shaderInstanceGetUniformLocation(program.vertexShader, "modelView");
	uLoc_fog        = shaderInstanceGetUniformLocation(program.vertexShader, "fogParams");

	C3D_TexInit(&whiteTex, 8, 8, GPU_RGB565);
	memset(whiteTex.data, 0xFF, 8 * 8 * 2);
	C3D_TexFlush(&whiteTex);
}

static void sceneExit(void)
{
	C3D_TexDelete(&whiteTex);
	shaderProgramFree(&program);
	DVLB_Free(vshader_dvlb);
}

static void render(void)
{
	bind3D();
	C3D_Mtx projection, view;
	Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(70.0f), C3D_AspectRatioTop, 0.05f, fmaxf(30.0f, FOG_FAR * 1.5f), false);

	float eyeY = eyeHeight();
	Mtx_Identity(&view);
	Mtx_RotateX(&view, -P.pitch * (float)M_PI / 180.0f, true);
	Mtx_RotateY(&view, P.yaw * (float)M_PI / 180.0f, true);
	Mtx_Translate(&view, -P.pos[0], -eyeY, -P.pos[2], true);

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &projection);
	roomSetFogColor(world.fogColor >> 16, world.fogColor >> 8, world.fogColor);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_fog, 1.0f / (FOG_FAR - FOG_NEAR), FOG_FAR / (FOG_FAR - FOG_NEAR), 1.0f / 255.0f, 0.0f);

	float cam[3] = { P.pos[0], eyeY, P.pos[2] };
	worldDraw(&world, &view, uLoc_modelView, cam, P.yaw, &whiteTex, FOG_FAR);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	itemsDraw(&view, uLoc_modelView, cam, &whiteTex, FOG_FAR);
	npc173Draw(&view, uLoc_modelView, &whiteTex);
	npcsDraw(&view, uLoc_modelView, &whiteTex, cam, FOG_FAR);
	eventsDraw(&world, &view, uLoc_modelView, &whiteTex, cam);
}

int main(void)
{
	osSetSpeedupEnable(true);
	gfxInitDefault();
	romfsInit();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
	hudInit();
	audioInit();

	C3D_RenderTarget* top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	sceneInit();
	srand((unsigned)osGetTime());
	bool ok = worldLoadTemplates(&world);
	itemsLoadTemplates();
	npc173Load();
	eventsLoad();
	g_hud.mode = HUD_TITLE;
	g_hud.hasSave = saveExists();
	if (!ok) hudMessage("No rooms in romfs:/rooms.txt", 1000);

	u64 last = osGetTime(), fpsTime = last;
	int frames = 0;
	float fps = 60;

	while (aptMainLoop() && !quit) {
		hidScanInput();
		u32 kDown = hidKeysDown(), kHeld = hidKeysHeld();
		touchPosition touch;
		hidTouchRead(&touch);

		static int padLastX = -1, padLastY = -1;
		bool padActive = g_hud.lookPad && g_hud.mode == HUD_GAME && !P.dead && !P.frozen;
		bool touching = (kHeld & KEY_TOUCH) != 0;
		if (padActive && touching && touch.px >= PAD_X && touch.px < PAD_X + PAD_W && touch.py >= PAD_Y &&
		    touch.py < PAD_Y + PAD_H) {
			if (padLastX >= 0 && !(kDown & KEY_TOUCH)) {
				P.yaw += (touch.px - padLastX) * 0.45f;
				P.pitch -= (touch.py - padLastY) * 0.45f;
				if (P.pitch > 85.0f) P.pitch = 85.0f;
				if (P.pitch < -85.0f) P.pitch = -85.0f;
			}
			padLastX = touch.px;
			padLastY = touch.py;
		} else {
			padLastX = padLastY = -1;
		}
		if (padActive && touching && touch.px >= PAD_BLINK_X && touch.px < PAD_BLINK_X + PAD_BTN_W &&
		    touch.py >= PAD_BTN_Y && touch.py < PAD_BTN_Y + PAD_BTN_H) {
			kHeld |= KEY_R;
			if (kDown & KEY_TOUCH) kDown |= KEY_R;
		}

		u64 now = osGetTime();
		float dt = (now - last) / 1000.0f;
		if (dt > 0.1f) dt = 0.1f;
		last = now;

		if (g_hud.mode == HUD_TITLE && (kDown & (KEY_A | KEY_START)) && ok) {
			newGame((uint32_t)osGetTime() * 2654435761u, true);
			kDown &= ~(KEY_A | KEY_START);
		}
		if (kDown & KEY_START) {
			if (g_hud.mode == HUD_GAME) g_hud.mode = HUD_PAUSE;
			else if (g_hud.mode == HUD_PAUSE) g_hud.mode = HUD_GAME;
		}
		bool playing = g_hud.mode == HUD_GAME || g_hud.mode == HUD_DOCUMENT || g_hud.mode == HUD_KEYPAD;
		if (playing) {
			static const u32 alias[4][2] = { { KEY_A, KEY_L }, { KEY_B, KEY_DDOWN }, { KEY_X, KEY_R }, { KEY_Y, KEY_DUP } };
			for (int i = 0; i < 4; i++) {
				if (kDown & alias[i][0]) kDown |= alias[i][1];
				if (kHeld & alias[i][0]) kHeld |= alias[i][1];
			}
		}
		if (playing && !P.dead) {
			if (P.frozen) updateBlink(dt, 0, 0);
			else updatePlayer(dt, kDown, kHeld);
			float eye[3] = { P.pos[0], eyeHeight(), P.pos[2] };
			memcpy(s_eye, eye, sizeof(eye));
			usableDoor = worldUsableDoor(&world, P.pos, P.yaw);
			usableItem = usableDoor < 0 ? itemsPickable(eye, P.yaw, P.pitch) : -1;
			if (kDown & KEY_L) interact();
			worldUpdateDoors(&world, dt);
			itemsUpdate(&world, dt);
			PlayerView pv = { P.pos, eye, P.yaw, P.pitch, eyesClosed(), P.dead };
			const char* death = npc173Update(&world, &pv, dt);
			const char* death2 = npcsUpdate(&world, &pv, dt);
			if (!death) death = death2;
			if (introRunning()) {
				IntroPlayer ip = introPlayer();
				ip.eyesClosed = eyesClosed();
				const char* d3 = introUpdate(&world, &ip, dt);
				if (d3 && !death) death = d3;
			}
			bool grabbed = npc106TakeGrab();
#ifdef DEBUG_TOOLS
			if (P.noclip) grabbed = false;
#endif
			if (grabbed) {
				PdPlayer pp = { P.pos, &P.yaw, &P.pitch, &P.blinkTimer, &P.injuries, &P.fade, &P.crouch, s_eye };
				pdEnter(&world, &pp);
			}
			if (pdActive() && !death) {
				PdPlayer pp = { P.pos, &P.yaw, &P.pitch, &P.blinkTimer, &P.injuries, &P.fade, &P.crouch, s_eye };
				death = pdUpdate(&world, &pp, dt);
			}
			if (!death) {
				EvPlayer ep = { P.pos, &P.yaw, &P.pitch, &P.blinkTimer, &P.injuries, s_eye, eyesClosed(), P.crouch, P.noise };
				death = eventsUpdate(&world, &ep, dt);
				if (eventsTakePocketDimension()) {
					PdPlayer pp = { P.pos, &P.yaw, &P.pitch, &P.blinkTimer, &P.injuries, &P.fade, &P.crouch, s_eye };
					pdEnter(&world, &pp);
				}
			}
			if (!death) death = updateWear(dt);

			g_playerNoise = P.noise;
			if (g_playerInjuries > 0) {
				P.injuries += g_playerInjuries;
				g_playerInjuries = 0;
				if (P.injuries > 4.0f && !death)
					death = "Subject D-9341. Cause of death: multiple lacerations and severe blunt force trauma.";
			}
			if (g_staminaDrain > 0) {
				g_wear.staminaEffect = g_staminaDrain;
				g_wear.staminaEffectTime = 1000 / 70.0f;
				g_staminaDrain = 0;
			}
			npcsCheckMTF(&world, P.pos);

			if (P.injuries > 1.0f) {
				float before = P.bloodloss;
				P.bloodloss = fminf(P.bloodloss + fminf(P.injuries, 3.5f) / 300.0f * dt * 70.0f, 100.0f);
				if (before <= 60 && P.bloodloss > 60) hudMessage("You are feeling faint from the amount of blood you have lost.", 4);
			}
			if (P.bloodloss > 60) P.crouch = true;
			if (P.bloodloss >= 100 && !death) death = "Subject D-9341 died of blood loss.";
			const char* ending = eventsTakeEnding();
			if (ending) {
				sfxStopAll();
				musicPlay(NULL, 0);
				hudStartEnding(ending);
				death = NULL;
			}
			if (death) die(death);
		} else if (P.dead) {
			P.dark = fminf(0.85f, P.dark + dt * 0.5f);
			worldUpdateDoors(&world, dt);
		}
		hudUpdate(dt);
		updateAudio(dt);
		audioUpdate();

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C3D_RenderTargetClear(top, C3D_CLEAR_ALL, g_hud.mode == HUD_TITLE ? CLEAR_COLOR : ((world.fogColor & 0xFFFFFF) << 8) | 0xFF, 0);
		C3D_FrameDrawOn(top);
		if (g_hud.mode != HUD_TITLE) render();

		HudInfo info = {
			.blink = fmaxf(0.0f, P.blinkTimer) / P.blinkFreq,
			.stamina = fmaxf(0.0f, P.stamina),
			.eyelid = P.dead ? 0 : P.dark,
			.dark = fmaxf(fmaxf(fmaxf(P.dead ? P.dark : 0, P.fade), eventsLightBlink() * 0.95f),
			              P.bloodloss > 35 ? (P.bloodloss - 35) / 130.0f : 0.0f),
			.crosshair = (usableDoor >= 0 || usableItem >= 0 || eventsHover()) && !P.dead,
			.crouch = P.crouch,
			.hasNav = itemsHave("nav"),
			.injuries = P.injuries,
			.bleeding = P.bloodloss > 0,
			.nvg = g_wear.nvg,
			.world = &world,
			.pos = P.pos,
			.yaw = P.yaw,
			.fps = fps,
			.debug = NULL,
		};
#ifdef DEBUG_TOOLS
		static char dbg[128];
		if (g_hud.mode == HUD_GAME) {
			char n[48], ev[48];
			npcDebugInfo(P.pos, n, sizeof(n));
			eventsDebugInfo(&world, P.pos, ev, sizeof(ev));
			snprintf(dbg, sizeof(dbg), "%.0f,%.1f,%.0f %s | %s L%luk ch%d", P.pos[0], P.pos[1], P.pos[2],
			         ev[0] ? ev : worldRoomAt(&world, P.pos), n, (unsigned long)(linearSpaceFree() / 1024),
			         audioBusyChannels());
			info.debug = dbg;
		}
#endif
		if (g_hud.mode == HUD_TITLE) hudDrawTitleTop(top);
		else if (g_hud.mode == HUD_ENDING) hudDrawEndingTop(top);
		else hudDrawTop(top, &info);
		HudAction act = hudDrawBottom(bottom, &info, (kDown & KEY_TOUCH) != 0, touch.px, touch.py);
		C2D_Flush();
		C3D_FrameEnd(0);
		handleHud(act);

		frames++;
		if (now - fpsTime >= 500) {
			fps = frames * 1000.0f / (now - fpsTime);
			frames = 0;
			fpsTime = now;
		}
	}

	audioExit();
	sceneExit();
	hudExit();
	C3D_Fini();
	romfsExit();
	gfxExit();
	return 0;
}
