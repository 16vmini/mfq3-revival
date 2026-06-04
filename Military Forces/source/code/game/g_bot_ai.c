/*
 * g_bot_ai.c — Core AI framework for MFQ3 bot system
 *
 * Implements the state machine, target acquisition, combat decisions,
 * and the main think dispatcher that routes to vehicle-specific AI.
 *
 * The state machine cycles through:
 *   IDLE → PATROL → CHASE → ATTACK → FLEE → RETURN_TO_BASE
 *
 * Adapted from original commented-out drone code in g_droneutil.c,
 * g_droneground.c, and g_droneplane.c.
 */

#include "g_bot.h"


/*
=============================================================================
  GLOBAL STATE
=============================================================================
*/

botGlobals_t botGlobals;


/*
=============================================================================
  INITIALIZATION
=============================================================================
*/

/*
  Bot_Init — Initialize the entire bot system. Call once at game init.
  Sets up the global state, waypoint system, and registers console commands.
*/
void Bot_Init( void )
{
	memset( &botGlobals, 0, sizeof(botGlobals) );
	Bot_InitWaypoints();
	Bot_RegisterCommands();
	botGlobals.initialized = qtrue;
	Com_Printf( "Bot AI system initialized\n" );
}


/*
=============================================================================
  STATE MACHINE
=============================================================================
*/

/*
  Bot_SetState — Transition a bot to a new AI state with debug logging.
*/
void Bot_SetState( botState_t *bs, botAIState_t newState )
{
	if( !bs || bs->state == newState ) return;

#ifdef _DEBUG
	Com_Printf( "Bot %d: state %d -> %d\n", bs->entityNum, bs->state, newState );
#endif

	bs->state = newState;

	/* Reset state-specific timers */
	switch( newState ) {
	case BOT_STATE_ATTACK:
		bs->combatTimer = theLevel.time_;
		break;
	case BOT_STATE_FLEE:
		bs->fleeTimer = theLevel.time_;
		break;
	default:
		break;
	}
}


