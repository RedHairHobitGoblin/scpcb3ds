extern float g_playerNoise;
extern float g_playerInjuries;
extern float g_staminaDrain;

static void hurt(float amount) { g_playerInjuries += amount; }

static const char* update939(World* w, Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	float head[3];
	centre(n, 0.8f, head);
	int voice = (int)(n - s_npcs) % 3;
	char snd[32];
	switch (n->state) {
	case 0:
		npcAnimate(n, 290, 405, 0.1f, true, dt);
		break;
	case 1:
		n->currSpeed = curve(0, n->currSpeed, 5.0f, dt);
		npcAnimate(n, 175, 297, 0.22f, false, dt);
		if (n->frame >= 296) n->state = 2;
		break;
	case 2:
		n->currSpeed = curve(n->speed * 0.3f, n->currSpeed, 10.0f, dt);
		npcAnimate(n, 644, 683, 28 * n->currSpeed, true, dt);
		npcWalkTo(w, n, n->goal, FRAMES(n->currSpeed), dt);
		if (distXZ(n->pos, n->goal) < 0.5f || n->timer <= 0) {
			n->goal[0] = n->pos[0] + frand(-4, 4);
			n->goal[2] = n->pos[2] + frand(-4, 4);
			n->goal[1] = n->pos[1];
			n->timer = 70 * 6;
		}
		n->timer -= FRAMES(dt);
		if (rand() % 700 == 0 && !sfxPlaying(n->chan)) {
			snprintf(snd, sizeof(snd), "scp/939/%dlure%d", voice, 1 + rand() % 10);
			n->chan = sfxPlay3D(snd, head, 15.0f, 1.0f);
		}
		break;
	case 3:
		if (dist > 1.0f) {
			n->currSpeed = curve(n->speed, n->currSpeed, 20.0f, dt);
			npcAnimate(n, 449, 464, 6 * n->currSpeed, true, dt);
			npcWalkTo(w, n, pv->pos, FRAMES(n->currSpeed), dt);
		} else {
			n->currSpeed = 0;
			npcFace(n, pv->pos, 10, dt);
			npcAnimate(n, 18, 68, 0.5f, true, dt);
			if (crossed(n, 40)) {
				sfxPlay("general/slash1", 1.0f);
				hurt(frand(1.5f, 2.5f) - g_wear.vest * 0.5f);
			}
		}
		if (dist > 8.0f && g_playerNoise < 1.0f) n->state = 2;
		break;
	}

	if (n->state < 3 && !pv->dead) {
		float d = dist - (dist < 4.0f && worldLineOfSight(w, pv->eye, head) ? 1.0f : 0.0f);
		if (g_playerNoise * 1.2f > d || d < 1.5f) {
			snprintf(snd, sizeof(snd), "scp/939/%dattack%d", voice, 1 + rand() % 3);
			n->chan = sfxPlay3D(snd, head, 15.0f, 1.0f);
			sfxPlay("scp/939/attack", 1.0f);
			n->state = 3;
		} else if (g_playerNoise * 1.6f > d && n->state == 0) {
			snprintf(snd, sizeof(snd), "scp/939/%dalert%d", voice, 1 + rand() % 3);
			n->chan = sfxPlay3D(snd, head, 15.0f, 1.0f);
			n->state = 1;
		}
	}
	return NULL;
}

