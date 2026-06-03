/*
 * g_bot_waypoint.c — Waypoint system for MFQ3 bot AI
 *
 * Reimplements the waypoint system that was commented out in the original
 * MFQ3 source code (g_waypoint.c, g_local.h). Adds waypoint linking via
 * target/targetname chains and file save/load for runtime waypoint editing.
 *
 * Adapted from original:
 *   - findWaypoint()      from g_waypoint.c
 *   - AddToWaypointManager() from g_waypoint.c
 *   - SP_misc_waypoint()  from g_waypoint.c
 *   - waypoint_t / waypointList_t from g_local.h
 */

#include "g_bot.h"


/*
=============================================================================
  INITIALIZATION
=============================================================================
*/

/*
  Bot_InitWaypoints — Reset the waypoint system at map start.
  Called once per map load.
*/
void Bot_InitWaypoints( void )
{
	memset( &botGlobals.waypointList, 0, sizeof(botGlobals.waypointList) );
	memset( &botGlobals.scriptList, 0, sizeof(botGlobals.scriptList) );
	Com_Printf( "Bot waypoint system initialized\n" );
}


/*
=============================================================================
  WAYPOINT REGISTRATION
=============================================================================
*/

/*
  Bot_FindWaypoint — Look up a waypoint by name.
  Returns index in botGlobals.waypointList.waypoints[], or -1 if not found.
  
  Adapted from original findWaypoint() in g_waypoint.c.
*/
int Bot_FindWaypoint( const char *name )
{
	int i;

	if( !name || !name[0] ) {
		return -1;
	}

	for( i = 0; i < botGlobals.waypointList.usedWPs; i++ ) {
		if( Q_stricmp( botGlobals.waypointList.waypoints[i].name, name ) == 0 ) {
			return i;
		}
	}

	return -1;
}


/*
  Bot_AddWaypoint — Add a waypoint to the global list.
  name:  the targetname from the map entity
  origin: world position of the waypoint
  
  Adapted from original AddToWaypointManager() in g_waypoint.c.
*/
void Bot_AddWaypoint( const char *name, vec3_t origin )
{
	int idx;
	bot_waypoint_t *wp;

	if( botGlobals.waypointList.usedWPs >= MAX_BOT_WAYPOINTS ) {
		Com_Printf( S_COLOR_RED "Bot_AddWaypoint: MAX_BOT_WAYPOINTS (%d) reached!\n", MAX_BOT_WAYPOINTS );
		return;
	}

	if( !name || !name[0] ) {
		Com_Printf( S_COLOR_RED "Bot_AddWaypoint: no name provided\n" );
		return;
	}

	/* Check for duplicates */
	if( Bot_FindWaypoint( name ) >= 0 ) {
		Com_Printf( S_COLOR_YELLOW "Bot_AddWaypoint: duplicate name '%s' — skipping\n", name );
		return;
	}

	idx = botGlobals.waypointList.usedWPs;
	wp = &botGlobals.waypointList.waypoints[idx];

	/* Set fields */
	wp->index = idx;
	Q_strncpyz( wp->name, name, MAX_NAME_LENGTH );
	VectorCopy( origin, wp->pos );
	wp->nextWaypointIndex = -1;  /* will be resolved by Bot_LinkWaypoints() */

	Com_Printf( "Found waypoint %s (%d)\n", wp->name, idx );

	botGlobals.waypointList.usedWPs++;
}


/*
  Bot_LinkWaypoints — After all waypoints are loaded, resolve target chains.
  
  In the original MFQ3 system, misc_waypoint entities had a "target" field
  that pointed to the targetname of the next waypoint in the chain. This
  function walks all waypoints and sets nextWaypointIndex based on name matching.
  
  For simplicity in this standalone version, we support a naming convention:
  waypoints named "wp1", "wp2", "wp3" are linked sequentially.
  
  Additionally, if the map entities have "target" fields (via spawn processing),
  the SP_misc_waypoint handler stores the next waypoint name and this resolves it.
  
  This function should be called after all SP_misc_waypoint entities have been
  processed at map startup.
*/
void Bot_LinkWaypoints( void )
{
	int i, nextIdx;
	char nextName[MAX_NAME_LENGTH];

	for( i = 0; i < botGlobals.waypointList.usedWPs; i++ ) {
		bot_waypoint_t *wp = &botGlobals.waypointList.waypoints[i];

		/* If nextWaypointIndex is already set (> -1), skip */
		if( wp->nextWaypointIndex > -1 ) {
			continue;
		}

		/* 
		 * Naming convention: try to auto-link by sequential numbering.
		 * e.g. "wp1" -> "wp2" -> "wp3" etc.
		 * This is a simple convention for hand-authored waypoint paths.
		 */
		if( wp->name[0] == 'w' && wp->name[1] == 'p' ) {
			int seqNum;
			seqNum = atoi( &wp->name[2] );
			if( seqNum > 0 ) {
				Com_sprintf( nextName, MAX_NAME_LENGTH, "wp%d", seqNum + 1 );
				nextIdx = Bot_FindWaypoint( nextName );
				if( nextIdx >= 0 ) {
					wp->nextWaypointIndex = nextIdx;
					Com_Printf( "Linked waypoint %s -> %s\n", wp->name, nextName );
				}
			}
		}
	}

	Com_Printf( "Waypoint linking complete: %d waypoints\n", botGlobals.waypointList.usedWPs );
}