/*
  Bot_UpdateState — Evaluate current situation and decide state transitions.
  
  This is the heart of the AI decision-making. Called every think frame.
  
  State transition logic:
  
  IDLE:
    - If has waypoints → PATROL
    - If enemies nearby → CHASE
    
  PATROL:
    - If enemies in range → CHASE
    - If health low → FLEE
    
  CHASE:
    - If close enough to target → ATTACK
    - If target lost → PATROL
    - If health low → FLEE
    
  ATTACK:
    - If combat timeout (target still alive after too long) → CHASE
    - If target dead → PATROL
    - If health low → FLEE
    
  FLEE:
    - If flee timeout → RETURN_TO_BASE
    - If far from threat → PATROL
    
  RETURN_TO_BASE:
    - If near home → IDLE
*/
void Bot_UpdateState( botState_t *bs )
{
	GameEntity *ent;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return;

	switch( bs->state ) {

	case BOT_STATE_IDLE:
		/* Idle bots start patrolling if they have waypoints */
		if( bs->currentWaypoint >= 0 ) {
			Bot_SetState( bs, BOT_STATE_PATROL );
		}
		/* Otherwise, scan for targets */
		else if( theLevel.time_ - bs->lastScanTime > bs->scanInterval ) {
			bs->lastScanTime = theLevel.time_;
			if( Bot_AcquireTarget( bs ) >= 0 ) {
				Bot_SetState( bs, BOT_STATE_CHASE );
			}
		}
		break;

	case BOT_STATE_PATROL:
		/* Check for enemies */
		if( theLevel.time_ - bs->lastScanTime > bs->scanInterval ) {
			bs->lastScanTime = theLevel.time_;
			if( Bot_AcquireTarget( bs ) >= 0 ) {
				Bot_SetState( bs, BOT_STATE_CHASE );
				return;
			}
		}
		/* Check health */
		if( Bot_ShouldFlee( bs ) ) {
			Bot_SetState( bs, BOT_STATE_FLEE );
		}
		break;

	case BOT_STATE_CHASE:
		/* Validate target still exists */
		if( bs->targetEntityNum < 0 ) {
			Bot_SetState( bs, BOT_STATE_PATROL );
			break;
		}
		{
			GameEntity *target = theLevel.getEntity( bs->targetEntityNum );
			vec3_t d;
			if( !target || !target->inuse_ || target->health_ <= 0 ) {
				bs->targetEntityNum = -1;
				Bot_SetState( bs, BOT_STATE_PATROL );
				break;
			}
			/* keep distance fresh each frame so engage/disengage tracks motion */
			VectorSubtract( target->r.currentOrigin, ent->r.currentOrigin, d );
			bs->targetDist = VectorLength( d );
		}
		/* target flew away -> forget it and drop back to circling */
		if( bs->targetDist > BOT_DISENGAGE_RADIUS ) {
			bs->targetEntityNum = -1;
			Bot_SetState( bs, BOT_STATE_PATROL );
			break;
		}
		/* close enough to open fire? */
		if( bs->targetDist < BOT_ATTACK_RADIUS ) {
			Bot_SetState( bs, BOT_STATE_ATTACK );
		}
		/* Health check */
		if( Bot_ShouldFlee( bs ) ) {
			Bot_SetState( bs, BOT_STATE_FLEE );
		}
		break;

	case BOT_STATE_ATTACK:
		/* Validate target */
		if( bs->targetEntityNum < 0 ) {
			Bot_SetState( bs, BOT_STATE_PATROL );
			break;
		}
		{
			GameEntity *target = theLevel.getEntity( bs->targetEntityNum );
			vec3_t d;
			if( !target || !target->inuse_ || target->health_ <= 0 ) {
				bs->targetEntityNum = -1;
				Bot_SetState( bs, BOT_STATE_PATROL );
				break;
			}
			VectorSubtract( target->r.currentOrigin, ent->r.currentOrigin, d );
			bs->targetDist = VectorLength( d );
		}
		/* target flew away entirely -> circle again */
		if( bs->targetDist > BOT_DISENGAGE_RADIUS ) {
			bs->targetEntityNum = -1;
			Bot_SetState( bs, BOT_STATE_PATROL );
			break;
		}
		/* drifted out of firing range but still trackable -> chase to re-close */
		if( bs->targetDist > BOT_ATTACK_RADIUS ) {
			Bot_SetState( bs, BOT_STATE_CHASE );
			break;
		}
		/* Health check */
		if( Bot_ShouldFlee( bs ) ) {
			Bot_SetState( bs, BOT_STATE_FLEE );
		}
		break;

	case BOT_STATE_FLEE:
		/* Flee timeout — head home */
		if( theLevel.time_ - bs->fleeTimer > BOT_FLEE_TIMEOUT ) {
			Bot_SetState( bs, BOT_STATE_RETURN_TO_BASE );
		}
		/* If we've recovered health or the threat is gone, resume patrol */
		if( !Bot_ShouldFlee( bs ) && theLevel.time_ - bs->fleeTimer > 3000 ) {
			Bot_SetState( bs, BOT_STATE_PATROL );
		}
		break;

	case BOT_STATE_RETURN_TO_BASE:
		{
			float homeDist;
			vec3_t homeDir;
			VectorSubtract( bs->homeOrigin, ent->r.currentOrigin, homeDir );
			homeDist = VectorLength( homeDir );
			if( homeDist < BOT_WAYPOINT_REACH * 2 ) {
				Bot_SetState( bs, BOT_STATE_IDLE );
			}
		}
		break;

	case BOT_STATE_TAKEOFF:
		/* Takeoff state is managed by vehicle-specific code */
		break;

	case BOT_STATE_LANDING:
		/* Landing state is managed by vehicle-specific code */
		break;

	default:
		break;
	}
}


/*
=============================================================================
  TARGET ACQUISITION
=============================================================================
*/

