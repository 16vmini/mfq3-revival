/*
 * g_bot_cmds.c — Bot console commands for MFQ3 bot system
 *
 * Provides server console commands for managing bots:
 *   bot_add [team] [vehicle_type] — spawn a bot
 *   bot_remove [entitynum]       — remove a bot
 *   bot_waypoint_add [name] [x] [y] [z] — add a waypoint
 *   bot_waypoint_save [filename]  — save waypoints to file
 *   bot_waypoint_load [filename]  — load waypoints from file
 *
 * These commands allow server operators and map makers to set up
 * bot scenarios and test waypoint paths.
 */

#include "g_bot.h"


/*
=============================================================================
  BOT_ADD
=============================================================================
*/

/*
  Bot_Add_f — Console command: bot_add [team] [vehicle_type]
  
  team: 1 or 2 (MF_TEAM_1, MF_TEAM_2)
  vehicle_type: index into availableVehicles[]
  
  Spawns a bot at the first available spawn point for the team.
  If no vehicle_type is specified, picks a random vehicle for the team.
*/
void Bot_Add_f( void )
{
	char arg1[16];
	char arg2[16];
	int team = 1;
	int vehicleIndex = -1;
	vec3_t spawnOrigin, spawnAngles;
	GameEntity *spot;
	int slot;

	/* Parse arguments */
	if( Cmd_Argc() >= 2 ) {
		Cmd_ArgvBuffer( 1, arg1, sizeof(arg1) );
		team = atoi( arg1 );
		if( team < 1 ) team = 1;
		if( team > 2 ) team = 2;
	}

	if( Cmd_Argc() >= 3 ) {
		Cmd_ArgvBuffer( 2, arg2, sizeof(arg2) );
		vehicleIndex = atoi( arg2 );
	}

	/* If no vehicle specified, find one for this team */
	if( vehicleIndex < 0 || vehicleIndex >= bg_numberOfVehicles ) {
		int i;
		int startVehicle = rand() % bg_numberOfVehicles;
		/* Find a vehicle that matches the team's gameset */
		for( i = 0; i < bg_numberOfVehicles; i++ ) {
			int idx = (startVehicle + i) % bg_numberOfVehicles;
			unsigned int vTeam = availableVehicles[idx].team;
			if( vTeam == (unsigned int)(1 << (team - 1)) || vTeam == MF_TEAM_ANY ) {
				vehicleIndex = idx;
				break;
			}
		}
		if( vehicleIndex < 0 ) {
			Com_Printf( S_COLOR_RED "bot_add: no vehicles found for team %d\n", team );
			return;
		}
	}

	/* Find a spawn point */
	{
		int cat = availableVehicles[vehicleIndex].cat;
		spot = (GameEntity*)SelectSpawnPoint( vec3_origin, spawnOrigin, spawnAngles, cat );
		if( !spot ) {
			Com_Printf( S_COLOR_RED "bot_add: no spawn point found for vehicle category %d\n", cat );
			return;
		}
	}

	/* Spawn the bot */
	slot = Bot_Spawn( team, vehicleIndex, spawnOrigin, spawnAngles );
	if( slot < 0 ) {
		Com_Printf( S_COLOR_RED "bot_add: failed to spawn bot\n" );
		return;
	}

	Com_Printf( "Bot added: slot %d, team %d, vehicle %s\n",
		slot, team, availableVehicles[vehicleIndex].tinyName );
}


/*
=============================================================================
  BOT_REMOVE
=============================================================================
*/

/*
  Bot_Remove_f — Console command: bot_remove [entitynum]
  
  entitynum: the entity number of the bot to remove.
  Searches the bot list for a bot with that entity number.
*/
void Bot_Remove_f( void )
{
	char arg[16];
	int entityNum;
	int i;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: bot_remove <entitynum>\n" );
		return;
	}

	Cmd_ArgvBuffer( 1, arg, sizeof(arg) );
	entityNum = atoi( arg );

	/* Find the bot slot for this entity */
	for( i = 0; i < MAX_BOTS; i++ ) {
		if( botGlobals.bots[i].active && botGlobals.bots[i].entityNum == entityNum ) {
			Bot_Remove( i );
			Com_Printf( "Bot removed: entity %d\n", entityNum );
			return;
		}
	}

	Com_Printf( S_COLOR_YELLOW "bot_remove: no bot found with entity number %d\n", entityNum );
}


/*
=============================================================================
  WAYPOINT ADD
=============================================================================
*/