static const char* update966(World* w, Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	float head[3];
	centre(n, 1.4f, head);
	n->hidden = g_wear.nvg == 0;
	switch (n->state) {
	case 0:
		npcAnimate(n, 2, 214, 0.25f, true, dt);
		if (dist < 16 && rand() % 400 == 0) n->state = 1 + rand() % 6;
		break;
	case 1:
	case 2:
		npcAnimate(n, 214, 257, 0.25f, false, dt);
		if (crossed(n, 220)) {
			char snd[24];
			snprintf(snd, sizeof(snd), "scp/966/echo%d", 1 + rand() % 3);
			sfxPlay3D(snd, head, 10.0f, 1.0f);
			if (dist < 8) {
				g_staminaDrain = 2.0f;
				static const char* msgs[] = { "You feel exhausted.", "You can't seem to stay awake.",
				                              "You feel like you haven't slept in days.", "Your eyelids feel heavy." };
				hudMessage(msgs[rand() % 4], 5);
			}
		}
		if (n->frame > 256) n->state = 0;
		break;
	case 3:
	case 4:
		npcFace(n, pv->pos, 4, dt);
		npcAnimate(n, 257, 332, 0.25f, false, dt);
		if (n->frame > 331) n->state = 0;
		break;
	default:
		if (dist < 0.8f) {
			npcFace(n, pv->pos, 10, dt);
			npcAnimate(n, 332, 457, 0.4f, true, dt);
			if (crossed(n, 380)) {
				sfxPlay("general/slash2", 1.0f);
				hurt(frand(0.5f, 1.0f));
			}
		} else {
			n->currSpeed = curve(n->speed, n->currSpeed, 10.0f, dt);
			npcAnimate(n, 580, 628, n->currSpeed * 25.0f, true, dt);
			npcWalkTo(w, n, pv->pos, FRAMES(n->currSpeed), dt);
			if (dist > 20 || rand() % 900 == 0) n->state = 0;
		}
		break;
	}
	return NULL;
}

static const char* updateZombie(World* w, Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	bool surgeon = n->type == NPC_SURGEON;
	switch (n->state) {
	case 0:
		if (surgeon) npcAnimate(n, 1, 10, 0.1f, true, dt);
		else npcAnimate(n, 719, 777, 0.2f, false, dt);
		if (dist < 5.0f) n->state = 1;
		break;
	case 1:
		if (surgeon) {
			n->state = 2;
		} else {
			npcAnimate(n, 155, 682, 1.5f, false, dt);
			if (n->frame >= 681) n->state = 2;
		}
		break;
	case 2:
		if (dist < 0.7f) {
			n->state = 3;
			n->frame = surgeon ? 100 : 2;
		} else if (dist < 20) {
			n->currSpeed = curve(n->speed, n->currSpeed, 20.0f, dt);
			if (surgeon) npcAnimate(n, 11, 70, n->currSpeed * 40, true, dt);
			else npcAnimate(n, 936, 1017, n->currSpeed * 60, true, dt);
			npcWalkTo(w, n, pv->pos, FRAMES(n->currSpeed), dt);
			if (rand() % 900 == 0) sfxPlay3D("scp/049/0492breath", n->pos, 8.0f, 1.0f);
		} else if (!surgeon) {
			npcAnimate(n, 778, 926, 0.1f, true, dt);
		}
		break;
	case 3:
		npcFace(n, pv->pos, 8, dt);
		if (surgeon) npcAnimate(n, 100, 160, 0.7f, false, dt);
		else npcAnimate(n, 2, 65, 0.7f, false, dt);
		if (crossed(n, surgeon ? 130 : 30) && dist < 1.0f) {
			char snd[32];
			snprintf(snd, sizeof(snd), "ambient/general/damage%d", 5 + rand() % 4);
			sfxPlay(snd, 1.0f);
			hurt(frand(0.4f, 1.0f));
			if (surgeon) g_wear.infect = fmaxf(g_wear.infect, 1.0f);
		}
		if (n->frame >= (surgeon ? 159 : 64)) n->state = 2;
		break;
	}
	return NULL;
}