/*
  Bot_AcquireTarget — Scan for enemy entities within scan range.
  
  Scans all entities in range. Prioritizes targets by:
  1. Closest distance
  2. Lowest health (tiebreaker)
  
  Returns the entity number of the best target, or -1 if none found.
  
  For team games, only targets entities on the opposing team.
*/
int Bot_AcquireTarget( botState_t *bs )
{
	GameEntity *ent;
	GameEntity *candidate;
	float bestDist = BOT_ENGAGE_RADIUS + 1;
	float dist;
	int bestTarget = -1;
	vec3_t diff;
	int i, count;
	int entityList[128];
	vec3_t mins, maxs;
	vec3_t botForward, botAngles;

	if( !bs || !bs->active ) return -1;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return -1;

	/* the bot's nose direction — used to gate spotting to its front 180 deg
	   (approach from behind and it won't see you) */
	if( ent->client_ ) VectorCopy( ent->client_->ps_.vehicleAngles, botAngles );
	else               VectorCopy( ent->s.apos.trBase, botAngles );
	AngleVectors( botAngles, botForward, NULL, NULL );

	/* Build a bounding box around the bot's position for area search */
	VectorSet( mins, ent->r.currentOrigin[0] - BOT_SCAN_RADIUS,
				 ent->r.currentOrigin[1] - BOT_SCAN_RADIUS,
				 ent->r.currentOrigin[2] - BOT_SCAN_RADIUS );
	VectorSet( maxs, ent->r.currentOrigin[0] + BOT_SCAN_RADIUS,
				 ent->r.currentOrigin[1] + BOT_SCAN_RADIUS,
				 ent->r.currentOrigin[2] + BOT_SCAN_RADIUS );

	/* Find entities in the area */
	count = SV_AreaEntities( mins, maxs, entityList, 128 );

	for( i = 0; i < count; i++ ) {
		candidate = theLevel.getEntity( entityList[i] );
		if( !candidate ) continue;
		if( !candidate->inuse_ ) continue;
		if( candidate->health_ <= 0 ) continue;
		if( candidate->s.number == bs->entityNum ) continue;  /* Don't target self */

		/* Team check: in team games, only target enemies */
		if( g_gametype.integer >= GT_TEAM ) {
			if( candidate->client_ ) {
				int myTeam = ent->client_ ? ent->client_->ps_.persistant[PERS_TEAM] : bs->team;
				int theirTeam = candidate->client_->ps_.persistant[PERS_TEAM];
				if( myTeam == theirTeam ) continue;  /* Same team, skip */
			}
		}

		/* Only target vehicles/infantry (entities with client) */
		if( !candidate->client_ ) continue;

		/* Line-of-sight check */
		{
			trace_t tr;
			SV_Trace( &tr, ent->r.currentOrigin, NULL, NULL,
				candidate->r.currentOrigin, ent->s.number, MASK_SHOT, qfalse );
			if( tr.entityNum != entityList[i] && tr.fraction < 1.0f ) {
				continue;  /* Blocked by world */
			}
		}

		/* Distance check (must be within engage range) */
		VectorSubtract( candidate->r.currentOrigin, ent->r.currentOrigin, diff );
		dist = VectorLength( diff );
		if( dist > BOT_ENGAGE_RADIUS ) continue;

		/* Front-180 vision gate: only spot targets ahead of the nose.
		   dot(forward, dirToTarget) > 0 == within the forward hemisphere. */
		if( dist > 1.0f ) {
			vec3_t dirNorm;
			VectorScale( diff, 1.0f / dist, dirNorm );
			if( DotProduct( botForward, dirNorm ) <= BOT_VISION_DOT ) {
				continue;	/* behind the bot — unseen */
			}
		}

		if( dist < bestDist ) {
			bestDist = dist;
			bestTarget = entityList[i];
		}
	}

	bs->targetEntityNum = bestTarget;
	bs->targetDist = bestDist;

	return bestTarget;
}


/*
=============================================================================
  COMBAT DECISIONS
=============================================================================
*/

/*
  Bot_ShouldFlee — Determine if the bot should flee based on health.
  Returns qtrue if health is below the flee threshold.
*/
qboolean Bot_ShouldFlee( botState_t *bs )
{
	GameEntity *ent;
	int maxHealth;

	if( !bs ) return qfalse;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return qfalse;

	maxHealth = availableVehicles[bs->vehicleIndex].maxhealth;
	if( maxHealth <= 0 ) return qfalse;

	if( (float)ent->health_ / (float)maxHealth < BOT_FLEE_HEALTH_FRAC ) {
		return qtrue;
	}

	return qfalse;
}