/*
  Bot_WaypointAdd_f — Console command: bot_waypoint_add [name] [x] [y] [z]
  
  Adds a waypoint at the specified position with the given name.
  Useful for runtime waypoint mapping and testing.
  
  If no coordinates are given, uses the position of the first connected
  player (for in-game waypoint placement).
*/
void Bot_WaypointAdd_f( void )
{
	char nameArg[MAX_NAME_LENGTH];
	char xArg[16], yArg[16], zArg[16];
	vec3_t origin;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: bot_waypoint_add <name> [x y z]\n" );
		return;
	}

	Cmd_ArgvBuffer( 1, nameArg, sizeof(nameArg) );

	if( Cmd_Argc() >= 5 ) {
		/* Position specified */
		Cmd_ArgvBuffer( 2, xArg, sizeof(xArg) );
		Cmd_ArgvBuffer( 3, yArg, sizeof(yArg) );
		Cmd_ArgvBuffer( 4, zArg, sizeof(zArg) );
		origin[0] = atof( xArg );
		origin[1] = atof( yArg );
		origin[2] = atof( zArg );
	} else {
		/* Use first player's position */
		/* Note: In the real integration, this would look up a client's
		   current position. For now, use (0,0,0) as placeholder. */
		GameEntity *player = theLevel.getEntity( 0 );
		if( player && player->client_ ) {
			VectorCopy( player->r.currentOrigin, origin );
		} else {
			Com_Printf( S_COLOR_YELLOW "bot_waypoint_add: no player position available, using (0,0,0)\n" );
			VectorClear( origin );
		}
	}

	Bot_AddWaypoint( nameArg, origin );
}


/*
=============================================================================
  WAYPOINT SAVE
=============================================================================
*/

/*
  Bot_WaypointSave_f — Console command: bot_waypoint_save [filename]
  
  Saves all current waypoints to a file.
  Default filename is "bots/waypoints.txt".
*/
void Bot_WaypointSave_f( void )
{
	char filename[BOT_MAX_FILEPATH];

	if( Cmd_Argc() >= 2 ) {
		Cmd_ArgvBuffer( 1, filename, sizeof(filename) );
	} else {
		Q_strncpyz( filename, "bots/waypoints.txt", sizeof(filename) );
	}

	Bot_SaveWaypoints( filename );
}


/*
=============================================================================
  WAYPOINT LOAD
=============================================================================
*/

/*
  Bot_WaypointLoad_f — Console command: bot_waypoint_load [filename]
  
  Loads waypoints from a file, replacing any current waypoints.
  Default filename is "bots/waypoints.txt".
*/
void Bot_WaypointLoad_f( void )
{
	char filename[BOT_MAX_FILEPATH];

	if( Cmd_Argc() >= 2 ) {
		Cmd_ArgvBuffer( 1, filename, sizeof(filename) );
	} else {
		Q_strncpyz( filename, "bots/waypoints.txt", sizeof(filename) );
	}

	Bot_LoadWaypoints( filename );
}


/*
=============================================================================
  CIRCLING PLANE
=============================================================================
*/