static const char* updateMTF(World* w, Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	float head[3];
	centre(n, 1.1f, head);
	bool sees = dist < 14.0f && worldLineOfSight(w, head, pv->eye);
	n->timer -= FRAMES(dt);
	if (sees) {
		if (n->state != 2) {
			char snd[32];
			snprintf(snd, sizeof(snd), "character/mtf/stop%d", 1 + rand() % 2);
			sfxPlay3D(snd, head, 20.0f, 1.0f);
			n->state = 2;
			n->state2 = 70 * 2;
		}
		npcFace(n, pv->pos, 8, dt);
		npcAnimate(n, 345, 357, 0.2f, false, dt);
		n->state2 -= FRAMES(dt);
		if (n->state2 <= 0) {
			sfxPlay3D("general/gunshot", head, 30.0f, 1.0f);
			if (rand() % 3 != 0) {
				sfxPlay("general/bullethit", 1.0f);
				hurt(frand(0.6f, 1.2f) - g_wear.vest * 0.3f);
			} else {
				sfxPlay("general/bulletmiss", 0.8f);
			}
			n->state2 = 70 * frand(0.6f, 1.2f);
		}
		memcpy(n->goal, pv->pos, sizeof(n->goal));
	} else {
		if (n->state == 2) n->state = 1;
		if (n->state == 1 || n->state == 0) {
			if (n->state == 0 || distXZ(n->pos, n->goal) < 0.5f) {
				memcpy(n->goal, pv->pos, sizeof(n->goal));
				n->state = 1;
			}
			n->currSpeed = curve(n->speed, n->currSpeed, 20.0f, dt);
			npcAnimate(n, 1, 38, n->currSpeed * 40, true, dt);
			npcWalkTo(w, n, n->goal, FRAMES(n->currSpeed), dt);
			if (n->timer <= 0 && !sfxPlaying(n->chan)) {
				char snd[32];
				snprintf(snd, sizeof(snd), "character/mtf/searching%d", 1 + rand() % 6);
				n->chan = sfxPlay3D(snd, head, 20.0f, 1.0f);
				n->timer = 70 * frand(15, 30);
			}
		}
	}
	return NULL;
}

static const char* update066(World* w, Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	npcAnimate(n, 1, 680, 0.6f, true, dt);
	if (dist < 6.0f) {
		npcWalkTo(w, n, pv->pos, FRAMES(n->speed * 0.6f), dt);
		if (dist < 2.0f && n->timer <= 0) {
			sfxPlay3D("scp/066/beethoven", n->pos, 15.0f, 1.0f);
			n->timer = 70 * 20;
		}
	}
	n->timer -= FRAMES(dt);
	return NULL;
}

static const char* update372(World* w, Npc* n, const PlayerView* pv, float dt)
{
	(void)w;
	npcAnimate(n, 1, 300, 1.0f, true, dt);
	n->timer -= FRAMES(dt);
	if (n->timer <= 0) {
		float yr = (pv->yaw + (rand() % 2 ? 60 : -60)) * DEG2RAD;
		n->pos[0] = pv->pos[0] + sinf(yr) * 1.5f;
		n->pos[1] = pv->pos[1] + frand(0, 1.0f);
		n->pos[2] = pv->pos[2] - cosf(yr) * 1.5f;
		n->state2 = (float)(rand() % 2 ? 1 : -1);
		n->timer = 70 * frand(40, 90);
		n->hidden = false;
		char snd[24];
		snprintf(snd, sizeof(snd), "scp/372/rustle%d", rand() % 3);
		sfxPlay3D(snd, n->pos, 5.0f, 0.6f);
	}
	if (!n->hidden) {
		n->pos[0] += cosf(pv->yaw * DEG2RAD) * n->state2 * 6.0f * dt;
		n->pos[2] += sinf(pv->yaw * DEG2RAD) * n->state2 * 6.0f * dt;
		if (n->timer < 70 * 38) n->hidden = true;
	}
	return NULL;
}