/*
  Bot_ShouldFire — Determine if the bot should fire its weapons.
  Checks angle alignment to target and fire interval timer.
  Returns qtrue if weapons should be fired this frame.
*/
qboolean Bot_ShouldFire( botState_t *bs )
{
	GameEntity *ent;
	GameEntity *target;
	vec3_t forward, dirToTarget, angles;
	float angleDiff;

	if( !bs || bs->targetEntityNum < 0 ) return qfalse;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) return qfalse;

	target = theLevel.getEntity( bs->targetEntityNum );
	if( !target || !target->inuse_ || target->health_ <= 0 ) return qfalse;

	/* Check fire interval timer */
	if( theLevel.time_ - bs->lastFireTime < bs->fireInterval ) {
		return qfalse;
	}

	/* Check angle alignment: how close is our forward to the target? */
	AngleVectors( ent->s.apos.trBase, forward, NULL, NULL );
	VectorSubtract( target->r.currentOrigin, ent->r.currentOrigin, dirToTarget );
	VectorNormalize( dirToTarget );

	/* Dot product gives cos of angle between forward and target direction */
	angleDiff = DotProduct( forward, dirToTarget );

	/* cos(5°) ≈ 0.996; if angle within tolerance, fire */
	if( angleDiff > (1.0f - BOT_FIRE_ANGLE_TOL * DEG2RAD(1)) ) {
		return qtrue;
	}

	/* Looser tolerance for less skilled bots */
	if( bs->aimSkill < 0.5f && angleDiff > 0.95f ) {
		return qtrue;
	}

	return qfalse;
}


/*
=============================================================================
  MAIN THINK DISPATCHER
=============================================================================
*/

/*
  Bot_Think — Main think function called every FRAMETIME (100ms).
  Dispatches to vehicle-specific think based on CAT_* category.
*/
void Bot_Think( botState_t *bs )
{
	if( !bs || !bs->active ) return;

	/* Update the state machine */
	Bot_UpdateState( bs );

	/* Dispatch to vehicle-specific think */
	switch( bs->vehicleCat ) {

	case CAT_PLANE:
		Bot_Plane_Think( bs );
		break;

	case CAT_GROUND:
		Bot_Ground_Think( bs );
		break;

	case CAT_HELO:
		Bot_Helo_Think( bs );
		break;

	case CAT_LQM:
		Bot_Infantry_Think( bs );
		break;

	case CAT_BOAT:
		Bot_Boat_Think( bs );
		break;

	default:
		Com_Printf( S_COLOR_YELLOW "Bot_Think: unknown vehicle category %d\n", bs->vehicleCat );
		break;
	}
}


/*
=============================================================================
  GLOBAL THINK TICK — Called from the game's main loop
=============================================================================
*/

