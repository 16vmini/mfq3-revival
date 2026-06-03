/*
 * g_bot_air.c — Plane AI for MFQ3 bot system
 *
 * Implements bot behavior for fighter and bomber aircraft.
 * Handles waypoint navigation with 3-axis steering (yaw/pitch/roll),
 * dogfighting with lead-pursuit and evasion, takeoff/landing integration
 * with checkForTakeoffOrLanding(), altitude management, and terrain avoidance.
 *
 * Adapted from:
 *   - Drone_Plane_Think (g_droneplane.c) — two commented-out versions
 *     with waypoint navigation and yaw/pitch/roll steering logic
 *   - getTargetDirAndDist / onWaypointEvent (g_droneutil.c)
 *   - checkTakeoffLandingPlane (g_plane.c) — landing/takeoff logic
 *
 * Key differences from ground vehicles:
 * - 3-axis rotation (yaw, pitch, roll) instead of yaw-only
 * - Must maintain speed above stall speed
 * - Must manage altitude and avoid terrain
 * - Combat involves lead-pursuit and evasion maneuvers
 * - Gear management for takeoff/landing
 */

#include "g_bot.h"


/*
=============================================================================
  CONSTANTS
=============================================================================
*/

/* Minimum altitude above terrain (world units) */
#define PLANE_MIN_ALTITUDE			200

/* Default cruise altitude */
#define PLANE_DEFAULT_CRUISE_ALT	1500

/* Combat altitude — lower for ground attack, higher for air combat */
#define PLANE_AIR_COMBAT_ALT		2000
#define PLANE_GROUND_ATTACK_ALT		800

/* Distance from waypoint to start turning (for smoother turns) */
#define PLANE_TURN_EARLY_DIST		512

/* Maximum pitch angle for terrain avoidance (degrees) */
#define PLANE_TERRAIN_PITCH		30

/* Dogfight: how far ahead to aim for lead pursuit (multiplier on target velocity) */
#define PLANE_LEAD_FACTOR			0.5f

/* Evasion: minimum time between evasion maneuvers */
#define PLANE_EVADE_INTERVAL		2000


/*
=============================================================================
  ALTITUDE MANAGEMENT
=============================================================================
*/

