/*
 * g_bot_ground.c — Ground Vehicle AI for MFQ3 bot system
 *
 * Implements bot behavior for tanks, APCs, trucks and other ground vehicles.
 * Handles waypoint navigation with ground physics, terrain avoidance,
 * turret rotation and combat firing, speed management.
 *
 * Adapted from:
 *   - Drone_Ground_Think (g_droneground.c) — original was just a stub
 *   - getTargetDirAndDist (g_droneutil.c) — direction/waypoint logic
 *
 * Ground vehicles are always OO_LANDED. They use yaw-only steering,
 * turret rotation to aim, and speed control for aim vs transit.
 */

#include "g_bot.h"


/*
=============================================================================
  NAVIGATION
=============================================================================
*/

/*
  Bot_Ground_Navigate — Drive toward the current waypoint or target.
  
  Ground vehicles navigate by:
  1. Computing desired yaw toward waypoint/target
  2. Steering (turning) toward that yaw at the vehicle's turnspeed
  3. Adjusting throttle based on whether we're turning (slow) or
     going straight (fast)
  
  The entity's s.apos.trBase[1] (yaw) is the hull direction.
  The entity's speed_ / client_->ps_.speed is forward velocity.
  The entity's client_->ps_.fixed_throttle controls engine power.
*/
void Bot_Ground_Navigate( botState_t *bs )
{
	GameEntity *ent;
	vec3_t dir, bearing;
	float dist, diff, turnspeed;
	float timediff;
	float desiredYaw;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* Time delta for this frame */
	timediff = (float)FRAMETIME / 1000.0f;

	/* Get direction to waypoint or target */
	if( bs->state == BOT_STATE_CHASE || bs->state == BOT_STATE_ATTACK ) {
		/* Navigate toward enemy target */
		GameEntity *target = theLevel.getEntity( bs->targetEntityNum );
		if( target && target->inuse_ ) {
			VectorSubtract( target->r.currentOrigin, ent->r.currentOrigin, dir );
			dist = VectorNormalize( dir );
			bs->targetDist = dist;
		} else {
			/* Lost target, fall back to waypoint */
			dist = Bot_GetWaypointDirAndDist( bs, dir );
		}
	} else if( bs->state == BOT_STATE_FLEE || bs->state == BOT_STATE_RETURN_TO_BASE ) {
		/* Navigate toward home */
		VectorSubtract( bs->homeOrigin, ent->r.currentOrigin, dir );
		dist = VectorNormalize( dir );
	} else {
		/* Navigate toward waypoint */
		dist = Bot_GetWaypointDirAndDist( bs, dir );
	}

	/* Check if we've reached the waypoint */
	if( bs->state == BOT_STATE_PATROL && dist < BOT_WAYPOINT_REACH ) {
		Bot_OnWaypointEvent( bs );
		/* After waypoint event, recompute direction */
		dist = Bot_GetWaypointDirAndDist( bs, dir );
	}

	/* Convert direction to desired yaw */
	vectoangles( dir, bearing );
	desiredYaw = bearing[1];

	/* Current yaw from entity angles */
	{
		float currentYaw = ent->s.apos.trBase[1];

		/* Compute angle difference */
		diff = desiredYaw - currentYaw;
		if( diff > 180 ) diff -= 360;
		else if( diff < -180 ) diff += 360;

		/* Steer toward desired yaw */
		turnspeed = availableVehicles[bs->vehicleIndex].turnspeed[1] * timediff;

		if( fabs( diff ) < turnspeed ) {
			/* Close enough — snap to desired */
			ent->s.apos.trBase[1] = desiredYaw;
			ent->s.apos.trDelta[1] = 0;
		} else if( diff > 0 ) {
			ent->s.apos.trBase[1] += turnspeed;
			ent->s.apos.trDelta[1] = availableVehicles[bs->vehicleIndex].turnspeed[1];
		} else {
			ent->s.apos.trBase[1] -= turnspeed;
			ent->s.apos.trDelta[1] = -availableVehicles[bs->vehicleIndex].turnspeed[1];
		}

		/* Normalize yaw to 0-360 */
		ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );
	}

	/* Update angles everywhere */
	VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
	ent->s.apos.trTime = theLevel.time_;

	/* Speed management:
	   - When turning significantly (> 15°), slow down for better aim
	   - When going straight, use military throttle
	   - In ATTACK state, slow down for better aim
	*/
	{
		float absDiff = fabs( diff );
		int throttle;

		if( bs->state == BOT_STATE_ATTACK ) {
			/* Slow down to aim */
			throttle = MF_THROTTLE_IDLE + 3;
		} else if( absDiff > 15.0f ) {
			/* Turning — slow down */
			throttle = MF_THROTTLE_MILITARY / 2;
		} else {
			/* Straight ahead — full speed */
			throttle = MF_THROTTLE_MILITARY;
		}

		if( ent->client_ ) {
			ent->client_->ps_.fixed_throttle = throttle;
			ent->speed_ = (float)availableVehicles[bs->vehicleIndex].maxspeed * throttle / MF_THROTTLE_MILITARY;
		}
	}
}


