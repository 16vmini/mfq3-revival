/*
 * g_bot_infantry.c — LQM (Dismounted Infantry) AI for MFQ3 bot system
 *
 * Implements bot behavior for on-foot soldiers (LQM = Little Quick Man).
 * Infantry is the most complex AI after aircraft due to cover-seeking
 * behavior, walking/running, and close-range combat.
 *
 * Key characteristics:
 * - On-foot movement (walking, running, crouching)
 * - Infantry weapons (rifles, missiles, etc.)
 * - Can take cover behind terrain/objects
 * - Lower health than vehicles
 * - Can board/exit vehicles (future extension)
 * - Uses LQM animation system
 *
 * Adapted from:
 *   - Drone_Ground_Think (g_droneground.c) — basic surface movement
 *   - MF_Spawn_LQM() (mf_vehiclespawn.c) — infantry spawn setup
 *   - LQM animation flags from bg_public.h (A_LQM_*)
 */

#include "g_bot.h"


/*
=============================================================================
  CONSTANTS
=============================================================================
*/

/* Infantry running speed (world units/sec) */
#define INFANTRY_RUN_SPEED		200

/* Infantry walking speed */
#define INFANTRY_WALK_SPEED		100

/* Cover search radius */
#define INFANTRY_COVER_RADIUS	512

/* Cover position trace distance (how far to look for cover) */
#define INFANTRY_COVER_TRACE_DIST	256

/* Minimum distance from cover position to consider "in cover" */
#define INFANTRY_COVER_REACH	48

/* How long to stay in cover before peeking (ms) */
#define INFANTRY_COVER_DURATION	3000

/* Time between cover searches (ms) */
#define INFANTRY_COVER_SEARCH_INTERVAL	2000


/*
=============================================================================
  NAVIGATION
=============================================================================
*/

/*
  Bot_Infantry_Navigate — Move the infantry bot toward its waypoint or target.
  
  Infantry movement is simpler than vehicles in some ways (no throttle,
  no stall) but more complex in others (can crouch, take cover, sprint).
  
  Movement uses the same yaw-only steering as ground vehicles, but
  with walking/running speed instead of vehicle throttle.
  
  The LQM animation flags control the visual appearance:
  - A_LQM_FORWARD for walking
  - A_LQM_FORWARD + sprint flag for running
  - A_LQM_CROUCH for crouching
*/
void Bot_Infantry_Navigate( botState_t *bs )
{
	GameEntity *ent;
	vec3_t dir, bearing;
	float dist, diff, turnspeed;
	float timediff;
	float desiredSpeed;
	qboolean running = qfalse;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

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
	} else if( bs->state == BOT_STATE_FLEE ) {
		/* Flee: run away from the threat */
		GameEntity *threat = theLevel.getEntity( bs->targetEntityNum );
		if( threat && threat->inuse_ ) {
			VectorSubtract( ent->r.currentOrigin, threat->r.currentOrigin, dir );
			VectorNormalize( dir );
			/* Add some lateral movement to avoid running straight back */
			dir[0] += (float)(ent->s.number % 3 - 1) * 0.3f;
			VectorNormalize( dir );
			dist = 999;  /* Not really measuring distance here */
		} else {
			VectorSubtract( bs->homeOrigin, ent->r.currentOrigin, dir );
			dist = VectorNormalize( dir );
		}
		running = qtrue;
	} else if( bs->state == BOT_STATE_RETURN_TO_BASE ) {
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

	/* ---- YAW: Rotate toward target ---- */
	diff = bearing[1] - ent->s.apos.trBase[1];
	if( diff > 180 ) diff -= 360;
	else if( diff < -180 ) diff += 360;

	/* Infantry turns faster than vehicles */
	turnspeed = 120.0f * timediff;  /* 120 deg/sec */

	if( fabs( diff ) < turnspeed ) {
		ent->s.apos.trBase[1] = bearing[1];
		ent->s.apos.trDelta[1] = 0;
	} else if( diff > 0 ) {
		ent->s.apos.trBase[1] += turnspeed;
		ent->s.apos.trDelta[1] = 120.0f;
	} else {
		ent->s.apos.trBase[1] -= turnspeed;
		ent->s.apos.trDelta[1] = -120.0f;
	}
	ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );

	/* Update angles */
	VectorCopy( ent->s.apos.trBase, ent->r.currentAngles );
	ent->s.apos.trTime = theLevel.time_;

	/* ---- SPEED: Walk or run ---- */
	if( bs->state == BOT_STATE_ATTACK ) {
		/* Walk during combat for better aim */
		desiredSpeed = INFANTRY_WALK_SPEED;
	} else if( bs->state == BOT_STATE_CHASE || bs->state == BOT_STATE_FLEE ) {
		/* Run when chasing or fleeing */
		desiredSpeed = INFANTRY_RUN_SPEED;
		running = qtrue;
	} else {
		/* Walk during patrol */
		desiredSpeed = INFANTRY_WALK_SPEED;
	}

	/* Set movement via client's speed and animation */
	if( ent->client_ ) {
		vec3_t forward;
		AngleVectors( ent->s.apos.trBase, forward, NULL, NULL );
		VectorScale( forward, desiredSpeed, ent->s.pos.trDelta );
		VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
		ent->s.pos.trTime = theLevel.time_;
		ent->client_->ps_.speed = (int)desiredSpeed;

		/* Set animation flags */
		if( desiredSpeed > 0 ) {
			if( running ) {
				ent->client_->ps_.torsoAnim = A_LQM_FORWARD;  /* Run forward */
				ent->client_->ps_.legsAnim = A_LQM_FORWARD;
			} else {
				ent->client_->ps_.torsoAnim = A_LQM_FORWARD;  /* Walk forward */
				ent->client_->ps_.legsAnim = A_LQM_FORWARD;
			}
		} else {
			ent->client_->ps_.torsoAnim = A_LQM_STAND;
			ent->client_->ps_.legsAnim = A_LQM_STAND;
		}
	}
}


