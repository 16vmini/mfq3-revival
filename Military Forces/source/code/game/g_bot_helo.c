/*
 * g_bot_helo.c — Helicopter AI for MFQ3 bot system
 *
 * Implements bot behavior for attack, recon, and transport helicopters.
 * Similar to plane AI but with hover capability, vertical movement,
 * and different takeoff/landing integration.
 *
 * Key differences from planes:
 * - Can hover (zero forward speed is valid)
 * - Vertical takeoff and landing
 * - Can strafe (move laterally without changing heading)
 * - Slower but more maneuverable
 * - Combat: hover and rotate to target, fire weapons
 *
 * Integrates with:
 *   - Entity_Helicopter::checkForTakeoffOrLanding() (g_helo.cpp)
 *   - MF_Spawn_Helo() (mf_vehiclespawn.c)
 */

#include "g_bot.h"


/*
=============================================================================
  CONSTANTS
=============================================================================
*/

/* Hover altitude when engaging targets */
#define HELO_COMBAT_HOVER_ALT	400

/* Default cruise altitude */
#define HELO_DEFAULT_CRUISE_ALT	600

/* Minimum altitude above terrain */
#define HELO_MIN_ALTITUDE		100

/* Takeoff climb rate (units per second) */
#define HELO_CLIMB_RATE			200

/* Strafe speed when repositioning */
#define HELO_STRAFE_SPEED		100

/* Trace distance for obstacle avoidance */
#define HELO_AVOID_DIST			256


/*
=============================================================================
  TAKEOFF
=============================================================================
*/

/*
  Bot_Helo_Takeoff — Handle helicopter takeoff sequence.
  
  Helicopters take off by:
  1. Increasing throttle (vertical lift)
  2. Climbing straight up to hover altitude
  3. Transitioning to patrol once at altitude
  
  Uses checkForTakeoffOrLanding() for ground detection.
*/
void Bot_Helo_Takeoff( botState_t *bs )
{
	GameEntity *ent;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	/* Apply throttle for vertical lift */
	ent->client_->ps_.fixed_throttle = MF_THROTTLE_MILITARY;

	/* Check if we've left the ground */
	if( !(ent->client_->ps_.ONOFF & OO_LANDED) ) {
		/* We're airborne — climb to hover altitude */
		float currentAlt = ent->r.currentOrigin[2];
		
		if( currentAlt >= bs->cruiseAltitude - 50 ) {
			/* Reached cruise altitude — transition to patrol */
			Bot_SetState( bs, BOT_STATE_PATROL );
			bs->wantsTakeoff = qfalse;
			ent->client_->ps_.fixed_throttle = MF_THROTTLE_IDLE + 5;
		} else {
			/* Keep climbing — pitch stays level, throttle provides lift */
			ent->s.apos.trBase[0] = 0;  /* Level pitch */
		}
	} else {
		/* Still on the ground — keep trying to lift off */
		ent->client_->ps_.fixed_throttle = MF_THROTTLE_MILITARY;
	}

	/* Update angles */
	VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
	ent->s.apos.trTime = theLevel.time_;
}


/*
=============================================================================
  LANDING
=============================================================================
*/

