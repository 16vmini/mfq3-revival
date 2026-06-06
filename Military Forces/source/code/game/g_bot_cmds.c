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
	float radius = 1200.0f;   /* tighter circle so it stays near you */
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
	spawnOrigin[2] = playerOrigin[2] + 150.0f;  /* roughly your altitude, easy to engage */
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
		wpPos[2] = playerOrigin[2] + 150.0f;  /* same altitude as spawn */

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
  Bot_AddTank_f -- Console command: bot_target

  Spawns a STATIONARY enemy ground vehicle on the ground directly below the
  player. A non-moving target to definitively test gun collision/damage with
  no tracking required: line up a strafing run, nose on it, fire.
*/
void Bot_AddTank_f( void )
{
	vec3_t		playerOrigin, spawnOrigin, spawnAngles, down;
	trace_t		tr;
	int			team = 1, vehicleIndex = -1, slot, j, i, playerNum = -1;
	bool		found = false;

	/* find the player to spawn beneath */
	for( j = 1; j <= theLevel.maxclients_; j++ ) {
		GameClient *cl = theLevel.getClient( j );
		if( cl && cl->pers_.connected_ == GameClient::ClientPersistant::CON_CONNECTED &&
			cl->sess_.sessionTeam_ != ClientBase::TEAM_SPECTATOR ) {
			GameEntity *pe = theLevel.getEntity( j );
			if( pe && pe->inuse_ ) {
				VectorCopy( pe->r.currentOrigin, playerOrigin );
				playerNum = j; found = true; break;
			}
		}
	}
	if( !found ) { Com_Printf( S_COLOR_RED "bot_target: no player found\n" ); return; }

	/* pick a modern ground vehicle */
	for( i = 0; i < bg_numberOfVehicles; i++ ) {
		if( ( availableVehicles[i].cat & CAT_GROUND ) &&
			( availableVehicles[i].gameset & MF_GAMESET_MODERN ) ) {
			vehicleIndex = i; break;
		}
	}
	if( vehicleIndex < 0 ) { Com_Printf( S_COLOR_RED "bot_target: no ground vehicle found\n" ); return; }

	/* spawn ~800 units AHEAD of the player (not on top of him), traced down to
	   the ground so it sits on flat terrain */
	{
		vec3_t fwd, ahead;
		GameEntity *pe = theLevel.getEntity( playerNum );
		AngleVectors( pe ? pe->r.currentAngles : vec3_origin, fwd, NULL, NULL );
		fwd[2] = 0; VectorNormalize( fwd );
		VectorMA( playerOrigin, 800, fwd, ahead );
		ahead[2] += 300.0f;            /* start above, trace down */
		VectorCopy( ahead, down );
		down[2] -= 10000.0f;
		SV_Trace( &tr, ahead, NULL, NULL, down, playerNum, MASK_PLAYERSOLID, false );
		VectorCopy( tr.endpos, spawnOrigin );
		spawnOrigin[2] += 30.0f;
	}
	VectorSet( spawnAngles, 0, 0, 0 );

	slot = Bot_Spawn( team, vehicleIndex, spawnOrigin, spawnAngles );
	if( slot < 0 ) { Com_Printf( S_COLOR_RED "bot_target: spawn failed\n" ); return; }

	Com_Printf( "bot_target: %s spawned ~800u ahead of you at %.0f %.0f %.0f\n",
		availableVehicles[vehicleIndex].tinyName, spawnOrigin[0], spawnOrigin[1], spawnOrigin[2] );
}


