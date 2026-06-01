# Feature: Infantry — "LQM" (Little Quaky Man)

**Verdict: Moderate, and ~80% already built.** A former team member built a near-complete
dismounted-soldier system, wired end-to-end as a 5th vehicle category (`CAT_LQM`). It is
**functionally complete but disabled at the data-table level**. The only genuinely-new work is
**enter/exit a vehicle**. Reactivate — don't redo.

## What already exists (cite file:line)
- **Movement (real, not stubbed):** `game/bg_lqmmove.c` — `PM_LQMMove()` (:80) does forward/back/
  strafe (diagonal scaling), crouch, jump, gravity, death; plus `PM_LQMGroundTrace` (:29),
  `PM_SlideMove_LQM` (:254), `PM_StepSlideMove_LQM` (:446) — Quake's ground/slide/step re-derived
  for LQM. Live-dispatched at `game/bg_pmove.c:859-861` for `CAT_LQM`.
- **Server entity:** `game/g_lqm.c` (C `Touch_LQM`/`LQM_Pain`) and `game/g_lqm.cpp`/`.h` (C++
  `Entity_Infantry : VehicleBase`); "run over by a vehicle" implemented (`g_lqm.cpp:7`). Spawn:
  `MF_Spawn_LQM` (`mf_vehiclespawn.c:97`), `MF_ClientSpawn` builds `Entity_Infantry` + ground-traces
  it (`mf_client.c:250-251, 489-503`).