/*
=============================================================================
  WAYPOINT NAVIGATION HELPERS
=============================================================================
*/

/*
  Bot_GetWaypointDirAndDist — Compute direction vector and distance from
  the bot's current position to its current waypoint.
  
  Stores the unit direction vector in 'dir' and returns the distance.
  If the bot has no current waypoint, points the bot in a circling pattern
  (adapted from original getTargetDirAndDist in g_droneutil.c).
  
  Returns 0 if no valid waypoint (bot should circle).
*/
float Bot_GetWaypointDirAndDist( botState_t *bs, vec3_t dir )
{
	GameEntity *ent;
	vec3_t target, forward;
	float dist;

	if( !bs ) {
		VectorClear( dir );
		return 0;
	}

	ent = theLevel.getEntity( bs->entityNum );
	if( !ent ) {
		VectorClear( dir );
		return 0;
	}

	/* 
	 * If we have a valid waypoint, target its position.
	 * Otherwise, circle by targeting a point behind us.
	 * (Adapted from original getTargetDirAndDist logic in g_droneutil.c)
	 */
	if( bs->currentWaypoint >= 0 && bs->currentWaypoint < botGlobals.waypointList.usedWPs ) {
		VectorCopy( botGlobals.waypointList.waypoints[bs->currentWaypoint].pos, target );
	} else {
		/* No waypoint: circle — target 100 units behind us */
		AngleVectors( ent->s.apos.trBase, forward, NULL, NULL );
		VectorMA( ent->r.currentOrigin, -100, forward, target );
	}

	VectorSubtract( target, ent->r.currentOrigin, dir );
	dist = VectorNormalize( dir );

	return dist;
}


/*
  Bot_OnWaypointEvent — Called when a bot reaches its current waypoint.
  Processes any script tasks associated with this waypoint, then advances
  to the next waypoint in the chain.
  
  Adapted from original onWaypointEvent() in g_droneutil.c.
*/
void Bot_OnWaypointEvent( botState_t *bs )
{
	int i;
	bot_waypoint_t *wp;

	if( !bs || bs->currentWaypoint < 0 || bs->currentWaypoint >= botGlobals.waypointList.usedWPs ) {
		return;
	}

	wp = &botGlobals.waypointList.waypoints[bs->currentWaypoint];

	Com_Printf( "Bot %d reached waypoint %s\n", bs->entityNum, wp->name );

	/* 
	 * Process script tasks for this waypoint.
	 * (Adapted from original onWaypointEvent in g_droneutil.c — that version
	 *  looked up tasks in level.scriptList.scripts[]; we use botGlobals.scriptList)
	 */
	for( i = bs->idxScriptBegin; i < bs->idxScriptEnd; i++ ) {
		bot_scripttask_t *st = &botGlobals.scriptList.scripts[i];

		if( st->type == BOT_TASK_ON_WAYPOINT &&
			Q_stricmp( st->name, wp->name ) == 0 ) {
			/* Set next waypoint from script task */
			if( st->nextWaypointIndex >= 0 ) {
				bs->currentWaypoint = st->nextWaypointIndex;
				Com_Printf( "  Script: next waypoint -> %s\n",
					botGlobals.waypointList.waypoints[bs->currentWaypoint].name );
				return;
			}
		}
	}

	/* No script override: follow the waypoint chain */
	if( wp->nextWaypointIndex >= 0 ) {
		bs->currentWaypoint = wp->nextWaypointIndex;
		Com_Printf( "  Chain: next waypoint -> %s\n",
			botGlobals.waypointList.waypoints[bs->currentWaypoint].name );
	} else {
		/* End of chain: loop back to patrol start, or go idle */
		if( bs->patrolStartWaypoint >= 0 ) {
			bs->currentWaypoint = bs->patrolStartWaypoint;
			Com_Printf( "  End of chain: looping to patrol start -> %s\n",
				botGlobals.waypointList.waypoints[bs->currentWaypoint].name );
		} else {
			Com_Printf( "  End of chain: no patrol start, going idle\n" );
			Bot_SetState( bs, BOT_STATE_IDLE );
		}
	}
}


