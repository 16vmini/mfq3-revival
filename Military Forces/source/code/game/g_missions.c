/*
 * $Id: g_missions.c,v 1.6 2006-01-29 14:03:41 thebjoern Exp $
 *
 * MFQ3 mission revival: server-side mission spawner + objective tracking.
 *
 * Reads missions/<map>/<name>.mis (the in-game mission editor's output),
 * parses it with the existing MF_ParseMissionScripts, and spawns real
 * server entities (vehicles + ground installations). When every objective
 * target is destroyed the mission ends via the standard intermission.
 *
 * Re-activated for the single-player "Mission 1" MVP. The original body of
 * this file (commented gentity_t code) was 100% dead; this replaces it.
 */

#include "g_local.h"
#include "g_entity.h"
#include "g_level.h"


// --- mission objective state (server-side) --------------------------
static int	s_missionTargetsTotal		= 0;	// primary objectives (SAM sites)
static int	s_missionTargetsRemaining	= 0;
static int	s_missionBonusTotal			= 0;	// secondary/bonus targets (vehicles)
static int	s_missionBonusDestroyed		= 0;
static bool	s_missionComplete			= false;
static bool	s_missionFailed				= false;

// programmatic objectives (.mis "Objectives" block): conditions evaluated each
// frame; once every one latches true the mission completes (no kills required).
static mission_objective_t	s_objectives[MAX_MISSION_OBJECTIVES];
static bool					s_objectiveDone[MAX_MISSION_OBJECTIVES];
static int					s_numObjectives	= 0;

// optional .mis flavour text shown on the end screens (empty = use defaults)
static char	s_completeText[128]	= "";
static char	s_failText[128]		= "";

// .mis PlayerStart: where the human spawns + what they fly (overrides the random
// deathmatch spawn so the mission's absolute enemy positions line up with us)
static bool		s_hasPlayerStart			= false;
static int		s_playerStartVehicle		= 0;
static vec3_t	s_playerStartOrigin;
static vec3_t	s_playerStartAngles;
static float	s_playerStartSpeed			= 0;

bool MF_GetMissionPlayerStart( int *vehicle, vec3_t origin, vec3_t angles )
{
	if( !s_hasPlayerStart ) return false;
	if( vehicle ) *vehicle = s_playerStartVehicle;
	if( origin )  VectorCopy( s_playerStartOrigin, origin );
	if( angles )  VectorCopy( s_playerStartAngles, angles );
	return true;
}

float MF_GetMissionPlayerSpeed( void )
{
	return s_hasPlayerStart ? s_playerStartSpeed : 0.0f;
}


// map a .mis team string ("red"/"blue"/"?") to a team id, -1 = none
static int G_MapTeamName( const char* name )
{
	if( !name )							return -1;
	if( !Q_stricmp( name, "red" ) )		return ClientBase::TEAM_RED;
	if( !Q_stricmp( name, "blue" ) )	return ClientBase::TEAM_BLUE;
	return -1;	// unknown / "?" -> no team
}


static bool G_LoadOverviewAndEntities( const char* filename,
		mission_overview_t* overview,
		mission_vehicle_t* vehs,
		mission_groundInstallation_t* gis )
{
	int				len;
	fileHandle_t	f;
	static char		inbuffer[MAX_MISSION_TEXT];

	len = FS_FOpenFileByMode( filename, &f, FS_READ );
	if( !f )
	{
		Com_Printf( S_COLOR_RED "mission file not found: %s\n", filename );
		return false;
	}
	if( len >= MAX_MISSION_TEXT )
	{
		Com_Printf( S_COLOR_RED "mission file too large: %s is %i, max allowed is %i\n",
			filename, len, MAX_MISSION_TEXT );
		FS_FCloseFile( f );
		return false;
	}

	FS_Read2( inbuffer, len, f );
	inbuffer[len] = 0;
	FS_FCloseFile( f );

	Com_Printf( S_COLOR_GREEN "Loaded mission script: %s\n", filename );

	MF_ParseMissionScripts( inbuffer, overview, vehs, gis );
	return true;
}


