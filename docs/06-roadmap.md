# Roadmap — toward "Mission 1"

The guiding scenario (the project owner's dream):

> **Spawn on a runway as a little person → run to a plane → get in → fly out and destroy a
> target → return to base → "Mission 1 complete."**

This decomposes neatly into the dormant systems. Below: the engine track (get a modern base) and
the gameplay track (reactivate the systems), then how they converge on Mission 1.

## Phase 0 — Done
- ✅ Classic MFQ3 builds & runs from source (x86). See [01-build-and-run.md](01-build-and-run.md).
- ✅ Aircraft-only pitch invert added (`m_invertAircraftPitch`).
- ✅ Stock ioquake3 base builds clean. See [02-ioq3-port.md](02-ioq3-port.md).
- ✅ Feasibility recon: infantry, bots, missions.

## Phase 1 — Port to ioquake3 (engine track)
Goal: MFQ3 gameplay running on the modern ioq3 engine (then Linux/Steam Deck).
1. Working tree `mfq3-ioq3` (copy of ioq3 base).
2. **Merge shared structs** — MFQ3 vehicle fields into `playerState_t`/`entityState_t` + `msg.c`
   (the bounded ~20+12 swap). *Foundation.*
3. Bring in `game`/`cgame`/`ui`/`vehiclelib`; reconcile the trap/syscall interface; build DLLs.
4. Re-apply engine hooks: aircraft pitch invert; **lift the ±90° view-pitch clamp** for flight.
5. Boot on Windows → then native **Linux / Steam Deck**.

> Decision already taken: keep ioq3's engine, bring MFQ3's gameplay (not the reverse). `usercmd` is
> stock-identical and the modules build as native DLLs, both confirmed.

## Phase 2 — Little people (gameplay track) — *next*
LQM is ~80% built and disabled. See [03-infantry-lqm.md](03-infantry-lqm.md).
1. Add a free Q3-format biped model to `mfdata` (OpenArena/GPL). *(No biped model ships today.)*
2. Re-enable a Sarge infantry row in `bg_vehicledata.c`; load `animation.cfg`; expose in UI.
3. Spawn directly as infantry; confirm walk/turn/jump/crouch/shoot/die. **← "spawn as a little person" works.**

## Phase 3 — Enter/Exit a vehicle (the one genuinely-new mechanic)
Not an engine-mode switch — a **vehicle-index swap** (`MF_SwapVehicle`, no death/respawn).
1. "Dismount" key: vehicle → LQM at a ground-traced origin; abandoned vehicle becomes a free entity.
2. "Enter": LQM use-trace vs nearby vehicle → swap into it; client re-registers models.
   **← "run to a plane and get in" works.**

## Phase 4 — Something to shoot (bots / NPCs)
See [04-bots.md](04-bots.md). MVP first.
1. Plumbing spike: fake client (ioq3 `SV_BotAllocateClient`) spawned in a vehicle, constant usercmd.
2. Boat/ground bot: `updateTargetTracking` + P-controller fills `usercmd_t`, fires when locked.
3. Or, simplest for Mission 1: a **stationary NPC SAM/flak target** (resurrect
   `g_groundinstallation.c`) — doesn't even need to move. **← "destroy a target" works.**

## Phase 5 — Mission wrapper (objectives)
See [05-singleplayer-missions.md](05-singleplayer-missions.md).
1. M1 "spawn from file": revive `G_LoadMissionScripts`; give `SP_ai_*` real bodies → the `.mis`
   spawns the target(s) from Phase 4.
2. M2 "objective + win": count enemy installations; all destroyed → intermission + a "return to
   base" trigger volume at the runway → **"Mission 1 complete."**
3. M3 "editor → play": launch a just-saved `.mis` from the IGME.

## Convergence: Mission 1
```
[Phase 2] spawn as LQM on a runway
   └─[Phase 3] run to the parked plane, press use → enter it
        └─ fly out (Phase 0 flight model + the invert option)
             └─[Phase 4] destroy the SAM target
                  └─[Phase 5] fly back to the base trigger → objective complete → win screen
```
Every arrow is a system the original team already started. The project is less "build a game" and
more **"finish the game that was almost shipped — on a modern engine."**

## Suggested order of attack
Engine (Phase 1) and gameplay (Phases 2–5) are largely independent. Recommended:
**reactivate LQM on the *classic* build first** (fast, proves the fun, needs only a model + un-comment),
in parallel with the ioq3 port — then carry the working gameplay onto the modern base.