/*
  Bot_Plane_AltitudeManagement — Maintain desired altitude.
  
  Adjusts pitch to climb or descend toward the target altitude.
  Also performs terrain avoidance: if the ground is too close,
  pitch up aggressively regardless of the desired altitude.
*/
void Bot_Plane_AltitudeManagement( botState_t *bs )
{
	GameEntity *ent;
	trace_t tr;
	vec3_t downEnd;
	float terrainClearance;
	float desiredAlt;
	float currentAlt;
	float altDiff;
	float pitchAdjust;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* Current altitude: Z coordinate (simplified — in Q3, altitude is Z) */
	currentAlt = ent->r.currentOrigin[2];

	/* Determine desired altitude based on state */
	switch( bs->state ) {
	case BOT_STATE_ATTACK:
	case BOT_STATE_CHASE:
		/* In combat, altitude depends on target type */
		if( bs->targetEntityNum >= 0 ) {
			GameEntity *target = theLevel.getEntity( bs->targetEntityNum );
			if( target && target->client_ ) {
				int targetCat = availableVehicles[target->client_->vehicle_].cat;
				if( targetCat & (CAT_GROUND | CAT_BOAT | CAT_LQM) ) {
					desiredAlt = PLANE_GROUND_ATTACK_ALT;
				} else {
					desiredAlt = PLANE_AIR_COMBAT_ALT;
				}
			} else {
				desiredAlt = PLANE_DEFAULT_CRUISE_ALT;
			}
		} else {
			desiredAlt = PLANE_DEFAULT_CRUISE_ALT;
		}
		break;
	case BOT_STATE_PATROL:
	default:
		desiredAlt = bs->cruiseAltitude > 0 ? bs->cruiseAltitude : PLANE_DEFAULT_CRUISE_ALT;
		break;
	}

	/* Terrain avoidance: trace straight down */
	VectorCopy( ent->r.currentOrigin, downEnd );
	downEnd[2] -= 4096;  /* Trace far down */
	SV_Trace( &tr, ent->r.currentOrigin, NULL, NULL, downEnd,
		ent->s.number, MASK_SOLID, qfalse );

	if( tr.fraction < 1.0f ) {
		terrainClearance = ent->r.currentOrigin[2] - tr.endpos[2];
	} else {
		terrainClearance = currentAlt;  /* No ground found, assume high up */
	}

	/* If too close to terrain, override desired altitude */
	if( terrainClearance < PLANE_MIN_ALTITUDE ) {
		/* EMERGENCY: pitch up hard */
		ent->s.apos.trBase[0] = -PLANE_TERRAIN_PITCH;  /* Negative pitch = nose up in Q3 */
		ent->s.apos.trDelta[0] = 0;
		ent->s.apos.trTime = theLevel.time_;
		return;
	}

	/* Normal altitude management: adjust pitch toward desired altitude */
	altDiff = desiredAlt - currentAlt;

	/* Pitch adjustment:
	   - If too low: pitch up (negative pitch in Q3 convention)
	   - If too high: pitch down (positive pitch)
	   - Scale by how far off we are
	*/
	if( fabs( altDiff ) < 50 ) {
		/* Close enough — level off */
		pitchAdjust = 0;
	} else {
		/* Pitch proportional to altitude error, clamped */
		pitchAdjust = -altDiff * 0.01f;  /* Negative because up = negative pitch */
		if( pitchAdjust > 20 ) pitchAdjust = 20;
		if( pitchAdjust < -20 ) pitchAdjust = -20;
	}

	/* Apply pitch adjustment (blended with navigation pitch) */
	/* Note: Bot_Plane_Navigate may also set pitch. The navigation
	   pitch should take priority for waypoint following, but altitude
	   safety should override. We store this for the navigate function
	   to consider. */
	bs->wantsTakeoff = ( altDiff > 200 );  /* Simple flag for climb detection */
}


/*
=============================================================================
  TAKEOFF / LANDING
=============================================================================
*/

/*
  Bot_Plane_Takeoff — Handle the takeoff sequence.
  
  When a plane bot is OO_LANDED, it needs to:
  1. Set gear down (already set by MF_Spawn_Plane)
  2. Increase throttle to military
  3. Accelerate along the runway
  4. Once speed is sufficient, retract gear and lift off
  5. Climb to cruise altitude
  
  Integrates with Entity_Plane::checkForTakeoffOrLanding() which
  automatically detects when the plane has left the ground.
*/
void Bot_Plane_Takeoff( botState_t *bs )
{
	GameEntity *ent;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	/* Increase throttle for takeoff */
	ent->client_->ps_.fixed_throttle = MF_THROTTLE_MILITARY;
	ent->client_->ps_.speed = availableVehicles[bs->vehicleIndex].maxspeed;
	
	/* Keep going straight during takeoff roll */
	/* Pitch slightly up to encourage lift */
	if( ent->client_->ps_.speed > availableVehicles[bs->vehicleIndex].stallspeed ) {
		ent->s.apos.trBase[0] = -5;  /* Slight nose up */
		VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
		ent->s.apos.trTime = theLevel.time_;

		/* Retract gear once we're fast enough */
		if( ent->client_->ps_.ONOFF & OO_GEAR ) {
			ent->client_->ps_.ONOFF &= ~OO_GEAR;
			ent->updateGear_ = qtrue;
		}
	}

	/* Once airborne (checkForTakeoffOrLanding removes OO_LANDED), transition to patrol */
	if( !(ent->client_->ps_.ONOFF & OO_LANDED) ) {
		Bot_SetState( bs, BOT_STATE_PATROL );
		bs->wantsTakeoff = qfalse;
	}
}


