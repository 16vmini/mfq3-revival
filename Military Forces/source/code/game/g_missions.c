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
	Com_sprintf( filename, sizeof(filename), "missions/%s/%s.mis", mapname, missionname );

	if( !G_LoadOverviewAndEntities( filename, &overview, vehicles, installations ) )
		return;

	// spawn mission vehicles - secondary/bonus targets (extra points, not required)
	for( i = 0; i < IGME_MAX_VEHICLES/4; ++i )
	{
		if( !vehicles[i].used ) break;
		GameEntity* v = G_SpawnMissionVehicle( vehicles[i].index, vehicles[i].team,
				vehicles[i].origin, vehicles[i].angles );
		if( v )
		{
			spawned++;
			v->flags_ |= FL_MISSION_BONUS;
			s_missionBonusTotal++;
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


// Called from Die_MiscVehicle when an FL_MISSION_TARGET entity is destroyed.
void G_MissionTargetDestroyed( GameEntity* target )
{
	if( s_missionComplete )
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
	s_missionComplete = true;
	// tell the client to show the Mission Complete screen (primary, bonusDone, bonusTotal)
	SV_GameSendServerCommand( -1, va( "mission_complete %d %d %d",
		s_missionTargetsTotal, s_missionBonusDestroyed, s_missionBonusTotal ) );
	SV_GameSendServerCommand( -1, "cp \"Mission complete!\n\"" );
	Com_Printf( S_COLOR_GREEN "Mission complete - %d/%d primary destroyed, %d/%d bonus destroyed.\n",
		s_missionTargetsTotal, s_missionTargetsTotal, s_missionBonusDestroyed, s_missionBonusTotal );

	// end the level via the standard intermission flow
	if( !theLevel.intermissiontime_ )
		BeginIntermission();
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