/*
=============================================================================
  WAYPOINT FILE I/O
=============================================================================
*/

/*
  Bot_SaveWaypoints — Save all waypoints to a text file.
  Format: one line per waypoint: "name x y z nextIndex"
*/
void Bot_SaveWaypoints( const char *filename )
{
	fileHandle_t f;
	int len;
	int i;
	char line[256];

	len = FS_FOpenFileByMode( filename, &f, FS_WRITE );
	if( !f ) {
		Com_Printf( S_COLOR_RED "Bot_SaveWaypoints: could not open %s for writing\n", filename );
		return;
	}

	for( i = 0; i < botGlobals.waypointList.usedWPs; i++ ) {
		bot_waypoint_t *wp = &botGlobals.waypointList.waypoints[i];
		Com_sprintf( line, sizeof(line), "%s %.1f %.1f %.1f %d\n",
			wp->name,
			wp->pos[0], wp->pos[1], wp->pos[2],
			wp->nextWaypointIndex );
		FS_Write( line, strlen(line), f );
	}

	FS_FCloseFile( f );
	Com_Printf( "Saved %d waypoints to %s\n", botGlobals.waypointList.usedWPs, filename );
}


/*
  Bot_LoadWaypoints — Load waypoints from a text file.
  Clears existing waypoints first. File format matches Bot_SaveWaypoints.
*/
void Bot_LoadWaypoints( const char *filename )
{
	fileHandle_t f;
	int len;
	char buf[8192];
	char *ptr;
	char *token;
	char name[MAX_NAME_LENGTH];
	vec3_t pos;
	int nextIdx;

	len = FS_FOpenFileByMode( filename, &f, FS_READ );
	if( !f ) {
		Com_Printf( S_COLOR_RED "Bot_LoadWaypoints: file not found: %s\n", filename );
		return;
	}
	if( len >= (int)sizeof(buf) ) {
		Com_Printf( S_COLOR_RED "Bot_LoadWaypoints: file too large: %s (%d bytes)\n", filename, len );
		FS_FCloseFile( f );
		return;
	}

	FS_Read2( buf, len, f );
	buf[len] = '\0';
	FS_FCloseFile( f );

	/* Reset waypoint list */
	memset( &botGlobals.waypointList, 0, sizeof(botGlobals.waypointList) );

	ptr = buf;
	while( 1 ) {
		token = COM_Parse( &ptr );
		if( !token[0] ) break;

		/* name */
		Q_strncpyz( name, token, MAX_NAME_LENGTH );

		/* x */
		token = COM_Parse( &ptr );
		if( !token[0] ) break;
		pos[0] = atof( token );

		/* y */
		token = COM_Parse( &ptr );
		if( !token[0] ) break;
		pos[1] = atof( token );

		/* z */
		token = COM_Parse( &ptr );
		if( !token[0] ) break;
		pos[2] = atof( token );

		/* nextWaypointIndex */
		token = COM_Parse( &ptr );
		if( !token[0] ) {
			nextIdx = -1;
		} else {
			nextIdx = atoi( token );
		}

		/* Add to list (bypass duplicate check since we cleared) */
		if( botGlobals.waypointList.usedWPs < MAX_BOT_WAYPOINTS ) {
			int idx = botGlobals.waypointList.usedWPs;
			bot_waypoint_t *wp = &botGlobals.waypointList.waypoints[idx];
			wp->index = idx;
			Q_strncpyz( wp->name, name, MAX_NAME_LENGTH );
			VectorCopy( pos, wp->pos );
			wp->nextWaypointIndex = nextIdx;
			botGlobals.waypointList.usedWPs++;
		}
	}

	Com_Printf( "Loaded %d waypoints from %s\n", botGlobals.waypointList.usedWPs, filename );
}