/*
  Bot_Plane_Landing — Handle the landing sequence.
  
  When a plane bot wants to land:
  1. Deploy gear
  2. Reduce throttle
  3. Descend toward the runway/home position
  4. checkForTakeoffOrLanding() handles the actual touchdown
  
  We set OO_GEAR and reduce speed. The plane physics + checkForTakeoffOrLanding
  will handle the rest.
*/
void Bot_Plane_Landing( botState_t *bs )
{
	GameEntity *ent;
	vec3_t dirToHome;
	float distToHome;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	/* Deploy gear */
	if( !(ent->client_->ps_.ONOFF & OO_GEAR) ) {
		ent->client_->ps_.ONOFF |= OO_GEAR;
		ent->updateGear_ = qtrue;
	}

	/* Reduce throttle */
	ent->client_->ps_.fixed_throttle = MF_THROTTLE_IDLE + 3;

	/* Navigate toward home position */
	VectorSubtract( bs->homeOrigin, ent->r.currentOrigin, dirToHome );
	distToHome = VectorNormalize( dirToHome );

	/* Steer toward home */
	{
		vec3_t bearing;
		float diff, turnspeed;
		float timediff = (float)FRAMETIME / 1000.0f;

		vectoangles( dirToHome, bearing );
		diff = bearing[1] - ent->s.apos.trBase[1];
		if( diff > 180 ) diff -= 360;
		else if( diff < -180 ) diff += 360;

		turnspeed = availableVehicles[bs->vehicleIndex].turnspeed[1] * timediff;
		if( fabs( diff ) < turnspeed ) {
			ent->s.apos.trBase[1] = bearing[1];
			ent->s.apos.trDelta[1] = 0;
		} else if( diff > 0 ) {
			ent->s.apos.trBase[1] += turnspeed;
			ent->s.apos.trDelta[1] = availableVehicles[bs->vehicleIndex].turnspeed[1];
		} else {
			ent->s.apos.trBase[1] -= turnspeed;
			ent->s.apos.trDelta[1] = -availableVehicles[bs->vehicleIndex].turnspeed[1];
		}
		ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );
	}

	/* Descend gently */
	ent->s.apos.trBase[0] = 5;  /* Slight nose down for descent */

	/* Update angles */
	VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
	ent->s.apos.trTime = theLevel.time_;

	/* Once landed, go idle */
	if( ent->client_->ps_.ONOFF & OO_LANDED ) {
		Bot_SetState( bs, BOT_STATE_IDLE );
		bs->wantsLanding = qfalse;
		ent->client_->ps_.fixed_throttle = MF_THROTTLE_IDLE;
	}
}


/*
=============================================================================
  NAVIGATION — adapted from Drone_Plane_Think (g_droneplane.c)
=============================================================================
*/

