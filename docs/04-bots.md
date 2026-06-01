# Feature: Vehicle Bots (air / ground / sea)

**Verdict: Feasible.** A bot in MFQ3 is simply *"something that fills a `usercmd_t` each frame for
a vehicle client."* A clean hook already exists, the steering is pure usercmd, the targeting brain
is live and reusable, and ioquake3 donates all the fake-client plumbing.

## The hook point (cite file:line)
`ClientThink_real` (`game/g_active.c:622`) reads a `usercmd_t` from `ent->client_->pers_.cmd_`
(:635), copies it into `pm.cmd` (:784), and calls `Pmove` (:823) → `PM_VehicleMove`
(`bg_pmove.c:846`) → per-category plane/ground/helo/boat/lqm physics. **Fill `pers_.cmd_` and the
existing physics do the rest — no physics changes needed.**

**Decisive fact:** vehicle steering is driven by usercmd **move fields, not view angles**. For a
plane (`bg_planemove.c:695-697`): `forwardmove`→pitch rate, `upmove`→yaw rate, `rightmove`→roll
rate. So a flight AI is a clamped **P-controller** (steer toward target), not view-angle math.

## Reusable brains (live today)
- **Target acquisition/lock:** `updateTargetTracking()` (`game/g_mfq3util.c:56`) runs every frame
  for the human radar lock — picks target category from radar mode, computes aim direction
  (ground/boat from `vehicleAngles`+`turretAngle`+`gunAngle` :95-100; air from the nose :104-105),
  traces within `radarRange`/`trackCone`, validates category + LOS, locks. Already 3D, category-aware.
- **3D intercept/lead:** the homing-missile think (`game/g_missile.c:805-933`): direction-to-target,
  seeker cone (`DotProduct(dir,targdir) < followcone_` :894), LOS (:902), course correction
  (`VectorMA(dir, 1.85f, targdir, …)` :910); flak lead at :1911-1922.

## Inherited from ioquake3 botlib (the plumbing, ~90% of the boring work)
- Fake client slot: `SV_BotAllocateClient` (`ioq3-reference/code/server/sv_bot.c:47`),
  `SV_BotFreeClient` (:77), `SVF_BOT` flag (already referenced live in MFQ3 `g_active.c:787`).
- Elementary-action → usercmd: `be_ea.c` (`EA_Move`/`EA_View`/`EA_Attack`, `EA_GetInput`),
  `BotInputToUserCommand` (`ai_main.c:817`), injected via `trap_BotUserCommand` (:1443,1583).
- **MFQ3's `usercmd_t` is the unmodified stock struct**, so the injection path drops in unchanged.
- **Discard:** the entire AAS nav stack (`be_aas_*.c`) and DM goal/item nav — it's 2.5D infantry
  pathfinding, meaningless for flight/turrets/boats. (Bot chat/personality is optional/cosmetic.)

## Dead reference AI (commented out — design reference only)
`g_groundinstallation.c` (turret target-acquire `Update_GI_Targets:52` + turn/lock/fire
`GroundInstallation_Think:163`), `g_miscvehicle.c`, `g_droneplane.c`/`g_droneground.c` (a vehicle
steering brain), and the waypoint/script structs in `g_local.h` — all 100% commented out. Resurrect
as reference, re-expressed as **usercmd output** rather than direct trajectory writes.

## Per-category difficulty
| Category | Difficulty | Notes |
|---|---|---|
| 🚤 Boat (`bg_boatmove.c`) | **Easy** | 2D water steering + separate turret; no terrain/altitude |
| 🚁 Helo (`bg_helomove.c`) | Moderate | hover + altitude hold, no stall |
| 🛡️ Tank (`bg_groundmove.c`) | Moderate | two loops: hull steer (`rightmove`:338) + independent turret/gun (`turretAngle`/`gunAngle`:339-340,467); needs ground nav |
| ✈️ Plane (`bg_planemove.c`) | **Hard** | 3D pursuit is fine; **terrain avoidance + stall/energy management** is the real obstacle |

## Navigation
No live nav system (waypoints/`g_scripts.c`/`g_missions.c` all commented out). Ground/sea bots can
revive a simple **named-waypoint follow** (steer toward `nextWaypoint`, advance on arrival) + LOS
probe. Air bots mostly **don't need waypoints** — "steer toward target + hold altitude band +
forward/down trace to pull up."

## MVP + roadmap
1. **Plumbing spike:** allocate a fake client, spawn it in a boat, inject a constant "drive forward"
   usercmd — prove motion.
2. **MVP:** boat bot that runs `updateTargetTracking` + a P-controller to turn toward a target and
   fire, using unmodified physics. (Boat = safest first; plane = most impressive.)
3. Generalize the controller to plane/helo (rate control) and tank (hull + turret loops).
4. Add air **terrain avoidance** (forward/down trace + pull-up) and simple ground/sea patrol.
5. Polish: weapon selection, evasion/flares, difficulty, optional chat.

**Build plan:** one new file `g_vehiclebot.c` with `BotThink_<category>()` filling `usercmd_t`;
use ioq3 botlib for the fake-client/inject plumbing; build the brain on MFQ3's live
`updateTargetTracking` + missile seeker math. No physics changes.