/*
  Bot_Ground_AvoidObstacles — Trace forward and to the sides to detect
  obstacles. If blocked, apply steering correction to go around.
  
  Uses three traces:
  - Forward: detect obstacle ahead
  - Left 45°: check if we can go left
  - Right 45°: check if we can go right
  
  If forward is blocked, steer toward whichever side is clearer.
*/
void Bot_Ground_AvoidObstacles( botState_t *bs )
{
	GameEntity *ent;
	trace_t tr;
	vec3_t forward, right, left;
	vec3_t fwdEnd, leftEnd, rightEnd;
	vec3_t angles;
	float leftClear, rightClear;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* Only avoid obstacles when patrolling or chasing */
	if( bs->state != BOT_STATE_PATROL && bs->state != BOT_STATE_CHASE ) {
		return;
	}

	/* Build trace directions from current angles */
	VectorCopy( ent->s.apos.trBase, angles );
	AngleVectors( angles, forward, right, NULL );

	/* Rotate right by 45° for the right check */
	RotatePointAroundVector( right, forward, right, 45 );
	/* Rotate left by -45° for the left check */
	RotatePointAroundVector( left, forward, right, -45 ); /* Note: reusing 'right' vec as up for rotation, not perfect but illustrative */
	
	/* Actually, let's do this properly */
	{
		vec3_t up = {0, 0, 1};
		vec3_t rightDir, leftDir;

		RotatePointAroundVector( rightDir, up, forward, -45 );
		RotatePointAroundVector( leftDir, up, forward, 45 );

		/* Forward trace */
		VectorMA( ent->r.currentOrigin, BOT_AVOID_TRACE_DIST, forward, fwdEnd );
		SV_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs,
			fwdEnd, ent->s.number, MASK_PLAYERSOLID, qfalse );

		if( tr.fraction < 1.0f ) {
			/* Obstacle ahead! Check left and right */
			VectorMA( ent->r.currentOrigin, BOT_AVOID_TRACE_DIST, leftDir, leftEnd );
			SV_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs,
				leftEnd, ent->s.number, MASK_PLAYERSOLID, qfalse );
			leftClear = tr.fraction;

			VectorMA( ent->r.currentOrigin, BOT_AVOID_TRACE_DIST, rightDir, rightEnd );
			SV_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs,
				rightEnd, ent->s.number, MASK_PLAYERSOLID, qfalse );
			rightClear = tr.fraction;

			/* Steer toward the clearer side */
			if( leftClear > rightClear ) {
				/* Steer left: add to yaw */
				ent->s.apos.trBase[1] += availableVehicles[bs->vehicleIndex].turnspeed[1] * (float)FRAMETIME / 1000.0f;
			} else {
				/* Steer right: subtract from yaw */
				ent->s.apos.trBase[1] -= availableVehicles[bs->vehicleIndex].turnspeed[1] * (float)FRAMETIME / 1000.0f;
			}

			ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );

			/* Slow down when avoiding */
			if( ent->client_ ) {
				ent->client_->ps_.fixed_throttle = MF_THROTTLE_IDLE + 2;
			}
		}
	}

	/* Stuck detection: if position hasn't changed much, we're stuck */
	{
		float moved;
		vec3_t movedir;
		VectorSubtract( ent->r.currentOrigin, bs->lastPos, movedir );
		moved = VectorLength( movedir );

		if( moved < 1.0f ) {
			bs->stuckTimer += FRAMETIME;
			if( bs->stuckTimer > 2000 ) {
				/* Stuck for 2+ seconds — reverse and turn */
				if( ent->client_ ) {
					ent->client_->ps_.fixed_throttle = MF_THROTTLE_REVERSE;
				}
				ent->s.apos.trBase[1] += 30.0f;
				ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );

				if( bs->stuckTimer > 4000 ) {
					bs->stuckTimer = 0;
				}
			}
		} else {
			bs->stuckTimer = 0;
		}

		VectorCopy( ent->r.currentOrigin, bs->lastPos );
	}
}