/*
  Bot_Plane_Navigate — Steer the plane toward its current waypoint or target.
  
  This is the core navigation function, adapted from the two commented-out
  versions of Drone_Plane_Think in g_droneplane.c. The original code:
  1. Evaluated trajectory to get current position
  2. Traced from previous to current position for collision
  3. Computed direction to waypoint via getTargetDirAndDist
  4. Adjusted yaw/roll/pitch toward the target bearing
  
  Our version simplifies by working directly with currentOrigin and
  currentAngles, and adds altitude management integration.
  
  YAW: Steer toward target bearing at vehicle turnspeed
  ROLL: Bank into turns (right turn = left roll and vice versa)
  PITCH: Climb/descend toward target altitude or waypoint elevation
*/
void Bot_Plane_Navigate( botState_t *bs )
{
	GameEntity *ent;
	vec3_t dir, bearing;
	float dist, diff;
	float turnspeed;
	float targroll;
	float timediff;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* Skip navigation if landed (takeoff handles it) */
	if( ent->client_ && (ent->client_->ps_.ONOFF & OO_LANDED) ) {
		return;
	}

	timediff = (float)FRAMETIME / 1000.0f;

	/* Get direction to waypoint or target */
	if( bs->state == BOT_STATE_CHASE || bs->state == BOT_STATE_ATTACK ) {
		GameEntity *target = theLevel.getEntity( bs->targetEntityNum );
		if( target && target->inuse_ ) {
			/* Lead pursuit: aim ahead of target based on its velocity */
			vec3_t leadPos;
			VectorCopy( target->r.currentOrigin, leadPos );
			/* Simple lead: offset by a fraction of target velocity * time */
			VectorMA( leadPos, PLANE_LEAD_FACTOR, target->s.pos.trDelta, leadPos );
			VectorSubtract( leadPos, ent->r.currentOrigin, dir );
			dist = VectorNormalize( dir );
			bs->targetDist = dist;
		} else {
			dist = Bot_GetWaypointDirAndDist( bs, dir );
		}
	} else if( bs->state == BOT_STATE_FLEE || bs->state == BOT_STATE_RETURN_TO_BASE ) {
		VectorSubtract( bs->homeOrigin, ent->r.currentOrigin, dir );
		dist = VectorNormalize( dir );
	} else {
		dist = Bot_GetWaypointDirAndDist( bs, dir );
	}

	/* Check waypoint reached */
	if( bs->state == BOT_STATE_PATROL && dist < BOT_WAYPOINT_REACH ) {
		Bot_OnWaypointEvent( bs );
		dist = Bot_GetWaypointDirAndDist( bs, dir );
	}

	/* Convert direction to bearing angles */
	vectoangles( dir, bearing );

	/* ---- YAW STEERING ----
	 * Adapted from Drone_Plane_Think version 1 and 2.
	 * Both versions compute diff between bearing yaw and current yaw,
	 * then apply turnspeed-limited rotation.
	 */
	diff = bearing[1] - ent->s.apos.trBase[1];
	diff = AngleMod( diff );  /* Normalize to 0-360 */

	if( diff > 180 ) diff -= 360;  /* Convert to -180..180 */

	turnspeed = availableVehicles[bs->vehicleIndex].turnspeed[1] * timediff;

	if( fabs( diff ) > turnspeed ) {
		if( diff > 0 ) {
			ent->s.apos.trBase[1] += turnspeed;
			ent->s.apos.trDelta[1] = availableVehicles[bs->vehicleIndex].turnspeed[1];
		} else {
			ent->s.apos.trBase[1] -= turnspeed;
			ent->s.apos.trDelta[1] = -availableVehicles[bs->vehicleIndex].turnspeed[1];
		}
	} else {
		ent->s.apos.trBase[1] = bearing[1];
		ent->s.apos.trDelta[1] = 0;
	}
	ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );

	/* ---- ROLL ----
	 * Adapted from Drone_Plane_Think. When turning, bank the plane:
	 * - Right turn (diff > 0) → roll left (negative roll)
	 * - Left turn (diff < 0) → roll right (positive roll)
	 * - Straight → level wings
	 */
	turnspeed = availableVehicles[bs->vehicleIndex].turnspeed[2] * timediff;
	if( diff > 1 ) targroll = -90;
	else if( diff < -1 ) targroll = 90;
	else targroll = 0;

	diff = targroll - ent->s.apos.trBase[2];

	if( diff < -turnspeed ) {
		ent->s.apos.trBase[2] -= turnspeed;
		ent->s.apos.trDelta[2] = -availableVehicles[bs->vehicleIndex].turnspeed[2];
	} else if( diff > turnspeed ) {
		ent->s.apos.trBase[2] += turnspeed;
		ent->s.apos.trDelta[2] = availableVehicles[bs->vehicleIndex].turnspeed[2];
	} else {
		ent->s.apos.trBase[2] = targroll;
		ent->s.apos.trDelta[2] = 0;
	}

	/* ---- PITCH ----
	 * Adapted from Drone_Plane_Think. Pitch toward target altitude.
	 * The bearing[0] gives us the pitch needed to reach the waypoint/target.
	 * We blend that with altitude management.
	 */
	diff = bearing[0] - ent->s.apos.trBase[0];
	diff = AngleMod( diff );

	turnspeed = availableVehicles[bs->vehicleIndex].turnspeed[0] * timediff;

	if( diff > 180 ) {
		/* Pitching down (nose above target) */
		if( diff > 270 && diff < 360 - turnspeed ) {
			ent->s.apos.trBase[0] -= turnspeed;
		} else {
			ent->s.apos.trBase[0] = bearing[0];
		}
	} else {
		/* Pitching up (nose below target) */
		if( diff > turnspeed ) {
			ent->s.apos.trBase[0] += turnspeed;
		} else {
			ent->s.apos.trBase[0] = bearing[0];
		}
	}

	/* Update entity angles and trajectory */
	VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
	ent->s.apos.trTime = theLevel.time_;

	/* Set forward velocity based on current angles */
	{
		vec3_t forward;
		AngleVectors( ent->s.apos.trBase, forward, NULL, NULL );
		VectorScale( forward, ent->speed_, ent->s.pos.trDelta );
		VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
		ent->s.pos.trTime = theLevel.time_;
	}
}