/*
=============================================================================
  COVER SEEKING
=============================================================================
*/

/*
  Bot_Infantry_SeekCover — Find and move to a cover position.
  
  When under fire, infantry bots try to find cover:
  1. Trace several directions from current position
  2. Find the nearest solid object that blocks line-of-sight to the threat
  3. Move to a position behind that object
  4. Stay in cover for a duration, then peek to return fire
  
  This is a simplified implementation. A more sophisticated version would:
  - Use pathfinding to find optimal cover
  - Consider multiple threats
  - Use suppressive fire from cover
*/
void Bot_Infantry_SeekCover( botState_t *bs )
{
	GameEntity *ent;
	GameEntity *threat;
	trace_t tr;
	vec3_t testDirs[8];
	vec3_t testEnd;
	vec3_t coverPos;
	float bestScore = -1;
	int bestDir = -1;
	int i;
	vec3_t up = {0, 0, 1};

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* Need a threat to seek cover from */
	if( bs->targetEntityNum < 0 ) return;

	threat = theLevel.getEntity( bs->targetEntityNum );
	if( !threat || !threat->inuse_ ) return;

	/* If already in cover and time hasn't expired, stay put */
	if( bs->wantsCover && 
		VectorLength( bs->coverOrigin ) > 0 &&
		theLevel.time_ - bs->fleeTimer < INFANTRY_COVER_DURATION ) {
		
		/* Move toward cover position */
		vec3_t toCover;
		float distToCover;
		VectorSubtract( bs->coverOrigin, ent->r.currentOrigin, toCover );
		distToCover = VectorNormalize( toCover );

		if( distToCover > INFANTRY_COVER_REACH ) {
			/* Still moving to cover */
			vec3_t bearing;
			vectoangles( toCover, bearing );
			ent->s.apos.trBase[1] = bearing[1];
			ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );
		} else {
			/* In cover — crouch and face the threat direction */
			vec3_t toThreat;
			VectorSubtract( threat->r.currentOrigin, ent->r.currentOrigin, toThreat );
			VectorNormalize( toThreat );
			{
				vec3_t bearing;
				vectoangles( toThreat, bearing );
				ent->s.apos.trBase[1] = bearing[1];
			}
			ent->s.apos.trBase[1] = AngleMod( ent->s.apos.trBase[1] );

			/* Crouch */
			if( ent->client_ ) {
				ent->client_->ps_.legsAnim = A_LQM_CROUCH;
				ent->client_->ps_.torsoAnim = A_LQM_CROUCH;
				ent->client_->ps_.speed = 0;
			}
		}
		return;
	}

	/* Search for cover: test 8 directions around us */
	for( i = 0; i < 8; i++ ) {
		float angle = (float)i * 45.0f;
		vec3_t forward;

		RotatePointAroundVector( forward, up, ent->s.apos.trBase, angle );
		VectorNormalize( forward );

		/* Trace from current position in this direction */
		VectorMA( ent->r.currentOrigin, INFANTRY_COVER_TRACE_DIST, forward, testEnd );
		SV_Trace( &tr, ent->r.currentOrigin, NULL, NULL, testEnd,
			ent->s.number, MASK_SOLID, qfalse );

		if( tr.fraction < 1.0f ) {
			/* Hit something — this could provide cover */
			/* Check if this position blocks line of sight to the threat */
			vec3_t behindCover;
			vec3_t coverToThreat;
			float dotProduct;

			/* Position just behind the cover object (toward us) */
			VectorMA( tr.endpos, -30, forward, behindCover );

			/* Check if cover blocks LOS to threat */
			VectorSubtract( threat->r.currentOrigin, behindCover, coverToThreat );
			VectorNormalize( coverToThreat );

			SV_Trace( &tr, behindCover, NULL, NULL, threat->r.currentOrigin,
				ent->s.number, MASK_SHOT, qfalse );

			if( tr.fraction < 0.9f ) {
				/* This cover blocks most of the LOS — good cover! */
				/* Score based on how much it blocks and how close it is */
				float score = tr.fraction;  /* Lower fraction = more blocking */
				vec3_t toCover;
				float coverDist;

				VectorSubtract( behindCover, ent->r.currentOrigin, toCover );
				coverDist = VectorLength( toCover );

				/* Prefer closer cover */
				score *= (1.0f - coverDist / INFANTRY_COVER_TRACE_DIST);

				if( score > bestScore ) {
					bestScore = score;
					bestDir = i;
					VectorCopy( behindCover, coverPos );
				}
			}
		}
	}

	if( bestDir >= 0 ) {
		/* Found cover — set it as our target */
		VectorCopy( coverPos, bs->coverOrigin );
		bs->wantsCover = qtrue;
		bs->fleeTimer = theLevel.time_;
	} else {
		/* No cover found — just run away */
		bs->wantsCover = qfalse;
	}
}