/*
  Bot_Frame — Called once per server frame. Iterates all active bots
  and runs their think functions. This should be called from G_RunFrame
  or equivalent.
  
  Each bot thinks at BOT_THINK_INTERVAL (100ms). We track when each bot
  last thought and only run it when enough time has elapsed.
*/
/*
  Bot_DriveVehicle — translate the bot's intent into a usercmd_t.

  The bot now inhabits a real client, so the engine runs Pmove on it from
  ent->client_->pers_.cmd_. The plane/helo flight models steer their
  vehicleAngles TOWARD the view angles derived from that usercmd, so we just
  point the view at the current waypoint and hold throttle. This replaces the
  PR's direct ps_/trDelta writes (which Pmove would overwrite).
*/
static void Bot_DriveVehicle( botState_t *bs )
{
	GameEntity	*ent;
	vec3_t		target, dir, desired, fwd;
	usercmd_t	*cmd;
	float		dist;
	int			i;

	if( !bs || !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent || !ent->client_ ) return;

	cmd = &ent->client_->pers_.cmd_;

	/* Stationary ground target (bot_target): just kill its velocity each frame so
	   it can't drift or fall -- no origin-snapping (that caused the bounce). It's
	   godmode (set in Bot_AddTank_f) so terrain can't crush it. */
	if( bs->vehicleCat & CAT_GROUND ) {
		VectorClear( ent->client_->ps_.velocity );
		cmd->serverTime = theLevel.time_;
		cmd->forwardmove = 0; cmd->rightmove = 0; cmd->upmove = 0; cmd->buttons = 0;
		return;
	}

	if( !( bs->vehicleCat & ( CAT_PLANE | CAT_HELO ) ) ) return;   /* air only beyond here */

	/* Decide where to point: run the target down when engaged, otherwise fly a
	   gentle wide circle so the bot loiters near the player and stays trackable
	   (a tight circle is unhittable, a straight line flies off the map). */
	{
		GameEntity	*tgt = ( bs->targetEntityNum >= 0 ) ? theLevel.getEntity( bs->targetEntityNum ) : NULL;
		bool		engaged = ( bs->state == BOT_STATE_CHASE || bs->state == BOT_STATE_ATTACK )
							  && tgt && tgt->inuse_ && tgt->health_ > 0;

		if( engaged ) {
			/* steer straight at the target's current position */
			VectorCopy( tgt->r.currentOrigin, target );
		} else if( bs->holdStraight ) {
			/* fly straight ahead on the current heading -- a dead-simple chase
			   target (used by the easy training missions) */
			vec3_t flat;
			VectorCopy( ent->client_->ps_.vehicleAngles, flat );
			flat[PITCH] = 0;
			flat[ROLL]  = 0;
			AngleVectors( flat, fwd, NULL, NULL );
			VectorMA( ent->r.currentOrigin, 1200, fwd, target );
		} else {
			/* gentle continuous turn -> wide circle, wings level */
			vec3_t flat;
			VectorCopy( ent->client_->ps_.vehicleAngles, flat );
			flat[PITCH] = 0;
			flat[ROLL]  = 0;
			flat[YAW]  += 25;
			AngleVectors( flat, fwd, NULL, NULL );
			VectorMA( ent->r.currentOrigin, 1200, fwd, target );
		}

		VectorSubtract( target, ent->r.currentOrigin, dir );
		dist = VectorNormalize( dir );

		/* (waypoint loop advance, only while loitering) */
		if( !engaged && bs->currentWaypoint >= 0 && dist < 500 ) {
			bs->currentWaypoint = botGlobals.waypointList.waypoints[bs->currentWaypoint].nextWaypointIndex;
		}

		vectoangles( dir, desired );

		/* TERRAIN AVOIDANCE: trace ahead of the nose; if solid world is close,
		   probe both sides and break toward whichever is MORE open (so it doesn't
		   turn straight into the obstacle) while pulling the nose up to climb. */
		{
			vec3_t	nose, ahead, lang, rang, ldir, rdir, lend, rend, flat;
			trace_t	tc, tl, tr;
			VectorCopy( ent->client_->ps_.vehicleAngles, flat );
			flat[PITCH] = 0;
			flat[ROLL]  = 0;
			AngleVectors( flat, nose, NULL, NULL );
			VectorMA( ent->r.currentOrigin, 1500, nose, ahead );
			SV_Trace( &tc, ent->r.currentOrigin, NULL, NULL, ahead, ent->s.number, MASK_SOLID, false );
			if( tc.fraction < 1.0f ) {
				/* probe 50 deg left and right */
				VectorCopy( flat, lang ); lang[YAW] += 50.0f;
				VectorCopy( flat, rang ); rang[YAW] -= 50.0f;
				AngleVectors( lang, ldir, NULL, NULL );
				AngleVectors( rang, rdir, NULL, NULL );
				VectorMA( ent->r.currentOrigin, 1500, ldir, lend );
				VectorMA( ent->r.currentOrigin, 1500, rdir, rend );
				SV_Trace( &tl, ent->r.currentOrigin, NULL, NULL, lend, ent->s.number, MASK_SOLID, false );
				SV_Trace( &tr, ent->r.currentOrigin, NULL, NULL, rend, ent->s.number, MASK_SOLID, false );
				/* steer toward the side with more clearance (YAW+ = left in Q3);
				   a straight-flying bot only climbs, it doesn't break course */
				if( !bs->holdStraight ) {
					if( tl.fraction >= tr.fraction )
						desired[YAW] = flat[YAW] + 60.0f;
					else
						desired[YAW] = flat[YAW] - 60.0f;
				}
				desired[PITCH] = -30.0f;   /* nose up hard (negative pitch climbs) */
			}
		}

		/* ALTITUDE FLOOR: trace straight down; if the ground is close below, pull
		   the nose up so the bot rides up and over rising terrain (mountains)
		   regardless of which way it's heading. Only ever steepens the climb. */
		{
			vec3_t	gdown;
			trace_t	tg;
			VectorCopy( ent->r.currentOrigin, gdown );
			gdown[2] -= 1000.0f;
			SV_Trace( &tg, ent->r.currentOrigin, NULL, NULL, gdown, ent->s.number, MASK_SOLID, false );
			if( tg.fraction < 1.0f ) {
				float agl = 1000.0f * tg.fraction;   /* height above ground */
				if( agl < 600.0f ) {
					float climb = -25.0f - ( 600.0f - agl ) * 0.06f;   /* steeper when lower */
					if( climb < -60.0f ) climb = -60.0f;
					if( desired[PITCH] > climb ) desired[PITCH] = climb;
				}
			}
		}

		/* build the usercmd so ps_.viewangles resolves to `desired`
		   (viewangles = SHORT2ANGLE(cmd.angles + delta_angles)) */
		cmd->serverTime = theLevel.time_;
		for( i = 0; i < 3; i++ ) {
			cmd->angles[i] = ANGLE2SHORT( desired[i] ) - ent->client_->ps_.delta_angles[i];
		}
		/* keep wings level while circling; let the pursuit bank/pitch freely */
		if( !engaged ) {
			cmd->angles[ROLL] = (short)( 0 - ent->client_->ps_.delta_angles[ROLL] );
		}
		cmd->forwardmove = 0;
		cmd->rightmove = 0;
		cmd->upmove = 0;
		cmd->buttons = 0;

		/* loiter slowly; push the throttle up to chase a target down */
		ent->client_->ps_.fixed_throttle = engaged ? 10 : 6;

		/* fire only when actually lined up (front-aligned + fire-rate cooldown) */
		if( bs->state == BOT_STATE_ATTACK && Bot_ShouldFire( bs ) ) {
			cmd->buttons |= BUTTON_ATTACK;
			bs->lastFireTime = theLevel.time_;
		}
	}
	/* (hitbox enlargement for relaxed detection is done in ClientThink_real,
	   right after Pmove, so it survives into the bullet-collision pass) */
}


