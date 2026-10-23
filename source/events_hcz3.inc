static const char* defName(const Event* e);

static bool s_remoteDoorOn = true;
static bool s_gatesOpened;

static void keepShut(World* w, int d, const EvPlayer* p)
{
	if (d < 0) return;
	Door* door = &w->doors[d];
	float dp[3] = { door->x, door->y, door->z };
	if (door->open && (door->openstate > 50 || distXZ(p->pos, dp) < 0.5f)) {
		door->openstate = fminf(door->openstate, 50);
		door->open = false;
		sfxPlay3D("door/doorerror", dp, 8.0f, 1.0f);
	}
}

static void gateDoors(World* w, EvPlayer* p)
{
	int ex = findRoom(w, "exit1");
	if (ex >= 0) {
		int d = worldRoomDoor(w, ex, 4);
		if (d >= 0) {
			w->doors[d].locked = !s_remoteDoorOn;
			if (s_remoteDoorOn && !s_gatesOpened) keepShut(w, d, p);
		}
	}
	int ga = findRoom(w, "gateaentrance");
	if (ga >= 0 && worldRoomIndexAt(w, p->pos) == ga) {
		int d = worldRoomDoor(w, ga, 1);
		if (d >= 0) {
			w->doors[d].locked = !s_remoteDoorOn;
			if (s_remoteDoorOn && !s_gatesOpened) keepShut(w, d, p);
		}
	}
}

static const char* evRoom2ccont(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p)) return NULL;
	Lever* lights = lever(e->room, 1);
	if (lights && e->state == 0 && aimAt(p, lights->pos, 1.5f) > 0.9f) {
		e->state = 1;
		sfxPlay("horror/horror7", 1.0f);
		sfxPlay("interact/leverflip", 1.0f);
	}
	bool on = leverOn(e->room, 1);
	if ((on ? 1 : 2) != (int)e->state2 && e->state2 != 0 && e->state > 0 && lights) playAt("general/lightswitch", lights->pos, 6.0f);
	e->state2 = on ? 1 : 2;
	s_remoteDoorOn = leverOn(e->room, 2);
	if (e->state > 0 && e->state < 200) {
		e->state += FRAMES(dt);
		if (lights) lights->pitch = curveValue(-85, lights->pitch, 5, dt);
	}
	return NULL;
}

static const char* evRoom079(World* w, Event* e, EvPlayer* p, float dt)
{
	float o0[3];
	if (!eventsRoomObject(w, e->room, 0, o0)) {
		e->removed = true;
		return NULL;
	}
	if (inRoom(w, e, p)) {
		if (e->state == 0) {
			float o2[3];
			if (eventsRoomObject(w, e->room, 2, o2)) {
				float c[3];
				roomCentre(w, e->room, c);
				spawnBody(w, NPC_GUARD, o2, yawTo(o2, c), 288);
			}
			e->state = 1;
		}
		if (s_remoteDoorOn) {
			keepShut(w, worldRoomDoor(w, e->room, 0), p);
		} else if (e->state < 10000) {
			if (e->state == 1) {
				e->state = 2;
			} else if (e->state == 2) {
				if (dist3(o0, p->pos) < 3.0f) {
					e->state = 3;
					e->state2 = 1;
					sfxStop(e->chan);
					e->chan = sfxPlay("scp/079/speech", 1.0f);
				}
			} else if (e->state < 2000) {
				if (!sfxPlaying(e->chan)) e->state += FRAMES(dt);
			} else if (dist3(o0, p->pos) < 2.5f) {
				e->state = 10001;
				sfxStop(e->chan);
				e->chan = sfxPlay("scp/079/refuse", 1.0f);
			}
		}
	}
	if (e->state2 == 1 && s_remoteDoorOn) {
		sfxStop(e->chan);
		e->chan = sfxPlay("scp/079/gateb", 1.0f);
		e->state2 = 2;
		s_gatesOpened = true;
	}
	return NULL;
}