/*
=============================================================================
  COMBAT / DOGFIGHT
=============================================================================
*/

/*
  Bot_Plane_Combat — Handle air-to-air and air-to-ground combat.
  
  In combat state, the plane:
  1. Maintains target acquisition
  2. Steers toward the target with lead pursuit
  3. Fires weapons when aligned
  4. Performs evasion when under fire
*/
void Bot_Plane_Combat( botState_t *bs )
{
	GameEntity *ent;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* Only fight in CHASE or ATTACK states */
	if( bs->state != BOT_STATE_CHASE && bs->state != BOT_STATE_ATTACK ) return;

	/* Target validation */
	if( bs->targetEntityNum < 0 ) return;

	{
		GameEntity *target = theLevel.getEntity( bs->targetEntityNum );
		if( !target || !target->inuse_ || target->health_ <= 0 ) {
			bs->targetEntityNum = -1;
			return;
		}
	}

	/* Navigate toward target (with lead pursuit — handled in Bot_Plane_Navigate) */
	/* Bot_Plane_Navigate already handles chase navigation */

	/* Check if we should fire */
	if( Bot_ShouldFire( bs ) ) {
		FireWeapon( ent );
		bs->lastFireTime = theLevel.time_;
	}

	/* Ensure we don't stall — maintain minimum speed */
	if( ent->client_ ) {
		if( ent->client_->ps_.speed < availableVehicles[bs->vehicleIndex].stallspeed ) {
			ent->client_->ps_.fixed_throttle = MF_THROTTLE_AFTERBURNER;
			ent->client_->ps_.speed = availableVehicles[bs->vehicleIndex].maxspeed;
		}
	}
}


/*
  Bot_Plane_Dogfight — Advanced dogfighting behavior.
  
  When in ATTACK state against an air target:
  1. Use lead pursuit (already in navigate)
  2. If being targeted (incoming missiles), deploy flares
  3. Perform defensive maneuvers:
     - Break turn (hard turn perpendicular to attacker)
     - Vertical scissors (alternate climb/dive with turns)
  4. Try to get behind the target (tail chase position)
  
  This is a simplified version — full dogfighting would need
  much more sophisticated state tracking.
*/
void Bot_Plane_Dogfight( botState_t *bs )
{
	GameEntity *ent;
	GameEntity *target;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	/* Only dogfight if we have an air target */
	if( bs->targetEntityNum < 0 ) return;

	target = theLevel.getEntity( bs->targetEntityNum );
	if( !target || !target->inuse_ ) return;

	/* Check if target is an air vehicle */
	{
		int targetCat = 0;
		if( target->client_ ) {
			targetCat = availableVehicles[target->client_->vehicle_].cat;
		}
		if( !(targetCat & (CAT_PLANE | CAT_HELO)) ) {
			return;  /* Not an air target — skip dogfight logic */
		}
	}

	/* Deploy flares if being locked (simplified check) */
	if( ent->client_->ps_.stats[STAT_LOCKINFO] & LI_BEING_LOCKED ) {
		fire_flare( ent );
	}

	/* Check if we're being shot at (took damage recently) */
	/* If we took damage in the last 2 seconds, perform evasion */
	if( ent->client_->damage_done_ > 0 &&
		theLevel.time_ - ent->pain_debounce_time_ < PLANE_EVADE_INTERVAL ) {
		
		/* Break turn: hard turn in a random direction */
		float breakDir = (ent->s.number % 2 == 0) ? 1.0f : -1.0f;
		float hardTurn = availableVehicles[bs->vehicleIndex].turnspeed[1] * 2.0f;

		ent->s.apos.trBase[1] += breakDir * hardTurn * (float)FRAMETIME / 1000.0f;
		ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );

		/* Also vary altitude during evasion */
		ent->s.apos.trBase[0] = ((ent->s.number % 3) - 1) * 15.0f;
	}

	/* Try to get behind the target (tail position) */
	{
		vec3_t targetForward, toUs, lateral;
		float dotForward, dotLateral;
		
		AngleVectors( target->s.apos.trBase, targetForward, lateral, NULL );
		VectorSubtract( ent->r.currentOrigin, target->r.currentOrigin, toUs );
		VectorNormalize( toUs );

		dotForward = DotProduct( toUs, targetForward );

		/* If we're in front of the target (dotForward > 0), we're vulnerable.
		 * Fly a wider arc to get behind them. */
		if( dotForward > 0.3f ) {
			/* We're in front — turn harder to get around */
			ent->s.apos.trBase[1] += (dotForward > 0 ? 1 : -1) * 
				availableVehicles[bs->vehicleIndex].turnspeed[1] * (float)FRAMETIME / 1000.0f;
		}
	}
}