/*
=============================================================================
  COMBAT
=============================================================================
*/

/*
  Bot_Ground_Combat — Handle turret rotation and weapon firing.
  
  Ground vehicles aim their turret at the target independently of hull direction.
  The turret angle is stored in the entity's s.angles2 or via the loadout system.
  When the turret is aligned with the target (within tolerance), fire.
  
  This is a simplified version — the real turret tracking would use the
  vehicle's weapon/turret data from availableVehicles[].
*/
void Bot_Ground_Combat( botState_t *bs )
{
	GameEntity *ent;
	GameEntity *target;
	vec3_t dirToTarget, bearing;
	float desiredYaw, desiredPitch;
	float yawDiff, pitchDiff;
	float turretTurnSpeed;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* Only fight in CHASE or ATTACK states */
	if( bs->state != BOT_STATE_CHASE && bs->state != BOT_STATE_ATTACK ) return;

	/* Need a valid target */
	if( bs->targetEntityNum < 0 ) return;

	target = theLevel.getEntity( bs->targetEntityNum );
	if( !target || !target->inuse_ || target->health_ <= 0 ) {
		bs->targetEntityNum = -1;
		return;
	}

	/* Compute direction to target */
	VectorSubtract( target->r.currentOrigin, ent->r.currentOrigin, dirToTarget );
	VectorNormalize( dirToTarget );
	vectoangles( dirToTarget, bearing );

	desiredYaw = bearing[1];
	desiredPitch = bearing[0];

	/* Turret turn speed — use a reasonable default */
	turretTurnSpeed = 45.0f * (float)FRAMETIME / 1000.0f;  /* 45 deg/sec */

	/*
	 * In the real MFQ3 system, turret angles are managed through the
	 * client's ps structure and weapon turret data. Here we approximate
	 * by rotating the hull toward the target (which for tanks with
	 * limited turret traverse is actually correct behavior).
	 *
	 * For a full implementation, turret rotation would be managed via
	 * the entity's s.torsoAnim or similar fields.
	 */

	/* Rotate hull toward target for now (simplification) */
	yawDiff = desiredYaw - ent->s.apos.trBase[1];
	if( yawDiff > 180 ) yawDiff -= 360;
	else if( yawDiff < -180 ) yawDiff += 360;

	if( fabs( yawDiff ) > turretTurnSpeed ) {
		if( yawDiff > 0 ) {
			ent->s.apos.trBase[1] += turretTurnSpeed;
		} else {
			ent->s.apos.trBase[1] -= turretTurnSpeed;
		}
	} else {
		ent->s.apos.trBase[1] = desiredYaw;
	}

	ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );
	VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
	ent->s.apos.trTime = theLevel.time_;

	/* Check if we should fire */
	if( Bot_ShouldFire( bs ) ) {
		/* Fire the weapon */
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
  Bot_Ground_Think — Main think function for ground vehicle bots.
  Called every 100ms (FRAMETIME) via Bot_Think dispatch.
  
  Adapted from original Drone_Ground_Think (g_droneground.c) which was
  just an empty stub with nextthink and SV_LinkEntity.
  
  Order of operations:
  1. Avoid obstacles (may override navigation)
  2. Navigate (steer toward waypoint/target/home)
  3. Combat (rotate turret, fire if aligned)
  4. Link entity
*/
void Bot_Ground_Think( botState_t *bs )
{
	GameEntity *ent;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* 1. Avoid obstacles */
	Bot_Ground_AvoidObstacles( bs );

	/* 2. Navigate */
	Bot_Ground_Navigate( bs );

	/* 3. Combat */
	Bot_Ground_Combat( bs );

	/* 4. Set next think time and link entity */
	ent->nextthink_ = theLevel.time_ + FRAMETIME;
	SV_LinkEntity( ent );
}