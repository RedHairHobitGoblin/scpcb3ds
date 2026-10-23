# Port status

Every system in SCP - Containment Breach (v1.3.11 source in `upstream/`), and where the 3DS port stands.
`[x]` done, `[~]` partly done, `[ ]` not started.

## World
- [x] Map generation (`CreateMap`), 3 zones, checkpoints, fixed rooms
- [x] All 96 rooms, textures, lightmaps, props
- [x] Doors between rooms and in rooms; keycards, scanners, keypads, locks
- [~] Elevators: the Gate A and Gate B lifts (UpdateElevators); room1lifts, 049, 008, 035 not yet
- [ ] Security cameras and monitors (CreateSecurityCam)
- [ ] Levers, buttons, generators (room2nuke, room2ccont, room079)
- [ ] Decals (blood, corrosion)
- [ ] Room overlap check for large rooms (`PreventRoomOverlap`)
- [ ] Dimension 1499
- [x] Fog colour and range per area (outdoors, pocket dimension trench)
- [x] Pocket dimension (start room, 8 corridors, throne room, pillars, eyes, trench, tower, exits)

## Player
- [x] Walking, sprinting, stamina, crouching, footsteps
- [x] Blinking (hold to keep eyes closed)
- [x] Death screen, retry
- [x] Injuries / bleeding (Injuries, Bloodloss)
- [ ] Sanity, heartbeat, camera shake, blur
- [ ] Falling damage
- [ ] Save/load (Save.bb; saving at monitors)
- [ ] Options (sensitivity, volume, invert)
- [ ] Difficulty levels (Safe / Euclid / Keter)

## Items (131 templates)
- [x] Pick up, inventory, drop, documents, keycards, first aid, eyedrops, S-NAV map
- [ ] Gas masks, hazmat suits, ballistic vests (wearables)
- [ ] Radios and channels, batteries
- [ ] Night vision goggles
- [ ] SCP-500, SCP-714, SCP-1025, SCP-427, SCP-513, SCP-148, SCP-1499, SCP-860
- [ ] Syringes, pills, cups (SCP-294)
- [x] SCP-914 refining: knob and key, booths, all 235 Use914 recipes, player in the booth
- [ ] SCP-294 drinks

## NPCs
- [x] SCP-173 (now collides with walls inside rooms)
- [x] SCP-106 (rises, chases, teleports, kills; femur breaker recontainment; Tesla gates fry it)
- [x] SCP-096 (sitting, rage, chase, and the wandering state from room2servers / 096spawn)
- [x] SCP-049 (waits in its chamber until room049's power is back; room2sl, room4)
- [x] Guards, Class-D (intro and scripted events)
- [x] SCP-049-2 zombies, SCP-008-1 surgeon (medibay)
- [x] SCP-939, SCP-966, SCP-066, SCP-372, SCP-513-1, SCP-1048-A, SCP-035 (host)
- [x] MTF (Nine-Tailed Fox) units
- [x] SCP-860-2 (forest), SCP-1499-1
- [ ] SCP-035 tentacles, the 1123 officer (a guard stands in)
- [ ] Apache (Gate A)

## Events (UpdateEvents.bb)
Ported: every event in InitEvents except the three below, including the femur breaker (room106),
008, the Tesla gates, room049's generator and elevators, room2servers (096 and the guard), 096spawn,
the electrical centre (remote door control: the gates stay shut until SCP-079 opens them), 079,
012, 035, 895 (coffin / coffin106), 970 (room2storage), 294 (system keyboard for the keypad,
drinks from SCP-294.ini), 1162, 1123 (audio-led), 205 (sounds; shadow figures not drawn),
1048a, room3pit1048, the gas airlocks, room2sl, room2closets, endroom106, room3storage, medibay,
pj, testroom, room4, room2offices2 / 035, room3pitduck, room1lifts, room2nuke (levers, warhead).

Levers (UpdateLever) are drawn and pulled with L; the 3DS shows a use-ring when one is in reach.

The three that build their own worlds (tested in Azahar: entered, walked, events fire):
- room2tunnel: the maintenance tunnels, a 19 x 19 maze of the seven mt*.rmesh pieces 8 m up, generated
  from the map seed (so it's the same after loading), two elevators, the generator room with SCP-500-01
  and night vision goggles, SCP-106 and two SCP-966 the first time down.
- room860: SCP-860's forest, a 10 x 10 grid of 12 m terrain tiles 30 m up (GenForestGrid / PlaceForest),
  the key-only wooden door, the logs, SCP-860-2 (all four states) and its music. The converter bakes the
  five tile types with the path texture pre-blended and about a fifth of the original's trees.
- dimension1499: the castle 800 m up and 40 m chunks streamed around the player (two rings instead of
  three), SCP-1499-1 wandering and attacking, the king's throne-room scene, SCP-1499 to enter and leave.
  Fewer SCP-1499-1 and no crowd of animated figures in the throne room.

The tunnels, the forest and 1499 are "overlay areas": while the player is inside one, only its pieces
(and doors) are drawn and collided with. room2test1074 is disabled in the original.

## Tested in Azahar (2026-10-21)
Femur breaker, 008, room049, room2servers, 035, 012, 079 (speech plays), the tunnels (106 rises at the
elevator), the forest (860-2 shows up), dimension 1499 (1499-1 wander), 294 (asks for quarters), 1162
(trade and lung), 970 corridor, storeroom (lights out), 205, 1123, room3pit duck, room2sl (049 waits).
A 200-door noclip tour runs without a crash.

Fixed along the way: roomLoad crashed when an allocation failed (it now fails cleanly); the sound
cache is capped at 3 MB and event models load on first use and free after 3 s unused, so room
streaming keeps its memory.

Debug build only: SELECT noclip (also god mode), ZR next event control (R+ZR skips five, and
sdmc:/3ds/scpcb3ds/spot.txt picks the first), ZL tunnels / forest / 1499.

## Audio
- Long sounds (voice lines, over ~12 s) stream from romfs instead of being decoded into memory.
- Azahar needs a `3ds/dspfirm.cdc` on its virtual SD card or ndsp won't start and the game is silent;
  its HLE audio accepts any placeholder file. Real hardware needs the firmware dumped with DSP1.

## Endings
- [x] Gate A (A1 Chaos Insurgency, A2 recontained 106 / MTF)
- [x] Gate B (B2 remote detonation, B3 MTF capture)
- [x] Ending screen and THE END (credits roll not yet)

## Presentation
- [x] Title screen, pause menu
- [x] Zone music, ambience, 3D sound
- [x] Touch inventory, look pad for Old 3DS
- [ ] Light flicker (LightBlink), dark rooms, flashlight-free darkness
- [ ] Loading screens with SCP descriptions
- [ ] Achievements
