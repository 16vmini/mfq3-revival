/*
 * g_bot_boat.c — Naval/Boat AI for MFQ3 bot system
 *
 * Implements bot behavior for patrol boats, transport boats, and other
 * watercraft. Boats are the simplest vehicle category — they operate
 * on the water surface with yaw-only steering and no altitude concerns.
 *
 * Key characteristics:
 * - Water surface only (no altitude changes)
 * - Yaw-only steering (like ground vehicles)
 * - Combat similar to ground vehicles: rotate to face target, fire
 * - No gear, no stall, no takeoff/landing
 * - OO_LANDED flag set (boats are "on the surface")
 *
 * Adapted from:
 *   - Drone_Ground_Think (g_droneground.c) — similar surface vehicle logic
 *   - MF_Spawn_Boat() (mf_vehiclespawn.c)
 */

#include "g_bot.h"


/*
=============================================================================
  NAVIGATION
=============================================================================
*/

/*
  Bot_Boat_Navigate — Steer the boat toward its waypoint or target.
  
  Boats navigate on the water surface with simple yaw steering.
  Very similar to ground vehicle navigation but without:
  - Obstacle avoidance (boats on open water)
  - Terrain following
  - Stuck detection (less relevant on water)
  
  For simplicity, we do basic waypoint-following with yaw rotation.
*/
void Bot_Boat_Navigate( botState_t *bs )
{
	GameEntity *ent;
	vec3_t dir, bearing;
	float dist, diff, turnspeed;
	float timediff;
	float desiredYaw;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

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

	/* Convert direction to desired yaw */
	vectoangles( dir, bearing );
	desiredYaw = bearing[1];

	/* ---- YAW: Steer toward target ---- */
	diff = desiredYaw - ent->s.apos.trBase[1];
	if( diff > 180 ) diff -= 360;
	else if( diff < -180 ) diff += 360;

	turnspeed = availableVehicles[bs->vehicleIndex].turnspeed[1] * timediff;

	if( fabs( diff ) < turnspeed ) {
		ent->s.apos.trBase[1] = desiredYaw;
		ent->s.apos.trDelta[1] = 0;
	} else if( diff > 0 ) {
		ent->s.apos.trBase[1] += turnspeed;
		ent->s.apos.trDelta[1] = availableVehicles[bs->vehicleIndex].turnspeed[1];
	} else {
		ent->s.apos.trBase[1] -= turnspeed;
		ent->s.apos.trDelta[1] = -availableVehicles[bs->vehicleIndex].turnspeed[1];
	}
	ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );

	/* Update angles */
	VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
	ent->s.apos.trTime = theLevel.time_;

	/* ---- SPEED ---- */
	{
		float desiredSpeed;
		float absDiff = fabs( diff );

		if( bs->state == BOT_STATE_ATTACK ) {
			/* Slow down to aim */
			desiredSpeed = (float)availableVehicles[bs->vehicleIndex].maxspeed * 0.3f;
		} else if( absDiff > 15.0f ) {
			/* Turning — moderate speed */
			desiredSpeed = (float)availableVehicles[bs->vehicleIndex].maxspeed * 0.5f;
		} else {
			/* Straight — full speed */
			desiredSpeed = (float)availableVehicles[bs->vehicleIndex].maxspeed * 0.8f;
		}

		/* Set velocity in forward direction */
		vec3_t forward;
		AngleVectors( ent->s.apos.trBase, forward, NULL, NULL );
		VectorScale( forward, desiredSpeed, ent->s.pos.trDelta );
		VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
		ent->s.pos.trTime = theLevel.time_;

		if( ent->client_ ) {
			ent->client_->ps_.speed = (int)desiredSpeed;
			ent->client_->ps_.fixed_throttle = (int)(desiredSpeed / availableVehicles[bs->vehicleIndex].maxspeed * 10);
		}
	}

	/* Keep pitch and roll level (we're on water) */
	ent->s.apos.trBase[0] = 0;
	ent->s.apos.trBase[2] = 0;
}


/*
=============================================================================
  COMBAT
=============================================================================
*/

/*
  Bot_Boat_Combat — Boat combat behavior.
  
  Boats fight similarly to ground vehicles:
  1. Rotate hull toward target
  2. Fire weapons when aligned
  
  Boats typically have turreted weapons (patrol boats) that can
  aim independently, but for simplicity we rotate the hull.
*/
void Bot_Boat_Combat( botState_t *bs )
{
	GameEntity *ent;
	GameEntity *target;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* Only fight in CHASE or ATTACK states */
	if( bs->state != BOT_STATE_CHASE && bs->state != BOT_STATE_ATTACK ) return;

	/* Validate target */
	if( bs->targetEntityNum < 0 ) return;

	target = theLevel.getEntity( bs->targetEntityNum );
	if( !target || !target->inuse_ || target->health_ <= 0 ) {
		bs->targetEntityNum = -1;
		return;
	}

	/* Additional steering toward target during combat */
	{
		vec3_t dirToTarget, bearing;
		float diff, turnspeed;
		float timediff = (float)FRAMETIME / 1000.0f;

		VectorSubtract( target->r.currentOrigin, ent->r.currentOrigin, dirToTarget );
		VectorNormalize( dirToTarget );
		vectoangles( dirToTarget, bearing );

		diff = bearing[1] - ent->s.apos.trBase[1];
		if( diff > 180 ) diff -= 360;
		else if( diff < -180 ) diff += 360;

		turnspeed = availableVehicles[bs->vehicleIndex].turnspeed[1] * timediff * 1.5f; /* Turn faster in combat */

		if( fabs( diff ) > turnspeed ) {
			if( diff > 0 ) {
				ent->s.apos.trBase[1] += turnspeed;
			} else {
				ent->s.apos.trBase[1] -= turnspeed;
			}
		} else {
			ent->s.apos.trBase[1] = bearing[1];
		}

		ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );
		VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
		ent->s.apos.trTime = theLevel.time_;
	}

	/* Fire weapons when aligned */
	if( Bot_ShouldFire( bs ) ) {
		FireWeapon( ent );
		bs->lastFireTime = theLevel.time_;
	}
}


/*
=============================================================================
  MAIN THINK
=============================================================================
*/

/*
  Bot_Boat_Think — Main think function for boat bots.
  Called every 100ms (FRAMETIME) via Bot_Think dispatch.
  
  Order of operations:
  1. Navigate (steer toward waypoint/target)
  2. Combat (rotate to target, fire when aligned)
  3. Link entity
*/
void Bot_Boat_Think( botState_t *bs )
{
	GameEntity *ent;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* 1. Navigate */
	Bot_Boat_Navigate( bs );

	/* 2. Combat */
	Bot_Boat_Combat( bs );

	/* 3. Link entity */
	ent->nextthink_ = theLevel.time_ + FRAMETIME;
	SV_LinkEntity( ent );
}