- **Client biped rendering (full):** `cgame/cg_lqm.c` `CG_LQM` (:58) → `CG_DrawLQM`
  (`cg_vehicledraw.c:621+`): separate legs/torso/head md3, `tag_torso`/`tag_head` linking,
  turn-angle blending, the full stock anim set (run/walk/crouch/back/jump/land/idle/attack/death),
  muzzle flash, flag carry. `CG_RegisterLQM` (`cg_lqm.c:14`), `CG_LQMObituary` (:191).
  (Note: MFQ3's `cg_players.c` was gutted 2635→278 lines; biped rendering moved here.)
- **Scale solved:** `LQM_SCALE = 0.1` (`bg_public.h:1125`); bbox computed from a Sarge md3 then
  ×0.1 (`vehiclelib/bg_vehicleinfo_infantry.cpp:43-48`); axis scaled at render
  (`cg_vehicledraw.c:888-889`). A soldier is credibly small next to the human-sized vehicles.

## Why it's dark
- The two infantry rows (Sarge, Major) are inside a `/* ... */` block in `bg_vehicledata.c`
  (~lines 3101-3201) → no LQM exists at runtime, so none is selectable.
- `animations_ = 0` — no `animation.cfg` parse wired in, so the renderer has no frame ranges.
- No UI vehicle-select exposure for `CAT_LQM` verified.

## The field-budget "problem" — already dodged
LQM needs no biped fields: it packs movement into the **`vehicleAnim` int as a bitfield**
(`A_LQM_STAND/FORWARD/BACKWARD/CROUCH/LEFT/RIGHT/EJECT/FLY/DIE/JUMP`, `bg_public.h:1126-1135`);
per-part frame/timer state is kept **client-side only** in `centity_t` and advanced locally. If we
ever want server-authoritative animation, there are **10 free `stats[]` slots and 6 free
`persistant[]` slots**, or (on the standalone ioq3 fork) we can just add the biped fields back.

## The one new thing: Enter/Exit vehicle
Nothing exists today (only way to change vehicle is die+respawn as `cg_nextVehicle`). **Good news:**
because LQM *is* a vehicle category, enter/exit is **not** a `PM_VEHICLE↔PM_NORMAL` switch — it's a
**vehicle-index swap**. State to save/restore on swap: vehicle index (drives dispatch/bbox/weapons/
camera), `origin` (eject beside vehicle, ground-traced), `viewheight`+bbox (auto from
`availableVehicles[]` in `PM_VehicleBoundingBox`, `bg_pmove.c:778-780`), `vehicleAngles`/`viewangles`,
velocity (zero on dismount), weapons, `ONOFF`, throttle (LQM ignores). The abandoned vehicle becomes
a free, re-enterable world entity. Hook: a new `MF_SwapVehicle()` reusing the spawn body *without*
the death/respawn path, called from `g_active.c` ClientThink / `mf_client.c`; client re-registers
models on swap (`cg_vehicle.c:619-620`). An `A_LQM_EJECT` anim bit already exists — they anticipated it.

## Assets needed
**No player/biped models ship in the paks** (confirmed: scanned all 7 pk3s, zero `players/`).
LQM expects standard Q3 player-model format (`lower`/`upper`/`head.md3` + `animation.cfg`). Drop in
any free Q3-format model — **OpenArena** (GPL/CC) or id's GPL'd models — and point the LQM data row
at it. One model is enough to prove "spawn as a little person."

## Recommended path (reactivate)
1. Uncomment a single infantry row (Sarge) in `bg_vehicledata.c`; load its `animation.cfg` into
   `animations[]`; expose it in vehicle-select UI. Add a biped model to `mfdata`.
2. Spawn directly as infantry (set `cg_nextVehicle` to the LQM index); confirm walk/turn/jump/
   crouch/shoot/die render — validates ~80% with zero new code.
3. Build `MF_SwapVehicle` (no-death index swap); bind a "dismount" key.
4. Add "enter": LQM use-trace vs nearby vehicle entities → swap back. Closes the loop.

---

## Reactivation log (this session) — WORKING, with a polish punch-list

**Status: a soldier spawns and walks, 90fps, no crash.** Committed on `feature/lqm`.

Fixes applied (see commit on feature/lqm):
1. Enabled the Sarge data row (`game/bg_vehicledata.c`).
2. `\sarge` console command + `mf_autoLQM` auto-spawn (`game/g_cmds.c`, `game/mf_client.c`).
3. **Spawn-point fix** — no map has `CAT_LQM` spawn points, so the picker found none →
   early return → half-spawn → engine `SV_BuildClientSnapshot: bad client`. Fix: LQM uses
   any spawn point (`game/mf_client.c`, the `spawnPoint->ent_category_` gate).
4. **Cache fix** — `CG_CacheVehicles` only caches vehicles whose `gameset` matches the current
   one (`cgame/cg_vehicle.c:568`); Sarge is `MODERN`, didn't match → `CG_CacheLQM` skipped →
   `animations` NULL → `CG_DrawLQM` null-deref crash. Fix: always cache `CAT_LQM`. + NULL-guard.
5. **Random-crash data fix** — Mark's `animation.cfg` referenced frames 0-24 but the model has
   1 frame; the old renderer read past the frame array as the anim "played" (random crash).
   Override pak `zz_lqmfix.pk3` (not in repo): all animations -> frame 0. Proper fix = a real
   multi-frame animated model.
- Model: CC0 `assets/lqm_sarge.pk3` (3 MD3 parts, correct tags), built from scratch.

### Remaining punch-list (next)
- **Persistent select overlay**: `cg_draw.c:2193` shows it while client `cg_vehicle == -1`. The
  LQM's vehicle index isn't reaching the client's `cg_vehicle` cvar via the "v" configstring
  (`game/g_client.c` ClientUserinfoChanged -> `cgame/cg_players.c:170-179`). Trace the sync.
- **Dies in water**: `bg_lqmmove.c` has no swim/water handling; add water checks (or make LQM
  amphibious-tolerant) instead of vehicle-style water death.
- **Can't shoot**: infantry weapon-fire path not wired (`g_weapon.c` / the LQM weapon in
  `bg_vehicledata` is `WI_MG_M4A1`); hook firing for `CAT_LQM`.
- **Tiny / can't see**: `LQM_SCALE 0.1`; tune render scale and/or third-person camera
  (`availableVehicles[].camDist/camHeight` for the LQM row) so the soldier is visible.
- **Enter/exit vehicles** (the dream's middle): `MF_SwapVehicle` index-swap + a spawned,
  walk-up-able vehicle entity in the map.
- Remove the temporary `MFtrace`/`DrawLQMtrace` debug logging before a clean release.