void Bot_Frame( void )
{
	int i;

	if( !botGlobals.initialized ) return;

	for( i = 0; i < MAX_BOTS; i++ ) {
		botState_t *bs = &botGlobals.bots[i];

		if( !bs->active ) continue;

		/* Drop dead/limbo bots so the AI never processes a stale slot (this was
		   the multi-bot crash: a killed/respawning bot left an invalid entity). */
		{
			GameEntity* be = theLevel.getEntity( bs->entityNum );
			if( !be || !be->inuse_ || !be->client_ ||
				be->health_ <= 0 || be->s.eType != ET_VEHICLE ) {
				bs->active = qfalse;
				if( botGlobals.numBots > 0 ) botGlobals.numBots--;
				continue;
			}
		}

		/* Check if it's time for this bot to think */
		/* For simplicity, every frame is fine since FRAMETIME = 100ms = BOT_THINK_INTERVAL */
		Bot_Think( bs );

		/* turn the bot's intent into a usercmd that Pmove will fly */
		Bot_DriveVehicle( bs );
	}
}


/*
=============================================================================
  SPAWN / DESPAWN
=============================================================================
*/

/*
  Bot_Spawn — Create a new bot entity and add it to the bot system.
  
  team:          MF_TEAM_1 or MF_TEAM_2
  vehicleIndex:  index into availableVehicles[]
  origin:        spawn position
  angles:        spawn angles
  
  Returns slot index in botGlobals.bots[], or -1 on failure.
  
  This function:
  1. Finds a free bot slot
  2. Spawns a GameEntity via the MFQ3 spawn system
  3. Initializes the vehicle via MF_Spawn_* functions
  4. Sets up the bot state
  5. Assigns the bot think function
*/
int Bot_Spawn( int team, int vehicleIndex, vec3_t origin, vec3_t angles )
{
	int slot;
	int clientNum;
	botState_t *bs;
	GameEntity *ent;
	completeVehicleData_t *vd;

	if( !botGlobals.initialized ) {
		Com_Printf( S_COLOR_RED "Bot_Spawn: bot system not initialized\n" );
		return -1;
	}

	/* Find a free slot */
	for( slot = 0; slot < MAX_BOTS; slot++ ) {
		if( !botGlobals.bots[slot].active ) break;
	}
	if( slot >= MAX_BOTS ) {
		Com_Printf( S_COLOR_RED "Bot_Spawn: no free bot slots (max %d)\n", MAX_BOTS );
		return -1;
	}

	/* Validate vehicle index */
	if( vehicleIndex < 0 || vehicleIndex >= bg_numberOfVehicles ) {
		Com_Printf( S_COLOR_RED "Bot_Spawn: invalid vehicle index %d\n", vehicleIndex );
		return -1;
	}

	vd = &availableVehicles[vehicleIndex];

	/* Spawn a real bot CLIENT. SV_AddBot allocates a client slot, connects it as
	   a bot, and runs the normal MF_ClientSpawn path — so the vehicle gets a
	   valid client_/playerState that the AI (and Pmove) can drive. This replaces
	   the PR's placeholder entity spawn, which left client_ NULL and crashed. */
	{
		char botname[32];
		Com_sprintf( botname, sizeof(botname), "Drone%d", slot );
		clientNum = SV_AddBot( vehicleIndex, team, botname );
	}
	if( clientNum < 0 ) {
		Com_Printf( S_COLOR_RED "Bot_Spawn: SV_AddBot failed\n" );
		return -1;
	}

	ent = theLevel.getEntity( clientNum );
	if( !ent || !ent->client_ ) {
		Com_Printf( S_COLOR_RED "Bot_Spawn: bot client %d has no entity/client\n", clientNum );
		return -1;
	}

	/* MF_ClientSpawn builds a fresh vehicle entity, which loses the SVF_BOT flag
	   set at connect. Without it, G_RunClient skips ClientThink_real for this
	   client -- so the vehicle never transitions ET_INVISIBLE -> ET_VEHICLE (stays
	   invisible) and Pmove never runs (no movement). Re-assert it here. */
	ent->r.svFlags |= SVF_BOT;

	/* Reposition the freshly-spawned bot to the requested spot (e.g. near the
	   player for circling), if a non-zero origin was supplied. */
	if( origin && ( origin[0] || origin[1] || origin[2] ) ) {
		VectorCopy( origin, ent->s.pos.trBase );
		VectorCopy( origin, ent->r.currentOrigin );
		VectorCopy( origin, ent->client_->ps_.origin );
		if( angles ) {
			VectorCopy( angles, ent->s.apos.trBase );
			VectorCopy( angles, ent->client_->ps_.vehicleAngles );
		}
		SV_LinkEntity( ent );
	}

	/* Initialize bot state */
	bs = &botGlobals.bots[slot];
	memset( bs, 0, sizeof(botState_t) );
	bs->active = qtrue;
	bs->entityNum = clientNum;
	bs->team = team;
	bs->vehicleCat = vd->cat;
	bs->vehicleIndex = vehicleIndex;
	bs->state = BOT_STATE_IDLE;
	bs->targetEntityNum = -1;
	bs->currentWaypoint = -1;
	bs->patrolStartWaypoint = -1;
	bs->scanInterval = 500;   /* Scan every 500ms */
	bs->lastScanTime = 0;
	bs->fireInterval = 200;   /* Default fire interval */
	bs->aimSkill = 0.7f;      /* Default aim skill */

	/* Air vehicle specific */
	bs->cruiseAltitude = 1500.0f;
	bs->wantsTakeoff = qfalse;
	bs->wantsLanding = qfalse;

	/* Ground vehicle specific */
	bs->wantsStop = qfalse;
	bs->stuckTimer = 0;

	/* Infantry specific */
	bs->wantsCover = qfalse;

	/* Script range */
	bs->idxScriptBegin = 0;
	bs->idxScriptEnd = 0;

	/* Store home position */
	VectorCopy( origin, bs->homeOrigin );
	VectorCopy( angles, bs->homeAngles );
	VectorCopy( origin, bs->lastPos );

	/* If there are waypoints, assign first one */
	if( botGlobals.waypointList.usedWPs > 0 ) {
		bs->currentWaypoint = 0;
		bs->patrolStartWaypoint = 0;
	}

	botGlobals.numBots++;

	Com_Printf( "Bot spawned: slot %d, entity %d, vehicle %s (cat %d)\n",
		slot, bs->entityNum, vd->tinyName, bs->vehicleCat );

	return slot;
}


/*
  Bot_Remove — Remove a bot by slot index.
  Frees the game entity and clears the bot state.
*/
void Bot_Remove( int slotIndex )
{
	GameEntity *ent;
	botState_t *bs;

	if( slotIndex < 0 || slotIndex >= MAX_BOTS ) return;

	bs = &botGlobals.bots[slotIndex];
	if( !bs->active ) return;

	ent = theLevel.getEntity( bs->entityNum );
	if( ent ) {
		ent->freeUp();
	}

	memset( bs, 0, sizeof(botState_t) );
	botGlobals.numBots--;

	Com_Printf( "Bot removed from slot %d\n", slotIndex );
}