void G_LoadMissionScripts()
{
	char							filename[MAX_FILEPATH];
	char							missionname[32];
	char							mapname[64];
	mission_overview_t				overview;
	mission_vehicle_t				vehicles[IGME_MAX_VEHICLES/4];
	mission_groundInstallation_t	installations[IGME_MAX_VEHICLES/4];
	int								i, spawned = 0;

	s_missionTargetsTotal		= 0;
	s_missionTargetsRemaining	= 0;
	s_missionBonusTotal			= 0;
	s_missionBonusDestroyed		= 0;
	s_missionComplete			= false;
	s_missionFailed				= false;
	s_hasPlayerStart			= false;
	s_numObjectives				= 0;
	memset( s_objectiveDone, 0, sizeof(s_objectiveDone) );
	s_completeText[0]			= 0;
	s_failText[0]				= 0;

	memset( &overview, 0, sizeof(overview) );
	memset( vehicles, 0, sizeof(vehicles) );
	memset( installations, 0, sizeof(installations) );

	if( strlen( mf_mission.string ) > 31 )
	{
		Com_Printf( S_COLOR_RED "mf_mission name too long -> no mission loaded\n" );
		return;
	}
	Q_strncpyz( missionname, mf_mission.string, sizeof(missionname) );
	if( !missionname[0] || !Q_stricmp( missionname, "none" ) )
	{
		Com_Printf( "No mission loaded (mf_mission is empty/none).\n" );
		return;
	}

	Cvar_VariableStringBuffer( "mapname", mapname, sizeof(mapname) );
	// Category-organized path: mf_mission carries the folder, e.g. "training/mission1"
	// -> missions/training/mission1.mis. The .mis Overview already names its own map.
	Com_sprintf( filename, sizeof(filename), "missions/%s.mis", missionname );

	if( !G_LoadOverviewAndEntities( filename, &overview, vehicles, installations ) )
		return;

	// store the .mis PlayerStart (used by MF_ClientSpawn to place the human)
	if( overview.hasPlayerStart )
	{
		s_hasPlayerStart     = true;
		s_playerStartVehicle = overview.playerVehicle;
		VectorCopy( overview.playerOrigin, s_playerStartOrigin );
		VectorCopy( overview.playerAngles, s_playerStartAngles );
		s_playerStartSpeed = overview.playerSpeed;
	}

	// store programmatic objectives (evaluated every frame in MF_MissionObjectiveFrame)
	s_numObjectives = overview.numObjectives;
	if( s_numObjectives > MAX_MISSION_OBJECTIVES ) s_numObjectives = MAX_MISSION_OBJECTIVES;
	for( i = 0; i < s_numObjectives; i++ )
		s_objectives[i] = overview.objectives[i];

	// optional custom end-screen messages
	Q_strncpyz( s_completeText, overview.completeText, sizeof(s_completeText) );
	Q_strncpyz( s_failText, overview.failText, sizeof(s_failText) );

	// reset the mission-bot objective list (aircraft enemies are tracked there)
	MF_MissionResetEnemies();

	// spawn mission vehicles. Aircraft become flying BOT objectives (primary);
	// everything else stays a static bonus target (optional, extra points).
	for( i = 0; i < IGME_MAX_VEHICLES/4; ++i )
	{
		if( !vehicles[i].used ) break;

		if( availableVehicles[vehicles[i].index].cat & ( CAT_PLANE | CAT_HELO ) )
		{
			bool passive = ( vehicles[i].behaviour == 1 );

			/* Queue the bandit for a player-relative spawn: the human lands at the
			   normal spawn point (absolute PlayerStart positioning breaks takeoff),
			   so we place the enemy ahead of them once they're in. Behaviour drives
			   whether it engages (aggressive) or holds straight (passive). */
			MF_QueueMissionAirEnemy( vehicles[i].index, vehicles[i].team, !passive );
			spawned++;
			s_missionTargetsTotal++;
			s_missionTargetsRemaining++;
		}
		else
		{
			GameEntity* v = G_SpawnMissionVehicle( vehicles[i].index, vehicles[i].team,
					vehicles[i].origin, vehicles[i].angles );
			if( v )
			{
				spawned++;
				v->flags_ |= FL_MISSION_BONUS;
				s_missionBonusTotal++;
			}
		}
	}

	// spawn ground installations; every one is a destroy-objective target
	for( i = 0; i < IGME_MAX_VEHICLES/4; ++i )
	{
		if( !installations[i].used ) break;
		GameEntity* gi = G_SpawnMissionGroundInstallation( installations[i].index,
				G_MapTeamName( installations[i].teamname ),
				installations[i].origin, installations[i].angles );
		if( gi )
		{
			spawned++;
			gi->flags_ |= FL_MISSION_TARGET;
			s_missionTargetsTotal++;
			s_missionTargetsRemaining++;
		}
	}

	Com_Printf( S_COLOR_GREEN "Mission '%s' on '%s': spawned %d entit%s, %d objective target%s.\n",
		missionname, mapname, spawned, (spawned == 1 ? "y" : "ies"),
		s_missionTargetsTotal, (s_missionTargetsTotal == 1 ? "" : "s") );
}