/*
  Bot_Helo_Landing — Handle helicopter landing sequence.
  
  Helicopters land by:
  1. Navigating toward the home position
  2. Reducing altitude gradually
  3. Reducing throttle on approach
  4. Setting down — checkForTakeoffOrLanding() handles touchdown
  
  OO_LANDEDTERRAIN flag is used for rough terrain landings.
*/
void Bot_Helo_Landing( botState_t *bs )
{
	GameEntity *ent;
	vec3_t dirToHome;
	float distToHome;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	/* Navigate toward home */
	VectorSubtract( bs->homeOrigin, ent->r.currentOrigin, dirToHome );
	distToHome = VectorNormalize( dirToHome );

	/* Steer toward home (yaw only) */
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

	/* Reduce altitude: reduce throttle to descend */
	if( distToHome < 500 ) {
		/* Close to home — reduce throttle and descend */
		ent->client_->ps_.fixed_throttle = MF_THROTTLE_IDLE;
	} else {
		ent->client_->ps_.fixed_throttle = MF_THROTTLE_IDLE + 3;
	}

	/* Deploy gear */
	if( !(ent->client_->ps_.ONOFF & OO_GEAR) ) {
		ent->client_->ps_.ONOFF |= OO_GEAR;
		ent->updateGear_ = qtrue;
	}

	/* Keep level during descent */
	ent->s.apos.trBase[0] = 0;
	ent->s.apos.trBase[2] = 0;

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
  NAVIGATION
=============================================================================
*/

/*
  Bot_Helo_Navigate — Steer the helicopter toward its waypoint or target.
  
  Helicopters navigate differently from planes:
  - They can hover, so zero speed is valid
  - They can strafe (move sideways) while keeping their heading
  - Altitude is managed by throttle, not pitch
  - They don't bank into turns as dramatically
  
  Navigation steps:
  1. Compute direction to waypoint/target
  2. Rotate heading (yaw) toward target
  3. Move forward at a moderate speed
  4. Manage altitude
*/
void Bot_Helo_Navigate( botState_t *bs )
{
	GameEntity *ent;
	vec3_t dir, bearing;
	float dist, diff, turnspeed;
	float timediff;
	trace_t tr;
	vec3_t downEnd;
	float terrainClearance;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	/* Skip navigation if landed */
	if( ent->client_->ps_.ONOFF & OO_LANDED ) return;

	timediff = (float)FRAMETIME / 1000.0f;

	/* Get direction to waypoint or target */
	if( bs->state == BOT_STATE_CHASE || bs->state == BOT_STATE_ATTACK ) {
		GameEntity *target = theLevel.getEntity( bs->targetEntityNum );
		if( target && target->inuse_ ) {
			VectorSubtract( target->r.currentOrigin, ent->r.currentOrigin, dir );
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

	/* Convert direction to bearing */
	vectoangles( dir, bearing );

	/* ---- YAW: Rotate heading toward target ---- */
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

	/* ---- PITCH: Keep level (helicopters don't pitch to climb) ---- */
	ent->s.apos.trBase[0] = 0;
	ent->s.apos.trDelta[0] = 0;

	/* ---- ROLL: Gentle banking ---- */
	if( fabs( diff ) > 1 ) {
		float targroll = (diff > 0) ? -30 : 30;
		ent->s.apos.trBase[2] += (targroll - ent->s.apos.trBase[2]) * 0.1f;
	} else {
		ent->s.apos.trBase[2] *= 0.9f;  /* Level out */
	}

	/* ---- ALTITUDE MANAGEMENT ---- */
	{
		float desiredAlt;
		float currentAlt = ent->r.currentOrigin[2];

		/* Determine desired altitude */
		switch( bs->state ) {
		case BOT_STATE_ATTACK:
		case BOT_STATE_CHASE:
			desiredAlt = HELO_COMBAT_HOVER_ALT;
			break;
		default:
			desiredAlt = bs->cruiseAltitude > 0 ? bs->cruiseAltitude : HELO_DEFAULT_CRUISE_ALT;
			break;
		}

		/* Terrain avoidance */
		VectorCopy( ent->r.currentOrigin, downEnd );
		downEnd[2] -= 4096;
		SV_Trace( &tr, ent->r.currentOrigin, NULL, NULL, downEnd,
			ent->s.number, MASK_SOLID, qfalse );

		if( tr.fraction < 1.0f ) {
			terrainClearance = ent->r.currentOrigin[2] - tr.endpos[2];
		} else {
			terrainClearance = currentAlt;
		}

		if( terrainClearance < HELO_MIN_ALTITUDE ) {
			/* Emergency climb */
			ent->client_->ps_.fixed_throttle = MF_THROTTLE_MILITARY;
		} else {
			/* Adjust throttle to maintain altitude */
			float altDiff = desiredAlt - currentAlt;
			if( altDiff > 50 ) {
				ent->client_->ps_.fixed_throttle = MF_THROTTLE_IDLE + 5;
			} else if( altDiff < -50 ) {
				ent->client_->ps_.fixed_throttle = MF_THROTTLE_IDLE;
			} else {
				/* Hover: throttle just enough to maintain altitude */
				ent->client_->ps_.fixed_throttle = MF_THROTTLE_IDLE + 2;
			}
		}
	}

	/* ---- SPEED: Move forward toward waypoint ---- */
	{
		vec3_t forward;
		float desiredSpeed;

		/* In combat, hover (slow or zero forward speed for better aim) */
		if( bs->state == BOT_STATE_ATTACK ) {
			desiredSpeed = 50.0f;  /* Slow hover */
		} else if( bs->state == BOT_STATE_CHASE ) {
			desiredSpeed = (float)availableVehicles[bs->vehicleIndex].maxspeed * 0.7f;
		} else {
			desiredSpeed = (float)availableVehicles[bs->vehicleIndex].maxspeed * 0.5f;
		}

		AngleVectors( ent->s.apos.trBase, forward, NULL, NULL );
		VectorScale( forward, desiredSpeed, ent->s.pos.trDelta );
		VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
		ent->s.pos.trTime = theLevel.time_;
		ent->client_->ps_.speed = (int)desiredSpeed;
	}

	/* Update angles */
	VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
	ent->s.apos.trTime = theLevel.time_;
}


/*
=============================================================================
  COMBAT
=============================================================================
*/

/*
  Bot_Helo_Combat — Helicopter combat behavior.
  
  Helicopters fight by:
  1. Hovering at combat altitude
  2. Rotating to face the target (yaw)
  3. Firing weapons when aligned
  4. Deploying flares when locked
  
  Attack helicopters can use hellfire missiles against ground targets
  and autocannon/FFAR against both air and ground.
*/
void Bot_Helo_Combat( botState_t *bs )
{
	GameEntity *ent;
	GameEntity *target;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	/* Only fight in CHASE or ATTACK states */
	if( bs->state != BOT_STATE_CHASE && bs->state != BOT_STATE_ATTACK ) return;

	/* Validate target */
	if( bs->targetEntityNum < 0 ) return;

	target = theLevel.getEntity( bs->targetEntityNum );
	if( !target || !target->inuse_ || target->health_ <= 0 ) {
		bs->targetEntityNum = -1;
		return;
	}

	/* Navigate toward target (Bot_Helo_Navigate handles yaw steering) */

	/* Fire weapons when aligned */
	if( Bot_ShouldFire( bs ) ) {
		FireWeapon( ent );
		bs->lastFireTime = theLevel.time_;
	}

	/* Deploy flares if being locked */
	if( ent->client_->ps_.stats[STAT_LOCKINFO] & LI_BEING_LOCKED ) {
		fire_flare( ent );
	}

	/* Adjust combat hover altitude based on target type */
	if( target->client_ ) {
		int targetCat = availableVehicles[target->client_->vehicle_].cat;
		if( targetCat & (CAT_GROUND | CAT_BOAT | CAT_LQM) ) {
			/* Ground target: hover higher for safety */
			bs->cruiseAltitude = HELO_COMBAT_HOVER_ALT;
		} else {
			/* Air target: match altitude approximately */
			float targetAlt = target->r.currentOrigin[2];
			bs->cruiseAltitude = targetAlt + 100;  /* Slightly above */
		}
	}
}


/*
=============================================================================
  MAIN THINK
=============================================================================
*/

/*
  Bot_Helo_Think — Main think function for helicopter bots.
  Called every 100ms (FRAMETIME) via Bot_Think dispatch.
  
  Order of operations:
  1. Handle takeoff/landing if in those states
  2. Navigate (steer toward waypoint/target with yaw, manage altitude)
  3. Combat (rotate to face target, fire when aligned)
  4. Link entity
*/
void Bot_Helo_Think( botState_t *bs )
{
	GameEntity *ent;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	/* 1. Takeoff / Landing */
	if( bs->state == BOT_STATE_TAKEOFF ) {
		Bot_Helo_Takeoff( bs );
		ent->nextthink_ = theLevel.time_ + FRAMETIME;
		SV_LinkEntity( ent );
		return;
	}

	if( bs->state == BOT_STATE_LANDING ) {
		Bot_Helo_Landing( bs );
		ent->nextthink_ = theLevel.time_ + FRAMETIME;
		SV_LinkEntity( ent );
		return;
	}

	/* Handle helos that are landed but not in takeoff state */
	if( ent->client_->ps_.ONOFF & OO_LANDED ) {
		Bot_SetState( bs, BOT_STATE_TAKEOFF );
		bs->wantsTakeoff = qtrue;
		ent->nextthink_ = theLevel.time_ + FRAMETIME;
		SV_LinkEntity( ent );
		return;
	}

	/* 2. Navigate */
	Bot_Helo_Navigate( bs );

	/* 3. Combat */
	Bot_Helo_Combat( bs );

	/* 4. Link entity */
	ent->nextthink_ = theLevel.time_ + FRAMETIME;
	SV_LinkEntity( ent );
}