/*
=============================================================================
  COMBAT
=============================================================================
*/

/*
  Bot_Infantry_Combat — Infantry combat behavior.
  
  Infantry fights by:
  1. Facing the target
  2. Firing infantry weapons when aligned
  3. Taking cover when under heavy fire
  4. Using appropriate weapon type based on target category
*/
void Bot_Infantry_Combat( botState_t *bs )
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

	/* Face the target */
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

		turnspeed = 180.0f * timediff;  /* Infantry can turn quickly */

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

	/* Check if under heavy fire (took damage recently) — seek cover */
	if( ent->client_->damage_done_ > 0 && 
		theLevel.time_ - ent->pain_debounce_time_ < 1000 ) {
		Bot_Infantry_SeekCover( bs );
		return;  /* Don't fire while seeking cover */
	}

	/* Clear cover flag if no longer under fire */
	bs->wantsCover = qfalse;

	/* Fire weapons when aligned */
	if( Bot_ShouldFire( bs ) ) {
		FireWeapon( ent );
		bs->lastFireTime = theLevel.time_;
	}

	/* Deploy flares if being targeted (infantry can carry stingers) */
	if( ent->client_->ps_.stats[STAT_LOCKINFO] & LI_BEING_LOCKED ) {
		fire_flare( ent );
	}
}


/*
=============================================================================
  MAIN THINK
=============================================================================
*/

/*
  Bot_Infantry_Think — Main think function for infantry/LQM bots.
  Called every 100ms (FRAMETIME) via Bot_Think dispatch.
  
  Order of operations:
  1. Navigate (walk/run toward waypoint/target, or seek cover)
  2. Combat (face target, fire, take cover when under fire)
  3. Link entity
*/
void Bot_Infantry_Think( botState_t *bs )
{
	GameEntity *ent;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	/* 1. Navigate (includes cover seeking) */
	Bot_Infantry_Navigate( bs );

	/* 2. Combat */
	Bot_Infantry_Combat( bs );

	/* 3. Link entity */
	ent->nextthink_ = theLevel.time_ + FRAMETIME;
	SV_LinkEntity( ent );
}