static const char* evRoom012(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p)) return NULL;
	int d0 = worldRoomDoor(w, e->room, 0);
	float o2[3], o3[3];
	if (!eventsRoomObject(w, e->room, 2, o2) || !eventsRoomObject(w, e->room, 3, o3) || d0 < 0) {
		e->removed = true;
		return NULL;
	}
	Door* door = &w->doors[d0];
	float dp[3] = { door->x, door->y, door->z };
	if (e->state == 0) {
		if (dist3(p->pos, dp) < 2.5f && s_remoteDoorOn) {
			sfxPlay("horror/horror7", 1.0f);
			playAt("interact/leverflip", dp, 6.0f);
			e->state = 1;
			if (!door->open) worldToggleDoor(w, d0);
			door->locked = true;
		}
		return NULL;
	}
	sfxLoop3D(&e->chan, "music/012golgotha", o3, 5.0f, 1.0f);
	if (e->state < 90) e->state = curveValue(90, e->state, 500, dt);
	float ry = w->map.rooms[e->room].y;
	float box[3] = { o2[0], ry + (-130 - 448 * sinf(e->state * DEG2RAD)) * RSC, o2[2] };
	if (g_wear.ring714 || g_wear.gasMask >= 3 || g_wear.hazmat >= 3) return NULL;

	if (worldLineOfSight(w, p->eye, box)) {
		sfxLoop3D(&e->chan2, "music/012", o3, 10.0f, fmaxf(0.05f, e->state3 / (86.0f * 70.0f)));
		float rate = fmaxf(1.0f, 80 - e->state3 / 200.0f);
		float wantYaw = atan2f(box[0] - p->pos[0], -(box[2] - p->pos[2])) / DEG2RAD;
		float dyaw = wantYaw - *p->yaw;
		while (dyaw > 180) dyaw -= 360;
		while (dyaw < -180) dyaw += 360;
		*p->yaw += dyaw * fminf(1.0f, FRAMES(dt) / rate);
		float hd = distXZ(p->pos, box);

		float wantPitch = atan2f(box[1] - 0.05f - p->eye[1], fmaxf(hd, 0.01f)) / DEG2RAD - 25;
		*p->pitch += (wantPitch - *p->pitch) * fminf(1.0f, FRAMES(dt) / rate);
		if (hd < 0.6f) {
			float prev = e->state3;
			e->state3 = fminf(e->state3 + FRAMES(dt), 86 * 70);
#define AT(t) (e->state3 > (t) * 70 && prev <= (t) * 70)
			if (AT(1)) sfxPlay("scp/012/speech1", 1.0f);
			else if (AT(13)) {
				hudMessage("You start pushing your nails into your wrist, drawing blood.", 7);
				*p->injuries += 0.5f;
				sfxPlay("scp/012/speech2", 1.0f);
			} else if (AT(31)) {
				hudMessage("You tear open your left wrist and start writing on the composition with your blood.", 7);
				*p->injuries = fmaxf(*p->injuries, 1.5f);
				sfxPlay(rand() % 2 ? "scp/012/speech3" : "scp/012/speech4", 1.0f);
			} else if (AT(49)) {
				hudMessage("You push your fingers deeper into the wound.", 8);
				*p->injuries += 0.3f;
				sfxPlay("scp/012/speech5", 1.0f);
			} else if (AT(63)) {
				*p->injuries += 0.5f;
				sfxPlay("scp/012/speech6", 1.0f);
			} else if (AT(74)) {
				hudMessage("You rip the wound wide open. Grabbing scoops of blood pouring out.", 7);
				*p->injuries += 0.8f;
				sfxPlay("scp/012/speech7", 1.0f);
			} else if (AT(85)) {
				return "Subject D-9341 found in a pool of blood next to SCP-012. Subject seems to have ripped open his "
				       "wrists and written three extra lines to the composition before dying of blood loss.";
			}
#undef AT
		} else if (fabsf(dyaw) < 40) {
			float k = (40 - fabsf(dyaw)) * 0.02f * 0.025f * FRAMES(dt);
			p->pos[0] += (box[0] - p->pos[0]) / hd * k;
			p->pos[2] += (box[2] - p->pos[2]) / hd * k;
		}
	} else if (distXZ(p->pos, dp) < 4.5f && p->pos[1] - ry < -2.5f) {
		float wantYaw = atan2f(dp[0] - p->pos[0], -(dp[2] - p->pos[2])) / DEG2RAD;
		float dyaw = wantYaw - *p->yaw;
		while (dyaw > 180) dyaw -= 360;
		while (dyaw < -180) dyaw += 360;
		*p->yaw += dyaw * fminf(1.0f, FRAMES(dt) / 150);
		*p->pitch += (-90 - *p->pitch) * fminf(1.0f, FRAMES(dt) / 100);
		if (fabsf(dyaw) < 40) {
			float hd = fmaxf(distXZ(p->pos, dp), 0.01f), k = (40 - fabsf(dyaw)) * 0.008f * 0.025f * FRAMES(dt);
			p->pos[0] += (dp[0] - p->pos[0]) / hd * k;
			p->pos[2] += (dp[2] - p->pos[2]) / hd * k;
		}
	}
	return NULL;
}