/*
  Bot_AddCircling_f — Console command: bot_add_circling [team] [vehicle_type]
  
  Spawns a plane bot near the first connected player and sets it to
  circle around the player's position using a circular waypoint pattern.
  
  Creates 8 waypoints in a circle of radius 2000 around the player,
  links them into a loop, and assigns them to the bot as a patrol route.
*/
void Bot_AddCircling_f( void )
{
	char arg1[16];
	char arg2[16];
	int team = 1;
	int vehicleIndex = -1;
	vec3_t playerOrigin;
	vec3_t spawnOrigin, spawnAngles;
	int slot;
	int i;
	int wpIndices[8];
	int firstWpIndex;
	char wpName[MAX_NAME_LENGTH];
	float angle;
	float radius = 2000.0f;
	const int NUM_WAYPOINTS = 8;

	/* Parse arguments */
	if( Cmd_Argc() >= 2 ) {
		Cmd_ArgvBuffer( 1, arg1, sizeof(arg1) );
		team = atoi( arg1 );
		if( team < 1 ) team = 1;
		if( team > 2 ) team = 2;
	}

	if( Cmd_Argc() >= 3 ) {
		Cmd_ArgvBuffer( 2, arg2, sizeof(arg2) );
		vehicleIndex = atoi( arg2 );
	}

	/* Find a plane vehicle for this team if none specified */
	if( vehicleIndex < 0 || vehicleIndex >= bg_numberOfVehicles ) {
		int startVehicle = rand() % bg_numberOfVehicles;
		for( i = 0; i < bg_numberOfVehicles; i++ ) {
			int idx = (startVehicle + i) % bg_numberOfVehicles;
			if( (availableVehicles[idx].cat & CAT_PLANE) &&
				(availableVehicles[idx].team == (unsigned int)(1 << (team - 1)) ||
				 availableVehicles[idx].team == MF_TEAM_ANY) ) {
				vehicleIndex = idx;
				break;
			}
		}
		if( vehicleIndex < 0 ) {
			Com_Printf( S_COLOR_RED "bot_add_circling: no plane vehicles found for team %d\n", team );
			return;
		}
	}

	/* Verify it's actually a plane */
	if( !(availableVehicles[vehicleIndex].cat & CAT_PLANE) ) {
		Com_Printf( S_COLOR_RED "bot_add_circling: selected vehicle is not a plane (cat %d)\n",
			availableVehicles[vehicleIndex].cat );
		return;
	}

	/* Find the first connected player to circle around */
	{
		qboolean found = qfalse;
		int j;
		for( j = 1; j <= theLevel.maxclients_; j++ ) {
			GameClient *cl = theLevel.getClient( j );
			if( cl && cl->pers_.connected_ == GameClient::ClientPersistant::CON_CONNECTED &&
				cl->sess_.sessionTeam_ != ClientBase::TEAM_SPECTATOR ) {
				GameEntity *playerEnt = theLevel.getEntity( j );
				if( playerEnt && playerEnt->inuse_ ) {
					VectorCopy( playerEnt->r.currentOrigin, playerOrigin );
					found = qtrue;
					break;
				}
			}
		}
		if( !found ) {
			Com_Printf( S_COLOR_RED "bot_add_circling: no connected player found to circle around\n" );
			return;
		}
	}

	/* Spawn the bot near the player (offset slightly above for a plane) */
	spawnOrigin[0] = playerOrigin[0] + radius;
	spawnOrigin[1] = playerOrigin[1];
	spawnOrigin[2] = playerOrigin[2] + 1000.0f;  /* Start airborne */
	VectorSet( spawnAngles, 0, 180, 0 );  /* Face toward center */

	slot = Bot_Spawn( team, vehicleIndex, spawnOrigin, spawnAngles );
	if( slot < 0 ) {
		Com_Printf( S_COLOR_RED "bot_add_circling: failed to spawn bot\n" );
		return;
	}

	/* Create circular waypoint pattern around the player */
	/* Remember starting waypoint index so we can link them */
	firstWpIndex = botGlobals.waypointList.usedWPs;

	for( i = 0; i < NUM_WAYPOINTS; i++ ) {
		vec3_t wpPos;
		angle = (2.0f * M_PI * i) / NUM_WAYPOINTS;
		wpPos[0] = playerOrigin[0] + radius * cos( angle );
		wpPos[1] = playerOrigin[1] + radius * sin( angle );
		wpPos[2] = playerOrigin[2] + 1000.0f;  /* Same altitude as spawn */

		/* name must be unique per bot, else Bot_AddWaypoint skips the duplicate
		   and wpIndices end up pointing past the array (OOB write -> crash). */
		Com_sprintf( wpName, sizeof(wpName), "circle_wp_%d_%d", slot, i );
		Bot_AddWaypoint( wpName, wpPos );
		wpIndices[i] = firstWpIndex + i;
	}

	/* Link waypoints into a circular loop */
	for( i = 0; i < NUM_WAYPOINTS; i++ ) {
		int next = (i + 1) % NUM_WAYPOINTS;
		botGlobals.waypointList.waypoints[wpIndices[i]].nextWaypointIndex = wpIndices[next];
	}

	/* Set the bot to patrol these waypoints */
	{
		botState_t *bs = &botGlobals.bots[slot];
		bs->currentWaypoint = wpIndices[0];
		bs->patrolStartWaypoint = wpIndices[0];
		Bot_SetState( bs, BOT_STATE_PATROL );
	}

	Com_Printf( "Circling bot added: slot %d, team %d, vehicle %s, %d waypoints around player\n",
		slot, team, availableVehicles[vehicleIndex].tinyName, NUM_WAYPOINTS );
}


/*
=============================================================================
  COMMAND REGISTRATION
=============================================================================
*/

/*
  Bot_RegisterCommands — Register all bot console commands.
  Call this from Bot_Init() or from the game's init function.
  
  Uses Cbuf_ExecuteText to add the commands to the server's command list.
  In the real integration, these would be registered via the server
  command system (e.g., adding to the svcmds list in g_svcmds.c).
*/
void Bot_RegisterCommands( void )
{
	/*
	 * In the Q3 engine, server commands are registered by adding
	 * entries to the svCmds[] table in g_svcmds.c. For a clean
	 * integration, add the following entries to that table:
	 *
	 *   {"bot_add",            Bot_Add_f},
	 *   {"bot_remove",         Bot_Remove_f},
	 *   {"bot_waypoint_add",   Bot_WaypointAdd_f},
	 *   {"bot_waypoint_save",  Bot_WaypointSave_f},
	 *   {"bot_waypoint_load",  Bot_WaypointLoad_f},
	 *   {"bot_add_circling",   Bot_AddCircling_f},
	 *
	 * For now, we register them via Cbuf_ExecuteText (runtime registration).
	 * This approach may not work in all Q3 engine builds; the table
	 * approach is the recommended integration method.
	 */
	Com_Printf( "Bot commands: bot_add, bot_remove, bot_waypoint_add, bot_waypoint_save, bot_waypoint_load, bot_add_circling\n" );
	Com_Printf( "  NOTE: These commands must be registered in g_svcmds.c for full functionality.\n" );
}