// Externally-driven mission setup. The .mis loader spawns ground installations
// as objectives; the air-combat training scenarios instead spawn bot CLIENTS and
// register N of them as objectives here, so the same Complete/Failed screens +
// fire-to-restart flow drives them. Call once after spawning the scenario, then
// call G_MissionTargetDestroyed() as each registered enemy dies.
void G_MissionExternalBegin( int targets )
{
	s_missionTargetsTotal		= targets;
	s_missionTargetsRemaining	= targets;
	s_missionBonusTotal			= 0;
	s_missionBonusDestroyed		= 0;
	s_missionComplete			= false;
	s_missionFailed				= false;
}


// Fire the Complete screen + end the level. Shared by destroy-objective missions
// and programmatic-objective missions (they just pass different "primary" counts).
// push the optional flavour line (.mis success/failure) to the end screen; sent
// every time (empty clears any leftover) just before the mission_end banner.
static void G_MissionSendText( const char* msg )
{
	SV_GameSendServerCommand( -1, va( "mission_text \"%s\"", ( msg && msg[0] ) ? msg : "" ) );
}

// whitespace-token membership test (so "mission1" doesn't match "mission10")
static bool G_TokenInList( const char* list, const char* tok )
{
	int			tl = (int)strlen( tok );
	const char*	p = list;
	while( *p )
	{
		const char* s;
		while( *p == ' ' || *p == '\t' ) p++;
		s = p;
		while( *p && *p != ' ' && *p != '\t' ) p++;
		if( ( p - s ) == tl && !Q_strncmp( s, tok, tl ) ) return true;
	}
	return false;
}

// Record the current mission as completed in the archived mf_missionsDone list,
// so the Training menu can mark it done and default to the next one. Replays are
// still allowed - this only tracks, it never locks anything.
static void G_MissionRecordComplete( void )
{
	char		done[1024];
	const char*	id = mf_mission.string;		// e.g. "training/takeoff"

	if( !id || !id[0] || !Q_stricmp( id, "none" ) || !Q_stricmp( id, "default" ) )
		return;

	Cvar_VariableStringBuffer( "mf_missionsDone", done, sizeof(done) );
	if( G_TokenInList( done, id ) )
		return;	// already recorded

	if( done[0] ) Q_strcat( done, sizeof(done), " " );
	Q_strcat( done, sizeof(done), id );
	Cvar_Set( "mf_missionsDone", done );
	Com_Printf( "Mission progress: recorded '%s' complete.\n", id );
}

static void G_MissionSendComplete( int primaryDone, int primaryTotal )
{
	s_missionComplete = true;
	G_MissionRecordComplete();
	G_MissionSendText( s_completeText );
	// mission_end <success> <primaryDone> <primaryTotal> <bonusDone> <bonusTotal>
	SV_GameSendServerCommand( -1, va( "mission_end 1 %d %d %d %d",
		primaryDone, primaryTotal, s_missionBonusDestroyed, s_missionBonusTotal ) );
	SV_GameSendServerCommand( -1, "cp \"Mission complete!\n\"" );
	Com_Printf( S_COLOR_GREEN "Mission complete - %d/%d primary, %d/%d bonus.\n",
		primaryDone, primaryTotal, s_missionBonusDestroyed, s_missionBonusTotal );
	if( !theLevel.intermissiontime_ )
		BeginIntermission();
}