static void say035(Event* e, const char* line)
{
	char snd[40];
	snprintf(snd, sizeof(snd), "scp/035/%s", line);
	sfxStop(e->chan2);
	e->chan2 = sfxPlay(snd, 1.0f);
}

static const char* evRoom035(World* w, Event* e, EvPlayer* p, float dt)
{
	float o3[3], o4[3];
	if (!eventsRoomObject(w, e->room, 3, o3) || !eventsRoomObject(w, e->room, 4, o4)) {
		e->removed = true;
		return NULL;
	}
	int door[4];
	for (int i = 0; i < 4; i++) door[i] = worldRoomDoor(w, e->room, i);
	if (!inRoom(w, e, p)) {
		if (e->state == 0 && e->state4 == 0 && roomDist(w, e, p) < 20) {
			sfxPlay("room/035chamber/inproximity", 1.0f);
			e->state4 = 1;
		}
		return NULL;
	}
	Npc* h = npcGet(e->npc[0]);
	if (h && h->type != NPC_035) h = NULL;
	if (!h && e->state > 0) h = npcFind(NPC_035);
	if (h) e->npc[0] = npcIndex(h);

	if (e->state == 0) {
		if (dist3(p->pos, o3) < 2) {
			float sp[3] = { o4[0], o4[1] - 0.32f, o4[2] };
			h = npcGet(npcSpawn(NPC_035, sp, (float)w->map.rooms[e->room].angle + 270));
			if (h) {
				h->scripted = true;
				h->frame = 501;
				npcSnapToFloor(w, h);
				e->npc[0] = npcIndex(h);
			}
			e->state = 1;
		}
		return NULL;
	}
	if (e->state > 0 && !h) return NULL;

	if (e->state > 0) {
		if (e->state == 1) {
			if (dist3(p->pos, o3) < 1.2f && inView(p, h->pos)) {
				sfxPlay("scp/035/getup", 1.0f);
				e->state = 1.5f;
			}
			return NULL;
		}
		if (door[3] >= 0 && w->doors[door[3]].open) e->state2 = fmaxf(e->state2, 1);
		leverLock(e->room, 0, e->state2 == 20);
		if (!leverOn(e->room, 0)) {
			bool gas = leverOn(e->room, 1);
			if (gas || (e->state3 > 25 * 70 && e->state3 < 50 * 70)) {
				if (e->state3 > -30 * 70) {
					float prev = fabsf(e->state3);
					e->state3 = prev + FRAMES(dt);
					if (e->state3 > 1 && prev <= 1) {
						say035(e, "gased1");
					} else if (e->state3 > 15 * 70 && e->state3 < 25 * 70) {
						if (prev <= 15 * 70) {
							say035(e, "gased2");
							h->frame = 553;
						}
						h->frame = fmaxf(529, h->frame - 0.12f * FRAMES(dt));
					} else if (e->state3 > 25 * 70 && e->state3 < 35 * 70) {
						h->frame = fmaxf(524, h->frame - 0.08f * FRAMES(dt));
					} else if (e->state3 > 35 * 70) {
						npcAnimate(h, 524, 553, 0.08f, false, dt);
						if (prev <= 35 * 70) {
							say035(e, "gasedkilled1");
							sfxPlay("scp/035/killedgetup", 1.0f);
							e->state = 60 * 70;
						}
					}
				}
			} else {
				if (h->frame >= 501 && h->frame < 523) npcAnimate(h, 501, 523, 0.08f, false, dt);
				else if (h->frame >= 524 && h->frame < 553) npcAnimate(h, 524, 553, 0.08f, false, dt);
				else npcFace(h, p->pos, 15.0f / 70.0f * 10, dt);
				if (e->state3 > 0) {
					e->state3 = -e->state3;
					if (e->state3 < -35 * 70) {
						say035(e, "gasedkilled2");
						e->state = 60 * 70;
					} else {
						if (e->state3 < -20 * 70) {
							say035(e, "gasedstop2");
						} else {
							e->state3 = -21 * 70;
							say035(e, "gasedstop1");
						}
						e->state = 61 * 70;
					}
				} else {
					float prev = e->state;
					e->state += FRAMES(dt);
#define AT(t) (e->state > (t) * 70 && prev <= (t) * 70)
					if (AT(4)) {
						say035(e, "help1");
						e->state = 10 * 70;
					} else if (AT(20)) {
						say035(e, "help2");
					} else if (AT(40)) {
						say035(e, "idle1");
					} else if (AT(50)) {
						say035(e, "idle2");
					} else if (AT(80)) {
						if (e->state2) e->state = 130 * 70;
						else if (e->state3 < -30 * 70 || e->state3 != 0) say035(e, "gasedcloset");
						else say035(e, "closet1");
					} else if (e->state > 80 * 70) {
						if (e->state2) e->state = fmaxf(e->state, 100 * 70);
						if (AT(110)) {
							if (e->state2) {
								say035(e, "closet2");
								e->state = 130 * 70;
							} else {
								say035(e, "idle3");
							}
						} else if (AT(125)) {
							say035(e, e->state2 ? "closet2" : "idle4");
						} else if (AT(150)) {
							say035(e, "idle5");
						} else if (AT(200)) {
							say035(e, "idle6");
						}
					}
#undef AT
				}
			}
		} else {
			if (e->state2 < 10) {
				if (door[2] >= 0) {
					if (w->doors[door[2]].open) worldToggleDoor(w, door[2]);
					w->doors[door[2]].locked = true;
				}
				if (door[1] >= 0 && !w->doors[door[1]].open) worldToggleDoor(w, door[1]);
				if (e->state3 == 0) say035(e, "escape");
				else if (fabsf(e->state3) > 35 * 70) say035(e, "killedescape");
				else say035(e, "gasedescape");
				e->state2 = 20;
			}
			if (e->state2 == 20 && door[0] >= 0 && door[1] >= 0) {
				float d0p[3] = { w->doors[door[0]].x, w->doors[door[0]].y, w->doors[door[0]].z };
				float d1p[3] = { w->doors[door[1]].x, w->doors[door[1]].y, w->doors[door[1]].z };
				float dist = distXZ(d0p, h->pos);
				h->currSpeed = 0.012f;
				if (dist > 2.5f) {
					npcAnimate(h, 1, 38, h->currSpeed * 40, true, dt);
					npcWalkTo(w, h, d1p, FRAMES(h->currSpeed), dt);
				} else if (dist > 0.7f) {
					if (sfxPlaying(e->chan2)) {
						npcFace(h, p->pos, 2.0f, dt);
					} else {
						npcAnimate(h, 1, 38, h->currSpeed * 40, true, dt);
						npcWalkTo(w, h, d0p, FRAMES(h->currSpeed), dt);
					}
				} else {
					npcRemove(e->npc[0]);
					e->npc[0] = -1;
					e->state = -1;
					e->state2 = 0;
					e->state3 = 0;
					for (int i = 0; i < 3; i++)
						if (door[i] >= 0) w->doors[door[i]].locked = false;
					worldToggleDoor(w, door[1]);
				}
			}
		}
		return NULL;
	}

	float o7[3], o8[3];
	if (!eventsRoomObject(w, e->room, 7, o7) || !eventsRoomObject(w, e->room, 8, o8)) return NULL;
	bool inside = p->pos[0] > fminf(o7[0], o8[0]) && p->pos[0] < fmaxf(o7[0], o8[0]) &&
	              p->pos[2] > fminf(o7[2], o8[2]) && p->pos[2] < fmaxf(o7[2], o8[2]);
	if (inside) {
		float ry = w->map.rooms[e->room].y;
		Npc* t0 = npcGet(e->npc[1]);
		if (!t0 || t0->type != NPC_TENTACLE) {
			float o4[3];
			eventsRoomObject(w, e->room, 4, o4);
			o4[1] = ry;
			e->npc[1] = npcSpawn(NPC_TENTACLE, o4, 0);
		} else if (t0->state > 0) {
			Npc* t1 = npcGet(e->npc[2]);
			if (!t1 || t1->type != NPC_TENTACLE) {
				float c[3];
				roomCentre(w, e->room, c);
				c[1] = ry;
				e->npc[2] = npcSpawn(NPC_TENTACLE, c, (float)w->map.rooms[e->room].angle);
			}
		}
		e->state2 = fminf(e->state2 + FRAMES(dt) / 6000, 1.0f);
		e->state3 = curveValue(e->state2, e->state3, 50, dt);
		*p->injuries += FRAMES(dt) / (g_wear.hazmat ? 10000.0f : 5000.0f);
	} else {
		e->state2 = fmaxf(e->state2 - FRAMES(dt) / 2000, 0);
		e->state3 = fmaxf(e->state3 - FRAMES(dt) / 100, 0);
	}
	if (e->state3 > 0 && !g_wear.ring714 && g_wear.hazmat < 3 && g_wear.gasMask < 3) {
		float c[3];
		roomCentre(w, e->room, c);
		sfxLoop3D(&e->chan, "room/035chamber/whispers1", c, 10.0f, e->state3);
		if (e->state3 > 0.5f) sfxLoop3D(&e->chan2, "room/035chamber/whispers2", c, 10.0f, (e->state3 - 0.5f) * 2);
	}
	if (inside && *p->injuries > 3.9f)
		return "Class D Subject D-9341 found dead inside SCP-035's containment chamber. The subject exhibits heavy "
		       "hemorrhaging of blood vessels around the eyes and inside the mouth and nose. Sent for autopsy.";
	return NULL;
}