/*
  Bot_AddType_f -- Console command: bot_add_type <plane|helo|ground|boat> [team]

  Generalized bot spawner used by the in-game Add-Bot menu. Picks a vehicle of
  the requested category that is valid for the CURRENT gameset (mf_gameset) via
  the same lookup the player's vehicle-select screen uses, then spawns it:
    - air (plane/helo): circling overhead ahead of the player (Bot_DriveVehicle
      flies a gentle circle with no waypoints needed)
    - ground/boat: stationary, traced to the surface ~800u ahead of the player
*/
void Bot_AddType_f( void )
{
	char		arg[16], targ[8];
	vec3_t		playerOrigin, spawnOrigin, spawnAngles;
	int			team = 1, catIndex = 0, vehicleIndex = -1, slot, j, playerNum = -1;
	bool		found = false, air = true;

	Q_strncpyz( arg, "plane", sizeof(arg) );
	if( Cmd_Argc() >= 2 ) {
		Cmd_ArgvBuffer( 1, arg, sizeof(arg) );
		if( !Q_stricmp( arg, "helo" ) )                                     { catIndex = 2; air = true;  }
		else if( !Q_stricmp( arg, "ground" ) || !Q_stricmp( arg, "tank" ) ) { catIndex = 1; air = false; }
		else if( !Q_stricmp( arg, "boat" ) )                                { catIndex = 4; air = false; }
		else                                                                { catIndex = 0; air = true;  } /* plane */
	}
	if( Cmd_Argc() >= 3 ) {
		Cmd_ArgvBuffer( 2, targ, sizeof(targ) );
		team = atoi( targ );
		if( team < 1 ) team = 1;
		if( team > 2 ) team = 2;
	}

	/* find a category vehicle valid for the live gameset (any team) -- reuses
	   the player's own vehicle-select lookup so bots always match the gameset */
	vehicleIndex = MF_getIndexOfVehicleEx( -1, G_GetGameset(), MF_TEAM_ANY, catIndex, -1, -1, 0, false );
	if( vehicleIndex < 0 ) {
		Com_Printf( S_COLOR_RED "bot_add_type: no '%s' vehicle in this gameset\n", arg );
		return;
	}

	/* find the player to spawn near */
	for( j = 1; j <= theLevel.maxclients_; j++ ) {
		GameClient *cl = theLevel.getClient( j );
		if( cl && cl->pers_.connected_ == GameClient::ClientPersistant::CON_CONNECTED &&
			cl->sess_.sessionTeam_ != ClientBase::TEAM_SPECTATOR ) {
			GameEntity *pe = theLevel.getEntity( j );
			if( pe && pe->inuse_ ) {
				VectorCopy( pe->r.currentOrigin, playerOrigin );
				playerNum = j; found = true; break;
			}
		}
	}
	if( !found ) { Com_Printf( S_COLOR_RED "bot_add_type: no player found\n" ); return; }

	if( air ) {
		/* spawn ahead and above; Bot_DriveVehicle circles it without waypoints */
		spawnOrigin[0] = playerOrigin[0] + 1200.0f;
		spawnOrigin[1] = playerOrigin[1];
		spawnOrigin[2] = playerOrigin[2] + 150.0f;
		VectorSet( spawnAngles, 0, 180, 0 );
	} else {
		/* stationary: trace down to the surface ~800u ahead of the player */
		vec3_t		fwd, ahead, down;
		trace_t		tr;
		GameEntity *pe = theLevel.getEntity( playerNum );
		AngleVectors( pe ? pe->r.currentAngles : vec3_origin, fwd, NULL, NULL );
		fwd[2] = 0; VectorNormalize( fwd );
		VectorMA( playerOrigin, 800, fwd, ahead );
		ahead[2] += 300.0f;
		VectorCopy( ahead, down );
		down[2] -= 10000.0f;
		SV_Trace( &tr, ahead, NULL, NULL, down, playerNum, MASK_PLAYERSOLID, false );
		VectorCopy( tr.endpos, spawnOrigin );
		spawnOrigin[2] += 30.0f;
		VectorSet( spawnAngles, 0, 0, 0 );
	}

	slot = Bot_Spawn( team, vehicleIndex, spawnOrigin, spawnAngles );
	if( slot < 0 ) { Com_Printf( S_COLOR_RED "bot_add_type: spawn failed\n" ); return; }

	Com_Printf( "bot_add_type: %s (%s, team %d) added\n",
		availableVehicles[vehicleIndex].tinyName, arg, team );
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

/*
=============================================================================
  TRAINING MISSIONS

  A lightweight, self-contained training mode used by the Play -> Training
  submenu. When the player spawns and mf_trainingMission is set, we drop the
  scenario's enemies ahead of the (human) player and watch them; when the last
  one dies we center-print "MISSION COMPLETE". This is intentionally simple --
  the feature/missions spawner will later supersede it with full objective /
  Complete / Failed screens.
=============================================================================
*/

#define MAX_TRAIN_ENEMIES 8

static int  trainMission   = 0;     /* current mission number, 0 = none */
static int  trainPlayerNum = -1;    /* the human the mission is for */
static int  trainEnemies[MAX_TRAIN_ENEMIES];
static bool trainEnemyArmed[MAX_TRAIN_ENEMIES];  /* became a live vehicle at least once */
static int  trainEnemyCount = 0;
static bool trainDone = false;

/* air enemies QUEUED by the .mis loader at G_InitGame, then spawned fresh ahead
   of the human in MF_ClientBegin (so they're a findable intercept wherever the
   map drops the player -- spawning-relative is what the hard-coded version did). */
static int  missionPendingVeh[MAX_TRAIN_ENEMIES];
static int  missionPendingTeam[MAX_TRAIN_ENEMIES];
static bool missionPendingAggro[MAX_TRAIN_ENEMIES];   /* face + engage the player vs passive target */
static int  missionPendingCount = 0;


/*
  MF_SpawnTrainingMission -- spawn the scenario for `mission` ahead of `player`.
  Called from MF_ClientBegin for the human player. Bots carry SVF_BOT, so the
  caller guards against re-entry when these Bot_Spawn calls connect their bots.
*/
void MF_SpawnTrainingMission( int mission, GameEntity *player )
{
	static bool	spawning = false;   /* re-entrancy guard */
	vec3_t	fwd, right, pang, spawn, sang, back;
	int		plane, ground, slot, n, count;

	if( !player || !player->client_ ) return;

	/* Each Bot_Spawn below connects a bot, which runs through MF_ClientBegin ->
	   MF_ClientSpawn (which transiently clears the new bot's SVF_BOT) -> the
	   training hook again. The caller's SVF_BOT guard can therefore miss, so
	   block re-entry here: only the original (human) call spawns the scenario. */
	if( spawning ) return;
	spawning = true;

	/* reset mission tracking */
	trainMission = mission;
	trainPlayerNum = player->s.number;
	trainEnemyCount = 0;
	trainDone = false;

	/* turn the player's radar to AIR so the enemy shows up + missiles can lock */
	player->client_->ps_.ONOFF = ( player->client_->ps_.ONOFF & ~OO_RADAR ) | OO_RADAR_AIR;
	player->client_->pers_.lastRadar_ = ( player->client_->ps_.ONOFF & OO_RADAR );

	/* The player's heading: use the spawn VIEW angles -- ps_.vehicleAngles isn't
	   set until the first Pmove, so it still reads 0 here (which placed enemies
	   in world +X / behind the player). s.angles/viewangles hold spawn_angles. */
	VectorCopy( player->client_->ps_.viewangles, pang );
	pang[PITCH] = 0;
	pang[ROLL]  = 0;
	AngleVectors( pang, fwd, right, NULL );

	if( mission == 3 )
	{
		/* Ground Strike: a single ground target on the deck ahead */
		ground = MF_getIndexOfVehicleEx( -1, G_GetGameset(), MF_TEAM_ANY, 1, -1, -1, 0, false );
		if( ground >= 0 )
		{
			trace_t	tr;
			vec3_t	down;
			VectorMA( player->r.currentOrigin, 1500, fwd, spawn );
			spawn[2] += 300;
			VectorCopy( spawn, down );
			down[2] -= 12000;
			SV_Trace( &tr, spawn, NULL, NULL, down, player->s.number, MASK_PLAYERSOLID, false );
			VectorCopy( tr.endpos, spawn );
			spawn[2] += 30;
			VectorSet( sang, 0, pang[YAW] + 180, 0 );
			slot = Bot_Spawn( 1, ground, spawn, sang );
			if( slot >= 0 && trainEnemyCount < MAX_TRAIN_ENEMIES )
				trainEnemies[trainEnemyCount++] = botGlobals.bots[slot].entityNum;
		}
		G_MissionExternalBegin( trainEnemyCount );   /* register as mission objectives */
		spawning = false;
		return;
	}

	/* Intercept / Dogfight / Furball: one or more fighters ahead, facing back */
	plane = MF_getIndexOfVehicleEx( -1, G_GetGameset(), MF_TEAM_ANY, 0, -1, -1, 0, false );
	if( plane < 0 ) { spawning = false; return; }

	count = ( mission == 4 ) ? 3 : 1;
	for( n = 0; n < count; n++ )
	{
		VectorMA( player->r.currentOrigin, 500.0f + n * 400.0f, fwd, spawn );
		VectorMA( spawn, ( n - (count-1) * 0.5f ) * 250.0f, right, spawn );   /* fan out sideways */
		spawn[2] += 250;   /* a little altitude so it clears nearby terrain */
		if( mission == 1 )
		{
			/* Intercept: passive bandit flying AWAY. With our front-180 vision it
			   can't see the player closing on its tail, so it won't shoot back --
			   a clean "line up and gun it" trainer. */
			VectorSet( sang, 0, pang[YAW], 0 );
		}
		else
		{
			/* aggressive: nose pointed back at the player */
			VectorSubtract( player->r.currentOrigin, spawn, back );
			vectoangles( back, sang );
			VectorSet( sang, 0, sang[YAW], 0 );
		}
		slot = Bot_Spawn( 1, plane, spawn, sang );
		if( slot >= 0 )
		{
			if( mission == 1 )
				botGlobals.bots[slot].holdStraight = qtrue;   /* easy: flies straight, you chase */
			if( trainEnemyCount < MAX_TRAIN_ENEMIES )
				trainEnemies[trainEnemyCount++] = botGlobals.bots[slot].entityNum;
		}
	}
	G_MissionExternalBegin( trainEnemyCount );   /* register as mission objectives */
	spawning = false;
}


/*
  MF_MissionResetEnemies / MF_SpawnMissionBot -- used by the .mis loader
  (G_LoadMissionScripts) to spawn enemy AIRCRAFT as flying bot objectives at the
  mission's editor-placed positions. Tracked in trainEnemies[] so MF_TrainingFrame
  reports each kill to the mission engine's objective counter.
*/
void MF_MissionResetEnemies( void )
{
	trainEnemyCount = 0;
	trainDone = false;
	missionPendingCount = 0;
}

/* .mis loader: queue an air enemy to be spawned ahead of the player on spawn.
   aggressive=true -> faces the player and engages; false -> passive straight-flier. */
void MF_QueueMissionAirEnemy( int vehicleIndex, int team, bool aggressive )
{
	if( missionPendingCount >= MAX_TRAIN_ENEMIES ) return;
	missionPendingVeh[missionPendingCount]   = vehicleIndex;
	missionPendingTeam[missionPendingCount]  = team;
	missionPendingAggro[missionPendingCount] = aggressive;
	missionPendingCount++;
}

int MF_SpawnMissionBot( int vehicleIndex, int team, vec3_t origin, vec3_t angles, bool passive )
{
	int slot;

	if( trainEnemyCount >= MAX_TRAIN_ENEMIES ) return -1;
	if( vehicleIndex < 0 || vehicleIndex >= bg_numberOfVehicles ) return -1;
	if( team != 1 && team != 2 ) team = 1;   /* default enemy team */

	/* keep aircraft out of terrain: a .mis origin placed inside a mountain spawns
	   the bot in solid -> it dies on frame 1. Only lift if the origin is ACTUALLY
	   inside solid (raise in steps until clear). Tracing from far above wrongly
	   caught ceiling/clip brushes and flung the bot to the skybox. */
	if( availableVehicles[vehicleIndex].cat & ( CAT_PLANE | CAT_HELO ) )
	{
		int guard = 0;
		while( ( SV_PointContents( origin, ENTITYNUM_NONE ) & CONTENTS_SOLID ) && guard < 60 )
		{
			origin[2] += 200.0f;
			guard++;
		}
		if( guard > 0 )
			origin[2] += 300.0f;   /* extra clearance once out of the rock */
	}

	slot = Bot_Spawn( team, vehicleIndex, origin, angles );
	if( slot < 0 ) return -1;

	if( passive )
		botGlobals.bots[slot].holdStraight = qtrue;   /* easy straight-flying target */

	trainEnemyArmed[trainEnemyCount] = false;   /* confirmed alive before any kill counts */
	trainEnemies[trainEnemyCount++] = botGlobals.bots[slot].entityNum;
	return botGlobals.bots[slot].entityNum;
}


/*
  MF_PositionMissionEnemiesNearPlayer -- the .mis loader spawns aircraft enemies
  at G_InitGame, before the player exists and at fixed editor positions that may
  be far from a random deathmatch spawn. When the human spawns we teleport those
  enemies to just ahead of them (facing away, flying straight) so an air-combat
  mission is an immediate, findable intercept regardless of where the map put us.
*/
void MF_PositionMissionEnemiesNearPlayer( GameEntity *player )
{
	static bool	spawning = false;   /* re-entrancy guard: the bots we spawn connect
	                                   and re-enter MF_ClientBegin -> here */
	vec3_t	fwd, right, pang, spawn, sang, back;
	int		i;

	if( !player || !player->client_ || missionPendingCount == 0 ) return;
	if( spawning ) return;
	spawning = true;

	VectorCopy( player->client_->ps_.viewangles, pang );
	pang[PITCH] = 0;
	pang[ROLL]  = 0;
	AngleVectors( pang, fwd, right, NULL );

	for( i = 0; i < missionPendingCount; i++ )
	{
		bool aggro = missionPendingAggro[i];

		/* aggressive enemies start farther out for a head-on merge; passive ones
		   spawn close so the player slides onto their tail */
		VectorMA( player->r.currentOrigin, ( aggro ? 1500.0f : 600.0f ) + i * 400.0f, fwd, spawn );
		VectorMA( spawn, ( i - (missionPendingCount-1) * 0.5f ) * 250.0f, right, spawn );
		spawn[2] += 150.0f;

		if( aggro )
		{
			/* nose pointed back at the player -> the combat AI engages */
			VectorSubtract( player->r.currentOrigin, spawn, back );
			vectoangles( back, sang );
			VectorSet( sang, 0, sang[YAW], 0 );
		}
		else
		{
			VectorSet( sang, 0, pang[YAW], 0 );   /* face away -> easy intercept */
		}

		/* spawn fresh ahead of the player (MF_SpawnMissionBot tracks it in
		   trainEnemies[] for the objective counter; passive -> holdStraight) */
		MF_SpawnMissionBot( missionPendingVeh[i], missionPendingTeam[i], spawn, sang, !aggro );
	}

	missionPendingCount = 0;   /* consumed */
	spawning = false;
}


/*
  MF_TrainingFrame -- called once per server frame (after Bot_Frame). Reports
  each newly-dead training enemy to the mission engine, which fires the real
  Mission Complete screen (+ fire-to-restart) once the last one is down. Player
  death is handled by the engine's own G_MissionFailed hook (g_vehicle.cpp).
*/
void MF_TrainingFrame( void )
{
	int		i;

	if( trainDone || trainEnemyCount == 0 ) return;

	for( i = 0; i < trainEnemyCount; i++ )
	{
		GameEntity	*e;
		bool		alive;

		if( trainEnemies[i] < 0 ) continue;   /* already counted this kill */

		e = theLevel.getEntity( trainEnemies[i] );
		/* judge life by health/inuse, NOT eType: an unobserved flying bot reports
		   eType ET_INVISIBLE (=10) while perfectly alive, which falsely read as a kill */
		alive = ( e && e->inuse_ && e->health_ > 0 );

		if( alive )
		{
			trainEnemyArmed[i] = true;            /* confirmed alive -- arm the kill check */
		}
		else if( trainEnemyArmed[i] )
		{
			/* it was alive and now is dead/gone -> a real kill */
			trainEnemies[i] = -1;
			G_MissionTargetDestroyed( e );        /* engine: decrement + Complete at 0 */
		}
	}
}


/*
=============================================================================
  SURVEILLANCE DRONE
=============================================================================
*/

#define DRONE_STRIPS		6		/* number of zig-zag strips across the map */
#define DRONE_ALTITUDE		1800.0f /* fixed cruise height above sea level */
#define DRONE_STRIP_NAME_PREFIX "drone_swp_"

/*
  Bot_SpawnSurveillanceDrone
  Spawns a friendly plane bot in surveillance mode flying a lawnmower
  zig-zag pattern over the whole map at fixed altitude. It never engages
  enemies or fires. The drone cam PiP feed can be aimed at it via \dronecam.

  Derives map extents from the existing spawn points (already proven approach).
  Creates a set of named waypoints DRONE_STRIP_NAME_PREFIX%d forming a looping
  strip pattern, then spawns the bot on team 1 using the first available plane.
*/
int Bot_SpawnSurveillanceDrone( void )
{
	GameEntity	*spot;
	vec3_t		mins, maxs, origin, angles;
	float		xMin, xMax, yMin, yMax;
	float		xStep, y;
	int			i, strip, slot, planeIdx, firstWp, numWp;
	char		wpName[32];
	bool		first = true;

	/* --- derive map bounds from spawn points --- */
	xMin = yMin =  99999.0f;
	xMax = yMax = -99999.0f;

	spot = NULL;
	while( (spot = G_Find( spot, FOFS(classname_), "info_player_deathmatch" )) != NULL )
	{
		if( spot->r.currentOrigin[0] < xMin ) xMin = spot->r.currentOrigin[0];
		if( spot->r.currentOrigin[0] > xMax ) xMax = spot->r.currentOrigin[0];
		if( spot->r.currentOrigin[1] < yMin ) yMin = spot->r.currentOrigin[1];
		if( spot->r.currentOrigin[1] > yMax ) yMax = spot->r.currentOrigin[1];
		first = false;
	}

	if( first )
	{
		/* No spawn points found: default to a 10000-unit arena */
		xMin = yMin = -5000; xMax = yMax = 5000;
	}

	/* Expand bounds a bit so the drone sweeps past the edge */
	xMin -= 512; xMax += 512;
	yMin -= 512; yMax += 512;

	/* --- generate lawnmower strip waypoints --- */
	xStep = (xMax - xMin) / (float)DRONE_STRIPS;
	firstWp = botGlobals.waypointList.usedWPs;
	numWp = 0;

	for( strip = 0; strip <= DRONE_STRIPS; strip++ )
	{
		float x = xMin + strip * xStep;
		/* alternate Y direction for the zig-zag */
		float y0 = (strip % 2 == 0) ? yMin : yMax;
		float y1 = (strip % 2 == 0) ? yMax : yMin;

		Com_sprintf( wpName, sizeof(wpName), DRONE_STRIP_NAME_PREFIX "%d_a", strip );
		VectorSet( origin, x, y0, DRONE_ALTITUDE );
		Bot_AddWaypoint( wpName, origin );
		numWp++;

		Com_sprintf( wpName, sizeof(wpName), DRONE_STRIP_NAME_PREFIX "%d_b", strip );
		VectorSet( origin, x, y1, DRONE_ALTITUDE );
		Bot_AddWaypoint( wpName, origin );
		numWp++;
	}

	/* Link the waypoints into a loop (each points to the next, last wraps) */
	for( i = firstWp; i < firstWp + numWp; i++ )
	{
		int next = (i + 1 < firstWp + numWp) ? (i + 1) : firstWp;
		botGlobals.waypointList.waypoints[i].nextWaypointIndex = next;
	}

	/* --- pick a plane vehicle --- */
	planeIdx = MF_getIndexOfVehicleEx( -1, G_GetGameset(), MF_TEAM_ANY, CAT_PLANE, -1, -1, 0, false );
	if( planeIdx < 0 )
	{
		Com_Printf( S_COLOR_RED "Bot_SpawnSurveillanceDrone: no plane vehicle available\n" );
		return -1;
	}

	/* Spawn at first waypoint position */
	VectorCopy( botGlobals.waypointList.waypoints[firstWp].pos, origin );
	VectorSet( angles, 0, 0, 0 );

	slot = Bot_Spawn( 1, planeIdx, origin, angles );
	if( slot < 0 )
	{
		Com_Printf( S_COLOR_RED "Bot_SpawnSurveillanceDrone: Bot_Spawn failed\n" );
		return -1;
	}

	/* Configure surveillance mode: never attack, fixed altitude, waypoint loop */
	botGlobals.bots[slot].surveillanceMode = qtrue;
	botGlobals.bots[slot].cruiseAltitude   = DRONE_ALTITUDE;
	botGlobals.bots[slot].currentWaypoint  = firstWp;
	botGlobals.bots[slot].patrolStartWaypoint = firstWp;

	Com_Printf( S_COLOR_GREEN "Surveillance drone spawned (slot %d, ent %d, %d waypoints, %.0f alt)\n",
		slot, botGlobals.bots[slot].entityNum, numWp, DRONE_ALTITUDE );

	return slot;
}

/* Console command: bot_drone */
void Bot_Drone_f( void )
{
	int slot = Bot_SpawnSurveillanceDrone();
	if( slot >= 0 )
		Com_Printf( "Use \\listdrones / \\dronecam 1 to view the feed.\n" );
}