// Called from Die_MiscVehicle when an FL_MISSION_TARGET entity is destroyed.
void G_MissionTargetDestroyed( GameEntity* target )
{
	if( s_missionComplete || s_missionFailed )
		return;

	if( s_missionTargetsRemaining > 0 )
		s_missionTargetsRemaining--;

	if( s_missionTargetsRemaining > 0 )
	{
		Com_Printf( "Mission: objective target destroyed (%d/%d remaining).\n",
			s_missionTargetsRemaining, s_missionTargetsTotal );
		SV_GameSendServerCommand( -1, va( "cp \"Target destroyed - %d remaining\n\"", s_missionTargetsRemaining ) );
		return;
	}

	// all primary objectives destroyed -> mission complete
	G_MissionSendComplete( s_missionTargetsTotal, s_missionTargetsTotal );
}

// Called when the player's aircraft is destroyed during a single-player mission.
void G_MissionFailed( void )
{
	int primaryDone;

	if( s_missionComplete || s_missionFailed )
		return;
	if( s_missionTargetsTotal == 0 && s_numObjectives == 0 )	// no mission -> nothing to fail
		return;

	s_missionFailed = true;
	if( s_numObjectives > 0 )
	{
		int done = 0, k;
		for( k = 0; k < s_numObjectives; k++ ) if( s_objectiveDone[k] ) done++;
		primaryDone = done;
		G_MissionSendText( s_failText );
		SV_GameSendServerCommand( -1, va( "mission_end 0 %d %d %d %d",
			primaryDone, s_numObjectives, s_missionBonusDestroyed, s_missionBonusTotal ) );
		SV_GameSendServerCommand( -1, "cp \"MISSION FAILED\n\"" );
		Com_Printf( S_COLOR_RED "Mission failed (%d/%d objectives).\n", primaryDone, s_numObjectives );
		if( !theLevel.intermissiontime_ )
			BeginIntermission();
		return;
	}
	primaryDone = s_missionTargetsTotal - s_missionTargetsRemaining;
	G_MissionSendText( s_failText );
	SV_GameSendServerCommand( -1, va( "mission_end 0 %d %d %d %d",
		primaryDone, s_missionTargetsTotal, s_missionBonusDestroyed, s_missionBonusTotal ) );
	SV_GameSendServerCommand( -1, "cp \"MISSION FAILED\n\"" );
	Com_Printf( S_COLOR_RED "Mission failed - player destroyed (%d/%d primary).\n",
		primaryDone, s_missionTargetsTotal );

	// end the level via the standard intermission flow
	if( !theLevel.intermissiontime_ )
		BeginIntermission();
}

// find the local human (non-bot) player entity for objective evaluation
static GameEntity* G_MissionFindPlayer( void )
{
	int i;
	for( i = 0; i < theLevel.maxclients_; i++ )
	{
		GameEntity* p = theLevel.getEntity( i );
		if( !p || !p->client_ ) continue;
		if( p->r.svFlags & SVF_BOT ) continue;
		if( p->client_->pers_.connected_ != GameClient::ClientPersistant::CON_CONNECTED ) continue;
		return p;
	}
	return NULL;
}

// player height above the terrain directly below (AGL), world units.
// Use the playerstate origin (live, authoritative) - the entity-state s.origin
// lags/stays at spawn for the local player, which froze AGL at gear height.
static float G_MissionPlayerAGL( GameEntity* p )
{
	trace_t	tr;
	vec3_t	org, down;

	VectorCopy( p->client_->ps_.origin, org );
	VectorCopy( org, down );
	down[2] -= 100000.0f;
	SV_Trace( &tr, org, NULL, NULL, down, p->s.number, MASK_SOLID, false );
	if( tr.fraction < 1.0f )
		return org[2] - tr.endpos[2];
	return org[2];	// nothing below -> fall back to absolute Z
}