static const char* evCoffin(World* w, Event* e, EvPlayer* p, float dt)
{
	if (!inRoom(w, e, p)) return NULL;
	float o1[3];
	if (!eventsRoomObject(w, e->room, 1, o1)) {
		e->removed = true;
		return NULL;
	}
	float ry = w->map.rooms[e->room].y, pit = ry - 1531 * RSC;
	float d = dist3(p->pos, o1);
	Npc* g = npcGet(e->npc[0]);
	if (g && g->type != NPC_GUARD) g = NULL;
	if (d < 1.5f) {
		Npc* o = npcFind(NPC_106);
		if (!s_contained106 && !strcmp(defName(e), "coffin106") && e->state2 == 0 && o && o->hidden && !o->scripted) {
			o->pos[0] = o1[0];
			o->pos[1] = pit;
			o->pos[2] = o1[2];
			o->hidden = false;
			o->frame = 110;
			o->state = 1;
			e->state2 = 1;
		}
	} else if (d < 3.0f) {
		if (!g && e->state3 == 0) {
			float c[3];
			roomCentre(w, e->room, c);
			c[1] = ry;
			e->npc[0] = npcSpawn(NPC_GUARD, c, (float)w->map.rooms[e->room].angle + 90);
			g = npcGet(e->npc[0]);
			if (g) {
				g->scripted = true;
				g->dead = true;
				g->frame = 270;
				char snd[40];
				snprintf(snd, sizeof(snd), "room/895chamber/guardidle%d", 1 + rand() % 3);
				e->chan = sfxPlay3D(snd, g->pos, 10.0f, 1.0f);
			}
			e->state3 = 1;
		}
	} else if (d > 5.0f && g && e->state3 == 1) {
		sfxStop(e->chan);
		char snd[40];
		snprintf(snd, sizeof(snd), "room/895chamber/guardscream%d", 1 + rand() % 3);
		e->chan = sfxPlay3D(snd, g->pos, 100.0f, 1.0f);
		e->state3 = 2;
		g->state2 = 0;
		g->currSpeed = 0;
	}
	if (g) {
		if (e->state3 == 2) {
			g->state2 += FRAMES(dt);
			if (g->state2 >= 70) {
				g->currSpeed += 9.8f * dt;
				g->pos[1] -= g->currSpeed * dt;
			}
			if (g->pos[1] <= pit) {
				g->pos[1] = pit;
				sfxPlay3D("general/bodyfall", g->pos, 15.0f, 1.0f);
				e->state3 = 3;
			}
		} else if (e->state3 == 3) {
			npcAnimate(g, 270, 286, 0.4f, false, dt);
			sfxLoop3D(&e->chan2, "room/895chamber/guardradio", g->pos, 5.0f, 1.0f);
		}
	}

	if (g_wear.nvg && g_wear.nvgBattery > 0 && d < 4.0f && !g_wear.ring714) {
		float wantYaw = atan2f(o1[0] - p->pos[0], -(o1[2] - p->pos[2])) / DEG2RAD;
		float dyaw = wantYaw - *p->yaw;
		while (dyaw > 180) dyaw -= 360;
		while (dyaw < -180) dyaw += 360;
		*p->yaw += dyaw * fminf(1.0f, FRAMES(dt) / 4);
		float hd = distXZ(p->pos, o1);
		float wantPitch = 60.0f * fminf(fmaxf((2.0f - hd) / 2.0f, 0), 1);
		*p->pitch = *p->pitch * 0.8f + wantPitch * 0.2f;
		float prev = e->state4;
		e->state4 -= FRAMES(dt) * 1.1f / g_wear.nvg;
		if (e->state4 < -500 && prev >= -500) sfxPlay("horror/horror0", 1.0f);
		if (e->state4 < -800 && prev >= -800) sfxPlay("horror/horror1", 1.0f);
		s_lightBlink = fmaxf(s_lightBlink, e->state4 < -800 ? 0.5f : 0);
		if (e->state4 < -1000) {
			return g_wear.nvg > 1
			           ? "\"Class D viewed SCP-895 through a pair of digital night vision goggles, presumably enhanced by "
			             "SCP-914. It might be possible that the subject was able to resist the memetic effects partially "
			             "through these goggles. The goggles have been stored for further study.\""
			           : "\"Class D viewed SCP-895 through a pair of digital night vision goggles, killing him.\"";
		}
	} else {
		e->state4 = fminf(0, e->state4 + FRAMES(dt));
	}
	return NULL;
}