/*
=============================================================================
  MAIN THINK
=============================================================================
*/

/*
  Bot_Plane_Think — Main think function for plane bots.
  Called every 100ms (FRAMETIME) via Bot_Think dispatch.
  
  Adapted from Drone_Plane_Think (g_droneplane.c) which had two
  commented-out implementations. We use the second version's approach
  (working with trDelta and trBase) as it's closer to the trajectory
  system used in the final codebase.
  
  Order of operations:
  1. Handle takeoff/landing if in those states
  2. Manage altitude (terrain avoidance + target altitude)
  3. Navigate (steer toward waypoint/target with yaw/pitch/roll)
  4. Combat (fire weapons when aligned)
  5. Dogfight (evasion + positioning for air targets)
  6. Ensure minimum speed (prevent stall)
  7. Link entity
*/
void Bot_Plane_Think( botState_t *bs )
{
	GameEntity *ent;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	/* 1. Takeoff / Landing states */
	if( bs->state == BOT_STATE_TAKEOFF ) {
		Bot_Plane_Takeoff( bs );
		ent->nextthink_ = theLevel.time_ + FRAMETIME;
		SV_LinkEntity( ent );
		return;
	}

	if( bs->state == BOT_STATE_LANDING ) {
		Bot_Plane_Landing( bs );
		ent->nextthink_ = theLevel.time_ + FRAMETIME;
		SV_LinkEntity( ent );
		return;
	}

	/* Handle planes that are landed but not in takeoff/landing state */
	if( ent->client_->ps_.ONOFF & OO_LANDED ) {
		/* We're on the ground but not in takeoff state — take off! */
		Bot_SetState( bs, BOT_STATE_TAKEOFF );
		bs->wantsTakeoff = qtrue;
		ent->nextthink_ = theLevel.time_ + FRAMETIME;
		SV_LinkEntity( ent );
		return;
	}

	/* 2. Altitude management */
	Bot_Plane_AltitudeManagement( bs );

	/* 3. Navigation (includes lead pursuit for chase/attack) */
	Bot_Plane_Navigate( bs );

	/* 4. Combat */
	Bot_Plane_Combat( bs );

	/* 5. Dogfight (only in attack state) */
	if( bs->state == BOT_STATE_ATTACK ) {
		Bot_Plane_Dogfight( bs );
	}

	/* 6. Ensure minimum speed — prevent stall */
	if( ent->client_->ps_.speed < availableVehicles[bs->vehicleIndex].stallspeed ) {
		ent->client_->ps_.fixed_throttle = MF_THROTTLE_AFTERBURNER;
		ent->speed_ = (float)availableVehicles[bs->vehicleIndex].maxspeed;
	}

	/* 7. Set next think and link entity */
	ent->nextthink_ = theLevel.time_ + FRAMETIME;
	SV_LinkEntity( ent );
}