// current measured value for an objective's Type
static float G_MissionMeasure( const mission_objective_t* obj, GameEntity* p )
{
	switch( obj->type )
	{
	case MOBJ_ALTITUDE:	return G_MissionPlayerAGL( p );
	case MOBJ_KILLS:	return (float)( s_missionTargetsTotal - s_missionTargetsRemaining );
	default:			return 0.0f;
	}
}

static bool G_MissionCompare( float v, int op, float threshold )
{
	switch( op )
	{
	case MOP_GT:	return v >  threshold;
	case MOP_GE:	return v >= threshold;
	case MOP_LT:	return v <  threshold;
	case MOP_LE:	return v <= threshold;
	case MOP_EQ:	return v == threshold;
	default:		return false;
	}
}

// Called every server frame. Latches each programmatic objective true the first
// time its condition is met; once all are done the mission completes (no kill
// needed). New measures plug into G_MissionMeasure - the rest is generic.
void MF_MissionObjectiveFrame( void )
{
	GameEntity*	p;
	int			i, done = 0;

	if( s_numObjectives == 0 ) return;
	if( s_missionComplete || s_missionFailed ) return;

	p = G_MissionFindPlayer();
	if( !p || p->health_ <= 0 ) return;	// no live player -> nothing to measure

	for( i = 0; i < s_numObjectives; i++ )
	{
		if( !s_objectiveDone[i] )
		{
			float v = G_MissionMeasure( &s_objectives[i], p );
			if( G_MissionCompare( v, s_objectives[i].op, s_objectives[i].value ) )
			{
				s_objectiveDone[i] = true;
				if( s_objectives[i].text[0] )
					SV_GameSendServerCommand( -1, va( "cp \"%s\n\"", s_objectives[i].text ) );
				Com_Printf( "Mission objective met: %s\n", s_objectives[i].text );
			}
		}
		if( s_objectiveDone[i] ) done++;
	}

	if( done >= s_numObjectives )
		G_MissionSendComplete( s_numObjectives, s_numObjectives );
}

// Called every server frame. At a mission end screen (complete or failed),
// a human pressing FIRE restarts the mission. This replaces the gametype-
// dependent intermission exit (which never fires in single-player and needs
// an unintuitive "ready-up" in FFA).
void G_MissionRunIntermission( void )
{
	int i;

	if( !s_missionComplete && !s_missionFailed )
		return;
	if( !theLevel.intermissiontime_ )
		return;
	// let the player read the screen for a moment before FIRE counts
	if( theLevel.time_ < theLevel.intermissiontime_ + 2000 )
		return;

	for( i = 0; i < theLevel.maxclients_; i++ )
	{
		GameEntity* p = theLevel.getEntity( i );
		if( !p || !p->client_ )
			continue;
		if( p->r.svFlags & SVF_BOT )
			continue;
		if( p->client_->pers_.connected_ != GameClient::ClientPersistant::CON_CONNECTED )
			continue;
		if( p->client_->pers_.cmd_.buttons & ( BUTTON_ATTACK | BUTTON_ATTACK_MAIN ) )
		{
			// Full map reload (not map_restart): the lightweight restart leaves
			// the collision world in a state our spawn-time ground trace crashes
			// on. A full "map <name>" reproduces the clean initial-load path.
			char mapname[64];
			Cvar_VariableStringBuffer( "mapname", mapname, sizeof(mapname) );
			Com_Printf( "Mission: restart requested -> reloading map %s\n", mapname );
			Cbuf_ExecuteText( EXEC_APPEND, va( "map %s\n", mapname ) );
			return;
		}
	}
}

// Called from Die_MiscVehicle when an FL_MISSION_BONUS entity is destroyed.
void G_MissionBonusDestroyed( GameEntity* target )
{
	s_missionBonusDestroyed++;
	Com_Printf( "Mission: bonus target destroyed (%d/%d).\n",
		s_missionBonusDestroyed, s_missionBonusTotal );
	SV_GameSendServerCommand( -1, va( "cp \"Bonus target destroyed!  (%d/%d)\n\"",
		s_missionBonusDestroyed, s_missionBonusTotal ) );
}
