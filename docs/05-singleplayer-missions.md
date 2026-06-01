# Feature: Single-Player & Missions

**Verdict: Moderate.** The hardest, most uncertain part — a working in-game level/unit **editor**
with a serializable file format and parser — already exists and round-trips. The remaining work is
conventional game-entity plumbing on top of a sound data model. The core gap is a **client/server
disconnect**: everything mission-related lives client-side in the editor; the server-side spawner
is commented out.

## Gametypes (cite file:line)
Defined in `game/bg_public.h:77-88`: `GT_FFA=0, GT_TOURNAMENT=1, GT_SINGLE_PLAYER=2,
GT_MISSION_EDITOR=3, GT_TEAM=4, GT_CTF=5`. The UI list `{"Single Player" 2}{"Mission Editor" 3}` is
parsed from a data file (`UI_ParseGameInfo`, `ui/ui_main.c:5640-5644`).

- **GT_SINGLE_PLAYER (2): functional but it is inherited Quake-3 "single player = FFA vs bots,"
  NOT a mission mode.** It only tweaks match plumbing (skip logging `g_main.c:328`, tied rank with
  1 player :714, tournament intermission :865, no rankings). No objectives, no scripted enemies.
- **GT_MISSION_EDITOR (3): functional placement/export tool.** On a gametype-3 server the client
  enters editor mode: `ME_Init_MissionEditor()` (`cg_main.c:2068`), `CG_Draw_IGME()` each frame
  (`cg_view.c:705`); free-camera unit placement; saves/loads `.mis` files.

## Mission data model + parser — REAL and complete
- Structs (`bg_public.h:1028-1065`): `mission_overview_t` (map, gameset, gametype, name, `objective`
  goal-string), `mission_vehicle_t` (index/name/team/origin/angles + `waypoints[32]`),
  `mission_groundInstallation_t`. `IGME_MAX_VEHICLES=64`.
- Parser (`game/bg_mfq3util.c`): `MF_ParseMissionScripts` (:935) → `MF_ParseOverview` (:604),
  `MF_ParseEntities` (:880), `MF_ParseVehicle` (:704), `MF_ParseGroundInstallation` (:794),
  `MF_ParseWaypoints` (:654); validation `MF_CheckMissionScriptOverviewValid` (:33).

## In-game Mission Editor (IGME) — substantial, working
`cgame/cg_missioneditor.c` (~1096 lines), state in `cg_local.h:881-917`. Capabilities: spawn
vehicles (`ME_SpawnVehicle:666`) & ground installations (`ME_SpawnGroundInstallation:710`) via
`me_spawn`/`me_spawngi`; crosshair ray-pick select + multi-select; drag/move/rotate traced to world;
delete; copy/paste; per-vehicle waypoint placement; live 3D render of placed units. **Save**
(`ME_ExportToScript:846`): writes `missions/<map>/<name>.mis` (brace-delimited Overview + Entities).
**Load** (`ME_ImportScript:1053`): re-displays into the *editor's* arrays (editor-only).
Placeholders to clean up: GI name hardcoded "SAM Turret", team "?", goal always "SearchAndDestroy".

## The gaps (what a real SP/campaign needs)
1. **Server-side spawning is commented out.** `game/g_missions.c` (`G_LoadMissionScripts`,
   `G_SpawnMissionVehicles`, `G_SpawnMissionGroundInstallations`) is 100% dead;
   `g_main.c:411` has `//G_LoadMissionScripts();`. A `.mis` file today spawns **zero** live entities.
2. **NPC/installation entities are stubs.** `misc_vehicle`/`misc_waypoint` are commented out of the
   spawn table (`g_spawn.c:234-235`); `SP_ai_radar`/`SP_ai_sam`/`SP_ai_flak` immediately `freeUp()`
   (`g_mfq3ents.c:355-372`). (AI behavior = the bots work, [04-bots.md](04-bots.md).)
3. **No real objective system.** The networked `objectives` field is *misnamed* — it's a CTF flag-
   carry bitfield (`OB_REDFLAG/OB_BLUEFLAG`, `bg_public.h:242-246`), not mission goals. No objective
   list, completion state, or win/lose. The `.mis` `goal` is a dead free-text string.
4. No mission-select menu, briefings, objective HUD, campaign progression, or save-game.

## Build on existing code — do not replace
Keep `mission_*_t`, `MF_ParseMissionScripts`, the `.mis` format, and the IGME editor. Replace only
the dead `g_missions.c` body and the `SP_ai_*` stubs.

## Roadmap
- **M1 — "Spawn from file"** (mostly un-commenting): rewrite `G_LoadMissionScripts` to call the
  existing parser and spawn real `gentity_t`s; call it from `g_main.c:411`; give `SP_ai_sam`/`flak`
  real bodies (or route through the normal ground-installation entity). → a map loads, the mission
  spawns 2 physically-present, shootable SAM installations.
- **M2 — "One objective + win screen":** minimal server objective (count enemy-team installations);
  when all destroyed, trigger the existing intermission (`ExitLevel`/`BeginIntermission`); add one
  HUD line "Destroy all targets: X/Y".
- **M3 — "Editor → play loop":** a command/button to launch the just-saved `.mis` (set `mf_mission`,
  `g_gametype 2`, `map <name>`) so a designer places units, exports, and plays immediately.
- **M4+ — Campaign:** richer objective types (reuse the `goal` string), briefings, a mission-select
  menu (new `.menu`), save/progression, and triggers/events (revive the commented `g_scripts.c`
  waypoint/task engine).

M1–M2 are small and high-confidence — they prove a playable scripted SP mission end to end.
