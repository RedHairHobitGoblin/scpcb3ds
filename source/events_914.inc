static const char* s_914Settings[5] = { "rough", "coarse", "1:1", "fine", "very fine" };
static int s_914Setting = 2;

static int s_914Hover;
static int s_914Room = -1;
static Event* s_914Event;

static int nearestDoorTo(const World* w, const float p[3])
{
	int best = -1;
	float bd = 1.5f;
	for (int i = 0; i < w->ndoors; i++) {
		float dp[3] = { w->doors[i].x, w->doors[i].y, w->doors[i].z };
		float d = distXZ(dp, p);
		if (d < bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

static float aimAt(const EvPlayer* p, const float target[3], float reach)
{
	float d[3] = { target[0] - p->eye[0], target[1] - p->eye[1], target[2] - p->eye[2] };
	float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (l > reach || l < 1e-3f) return -1;
	float yr = *p->yaw * DEG2RAD, pr = *p->pitch * DEG2RAD;
	float f[3] = { sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr) };
	return (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / l;
}

static const char* ev914(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p)) {
		if (s_914Room == e->room) s_914Hover = 0;
		return NULL;
	}
	float key[3], knob[3], intake[3], output[3];
	if (!eventsRoomObject(w, e->room, 0, key) || !eventsRoomObject(w, e->room, 1, knob) ||
	    !eventsRoomObject(w, e->room, 2, intake) || !eventsRoomObject(w, e->room, 3, output)) {
		e->removed = true;
		return NULL;
	}
	s_914Room = e->room;
	s_914Event = e;
	s_914Hover = 0;
	if (e->state == 0) {
		float ak = aimAt(p, knob, 1.0f), ay = aimAt(p, key, 1.0f);
		if (ak > 0.93f || ay > 0.93f) s_914Hover = ak >= ay ? 1 : 2;
	}
	if (e->state == 0) return NULL;

	int din = nearestDoorTo(w, intake), dout = nearestDoorTo(w, output);
	float before = e->state;
	e->state += FRAMES(dt);
	const char* setting = s_914Settings[s_914Setting];
	if (dout >= 0 && w->doors[dout].open) worldToggleDoor(w, dout);
	if (before <= 70 * 2 && e->state > 70 * 2 && din >= 0 && w->doors[din].open) {
		sfxPlay3D("scp/914/doorclose", intake, 10.0f, 1.0f);
		w->doors[din].open = false;
	}
	bool inBooth = distXZ(p->pos, intake) < 170 * RSC;
	if (inBooth) {
		if ((!strcmp(setting, "rough") || !strcmp(setting, "coarse")) && before <= 70 * 2.6f && e->state > 70 * 2.6f)
			sfxPlay("scp/914/playerdeath", 1.0f);
		if (e->state > 70 * 3) {
			*p->blinkTimer = -10;
			if (before <= 70 * 3) {
				if (!strcmp(setting, "rough")) {
					return "\"A heavily mutilated corpse found inside the output booth of SCP-914. DNA testing identified the "
					       "corpse as Class D Subject D-9341. The subject had obviously been \"refined\" by SCP-914 on the "
					       "\"Rough\" setting, but we are still confused as to how he ended up inside the intake booth and who "
					       "or what wound the key.\"";
				}
				sfxPlay("scp/914/playeruse", 1.0f);
			}
		}
	}
	if (e->state > 12 * 70) {
		for (int i = 0; i < g_items.nitems; i++) {
			WorldItem* it = &g_items.items[i];
			if (it->active && distXZ(it->pos, intake) < 180 * RSC && fabsf(it->pos[1] - intake[1]) < 2.0f)
				items914(i, setting, output);
		}
		if (distXZ(p->pos, intake) < 160 * RSC) {
			if (!strcmp(setting, "coarse")) {
				*p->injuries = 4.0f;
				hudMessage("You notice countless small incisions all around your body. They are bleeding heavily.", 8);
			} else if (!strcmp(setting, "fine") || !strcmp(setting, "very fine")) {
				hudMessage("You feel strangely energetic.", 6);
			}
			p->pos[0] = output[0];
			p->pos[1] = output[1] + 0.5f;
			p->pos[2] = output[2];
		}
		for (int k = 0; k < 2; k++) {
			int d = k == 0 ? din : dout;
			if (d >= 0 && !w->doors[d].open) worldToggleDoor(w, d);
		}
		sfxPlay3D("scp/914/dooropen", intake, 10.0f, 1.0f);
		sfxPlay3D("scp/914/dooropen", output, 10.0f, 1.0f);
		e->state = 0;
	}
	return NULL;
}

static bool interact914(World* w)
{
	if (s_914Hover == 0 || s_914Room < 0) return false;
	char msg[48];
	if (s_914Hover == 1) {
		s_914Setting = (s_914Setting + 1) % 5;
		snprintf(msg, sizeof(msg), "The knob is now set to %s.", s_914Settings[s_914Setting]);
		sfxPlay("interact/leverflip", 1.0f);
		hudMessage(msg, 3);
		return true;
	}
	Event* e = s_914Event;
	if (e && e->state == 0) {
		float knob[3];
		eventsRoomObject(w, e->room, 1, knob);
		e->state = 1;
		sfxPlay3D("scp/914/refining", knob, 15.0f, 1.0f);
		hudMessage("You wind the key.", 3);
	}
	return true;
}