static const char* update5131(World* w, Npc* n, const PlayerView* pv, float dt)
{
	(void)w;
	(void)dt;
	npcAnimate(n, 1, 260, 0.2f, true, dt);
	n->timer -= FRAMES(dt);
	if (n->timer <= 0) {
		float yr = (pv->yaw + 180 + frand(-50, 50)) * DEG2RAD;
		n->pos[0] = pv->pos[0] + sinf(yr) * 3.0f;
		n->pos[1] = pv->pos[1] - 0.3f;
		n->pos[2] = pv->pos[2] - cosf(yr) * 3.0f;
		n->yaw = atan2f(-(pv->pos[0] - n->pos[0]), -(pv->pos[2] - n->pos[2])) / DEG2RAD;
		n->hidden = false;
		n->timer = 70 * frand(20, 40);
	}

	float dx = n->pos[0] - pv->pos[0], dz = n->pos[2] - pv->pos[2], l = sqrtf(dx * dx + dz * dz);
	float yr = pv->yaw * DEG2RAD;
	if (!n->hidden && l > 0.1f && (dx * sinf(yr) - dz * cosf(yr)) / l > 0.7f) {
		n->hidden = true;
		sfxPlay("horror/horror4", 0.8f);
	}
	return NULL;
}

static const char* update1048a(World* w, Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	npcAnimate(n, 1, 100, 0.3f, true, dt);
	if (dist > 1.0f && dist < 12.0f) npcWalkTo(w, n, pv->pos, FRAMES(n->speed), dt);
	if (dist < 1.0f) {
		n->timer -= FRAMES(dt);
		if (n->timer <= 0) {
			sfxPlay3D("scp/1048a/shriek", n->pos, 10.0f, 1.0f);
			hurt(0.1f);
			n->timer = 70 * 6;
		}
	}
	return NULL;
}

static float yawToPlayer(const Npc* n, const PlayerView* pv)
{
	float want = atan2f(-(pv->pos[0] - n->pos[0]), -(pv->pos[2] - n->pos[2])) / DEG2RAD;
	return fabsf(fmodf(want - n->yaw + 540.0f, 360.0f) - 180.0f);
}

static const char* updateTentacle(Npc* n, const PlayerView* pv, float dt)
{
	float dist = dist3(n->pos, pv->pos);
	if (dist > HIDE_DISTANCE) return NULL;
	switch (n->state) {
	case 0:
		n->hidden = n->frame < 283;
		if (n->frame < 283) {
			if (dist < 2.5f) {
				n->frame = 284;
				sfxPlay("room/035chamber/tentaclespawn", 1.0f);
			}
			break;
		}
		npcFace(n, pv->pos, 25.0f / 70.0f * 10, dt);
		npcAnimate(n, 283, 389, 0.3f, false, dt);
		if (n->frame > 388) n->state = 1;
		break;
	case 1:
		sfxLoop3D(&n->chan2, "room/035chamber/tentacleidle", n->pos, 6.0f, 1.0f);
		if (dist < 1.8f && yawToPlayer(n, pv) < 20) n->state = 2;
		npcFace(n, pv->pos, 25.0f / 70.0f * 10, dt);
		npcAnimate(n, 33, 174, 0.3f, true, dt);
		break;
	case 2:
		if (n->frame > 33 && n->frame < 174) {
			npcAnimate(n, 33, 174, 2.0f, false, dt);
			break;
		}
		npcFace(n, pv->pos, 10.0f / 70.0f * 10, dt);
		if (n->frame > 33) {
			sfxStop(n->chan2);
			n->chan2 = -1;
			n->frame = 2;
			sfxPlay(rand() % 2 ? "room/035chamber/tentacleattack1" : "room/035chamber/tentacleattack2", 1.0f);
		}
		npcAnimate(n, 2, 32, 0.3f, false, dt);
		if (crossed(n, 5) && dist < 1.8f && yawToPlayer(n, pv) < 20) {
			float dmg = frand(1.0f, 1.5f) - g_wear.vest * 0.5f;
			hurt(dmg);
			sfxPlay(rand() % 2 ? "ambient/general/damage2" : "ambient/general/damage3", 1.0f);
			if (pv->injuries + dmg > 3.0f)
				return "\"We will need more than the regular cleaning team to take care of this. Large and highly active "
				       "tentacle-like appendages seem to have formed here. Their level of aggression is unlike anything we've "
				       "seen before - it looks like they have beaten some unfortunate Class D to death at some point during "
				       "the breach.\"";
		}
		if (n->frame >= 32) {
			n->state = 1;
			n->frame = 173;
		}
		break;
	}
	return NULL;
}
